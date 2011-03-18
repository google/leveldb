// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "db/version_set.h"

#include <algorithm>
#include <stdio.h>
#include "db/filename.h"
#include "db/log_reader.h"
#include "db/log_writer.h"
#include "db/memtable.h"
#include "db/table_cache.h"
#include "include/env.h"
#include "include/table_builder.h"
#include "table/merger.h"
#include "table/two_level_iterator.h"
#include "util/coding.h"
#include "util/logging.h"

namespace leveldb {

static double MaxBytesForLevel(int level) {
  if (level == 0) {
    return 4 * 1048576.0;
  } else {
    double result = 10 * 1048576.0;
    while (level > 1) {
      result *= 10;
      level--;
    }
    return result;
  }
}

static uint64_t MaxFileSizeForLevel(int level) {
  return 2 << 20;       // We could vary per level to reduce number of files?
}

namespace {
std::string IntSetToString(const std::set<uint64_t>& s) {
  std::string result = "{";
  for (std::set<uint64_t>::const_iterator it = s.begin();
       it != s.end();
       ++it) {
    result += (result.size() > 1) ? "," : "";
    result += NumberToString(*it);
  }
  result += "}";
  return result;
}
}

Version::~Version() {
  assert(refs_ == 0);
  for (int level = 0; level < config::kNumLevels; level++) {
    for (int i = 0; i < files_[level].size(); i++) {
      FileMetaData* f = files_[level][i];
      assert(f->refs >= 0);
      f->refs--;
      if (f->refs <= 0) {
        delete f;
      }
    }
  }
  delete cleanup_mem_;
}

// An internal iterator.  For a given version/level pair, yields
// information about the files in the level.  For a given entry, key()
// is the largest key that occurs in the file, and value() is an
// 8-byte value containing the file number of the file, encoding using
// EncodeFixed64.
class Version::LevelFileNumIterator : public Iterator {
 public:
  LevelFileNumIterator(const Version* version,
                       const std::vector<FileMetaData*>* flist)
      : icmp_(version->vset_->icmp_.user_comparator()),
        flist_(flist),
        index_(flist->size()) {        // Marks as invalid
  }
  virtual bool Valid() const {
    return index_ < flist_->size();
  }
  virtual void Seek(const Slice& target) {
    uint32_t left = 0;
    uint32_t right = flist_->size() - 1;
    while (left < right) {
      uint32_t mid = (left + right) / 2;
      int cmp = icmp_.Compare((*flist_)[mid]->largest.Encode(), target);
      if (cmp < 0) {
        // Key at "mid.largest" is < than "target".  Therefore all
        // files at or before "mid" are uninteresting.
        left = mid + 1;
      } else {
        // Key at "mid.largest" is >= "target".  Therefore all files
        // after "mid" are uninteresting.
        right = mid;
      }
    }
    index_ = left;
  }
  virtual void SeekToFirst() { index_ = 0; }
  virtual void SeekToLast() {
    index_ = flist_->empty() ? 0 : flist_->size() - 1;
  }
  virtual void Next() {
    assert(Valid());
    index_++;
  }
  virtual void Prev() {
    assert(Valid());
    if (index_ == 0) {
      index_ = flist_->size();  // Marks as invalid
    } else {
      index_--;
    }
  }
  Slice key() const {
    assert(Valid());
    return (*flist_)[index_]->largest.Encode();
  }
  Slice value() const {
    assert(Valid());
    EncodeFixed64(value_buf_, (*flist_)[index_]->number);
    return Slice(value_buf_, sizeof(value_buf_));
  }
  virtual Status status() const { return Status::OK(); }
 private:
  const InternalKeyComparator icmp_;
  const std::vector<FileMetaData*>* const flist_;
  int index_;

  mutable char value_buf_[8];  // Used for encoding the file number for value()
};

static Iterator* GetFileIterator(void* arg,
                                 const ReadOptions& options,
                                 const Slice& file_value) {
  TableCache* cache = reinterpret_cast<TableCache*>(arg);
  if (file_value.size() != 8) {
    return NewErrorIterator(
        Status::Corruption("FileReader invoked with unexpected value"));
  } else {
    return cache->NewIterator(options, DecodeFixed64(file_value.data()));
  }
}

Iterator* Version::NewConcatenatingIterator(const ReadOptions& options,
                                            int level) const {
  return NewTwoLevelIterator(
      new LevelFileNumIterator(this, &files_[level]),
      &GetFileIterator, vset_->table_cache_, options);
}

void Version::AddIterators(const ReadOptions& options,
                           std::vector<Iterator*>* iters) {
  // Merge all level zero files together since they may overlap
  for (int i = 0; i < files_[0].size(); i++) {
    iters->push_back(
        vset_->table_cache_->NewIterator(options, files_[0][i]->number));
  }

  // For levels > 0, we can use a concatenating iterator that sequentially
  // walks through the non-overlapping files in the level, opening them
  // lazily.
  for (int level = 1; level < config::kNumLevels; level++) {
    if (!files_[level].empty()) {
      iters->push_back(NewConcatenatingIterator(options, level));
    }
  }
}

void Version::Ref() {
  ++refs_;
}

void Version::Unref() {
  assert(refs_ >= 1);
  --refs_;
  if (refs_ == 0) {
    vset_->MaybeDeleteOldVersions();
    // TODO: try to delete obsolete files
  }
}

std::string Version::DebugString() const {
  std::string r;
  for (int level = 0; level < config::kNumLevels; level++) {
    // E.g., level 1: 17:123['a' .. 'd'] 20:43['e' .. 'g']
    r.append("level ");
    AppendNumberTo(&r, level);
    r.push_back(':');
    const std::vector<FileMetaData*>& files = files_[level];
    for (int i = 0; i < files.size(); i++) {
      r.push_back(' ');
      AppendNumberTo(&r, files[i]->number);
      r.push_back(':');
      AppendNumberTo(&r, files[i]->file_size);
      r.append("['");
      AppendEscapedStringTo(&r, files[i]->smallest.Encode());
      r.append("' .. '");
      AppendEscapedStringTo(&r, files[i]->largest.Encode());
      r.append("']");
    }
    r.push_back('\n');
  }
  return r;
}

// A helper class so we can efficiently apply a whole sequence
// of edits to a particular state without creating intermediate
// Versions that contain full copies of the intermediate state.
class VersionSet::Builder {
 private:
  typedef std::map<uint64_t, FileMetaData*> FileMap;
  VersionSet* vset_;
  FileMap files_[config::kNumLevels];

 public:
  // Initialize a builder with the files from *base and other info from *vset
  Builder(VersionSet* vset, Version* base)
      : vset_(vset) {
    for (int level = 0; level < config::kNumLevels; level++) {
      const std::vector<FileMetaData*>& files = base->files_[level];
      for (int i = 0; i < files.size(); i++) {
        FileMetaData* f = files[i];
        f->refs++;
        files_[level].insert(std::make_pair(f->number, f));
      }
    }
  }

  ~Builder() {
    for (int level = 0; level < config::kNumLevels; level++) {
      const FileMap& fmap = files_[level];
      for (FileMap::const_iterator iter = fmap.begin();
           iter != fmap.end();
           ++iter) {
        FileMetaData* f = iter->second;
        f->refs--;
        if (f->refs <= 0) {
          delete f;
        }
      }
    }
  }

  // Apply all of the edits in *edit to the current state.
  void Apply(VersionEdit* edit) {
    // Update compaction pointers
    for (int i = 0; i < edit->compact_pointers_.size(); i++) {
      const int level = edit->compact_pointers_[i].first;
      vset_->compact_pointer_[level] =
          edit->compact_pointers_[i].second.Encode().ToString();
    }

    // Delete files
    const VersionEdit::DeletedFileSet& del = edit->deleted_files_;
    for (VersionEdit::DeletedFileSet::const_iterator iter = del.begin();
         iter != del.end();
         ++iter) {
      const int level = iter->first;
      const uint64_t number = iter->second;
      FileMap::iterator fiter = files_[level].find(number);
      assert(fiter != files_[level].end());  // Sanity check for debug mode
      if (fiter != files_[level].end()) {
        FileMetaData* f = fiter->second;
        f->refs--;
        if (f->refs <= 0) {
          delete f;
        }
        files_[level].erase(fiter);
      }
    }

    // Add new files
    for (int i = 0; i < edit->new_files_.size(); i++) {
      const int level = edit->new_files_[i].first;
      FileMetaData* f = new FileMetaData(edit->new_files_[i].second);
      f->refs = 1;
      assert(files_[level].count(f->number) == 0);
      files_[level].insert(std::make_pair(f->number, f));
    }

    // Add large value refs
    for (int i = 0; i < edit->large_refs_added_.size(); i++) {
      const VersionEdit::Large& l = edit->large_refs_added_[i];
      vset_->RegisterLargeValueRef(l.large_ref, l.fnum, l.internal_key);
    }
  }

  // Save the current state in *v.
  void SaveTo(Version* v) {
    for (int level = 0; level < config::kNumLevels; level++) {
      const FileMap& fmap = files_[level];
      for (FileMap::const_iterator iter = fmap.begin();
           iter != fmap.end();
           ++iter) {
        FileMetaData* f = iter->second;
        f->refs++;
        v->files_[level].push_back(f);
      }
    }
  }
};

VersionSet::VersionSet(const std::string& dbname,
                       const Options* options,
                       TableCache* table_cache,
                       const InternalKeyComparator* cmp)
    : env_(options->env),
      dbname_(dbname),
      options_(options),
      table_cache_(table_cache),
      icmp_(*cmp),
      next_file_number_(2),
      manifest_file_number_(0),  // Filled by Recover()
      descriptor_file_(NULL),
      descriptor_log_(NULL),
      current_(new Version(this)),
      oldest_(current_) {
}

VersionSet::~VersionSet() {
  for (Version* v = oldest_; v != NULL; ) {
    Version* next = v->next_;
    assert(v->refs_ == 0);
    delete v;
    v = next;
  }
  delete descriptor_log_;
  delete descriptor_file_;
}

Status VersionSet::LogAndApply(VersionEdit* edit, MemTable* cleanup_mem) {
  edit->SetNextFile(next_file_number_);

  Version* v = new Version(this);
  {
    Builder builder(this, current_);
    builder.Apply(edit);
    builder.SaveTo(v);
  }

  std::string new_manifest_file;
  Status s = Finalize(v);

  // Initialize new descriptor log file if necessary by creating
  // a temporary file that contains a snapshot of the current version.
  if (s.ok()) {
    if (descriptor_log_ == NULL) {
      assert(descriptor_file_ == NULL);
      new_manifest_file = DescriptorFileName(dbname_, manifest_file_number_);
      edit->SetNextFile(next_file_number_);
      s = env_->NewWritableFile(new_manifest_file, &descriptor_file_);
      if (s.ok()) {
        descriptor_log_ = new log::Writer(descriptor_file_);
        s = WriteSnapshot(descriptor_log_);
      }
    }
  }

  // Write new record to log file
  if (s.ok()) {
    std::string record;
    edit->EncodeTo(&record);
    s = descriptor_log_->AddRecord(record);
    if (s.ok()) {
      s = descriptor_file_->Sync();
    }
  }

  // If we just created a new descriptor file, install it by writing a
  // new CURRENT file that points to it.
  if (s.ok() && !new_manifest_file.empty()) {
    s = SetCurrentFile(env_, dbname_, manifest_file_number_);
  }

  // Install the new version
  if (s.ok()) {
    assert(current_->next_ == NULL);
    assert(current_->cleanup_mem_ == NULL);
    current_->cleanup_mem_ = cleanup_mem;
    v->next_ = NULL;
    current_->next_ = v;
    current_ = v;
  } else {
    delete v;
    if (!new_manifest_file.empty()) {
      delete descriptor_log_;
      delete descriptor_file_;
      descriptor_log_ = NULL;
      descriptor_file_ = NULL;
      env_->DeleteFile(new_manifest_file);
    }
  }
  //Log(env_, options_->info_log, "State\n%s", current_->DebugString().c_str());

  return s;
}

Status VersionSet::Recover(uint64_t* log_number,
                           SequenceNumber* last_sequence) {
  struct LogReporter : public log::Reader::Reporter {
    Status* status;
    virtual void Corruption(size_t bytes, const Status& s) {
      if (this->status->ok()) *this->status = s;
    }
  };

  // Read "CURRENT" file, which contains a pointer to the current manifest file
  std::string current;
  Status s = ReadFileToString(env_, CurrentFileName(dbname_), &current);
  if (!s.ok()) {
    return s;
  }
  if (current.empty() || current[current.size()-1] != '\n') {
    return Status::Corruption("CURRENT file does not end with newline");
  }
  current.resize(current.size() - 1);

  std::string dscname = dbname_ + "/" + current;
  SequentialFile* file;
  s = env_->NewSequentialFile(dscname, &file);
  if (!s.ok()) {
    return s;
  }

  bool have_log_number = false;
  bool have_next_file = false;
  bool have_last_sequence = false;
  uint64_t next_file = 0;
  Builder builder(this, current_);

  {
    LogReporter reporter;
    reporter.status = &s;
    log::Reader reader(file, &reporter, true/*checksum*/);
    Slice record;
    std::string scratch;
    while (reader.ReadRecord(&record, &scratch) && s.ok()) {
      VersionEdit edit;
      s = edit.DecodeFrom(record);
      if (s.ok()) {
        if (edit.has_comparator_ &&
            edit.comparator_ != icmp_.user_comparator()->Name()) {
          s = Status::InvalidArgument(
              edit.comparator_ + "does not match existing comparator ",
              icmp_.user_comparator()->Name());
        }
      }

      if (s.ok()) {
        builder.Apply(&edit);
      }

      if (edit.has_log_number_) {
        *log_number = edit.log_number_;
        have_log_number = true;
      }

      if (edit.has_next_file_number_) {
        next_file = edit.next_file_number_;
        have_next_file = true;
      }

      if (edit.has_last_sequence_) {
        *last_sequence = edit.last_sequence_;
        have_last_sequence = true;
      }
    }
  }
  delete file;
  file = NULL;

  if (s.ok()) {
    if (!have_next_file) {
      s = Status::Corruption("no meta-nextfile entry in descriptor");
    } else if (!have_log_number) {
      s = Status::Corruption("no meta-lognumber entry in descriptor");
    } else if (!have_last_sequence) {
      s = Status::Corruption("no last-sequence-number entry in descriptor");
    }
  }

  if (s.ok()) {
    Version* v = new Version(this);
    builder.SaveTo(v);
    s = Finalize(v);
    if (!s.ok()) {
      delete v;
    } else {
      // Install recovered version
      v->next_ = NULL;
      current_->next_ = v;
      current_ = v;
      manifest_file_number_ = next_file;
      next_file_number_ = next_file + 1;
    }
  }

  return s;
}

Status VersionSet::Finalize(Version* v) {
  // Precomputed best level for next compaction
  int best_level = -1;
  double best_score = -1;

  Status s;
  for (int level = 0; s.ok() && level < config::kNumLevels; level++) {
    s = SortLevel(v, level);

    // Compute the ratio of current size to size limit.
    uint64_t level_bytes = 0;
    for (int i = 0; i < v->files_[level].size(); i++) {
      level_bytes += v->files_[level][i]->file_size;
    }
    double score = static_cast<double>(level_bytes) / MaxBytesForLevel(level);

    if (level == 0) {
      // Level-0 file sizes are going to be often much smaller than
      // MaxBytesForLevel(0) since we do not account for compression
      // when producing a level-0 file; and too many level-0 files
      // increase merging costs.  So use a file-count limit for
      // level-0 in addition to the byte-count limit.
      double count_score = v->files_[level].size() / 4.0;
      if (count_score > score) {
        score = count_score;
      }
    }

    if (score > best_score) {
      best_level = level;
      best_score = score;
    }
  }

  v->compaction_level_ = best_level;
  v->compaction_score_ = best_score;
  return s;
}

Status VersionSet::WriteSnapshot(log::Writer* log) {
  // TODO: Break up into multiple records to reduce memory usage on recovery?

  // Save metadata
  VersionEdit edit;
  edit.SetComparatorName(icmp_.user_comparator()->Name());

  // Save compaction pointers
  for (int level = 0; level < config::kNumLevels; level++) {
    if (!compact_pointer_[level].empty()) {
      InternalKey key;
      key.DecodeFrom(compact_pointer_[level]);
      edit.SetCompactPointer(level, key);
    }
  }

  // Save files
  for (int level = 0; level < config::kNumLevels; level++) {
    const std::vector<FileMetaData*>& files = current_->files_[level];
    for (int i = 0; i < files.size(); i++) {
      const FileMetaData* f = files[i];
      edit.AddFile(level, f->number, f->file_size, f->smallest, f->largest);
    }
  }

  // Save large value refs
  for (LargeValueMap::const_iterator it = large_value_refs_.begin();
       it != large_value_refs_.end();
       ++it) {
    const LargeValueRef& ref = it->first;
    const LargeReferencesSet& pointers = it->second;
    for (LargeReferencesSet::const_iterator j = pointers.begin();
         j != pointers.end();
         ++j) {
      edit.AddLargeValueRef(ref, j->first, j->second);
    }
  }

  std::string record;
  edit.EncodeTo(&record);
  return log->AddRecord(record);
}

// Helper to sort by tables_[file_number].smallest
struct VersionSet::BySmallestKey {
  const InternalKeyComparator* internal_comparator;

  bool operator()(FileMetaData* f1, FileMetaData* f2) const {
    return internal_comparator->Compare(f1->smallest, f2->smallest) < 0;
  }
};

Status VersionSet::SortLevel(Version* v, uint64_t level) {
  Status result;
  BySmallestKey cmp;
  cmp.internal_comparator = &icmp_;
  std::sort(v->files_[level].begin(), v->files_[level].end(), cmp);

  if (result.ok() && level > 0) {
    // There should be no overlap
    for (int i = 1; i < v->files_[level].size(); i++) {
      const InternalKey& prev_end = v->files_[level][i-1]->largest;
      const InternalKey& this_begin = v->files_[level][i]->smallest;
      if (icmp_.Compare(prev_end, this_begin) >= 0) {
        result = Status::Corruption(
            "overlapping ranges in same level",
            (EscapeString(prev_end.Encode()) + " vs. " +
             EscapeString(this_begin.Encode())));
        break;
      }
    }
  }
  return result;
}

int VersionSet::NumLevelFiles(int level) const {
  assert(level >= 0);
  assert(level < config::kNumLevels);
  return current_->files_[level].size();
}

uint64_t VersionSet::ApproximateOffsetOf(Version* v, const InternalKey& ikey) {
  uint64_t result = 0;
  for (int level = 0; level < config::kNumLevels; level++) {
    const std::vector<FileMetaData*>& files = v->files_[level];
    for (int i = 0; i < files.size(); i++) {
      if (icmp_.Compare(files[i]->largest, ikey) <= 0) {
        // Entire file is before "ikey", so just add the file size
        result += files[i]->file_size;
      } else if (icmp_.Compare(files[i]->smallest, ikey) > 0) {
        // Entire file is after "ikey", so ignore
        if (level > 0) {
          // Files other than level 0 are sorted by meta->smallest, so
          // no further files in this level will contain data for
          // "ikey".
          break;
        }
      } else {
        // "ikey" falls in the range for this table.  Add the
        // approximate offset of "ikey" within the table.
        Table* tableptr;
        Iterator* iter = table_cache_->NewIterator(
            ReadOptions(), files[i]->number, &tableptr);
        if (tableptr != NULL) {
          result += tableptr->ApproximateOffsetOf(ikey.Encode());
        }
        delete iter;
      }
    }
  }

  // Add in large value files which are references from internal keys
  // stored in the table files
  //
  // TODO(opt): this is O(# large values in db).  If this becomes too slow,
  // we could store an auxiliary data structure indexed by internal key
  for (LargeValueMap::const_iterator it = large_value_refs_.begin();
       it != large_value_refs_.end();
       ++it) {
    const LargeValueRef& lref = it->first;
    for (LargeReferencesSet::const_iterator it2 = it->second.begin();
         it2 != it->second.end();
         ++it2) {
      if (icmp_.Compare(it2->second, ikey.Encode()) <= 0) {
        // Internal key for large value is before our key of interest
        result += lref.ValueSize();
      }
    }
  }


  return result;
}

bool VersionSet::RegisterLargeValueRef(const LargeValueRef& large_ref,
                                       uint64_t fnum,
                                       const InternalKey& internal_key) {
  LargeReferencesSet* refs = &large_value_refs_[large_ref];
  bool is_first = refs->empty();
  refs->insert(make_pair(fnum, internal_key.Encode().ToString()));
  return is_first;
}

void VersionSet::CleanupLargeValueRefs(const std::set<uint64_t>& live_tables,
                                       uint64_t log_file_num) {
  for (LargeValueMap::iterator it = large_value_refs_.begin();
       it != large_value_refs_.end();
       ) {
    LargeReferencesSet* refs = &it->second;
    for (LargeReferencesSet::iterator ref_it = refs->begin();
         ref_it != refs->end();
         ) {
      if (ref_it->first != log_file_num &&              // Not in log file
          live_tables.count(ref_it->first) == 0) {      // Not in a live table
        // No longer live: erase
        LargeReferencesSet::iterator to_erase = ref_it;
        ++ref_it;
        refs->erase(to_erase);
      } else {
        // Still live: leave this reference alone
        ++ref_it;
      }
    }
    if (refs->empty()) {
      // No longer any live references to this large value: remove from
      // large_value_refs
      Log(env_, options_->info_log, "large value is dead: '%s'",
          LargeValueRefToFilenameString(it->first).c_str());
      LargeValueMap::iterator to_erase = it;
      ++it;
      large_value_refs_.erase(to_erase);
    } else {
      ++it;
    }
  }
}

bool VersionSet::LargeValueIsLive(const LargeValueRef& large_ref) {
  LargeValueMap::iterator it = large_value_refs_.find(large_ref);
  if (it == large_value_refs_.end()) {
    return false;
  } else {
    assert(!it->second.empty());
    return true;
  }
}

void VersionSet::MaybeDeleteOldVersions() {
  // Note: it is important to delete versions in order since a newer
  // version with zero refs may be holding a pointer to a memtable
  // that is used by somebody who has a ref on an older version.
  while (oldest_ != current_ && oldest_->refs_ == 0) {
    Version* next = oldest_->next_;
    delete oldest_;
    oldest_ = next;
  }
}

void VersionSet::AddLiveFiles(std::set<uint64_t>* live) {
  for (Version* v = oldest_; v != NULL; v = v->next_) {
    for (int level = 0; level < config::kNumLevels; level++) {
      const std::vector<FileMetaData*>& files = v->files_[level];
      for (int i = 0; i < files.size(); i++) {
        live->insert(files[i]->number);
      }
    }
  }
}

// Store in "*inputs" all files in "level" that overlap [begin,end]
void VersionSet::GetOverlappingInputs(
    int level,
    const InternalKey& begin,
    const InternalKey& end,
    std::vector<FileMetaData*>* inputs) {
  inputs->clear();
  Slice user_begin = begin.user_key();
  Slice user_end = end.user_key();
  const Comparator* user_cmp = icmp_.user_comparator();
  for (int i = 0; i < current_->files_[level].size(); i++) {
    FileMetaData* f = current_->files_[level][i];
    if (user_cmp->Compare(f->largest.user_key(), user_begin) < 0 ||
        user_cmp->Compare(f->smallest.user_key(), user_end) > 0) {
      // Either completely before or after range; skip it
    } else {
      inputs->push_back(f);
    }
  }
}

// Stores the minimal range that covers all entries in inputs in
// *smallest, *largest.
// REQUIRES: inputs is not empty
void VersionSet::GetRange(const std::vector<FileMetaData*>& inputs,
                          InternalKey* smallest,
                          InternalKey* largest) {
  assert(!inputs.empty());
  smallest->Clear();
  largest->Clear();
  for (int i = 0; i < inputs.size(); i++) {
    FileMetaData* f = inputs[i];
    if (i == 0) {
      *smallest = f->smallest;
      *largest = f->largest;
    } else {
      if (icmp_.Compare(f->smallest, *smallest) < 0) {
        *smallest = f->smallest;
      }
      if (icmp_.Compare(f->largest, *largest) > 0) {
        *largest = f->largest;
      }
    }
  }
}

Iterator* VersionSet::MakeInputIterator(Compaction* c) {
  ReadOptions options;
  options.verify_checksums = options_->paranoid_checks;
  options.fill_cache = false;

  // Level-0 files have to be merged together.  For other levels,
  // we will make a concatenating iterator per level.
  // TODO(opt): use concatenating iterator for level-0 if there is no overlap
  const int space = (c->level() == 0 ? c->inputs_[0].size() + 1 : 2);
  Iterator** list = new Iterator*[space];
  int num = 0;
  for (int which = 0; which < 2; which++) {
    if (!c->inputs_[which].empty()) {
      if (c->level() + which == 0) {
        const std::vector<FileMetaData*>& files = c->inputs_[which];
        for (int i = 0; i < files.size(); i++) {
          list[num++] = table_cache_->NewIterator(options, files[i]->number);
        }
      } else {
        // Create concatenating iterator for the files from this level
        list[num++] = NewTwoLevelIterator(
            new Version::LevelFileNumIterator(
                c->input_version_, &c->inputs_[which]),
            &GetFileIterator, table_cache_, options);
      }
    }
  }
  assert(num <= space);
  Iterator* result = NewMergingIterator(&icmp_, list, num);
  delete[] list;
  return result;
}

Compaction* VersionSet::PickCompaction() {
  if (!NeedsCompaction()) {
    return NULL;
  }
  const int level = current_->compaction_level_;
  assert(level >= 0);

  Compaction* c = new Compaction(level);
  c->input_version_ = current_;
  c->input_version_->Ref();

  // Pick the first file that comes after compact_pointer_[level]
  for (int i = 0; i < current_->files_[level].size(); i++) {
    FileMetaData* f = current_->files_[level][i];
    if (compact_pointer_[level].empty() ||
        icmp_.Compare(f->largest.Encode(), compact_pointer_[level]) > 0) {
      c->inputs_[0].push_back(f);
      break;
    }
  }
  if (c->inputs_[0].empty()) {
    // Wrap-around to the beginning of the key space
    c->inputs_[0].push_back(current_->files_[level][0]);
  }

  // Find the range we are compacting
  InternalKey smallest, largest;
  GetRange(c->inputs_[0], &smallest, &largest);

  // Files in level 0 may overlap each other, so pick up all overlapping ones
  if (level == 0) {
    // Note that the next call will discard the file we placed in
    // c->inputs_[0] earlier and replace it with an overlapping set
    // which will include the picked file.
    GetOverlappingInputs(0, smallest, largest, &c->inputs_[0]);
    assert(!c->inputs_[0].empty());
    GetRange(c->inputs_[0], &smallest, &largest);
  }

  GetOverlappingInputs(level+1, smallest, largest, &c->inputs_[1]);

  // See if we can grow the number of inputs in "level" without
  // changing the number of "level+1" files we pick up.
  if (!c->inputs_[1].empty()) {
    // Get entire range covered by compaction
    std::vector<FileMetaData*> all = c->inputs_[0];
    all.insert(all.end(), c->inputs_[1].begin(), c->inputs_[1].end());
    InternalKey all_start, all_limit;
    GetRange(all, &all_start, &all_limit);

    std::vector<FileMetaData*> expanded0;
    GetOverlappingInputs(level, all_start, all_limit, &expanded0);
    if (expanded0.size() > c->inputs_[0].size()) {
      InternalKey new_start, new_limit;
      GetRange(expanded0, &new_start, &new_limit);
      std::vector<FileMetaData*> expanded1;
      GetOverlappingInputs(level+1, new_start, new_limit, &expanded1);
      if (expanded1.size() == c->inputs_[1].size()) {
        Log(env_, options_->info_log,
            "Expanding@%d %d+%d to %d+%d\n",
            level,
            int(c->inputs_[0].size()),
            int(c->inputs_[1].size()),
            int(expanded0.size()),
            int(expanded1.size()));
        smallest = new_start;
        largest = new_limit;
        c->inputs_[0] = expanded0;
        c->inputs_[1] = expanded1;
      }
    }
  }

  if (false) {
    Log(env_, options_->info_log, "Compacting %d '%s' .. '%s'",
        level,
        EscapeString(smallest.Encode()).c_str(),
        EscapeString(largest.Encode()).c_str());
  }

  // Update the place where we will do the next compaction for this level.
  // We update this immediately instead of waiting for the VersionEdit
  // to be applied so that if the compaction fails, we will try a different
  // key range next time.
  compact_pointer_[level] = largest.Encode().ToString();
  c->edit_.SetCompactPointer(level, largest);

  return c;
}

Compaction* VersionSet::CompactRange(
    int level,
    const InternalKey& begin,
    const InternalKey& end) {
  std::vector<FileMetaData*> inputs;
  GetOverlappingInputs(level, begin, end, &inputs);
  if (inputs.empty()) {
    return NULL;
  }

  Compaction* c = new Compaction(level);
  c->input_version_ = current_;
  c->input_version_->Ref();
  c->inputs_[0] = inputs;

  // Find the range we are compacting
  InternalKey smallest, largest;
  GetRange(c->inputs_[0], &smallest, &largest);

  GetOverlappingInputs(level+1, smallest, largest, &c->inputs_[1]);
  if (false) {
    Log(env_, options_->info_log, "Compacting %d '%s' .. '%s'",
        level,
        EscapeString(smallest.Encode()).c_str(),
        EscapeString(largest.Encode()).c_str());
  }
  return c;
}

Compaction::Compaction(int level)
    : level_(level),
      max_output_file_size_(MaxFileSizeForLevel(level)),
      input_version_(NULL) {
  for (int i = 0; i < config::kNumLevels; i++) {
    level_ptrs_[i] = 0;
  }
}

Compaction::~Compaction() {
  if (input_version_ != NULL) {
    input_version_->Unref();
  }
}

void Compaction::AddInputDeletions(VersionEdit* edit) {
  for (int which = 0; which < 2; which++) {
    for (int i = 0; i < inputs_[which].size(); i++) {
      edit->DeleteFile(level_ + which, inputs_[which][i]->number);
    }
  }
}

bool Compaction::IsBaseLevelForKey(const Slice& user_key) {
  // Maybe use binary search to find right entry instead of linear search?
  const Comparator* user_cmp = input_version_->vset_->icmp_.user_comparator();
  for (int lvl = level_ + 2; lvl < config::kNumLevels; lvl++) {
    const std::vector<FileMetaData*>& files = input_version_->files_[lvl];
    for (; level_ptrs_[lvl] < files.size(); ) {
      FileMetaData* f = files[level_ptrs_[lvl]];
      if (user_cmp->Compare(user_key, f->largest.user_key()) <= 0) {
        // We've advanced far enough
        if (user_cmp->Compare(user_key, f->smallest.user_key()) >= 0) {
          // Key falls in this file's range, so definitely not base level
          return false;
        }
        break;
      }
      level_ptrs_[lvl]++;
    }
  }
  return true;
}

void Compaction::ReleaseInputs() {
  if (input_version_ != NULL) {
    input_version_->Unref();
    input_version_ = NULL;
  }
}

}
