// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef STORAGE_LEVELDB_DB_DB_IMPL_H_
#define STORAGE_LEVELDB_DB_DB_IMPL_H_

#include <set>
#include "db/dbformat.h"
#include "db/log_writer.h"
#include "db/snapshot.h"
#include "include/db.h"
#include "include/env.h"
#include "port/port.h"

namespace leveldb {

class MemTable;
class TableCache;
class Version;
class VersionEdit;
class VersionSet;

class DBImpl : public DB {
 public:
  DBImpl(const Options& options, const std::string& dbname);
  virtual ~DBImpl();

  // Implementations of the DB interface
  virtual Status Put(const WriteOptions&, const Slice& key, const Slice& value);
  virtual Status Delete(const WriteOptions&, const Slice& key);
  virtual Status Write(const WriteOptions& options, WriteBatch* updates);
  virtual Status Get(const ReadOptions& options,
                     const Slice& key,
                     std::string* value);
  virtual Iterator* NewIterator(const ReadOptions&);
  virtual const Snapshot* GetSnapshot();
  virtual void ReleaseSnapshot(const Snapshot* snapshot);
  virtual bool GetProperty(const Slice& property, uint64_t* value);
  virtual void GetApproximateSizes(const Range* range, int n, uint64_t* sizes);

  // Extra methods (for testing) that are not in the public DB interface

  // Compact any files in the named level that overlap [begin,end]
  void TEST_CompactRange(
      int level,
      const std::string& begin,
      const std::string& end);

  // Force current memtable contents to be compacted.
  Status TEST_CompactMemTable();

  // Return an internal iterator over the current state of the database.
  // The keys of this iterator are internal keys (see format.h).
  // The returned iterator should be deleted when no longer needed.
  Iterator* TEST_NewInternalIterator();

 private:
  friend class DB;

  Iterator* NewInternalIterator(const ReadOptions&,
                                SequenceNumber* latest_snapshot);

  Status NewDB();

  // Recover the descriptor from persistent storage.  May do a significant
  // amount of work to recover recently logged updates.  Any changes to
  // be made to the descriptor are added to *edit.
  Status Recover(VersionEdit* edit);

  // Apply the specified updates and save the resulting descriptor to
  // persistent storage.  If cleanup_mem is non-NULL, arrange to
  // delete it when all existing snapshots have gone away iff Install()
  // returns OK.
  Status Install(VersionEdit* edit,
                 uint64_t new_log_number,
                 MemTable* cleanup_mem);

  void MaybeIgnoreError(Status* s) const;

  // Delete any unneeded files and stale in-memory entries.
  void DeleteObsoleteFiles();

  // Called when an iterator over a particular version of the
  // descriptor goes away.
  static void Unref(void* arg1, void* arg2);

  // Compact the in-memory write buffer to disk.  Switches to a new
  // log-file/memtable and writes a new descriptor iff successful.
  Status CompactMemTable();

  Status RecoverLogFile(uint64_t log_number,
                        VersionEdit* edit,
                        SequenceNumber* max_sequence);

  Status WriteLevel0Table(MemTable* mem, VersionEdit* edit);

  bool HasLargeValues(const WriteBatch& batch) const;

  // Process data in "*updates" and return a status.  "assigned_seq"
  // is the sequence number assigned to the first mod in "*updates".
  // If no large values are encountered, "*final" is set to "updates".
  // If large values were encountered, registers the references of the
  // large values with the VersionSet, writes the large values to
  // files (if appropriate), and allocates a new WriteBatch with the
  // large values replaced with indirect references and stores a
  // pointer to the new WriteBatch in *final.  If *final != updates on
  // return, then the client should delete *final when no longer
  // needed.  Returns OK on success, and an appropriate error
  // otherwise.
  Status HandleLargeValues(SequenceNumber assigned_seq,
                           WriteBatch* updates,
                           WriteBatch** final);

  // Helper routine for HandleLargeValues
  void MaybeCompressLargeValue(
      const Slice& raw_value,
      Slice* file_bytes,
      std::string* scratch,
      LargeValueRef* ref);

  struct CompactionState;

  void MaybeScheduleCompaction();
  static void BGWork(void* db);
  void BackgroundCall();
  void BackgroundCompaction();
  void CleanupCompaction(CompactionState* compact);
  Status DoCompactionWork(CompactionState* compact);

  Status OpenCompactionOutputFile(CompactionState* compact);
  Status FinishCompactionOutputFile(CompactionState* compact, Iterator* input);
  Status InstallCompactionResults(CompactionState* compact);

  // Constant after construction
  Env* const env_;
  const InternalKeyComparator internal_comparator_;
  const Options options_;  // options_.comparator == &internal_comparator_
  bool owns_info_log_;
  const std::string dbname_;

  // table_cache_ provides its own synchronization
  TableCache* table_cache_;

  // Lock over the persistent DB state.  Non-NULL iff successfully acquired.
  FileLock* db_lock_;

  // State below is protected by mutex_
  port::Mutex mutex_;
  port::AtomicPointer shutting_down_;
  port::CondVar bg_cv_;         // Signalled when !bg_compaction_scheduled_
  port::CondVar compacting_cv_;  // Signalled when !compacting_
  SequenceNumber last_sequence_;
  MemTable* mem_;
  WritableFile* logfile_;
  log::Writer* log_;
  uint64_t log_number_;
  SnapshotList snapshots_;

  // Set of table files to protect from deletion because they are
  // part of ongoing compactions.
  std::set<uint64_t> pending_outputs_;

  // Has a background compaction been scheduled or is running?
  bool bg_compaction_scheduled_;

  // Is there a compaction running?
  bool compacting_;

  VersionSet* versions_;

  // Have we encountered a background error in paranoid mode?
  Status bg_error_;

  // No copying allowed
  DBImpl(const DBImpl&);
  void operator=(const DBImpl&);

  const Comparator* user_comparator() const {
    return internal_comparator_.user_comparator();
  }
};

// Sanitize db options.  The caller should delete result.info_log if
// it is not equal to src.info_log.
extern Options SanitizeOptions(const std::string& db,
                               const InternalKeyComparator* icmp,
                               const Options& src);

}

#endif  // STORAGE_LEVELDB_DB_DB_IMPL_H_
