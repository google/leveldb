// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
//
// The representation of a DBImpl consists of a set of Versions.  The
// newest version is called "current".  Older versions may be kept
// around to provide a consistent view to live iterators.
//
// Each Version keeps track of a set of Table files per level.  The
// entire set of versions is maintained in a VersionSet.
//
// Version,VersionSet are thread-compatible, but require external
// synchronization on all accesses.

#ifndef STORAGE_LEVELDB_DB_VERSION_SET_H_
#define STORAGE_LEVELDB_DB_VERSION_SET_H_

#include <map>
#include <set>
#include <vector>
#include "db/dbformat.h"
#include "db/version_edit.h"
#include "port/port.h"

namespace leveldb {

// Grouping of constants.  We may want to make some of these
// parameters set via options.
namespace config {
static const int kNumLevels = 7;
}

namespace log { class Writer; }

class Compaction;
class Iterator;
class MemTable;
class TableBuilder;
class TableCache;
class Version;
class VersionSet;
class WritableFile;

class Version {
 public:
  // Append to *iters a sequence of iterators that will
  // yield the contents of this Version when merged together.
  // REQUIRES: This version has been saved (see VersionSet::SaveTo)
  void AddIterators(const ReadOptions&, std::vector<Iterator*>* iters);

  // Reference count management (so Versions do not disappear out from
  // under live iterators)
  void Ref();
  void Unref();

  // Return a human readable string that describes this version's contents.
  std::string DebugString() const;

 private:
  friend class Compaction;
  friend class VersionSet;

  class LevelFileNumIterator;
  Iterator* NewConcatenatingIterator(const ReadOptions&, int level) const;

  VersionSet* vset_;            // VersionSet to which this Version belongs
  Version* next_;               // Next version in linked list
  int refs_;                    // Number of live refs to this version
  MemTable* cleanup_mem_;       // NULL, or table to delete when version dropped

  // List of files per level
  std::vector<FileMetaData*> files_[config::kNumLevels];

  // Level that should be compacted next and its compaction score.
  // Score < 1 means compaction is not strictly needed.  These fields
  // are initialized by Finalize().
  double compaction_score_;
  int compaction_level_;

  explicit Version(VersionSet* vset)
      : vset_(vset), next_(NULL), refs_(0),
        cleanup_mem_(NULL),
        compaction_score_(-1),
        compaction_level_(-1) {
  }

  ~Version();

  // No copying allowed
  Version(const Version&);
  void operator=(const Version&);
};

class VersionSet {
 public:
  VersionSet(const std::string& dbname,
             const Options* options,
             TableCache* table_cache,
             const InternalKeyComparator*);
  ~VersionSet();

  // Apply *edit to the current version to form a new descriptor that
  // is both saved to persistent state and installed as the new
  // current version.  Iff Apply() returns OK, arrange to delete
  // cleanup_mem (if cleanup_mem != NULL) when it is no longer needed
  // by older versions.
  Status LogAndApply(VersionEdit* edit, MemTable* cleanup_mem);

  // Recover the last saved descriptor from persistent storage.
  Status Recover(uint64_t* log_number, SequenceNumber* last_sequence);

  // Save current contents to *log
  Status WriteSnapshot(log::Writer* log);

  // Return the current version.
  Version* current() const { return current_; }

  // Return the current manifest file number
  uint64_t ManifestFileNumber() const { return manifest_file_number_; }

  // Allocate and return a new file number
  uint64_t NewFileNumber() { return next_file_number_++; }

  // Return the number of Table files at the specified level.
  int NumLevelFiles(int level) const;

  // Pick level and inputs for a new compaction.
  // Returns NULL if there is no compaction to be done.
  // Otherwise returns a pointer to a heap-allocated object that
  // describes the compaction.  Caller should delete the result.
  Compaction* PickCompaction();

  // Return a compaction object for compacting the range [begin,end] in
  // the specified level.  Returns NULL if there is nothing in that
  // level that overlaps the specified range.  Caller should delete
  // the result.
  Compaction* CompactRange(
      int level,
      const InternalKey& begin,
      const InternalKey& end);

  // Create an iterator that reads over the compaction inputs for "*c".
  // The caller should delete the iterator when no longer needed.
  Iterator* MakeInputIterator(Compaction* c);

  // Returns true iff some level needs a compaction.
  bool NeedsCompaction() const { return current_->compaction_score_ >= 1; }

  // Add all files listed in any live version to *live.
  // May also mutate some internal state.
  void AddLiveFiles(std::set<uint64_t>* live);

  // Return the approximate offset in the database of the data for
  // "key" as of version "v".
  uint64_t ApproximateOffsetOf(Version* v, const InternalKey& key);

  // Register a reference to a large value with the specified
  // large_ref from the specified file number.  Returns "true" if this
  // is the first recorded reference to the "large_ref" value in the
  // database, and false otherwise.
  bool RegisterLargeValueRef(const LargeValueRef& large_ref,
                             uint64_t filenum,
                             const InternalKey& internal_key);

  // Cleanup the large value reference state by eliminating any
  // references from files that are not includes in either "live_tables"
  // or "log_file".
  void CleanupLargeValueRefs(const std::set<uint64_t>& live_tables,
                             uint64_t log_file_num);

  // Returns true if a large value with the given reference is live.
  bool LargeValueIsLive(const LargeValueRef& large_ref);

 private:
  class Builder;

  friend class Compaction;
  friend class Version;

  Status Finalize(Version* v);

  // Delete any old versions that are no longer needed.
  void MaybeDeleteOldVersions();

  struct BySmallestKey;
  Status SortLevel(Version* v, uint64_t level);

  void GetOverlappingInputs(
      int level,
      const InternalKey& begin,
      const InternalKey& end,
      std::vector<FileMetaData*>* inputs);

  void GetRange(const std::vector<FileMetaData*>& inputs,
                InternalKey* smallest,
                InternalKey* largest);

  Env* const env_;
  const std::string dbname_;
  const Options* const options_;
  TableCache* const table_cache_;
  const InternalKeyComparator icmp_;
  uint64_t next_file_number_;
  uint64_t manifest_file_number_;

  // Opened lazily
  WritableFile* descriptor_file_;
  log::Writer* descriptor_log_;

  // Versions are kept in a singly linked list that is never empty
  Version* current_;    // Pointer to the last (newest) list entry
  Version* oldest_;     // Pointer to the first (oldest) list entry

  // Map from large value reference to the set of <file numbers,internal_key>
  // values containing references to the value.  We keep the
  // internal key as a std::string rather than as an InternalKey because
  // we want to be able to easily use a set.
  typedef std::set<std::pair<uint64_t, std::string> > LargeReferencesSet;
  typedef std::map<LargeValueRef, LargeReferencesSet> LargeValueMap;
  LargeValueMap large_value_refs_;

  // Per-level key at which the next compaction at that level should start.
  // Either an empty string, or a valid InternalKey.
  std::string compact_pointer_[config::kNumLevels];

  // No copying allowed
  VersionSet(const VersionSet&);
  void operator=(const VersionSet&);
};

// A Compaction encapsulates information about a compaction.
class Compaction {
 public:
  ~Compaction();

  // Return the level that is being compacted.  Inputs from "level"
  // and "level+1" will be merged to produce a set of "level+1" files.
  int level() const { return level_; }

  // Return the object that holds the edits to the descriptor done
  // by this compaction.
  VersionEdit* edit() { return &edit_; }

  // "which" must be either 0 or 1
  int num_input_files(int which) const { return inputs_[which].size(); }

  // Return the ith input file at "level()+which" ("which" must be 0 or 1).
  FileMetaData* input(int which, int i) const { return inputs_[which][i]; }

  // Maximum size of files to build during this compaction.
  uint64_t MaxOutputFileSize() const { return max_output_file_size_; }

  // Add all inputs to this compaction as delete operations to *edit.
  void AddInputDeletions(VersionEdit* edit);

  // Returns true if the information we have available guarantees that
  // the compaction is producing data in "level+1" for which no data exists
  // in levels greater than "level+1".
  bool IsBaseLevelForKey(const Slice& user_key);

  // Release the input version for the compaction, once the compaction
  // is successful.
  void ReleaseInputs();

 private:
  friend class Version;
  friend class VersionSet;

  explicit Compaction(int level);

  int level_;
  uint64_t max_output_file_size_;
  Version* input_version_;
  VersionEdit edit_;

  // Each compaction reads inputs from "level_" and "level_+1"
  std::vector<FileMetaData*> inputs_[2];      // The two sets of inputs

  // State for implementing IsBaseLevelForKey

  // level_ptrs_ holds indices into input_version_->levels_: our state
  // is that we are positioned at one of the file ranges for each
  // higher level than the ones involved in this compaction (i.e. for
  // all L >= level_ + 2).
  int level_ptrs_[config::kNumLevels];
};

}

#endif  // STORAGE_LEVELDB_DB_VERSION_SET_H_
