// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "db/db_impl.h"

#include <algorithm>
#include <set>
#include <string>
#include <stdint.h>
#include <stdio.h>
#include <vector>
#include "db/builder.h"
#include "db/db_iter.h"
#include "db/dbformat.h"
#include "db/filename.h"
#include "db/log_reader.h"
#include "db/log_writer.h"
#include "db/memtable.h"
#include "db/table_cache.h"
#include "db/version_set.h"
#include "db/write_batch_internal.h"
#include "include/db.h"
#include "include/env.h"
#include "include/status.h"
#include "include/table.h"
#include "include/table_builder.h"
#include "port/port.h"
#include "table/block.h"
#include "table/merger.h"
#include "table/two_level_iterator.h"
#include "util/coding.h"
#include "util/logging.h"
#include "util/mutexlock.h"

namespace leveldb {

struct DBImpl::CompactionState {
  Compaction* const compaction;

  // Sequence numbers < smallest_snapshot are not significant since we
  // will never have to service a snapshot below smallest_snapshot.
  // Therefore if we have seen a sequence number S <= smallest_snapshot,
  // we can drop all entries for the same key with sequence numbers < S.
  SequenceNumber smallest_snapshot;

  // Files produced by compaction
  struct Output {
    uint64_t number;
    uint64_t file_size;
    InternalKey smallest, largest;
  };
  std::vector<Output> outputs;

  // State kept for output being generated
  WritableFile* outfile;
  TableBuilder* builder;

  uint64_t total_bytes;

  Output* current_output() { return &outputs[outputs.size()-1]; }

  explicit CompactionState(Compaction* c)
      : compaction(c),
        outfile(NULL),
        builder(NULL),
        total_bytes(0) {
  }
};

namespace {
class NullWritableFile : public WritableFile {
 public:
  virtual Status Append(const Slice& data) { return Status::OK(); }
  virtual Status Close() { return Status::OK(); }
  virtual Status Flush() { return Status::OK(); }
  virtual Status Sync() { return Status::OK(); }
};
}

// Fix user-supplied options to be reasonable
template <class T,class V>
static void ClipToRange(T* ptr, V minvalue, V maxvalue) {
  if (*ptr > maxvalue) *ptr = maxvalue;
  if (*ptr < minvalue) *ptr = minvalue;
}
Options SanitizeOptions(const std::string& dbname,
                        const InternalKeyComparator* icmp,
                        const Options& src) {
  Options result = src;
  result.comparator = icmp;
  ClipToRange(&result.max_open_files,           20,     50000);
  ClipToRange(&result.write_buffer_size,        64<<10, 1<<30);
  ClipToRange(&result.large_value_threshold,    16<<10, 1<<30);
  ClipToRange(&result.block_size,               1<<10,  4<<20);
  if (result.info_log == NULL) {
    // Open a log file in the same directory as the db
    src.env->CreateDir(dbname);  // In case it does not exist
    src.env->RenameFile(InfoLogFileName(dbname), OldInfoLogFileName(dbname));
    Status s = src.env->NewWritableFile(InfoLogFileName(dbname),
                                        &result.info_log);
    if (!s.ok()) {
      // No place suitable for logging
      result.info_log = new NullWritableFile;
    }
  }
  return result;
}

DBImpl::DBImpl(const Options& options, const std::string& dbname)
    : env_(options.env),
      internal_comparator_(options.comparator),
      options_(SanitizeOptions(dbname, &internal_comparator_, options)),
      owns_info_log_(options_.info_log != options.info_log),
      dbname_(dbname),
      db_lock_(NULL),
      shutting_down_(NULL),
      bg_cv_(&mutex_),
      compacting_cv_(&mutex_),
      last_sequence_(0),
      mem_(new MemTable(internal_comparator_)),
      logfile_(NULL),
      log_(NULL),
      log_number_(0),
      bg_compaction_scheduled_(false),
      compacting_(false) {
  // Reserve ten files or so for other uses and give the rest to TableCache.
  const int table_cache_size = options.max_open_files - 10;
  table_cache_ = new TableCache(dbname_, &options_, table_cache_size);

  versions_ = new VersionSet(dbname_, &options_, table_cache_,
                             &internal_comparator_);
}

DBImpl::~DBImpl() {
  // Wait for background work to finish
  mutex_.Lock();
  shutting_down_.Release_Store(this);  // Any non-NULL value is ok
  if (bg_compaction_scheduled_) {
    while (bg_compaction_scheduled_) {
      bg_cv_.Wait();
    }
  }
  mutex_.Unlock();

  if (db_lock_ != NULL) {
    env_->UnlockFile(db_lock_);
  }

  delete versions_;
  delete mem_;
  delete log_;
  delete logfile_;
  delete table_cache_;

  if (owns_info_log_) {
    delete options_.info_log;
  }
}

Status DBImpl::NewDB() {
  assert(log_number_ == 0);
  assert(last_sequence_ == 0);

  VersionEdit new_db;
  new_db.SetComparatorName(user_comparator()->Name());
  new_db.SetLogNumber(log_number_);
  new_db.SetNextFile(2);
  new_db.SetLastSequence(0);

  const std::string manifest = DescriptorFileName(dbname_, 1);
  WritableFile* file;
  Status s = env_->NewWritableFile(manifest, &file);
  if (!s.ok()) {
    return s;
  }
  {
    log::Writer log(file);
    std::string record;
    new_db.EncodeTo(&record);
    s = log.AddRecord(record);
    if (s.ok()) {
      s = file->Close();
    }
  }
  delete file;
  if (s.ok()) {
    // Make "CURRENT" file that points to the new manifest file.
    s = SetCurrentFile(env_, dbname_, 1);
  } else {
    env_->DeleteFile(manifest);
  }
  return s;
}

Status DBImpl::Install(VersionEdit* edit,
                       uint64_t new_log_number,
                       MemTable* cleanup_mem) {
  mutex_.AssertHeld();
  edit->SetLogNumber(new_log_number);
  edit->SetLastSequence(last_sequence_);
  return versions_->LogAndApply(edit, cleanup_mem);
}

void DBImpl::MaybeIgnoreError(Status* s) const {
  if (s->ok() || options_.paranoid_checks) {
    // No change needed
  } else {
    Log(env_, options_.info_log, "Ignoring error %s", s->ToString().c_str());
    *s = Status::OK();
  }
}

void DBImpl::DeleteObsoleteFiles() {
  // Make a set of all of the live files
  std::set<uint64_t> live = pending_outputs_;
  versions_->AddLiveFiles(&live);

  versions_->CleanupLargeValueRefs(live, log_number_);

  std::vector<std::string> filenames;
  env_->GetChildren(dbname_, &filenames); // Ignoring errors on purpose
  uint64_t number;
  LargeValueRef large_ref;
  FileType type;
  for (int i = 0; i < filenames.size(); i++) {
    if (ParseFileName(filenames[i], &number, &large_ref, &type)) {
      bool keep = true;
      switch (type) {
        case kLogFile:
          keep = (number == log_number_);
          break;
        case kDescriptorFile:
          // Keep my manifest file, and any newer incarnations'
          // (in case there is a race that allows other incarnations)
          keep = (number >= versions_->ManifestFileNumber());
          break;
        case kTableFile:
          keep = (live.find(number) != live.end());
          break;
        case kTempFile:
          // Any temp files that are currently being written to must
          // be recorded in pending_outputs_, which is inserted into "live"
          keep = (live.find(number) != live.end());
          break;
        case kLargeValueFile:
          keep = versions_->LargeValueIsLive(large_ref);
          break;
        case kCurrentFile:
        case kDBLockFile:
        case kInfoLogFile:
          keep = true;
          break;
      }

      if (!keep) {
        if (type == kTableFile) {
          table_cache_->Evict(number);
        }
        Log(env_, options_.info_log, "Delete type=%d #%lld\n",
            int(type),
            static_cast<unsigned long long>(number));
        env_->DeleteFile(dbname_ + "/" + filenames[i]);
      }
    }
  }
}

Status DBImpl::Recover(VersionEdit* edit) {
  mutex_.AssertHeld();

  // Ignore error from CreateDir since the creation of the DB is
  // committed only when the descriptor is created, and this directory
  // may already exist from a previous failed creation attempt.
  env_->CreateDir(dbname_);
  assert(db_lock_ == NULL);
  Status s = env_->LockFile(LockFileName(dbname_), &db_lock_);
  if (!s.ok()) {
    return s;
  }

  if (!env_->FileExists(CurrentFileName(dbname_))) {
    if (options_.create_if_missing) {
      s = NewDB();
      if (!s.ok()) {
        return s;
      }
    } else {
      return Status::InvalidArgument(
          dbname_, "does not exist (create_if_missing is false)");
    }
  } else {
    if (options_.error_if_exists) {
      return Status::InvalidArgument(
          dbname_, "exists (error_if_exists is true)");
    }
  }

  s = versions_->Recover(&log_number_, &last_sequence_);
  if (s.ok()) {
    // Recover from the log file named in the descriptor
    SequenceNumber max_sequence(0);
    if (log_number_ != 0) {  // log_number_ == 0 indicates initial empty state
      s = RecoverLogFile(log_number_, edit, &max_sequence);
    }
    if (s.ok()) {
      last_sequence_ =
          last_sequence_ > max_sequence ? last_sequence_ : max_sequence;
    }
  }

  return s;
}

Status DBImpl::RecoverLogFile(uint64_t log_number,
                              VersionEdit* edit,
                              SequenceNumber* max_sequence) {
  struct LogReporter : public log::Reader::Reporter {
    Env* env;
    WritableFile* info_log;
    const char* fname;
    Status* status;  // NULL if options_.paranoid_checks==false
    virtual void Corruption(size_t bytes, const Status& s) {
      Log(env, info_log, "%s%s: dropping %d bytes; %s",
          (this->status == NULL ? "(ignoring error) " : ""),
          fname, static_cast<int>(bytes), s.ToString().c_str());
      if (this->status != NULL && this->status->ok()) *this->status = s;
    }
  };

  mutex_.AssertHeld();

  // Open the log file
  std::string fname = LogFileName(dbname_, log_number);
  SequentialFile* file;
  Status status = env_->NewSequentialFile(fname, &file);
  if (!status.ok()) {
    MaybeIgnoreError(&status);
    return status;
  }

  // Create the log reader.
  LogReporter reporter;
  reporter.env = env_;
  reporter.info_log = options_.info_log;
  reporter.fname = fname.c_str();
  reporter.status = (options_.paranoid_checks ? &status : NULL);
  // We intentially make log::Reader do checksumming even if
  // paranoid_checks==false so that corruptions cause entire commits
  // to be skipped instead of propagating bad information (like overly
  // large sequence numbers).
  log::Reader reader(file, &reporter, true/*checksum*/);
  Log(env_, options_.info_log, "Recovering log #%llu",
      (unsigned long long) log_number);

  // Read all the records and add to a memtable
  std::string scratch;
  Slice record;
  WriteBatch batch;
  MemTable* mem = NULL;
  while (reader.ReadRecord(&record, &scratch) &&
         status.ok()) {
    if (record.size() < 12) {
      reporter.Corruption(
          record.size(), Status::Corruption("log record too small"));
      continue;
    }
    WriteBatchInternal::SetContents(&batch, record);

    if (mem == NULL) {
      mem = new MemTable(internal_comparator_);
    }
    status = WriteBatchInternal::InsertInto(&batch, mem);
    MaybeIgnoreError(&status);
    if (!status.ok()) {
      break;
    }
    const SequenceNumber last_seq =
        WriteBatchInternal::Sequence(&batch) +
        WriteBatchInternal::Count(&batch) - 1;
    if (last_seq > *max_sequence) {
      *max_sequence = last_seq;
    }

    if (mem->ApproximateMemoryUsage() > options_.write_buffer_size) {
      status = WriteLevel0Table(mem, edit);
      if (!status.ok()) {
        // Reflect errors immediately so that conditions like full
        // file-systems cause the DB::Open() to fail.
        break;
      }
      delete mem;
      mem = NULL;
    }
  }

  if (status.ok() && mem != NULL) {
    status = WriteLevel0Table(mem, edit);
    // Reflect errors immediately so that conditions like full
    // file-systems cause the DB::Open() to fail.
  }

  delete mem;
  delete file;
  return status;
}

Status DBImpl::WriteLevel0Table(MemTable* mem, VersionEdit* edit) {
  mutex_.AssertHeld();
  FileMetaData meta;
  meta.number = versions_->NewFileNumber();
  pending_outputs_.insert(meta.number);
  Iterator* iter = mem->NewIterator();
  Log(env_, options_.info_log, "Level-0 table #%llu: started",
      (unsigned long long) meta.number);
  Status s = BuildTable(dbname_, env_, options_, table_cache_,
                        iter, &meta, edit);
  Log(env_, options_.info_log, "Level-0 table #%llu: %lld bytes %s",
      (unsigned long long) meta.number,
      (unsigned long long) meta.file_size,
      s.ToString().c_str());
  delete iter;
  pending_outputs_.erase(meta.number);
  return s;
}

Status DBImpl::CompactMemTable() {
  mutex_.AssertHeld();

  WritableFile* lfile = NULL;
  uint64_t new_log_number = versions_->NewFileNumber();

  VersionEdit edit;

  // Save the contents of the memtable as a new Table
  Status s = WriteLevel0Table(mem_, &edit);
  if (s.ok()) {
    s = env_->NewWritableFile(LogFileName(dbname_, new_log_number), &lfile);
  }

  // Save a new descriptor with the new table and log number.
  if (s.ok()) {
    s = Install(&edit, new_log_number, mem_);
  }

  if (s.ok()) {
    // Commit to the new state
    mem_ = new MemTable(internal_comparator_);
    delete log_;
    delete logfile_;
    logfile_ = lfile;
    log_ = new log::Writer(lfile);
    log_number_ = new_log_number;
    DeleteObsoleteFiles();
    MaybeScheduleCompaction();
  } else {
    delete lfile;
    env_->DeleteFile(LogFileName(dbname_, new_log_number));
  }
  return s;
}

void DBImpl::TEST_CompactRange(
    int level,
    const std::string& begin,
    const std::string& end) {
  MutexLock l(&mutex_);
  while (compacting_) {
    compacting_cv_.Wait();
  }
  Compaction* c = versions_->CompactRange(
      level,
      InternalKey(begin, kMaxSequenceNumber, kValueTypeForSeek),
      InternalKey(end, 0, static_cast<ValueType>(0)));

  if (c != NULL) {
    CompactionState* compact = new CompactionState(c);
    DoCompactionWork(compact);  // Ignore error in test compaction
    CleanupCompaction(compact);
  }

  // Start any background compaction that may have been delayed by this thread
  MaybeScheduleCompaction();
}

Status DBImpl::TEST_CompactMemTable() {
  MutexLock l(&mutex_);
  return CompactMemTable();
}

void DBImpl::MaybeScheduleCompaction() {
  mutex_.AssertHeld();
  if (bg_compaction_scheduled_) {
    // Already scheduled
  } else if (compacting_) {
    // Some other thread is running a compaction.  Do not conflict with it.
  } else if (shutting_down_.Acquire_Load()) {
    // DB is being deleted; no more background compactions
  } else if (!versions_->NeedsCompaction()) {
    // No work to be done
  } else {
    bg_compaction_scheduled_ = true;
    env_->Schedule(&DBImpl::BGWork, this);
  }
}

void DBImpl::BGWork(void* db) {
  reinterpret_cast<DBImpl*>(db)->BackgroundCall();
}

void DBImpl::BackgroundCall() {
  MutexLock l(&mutex_);
  assert(bg_compaction_scheduled_);
  if (!shutting_down_.Acquire_Load() &&
      !compacting_) {
    BackgroundCompaction();
  }
  bg_compaction_scheduled_ = false;
  bg_cv_.SignalAll();

  // Previous compaction may have produced too many files in a level,
  // so reschedule another compaction if needed.
  MaybeScheduleCompaction();
}

void DBImpl::BackgroundCompaction() {
  mutex_.AssertHeld();
  Compaction* c = versions_->PickCompaction();
  if (c == NULL) {
    // Nothing to do
    return;
  }

  Status status;
  if (c->num_input_files(0) == 1 && c->num_input_files(1) == 0) {
    // Move file to next level
    FileMetaData* f = c->input(0, 0);
    c->edit()->DeleteFile(c->level(), f->number);
    c->edit()->AddFile(c->level() + 1, f->number, f->file_size,
                       f->smallest, f->largest);
    status = Install(c->edit(), log_number_, NULL);
    Log(env_, options_.info_log, "Moved #%lld to level-%d %lld bytes %s\n",
        static_cast<unsigned long long>(f->number),
        c->level() + 1,
        static_cast<unsigned long long>(f->file_size),
        status.ToString().c_str());
  } else {
    CompactionState* compact = new CompactionState(c);
    status = DoCompactionWork(compact);
    CleanupCompaction(compact);
  }
  delete c;

  if (status.ok()) {
    // Done
  } else if (shutting_down_.Acquire_Load()) {
    // Ignore compaction errors found during shutting down
  } else {
    Log(env_, options_.info_log,
        "Compaction error: %s", status.ToString().c_str());
    if (options_.paranoid_checks && bg_error_.ok()) {
      bg_error_ = status;
    }
  }
}

void DBImpl::CleanupCompaction(CompactionState* compact) {
  mutex_.AssertHeld();
  if (compact->builder != NULL) {
    // May happen if we get a shutdown call in the middle of compaction
    compact->builder->Abandon();
    delete compact->builder;
  } else {
    assert(compact->outfile == NULL);
  }
  delete compact->outfile;
  for (int i = 0; i < compact->outputs.size(); i++) {
    const CompactionState::Output& out = compact->outputs[i];
    pending_outputs_.erase(out.number);
  }
  delete compact;
}

Status DBImpl::OpenCompactionOutputFile(CompactionState* compact) {
  assert(compact != NULL);
  assert(compact->builder == NULL);
  uint64_t file_number;
  {
    mutex_.Lock();
    file_number = versions_->NewFileNumber();
    pending_outputs_.insert(file_number);
    CompactionState::Output out;
    out.number = file_number;
    out.smallest.Clear();
    out.largest.Clear();
    compact->outputs.push_back(out);
    mutex_.Unlock();
  }

  // Make the output file
  std::string fname = TableFileName(dbname_, file_number);
  Status s = env_->NewWritableFile(fname, &compact->outfile);
  if (s.ok()) {
    compact->builder = new TableBuilder(options_, compact->outfile);
  }
  return s;
}

Status DBImpl::FinishCompactionOutputFile(CompactionState* compact,
                                          Iterator* input) {
  assert(compact != NULL);
  assert(compact->outfile != NULL);
  assert(compact->builder != NULL);

  const uint64_t output_number = compact->current_output()->number;
  assert(output_number != 0);

  // Check for iterator errors
  Status s = input->status();
  const uint64_t current_entries = compact->builder->NumEntries();
  if (s.ok()) {
    s = compact->builder->Finish();
  } else {
    compact->builder->Abandon();
  }
  const uint64_t current_bytes = compact->builder->FileSize();
  compact->current_output()->file_size = current_bytes;
  compact->total_bytes += current_bytes;
  delete compact->builder;
  compact->builder = NULL;

  // Finish and check for file errors
  if (s.ok()) {
    s = compact->outfile->Sync();
  }
  if (s.ok()) {
    s = compact->outfile->Close();
  }
  delete compact->outfile;
  compact->outfile = NULL;

  if (s.ok() && current_entries > 0) {
    // Verify that the table is usable
    Iterator* iter = table_cache_->NewIterator(ReadOptions(),output_number);
    s = iter->status();
    delete iter;
    if (s.ok()) {
      Log(env_, options_.info_log,
          "Generated table #%llu: %lld keys, %lld bytes",
          (unsigned long long) output_number,
          (unsigned long long) current_entries,
          (unsigned long long) current_bytes);
    }
  }
  return s;
}


Status DBImpl::InstallCompactionResults(CompactionState* compact) {
  mutex_.AssertHeld();
  Log(env_, options_.info_log,  "Compacted %d@%d + %d@%d files => %lld bytes",
      compact->compaction->num_input_files(0),
      compact->compaction->level(),
      compact->compaction->num_input_files(1),
      compact->compaction->level() + 1,
      static_cast<long long>(compact->total_bytes));

  // Add compaction outputs
  compact->compaction->AddInputDeletions(compact->compaction->edit());
  const int level = compact->compaction->level();
  for (int i = 0; i < compact->outputs.size(); i++) {
    const CompactionState::Output& out = compact->outputs[i];
    compact->compaction->edit()->AddFile(
        level + 1,
        out.number, out.file_size, out.smallest, out.largest);
    pending_outputs_.erase(out.number);
  }
  compact->outputs.clear();

  Status s = Install(compact->compaction->edit(), log_number_, NULL);
  if (s.ok()) {
    compact->compaction->ReleaseInputs();
    DeleteObsoleteFiles();
  } else {
    // Discard any files we may have created during this failed compaction
    for (int i = 0; i < compact->outputs.size(); i++) {
      env_->DeleteFile(TableFileName(dbname_, compact->outputs[i].number));
    }
  }
  return s;
}

Status DBImpl::DoCompactionWork(CompactionState* compact) {
  Log(env_, options_.info_log,  "Compacting %d@%d + %d@%d files",
      compact->compaction->num_input_files(0),
      compact->compaction->level(),
      compact->compaction->num_input_files(1),
      compact->compaction->level() + 1);

  assert(versions_->NumLevelFiles(compact->compaction->level()) > 0);
  assert(compact->builder == NULL);
  assert(compact->outfile == NULL);
  if (snapshots_.empty()) {
    compact->smallest_snapshot = last_sequence_;
  } else {
    compact->smallest_snapshot = snapshots_.oldest()->number_;
  }

  // Release mutex while we're actually doing the compaction work
  compacting_ = true;
  mutex_.Unlock();

  Iterator* input = versions_->MakeInputIterator(compact->compaction);
  input->SeekToFirst();
  Status status;
  ParsedInternalKey ikey;
  std::string current_user_key;
  bool has_current_user_key = false;
  SequenceNumber last_sequence_for_key = kMaxSequenceNumber;
  for (; input->Valid() && !shutting_down_.Acquire_Load(); ) {
    // Handle key/value, add to state, etc.
    Slice key = input->key();
    bool drop = false;
    if (!ParseInternalKey(key, &ikey)) {
      // Do not hide error keys
      current_user_key.clear();
      has_current_user_key = false;
      last_sequence_for_key = kMaxSequenceNumber;
    } else {
      if (!has_current_user_key ||
          user_comparator()->Compare(ikey.user_key,
                                     Slice(current_user_key)) != 0) {
        // First occurrence of this user key
        current_user_key.assign(ikey.user_key.data(), ikey.user_key.size());
        has_current_user_key = true;
        last_sequence_for_key = kMaxSequenceNumber;
      }

      if (last_sequence_for_key <= compact->smallest_snapshot) {
        // Hidden by an newer entry for same user key
        drop = true;    // (A)
      } else if (ikey.type == kTypeDeletion &&
                 ikey.sequence <= compact->smallest_snapshot &&
                 compact->compaction->IsBaseLevelForKey(ikey.user_key)) {
        // For this user key:
        // (1) there is no data in higher levels
        // (2) data in lower levels will have larger sequence numbers
        // (3) data in layers that are being compacted here and have
        //     smaller sequence numbers will be dropped in the next
        //     few iterations of this loop (by rule (A) above).
        // Therefore this deletion marker is obsolete and can be dropped.
        drop = true;
      }

      last_sequence_for_key = ikey.sequence;
    }
#if 0
    Log(env_, options_.info_log,
        "  Compact: %s, seq %d, type: %d %d, drop: %d, is_base: %d, "
        "%d smallest_snapshot: %d",
        ikey.user_key.ToString().c_str(),
        (int)ikey.sequence, ikey.type, kTypeLargeValueRef, drop,
        compact->compaction->IsBaseLevelForKey(ikey.user_key),
        (int)last_sequence_for_key, (int)compact->smallest_snapshot);
#endif

    if (!drop) {
      // Open output file if necessary
      if (compact->builder == NULL) {
        status = OpenCompactionOutputFile(compact);
        if (!status.ok()) {
          break;
        }
      }
      if (compact->builder->NumEntries() == 0) {
        compact->current_output()->smallest.DecodeFrom(key);
      }
      compact->current_output()->largest.DecodeFrom(key);

      if (ikey.type == kTypeLargeValueRef) {
        if (input->value().size() != LargeValueRef::ByteSize()) {
          if (options_.paranoid_checks) {
            status = Status::Corruption("invalid large value ref");
            break;
          } else {
            Log(env_, options_.info_log,
                "compaction found invalid large value ref");
          }
        } else {
          compact->compaction->edit()->AddLargeValueRef(
              LargeValueRef::FromRef(input->value()),
              compact->current_output()->number,
              input->key());
          compact->builder->Add(key, input->value());
        }
      } else {
        compact->builder->Add(key, input->value());
      }

      // Close output file if it is big enough
      if (compact->builder->FileSize() >=
          compact->compaction->MaxOutputFileSize()) {
        status = FinishCompactionOutputFile(compact, input);
        if (!status.ok()) {
          break;
        }
      }
    }

    input->Next();
  }

  if (status.ok() && shutting_down_.Acquire_Load()) {
    status = Status::IOError("Deleting DB during compaction");
  }
  if (status.ok() && compact->builder != NULL) {
    status = FinishCompactionOutputFile(compact, input);
  }
  if (status.ok()) {
    status = input->status();
  }
  delete input;
  input = NULL;

  mutex_.Lock();

  if (status.ok()) {
    status = InstallCompactionResults(compact);
  }
  compacting_ = false;
  compacting_cv_.SignalAll();
  return status;
}

Iterator* DBImpl::NewInternalIterator(const ReadOptions& options,
                                      SequenceNumber* latest_snapshot) {
  mutex_.Lock();
  *latest_snapshot = last_sequence_;

  // Collect together all needed child iterators
  std::vector<Iterator*> list;
  list.push_back(mem_->NewIterator());
  versions_->current()->AddIterators(options, &list);
  Iterator* internal_iter =
      NewMergingIterator(&internal_comparator_, &list[0], list.size());
  versions_->current()->Ref();
  internal_iter->RegisterCleanup(&DBImpl::Unref, this, versions_->current());

  mutex_.Unlock();
  return internal_iter;
}

Iterator* DBImpl::TEST_NewInternalIterator() {
  SequenceNumber ignored;
  return NewInternalIterator(ReadOptions(), &ignored);
}

Status DBImpl::Get(const ReadOptions& options,
                   const Slice& key,
                   std::string* value) {
  // TODO(opt): faster implementation
  Iterator* iter = NewIterator(options);
  iter->Seek(key);
  bool found = false;
  if (iter->Valid() && user_comparator()->Compare(key, iter->key()) == 0) {
    Slice v = iter->value();
    value->assign(v.data(), v.size());
    found = true;
  }
  // Non-OK iterator status trumps everything else
  Status result = iter->status();
  if (result.ok() && !found) {
    result = Status::NotFound(Slice());  // Use an empty error message for speed
  }
  delete iter;
  return result;
}

Iterator* DBImpl::NewIterator(const ReadOptions& options) {
  SequenceNumber latest_snapshot;
  Iterator* internal_iter = NewInternalIterator(options, &latest_snapshot);
  SequenceNumber sequence =
      (options.snapshot ? options.snapshot->number_ : latest_snapshot);
  return NewDBIterator(&dbname_, env_,
                       user_comparator(), internal_iter, sequence);
}

void DBImpl::Unref(void* arg1, void* arg2) {
  DBImpl* impl = reinterpret_cast<DBImpl*>(arg1);
  Version* v = reinterpret_cast<Version*>(arg2);
  MutexLock l(&impl->mutex_);
  v->Unref();
}

const Snapshot* DBImpl::GetSnapshot() {
  MutexLock l(&mutex_);
  return snapshots_.New(last_sequence_);
}

void DBImpl::ReleaseSnapshot(const Snapshot* s) {
  MutexLock l(&mutex_);
  snapshots_.Delete(s);
}

// Convenience methods
Status DBImpl::Put(const WriteOptions& o, const Slice& key, const Slice& val) {
  return DB::Put(o, key, val);
}

Status DBImpl::Delete(const WriteOptions& options, const Slice& key) {
  return DB::Delete(options, key);
}

Status DBImpl::Write(const WriteOptions& options, WriteBatch* updates) {
  Status status;

  WriteBatch* final = NULL;
  {
    MutexLock l(&mutex_);
    if (!bg_error_.ok()) {
      status = bg_error_;
    } else if (mem_->ApproximateMemoryUsage() > options_.write_buffer_size) {
      status = CompactMemTable();
    }
    if (status.ok()) {
      status = HandleLargeValues(last_sequence_ + 1, updates, &final);
    }
    if (status.ok()) {
      WriteBatchInternal::SetSequence(final, last_sequence_ + 1);
      last_sequence_ += WriteBatchInternal::Count(final);

      // Add to log and apply to memtable
      status = log_->AddRecord(WriteBatchInternal::Contents(final));
      if (status.ok() && options.sync) {
        status = logfile_->Sync();
      }
      if (status.ok()) {
        status = WriteBatchInternal::InsertInto(final, mem_);
      }
    }

    if (options.post_write_snapshot != NULL) {
      *options.post_write_snapshot =
          status.ok() ? snapshots_.New(last_sequence_) : NULL;
    }
  }
  if (final != updates) {
    delete final;
  }

  return status;
}

bool DBImpl::HasLargeValues(const WriteBatch& batch) const {
  if (WriteBatchInternal::ByteSize(&batch) >= options_.large_value_threshold) {
    for (WriteBatchInternal::Iterator it(batch); !it.Done(); it.Next()) {
      if (it.op() == kTypeValue &&
          it.value().size() >= options_.large_value_threshold) {
        return true;
      }
    }
  }
  return false;
}

// Given "raw_value", determines the appropriate compression format to use
// and stores the data that should be written to the large value file in
// "*file_bytes", and sets "*ref" to the appropriate large value reference.
// May use "*scratch" as backing store for "*file_bytes".
void DBImpl::MaybeCompressLargeValue(
    const Slice& raw_value,
    Slice* file_bytes,
    std::string* scratch,
    LargeValueRef* ref) {
  switch (options_.compression) {
    case kLightweightCompression: {
      port::Lightweight_Compress(raw_value.data(), raw_value.size(), scratch);
      if (scratch->size() < (raw_value.size() / 8) * 7) {
        *file_bytes = *scratch;
        *ref = LargeValueRef::Make(raw_value, kLightweightCompression);
        return;
      }

      // Less than 12.5% compression: just leave as uncompressed data
      break;
    }
    case kNoCompression:
      // Use default code outside of switch
      break;
  }
  // Store as uncompressed data
  *file_bytes = raw_value;
  *ref = LargeValueRef::Make(raw_value, kNoCompression);
}

Status DBImpl::HandleLargeValues(SequenceNumber assigned_seq,
                                 WriteBatch* updates,
                                 WriteBatch** final) {
  if (!HasLargeValues(*updates)) {
    // Fast path: no large values found
    *final = updates;
  } else {
    // Copy *updates to a new WriteBatch, replacing the references to
    *final = new WriteBatch;
    SequenceNumber seq = assigned_seq;
    for (WriteBatchInternal::Iterator it(*updates); !it.Done(); it.Next()) {
      switch (it.op()) {
        case kTypeValue:
          if (it.value().size() < options_.large_value_threshold) {
            (*final)->Put(it.key(), it.value());
          } else {
            std::string scratch;
            Slice file_bytes;
            LargeValueRef large_ref;
            MaybeCompressLargeValue(
                it.value(), &file_bytes, &scratch, &large_ref);
            InternalKey ikey(it.key(), seq, kTypeLargeValueRef);
            if (versions_->RegisterLargeValueRef(large_ref, log_number_,ikey)) {
              // TODO(opt): avoid holding the lock here (but be careful about
              // another thread doing a Write and changing log_number_ or
              // having us get a different "assigned_seq" value).

              uint64_t tmp_number = versions_->NewFileNumber();
              pending_outputs_.insert(tmp_number);
              std::string tmp = TempFileName(dbname_, tmp_number);
              WritableFile* file;
              Status s = env_->NewWritableFile(tmp, &file);
              if (!s.ok()) {
                return s;     // Caller will delete *final
              }

              file->Append(file_bytes);

              s = file->Close();
              delete file;

              if (s.ok()) {
                const std::string fname =
                    LargeValueFileName(dbname_, large_ref);
                s = env_->RenameFile(tmp, fname);
              } else {
                Log(env_, options_.info_log, "Write large value: %s",
                    s.ToString().c_str());
              }
              pending_outputs_.erase(tmp_number);

              if (!s.ok()) {
                env_->DeleteFile(tmp);  // Cleanup; intentionally ignoring error
                return s;     // Caller will delete *final
              }
            }

            // Put an indirect reference in the write batch in place
            // of large value
            WriteBatchInternal::PutLargeValueRef(*final, it.key(), large_ref);
          }
          break;
        case kTypeLargeValueRef:
          return Status::Corruption("Corrupted write batch");
          break;
        case kTypeDeletion:
          (*final)->Delete(it.key());
          break;
      }
      seq = seq + 1;
    }
  }
  return Status::OK();
}

bool DBImpl::GetProperty(const Slice& property, uint64_t* value) {
  MutexLock l(&mutex_);
  Slice in = property;
  Slice prefix("leveldb.");
  if (!in.starts_with(prefix)) return false;
  in.remove_prefix(prefix.size());

  if (in.starts_with("num-files-at-level")) {
    in.remove_prefix(strlen("num-files-at-level"));
    uint64_t level;
    bool ok = ConsumeDecimalNumber(&in, &level) && in.empty();
    if (!ok || level < 0 || level >= config::kNumLevels) {
      return false;
    } else {
      *value = versions_->NumLevelFiles(level);
      return true;
    }
  }
  return false;
}

void DBImpl::GetApproximateSizes(
    const Range* range, int n,
    uint64_t* sizes) {
  // TODO(opt): better implementation
  Version* v;
  {
    MutexLock l(&mutex_);
    versions_->current()->Ref();
    v = versions_->current();
  }

  for (int i = 0; i < n; i++) {
    // Convert user_key into a corresponding internal key.
    InternalKey k1(range[i].start, kMaxSequenceNumber, kValueTypeForSeek);
    InternalKey k2(range[i].limit, kMaxSequenceNumber, kValueTypeForSeek);
    uint64_t start = versions_->ApproximateOffsetOf(v, k1);
    uint64_t limit = versions_->ApproximateOffsetOf(v, k2);
    sizes[i] = (limit >= start ? limit - start : 0);
  }

  {
    MutexLock l(&mutex_);
    v->Unref();
  }
}

// Default implementations of convenience methods that subclasses of DB
// can call if they wish
Status DB::Put(const WriteOptions& opt, const Slice& key, const Slice& value) {
  WriteBatch batch;
  batch.Put(key, value);
  return Write(opt, &batch);
}

Status DB::Delete(const WriteOptions& opt, const Slice& key) {
  WriteBatch batch;
  batch.Delete(key);
  return Write(opt, &batch);
}

DB::~DB() { }

Status DB::Open(const Options& options, const std::string& dbname,
                DB** dbptr) {
  *dbptr = NULL;

  DBImpl* impl = new DBImpl(options, dbname);
  impl->mutex_.Lock();
  VersionEdit edit;
  Status s = impl->Recover(&edit); // Handles create_if_missing, error_if_exists
  if (s.ok()) {
    impl->log_number_ = impl->versions_->NewFileNumber();
    WritableFile* lfile;
    s = options.env->NewWritableFile(LogFileName(dbname, impl->log_number_),
                                     &lfile);
    if (s.ok()) {
      impl->logfile_ = lfile;
      impl->log_ = new log::Writer(lfile);
      s = impl->Install(&edit, impl->log_number_, NULL);
    }
    if (s.ok()) {
      impl->DeleteObsoleteFiles();
    }
  }
  impl->mutex_.Unlock();
  if (s.ok()) {
    *dbptr = impl;
  } else {
    delete impl;
  }
  return s;
}

Status DestroyDB(const std::string& dbname, const Options& options) {
  Env* env = options.env;
  std::vector<std::string> filenames;
  // Ignore error in case directory does not exist
  env->GetChildren(dbname, &filenames);
  if (filenames.empty()) {
    return Status::OK();
  }

  FileLock* lock;
  Status result = env->LockFile(LockFileName(dbname), &lock);
  if (result.ok()) {
    uint64_t number;
    LargeValueRef large_ref;
    FileType type;
    for (int i = 0; i < filenames.size(); i++) {
      if (ParseFileName(filenames[i], &number, &large_ref, &type)) {
        Status del = env->DeleteFile(dbname + "/" + filenames[i]);
        if (result.ok() && !del.ok()) {
          result = del;
        }
      }
    }
    env->UnlockFile(lock);  // Ignore error since state is already gone
    env->DeleteFile(LockFileName(dbname));
    env->DeleteDir(dbname);  // Ignore error in case dir contains other files
  }
  return result;
}

}
