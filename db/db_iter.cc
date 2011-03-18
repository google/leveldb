// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "db/db_iter.h"

#include "db/filename.h"
#include "db/dbformat.h"
#include "include/env.h"
#include "include/iterator.h"
#include "port/port.h"
#include "util/logging.h"
#include "util/mutexlock.h"

namespace leveldb {

#if 0
static void DumpInternalIter(Iterator* iter) {
  for (iter->SeekToFirst(); iter->Valid(); iter->Next()) {
    ParsedInternalKey k;
    if (!ParseInternalKey(iter->key(), &k)) {
      fprintf(stderr, "Corrupt '%s'\n", EscapeString(iter->key()).c_str());
    } else {
      fprintf(stderr, "@ '%s'\n", k.DebugString().c_str());
    }
  }
}
#endif

namespace {

// Memtables and sstables that make the DB representation contain
// (userkey,seq,type) => uservalue entries.  DBIter
// combines multiple entries for the same userkey found in the DB
// representation into a single entry while accounting for sequence
// numbers, deletion markers, overwrites, etc.
class DBIter: public Iterator {
 public:
  DBIter(const std::string* dbname, Env* env,
         const Comparator* cmp, Iterator* iter, SequenceNumber s)
      : dbname_(dbname),
        env_(env),
        user_comparator_(cmp),
        iter_(iter),
        sequence_(s),
        large_(NULL),
        valid_(false) {
  }
  virtual ~DBIter() {
    delete iter_;
    delete large_;
  }
  virtual bool Valid() const { return valid_; }
  virtual Slice key() const {
    assert(valid_);
    return key_;
  }
  virtual Slice value() const {
    assert(valid_);
    if (large_ == NULL) {
      return value_;
    } else {
      MutexLock l(&large_->mutex);
      if (!large_->produced) {
        ReadIndirectValue();
      }
      return large_->value;
    }
  }

  virtual void Next() {
    assert(valid_);
    // iter_ is already positioned past DBIter::key()
    FindNextUserEntry();
  }

  virtual void Prev() {
    assert(valid_);
    bool ignored;
    ScanUntilBeforeCurrentKey(&ignored);
    FindPrevUserEntry();
  }

  virtual void Seek(const Slice& target) {
    ParsedInternalKey ikey(target, sequence_, kValueTypeForSeek);
    std::string tmp;
    AppendInternalKey(&tmp, ikey);
    iter_->Seek(tmp);
    FindNextUserEntry();
  }
  virtual void SeekToFirst() {
    iter_->SeekToFirst();
    FindNextUserEntry();
  }

  virtual void SeekToLast();

  virtual Status status() const {
    if (status_.ok()) {
      if (large_ != NULL && !large_->status.ok()) return large_->status;
      return iter_->status();
    } else {
      return status_;
    }
  }

 private:
  void FindNextUserEntry();
  void FindPrevUserEntry();
  void SaveKey(const Slice& k) { key_.assign(k.data(), k.size()); }
  void SaveValue(const Slice& v) {
    if (value_.capacity() > v.size() + 1048576) {
      std::string empty;
      swap(empty, value_);
    }
    value_.assign(v.data(), v.size());
  }
  bool ParseKey(ParsedInternalKey* key);
  void SkipPast(const Slice& k);
  void ScanUntilBeforeCurrentKey(bool* found_live);

  void ReadIndirectValue() const;

  struct Large {
    port::Mutex mutex;
    std::string value;
    bool produced;
    Status status;
  };

  const std::string* const dbname_;
  Env* const env_;

  const Comparator* const user_comparator_;

  // iter_ is positioned just past current entry for DBIter if valid_
  Iterator* const iter_;

  SequenceNumber const sequence_;
  Status status_;
  std::string key_;                  // Always a user key
  std::string value_;
  Large* large_;      // Non-NULL if value is an indirect reference
  bool valid_;

  // No copying allowed
  DBIter(const DBIter&);
  void operator=(const DBIter&);
};

inline bool DBIter::ParseKey(ParsedInternalKey* ikey) {
  if (!ParseInternalKey(iter_->key(), ikey)) {
    status_ = Status::Corruption("corrupted internal key in DBIter");
    return false;
  } else {
    return true;
  }
}

void DBIter::FindNextUserEntry() {
  if (large_ != NULL) {
    if (status_.ok() && !large_->status.ok()) {
      status_ = large_->status;
    }
    delete large_;
    large_ = NULL;
  }
  while (iter_->Valid()) {
    ParsedInternalKey ikey;
    if (!ParseKey(&ikey)) {
      // Skip past corrupted entry
      iter_->Next();
      continue;
    }
    if (ikey.sequence > sequence_) {
      // Ignore entries newer than the snapshot
      iter_->Next();
      continue;
    }

    switch (ikey.type) {
      case kTypeDeletion:
        SaveKey(ikey.user_key);  // Make local copy for use by SkipPast()
        iter_->Next();
        SkipPast(key_);
        // Do not return deleted entries.  Instead keep looping.
        break;

      case kTypeValue:
        SaveKey(ikey.user_key);
        SaveValue(iter_->value());
        iter_->Next();
        SkipPast(key_);
        // Yield the value we just found.
        valid_ = true;
        return;

      case kTypeLargeValueRef:
        SaveKey(ikey.user_key);
        // Save the large value ref as value_, and read it lazily on a call
        // to value()
        SaveValue(iter_->value());
        large_ = new Large;
        large_->produced = false;
        iter_->Next();
        SkipPast(key_);
        // Yield the value we just found.
        valid_ = true;
        return;
    }
  }
  valid_ = false;
  key_.clear();
  value_.clear();
  assert(large_ == NULL);
}

void DBIter::SkipPast(const Slice& k) {
  while (iter_->Valid()) {
    ParsedInternalKey ikey;
    // Note that if we cannot parse an internal key, we keep looping
    // so that if we have a run like the following:
    //     <x,100,v> => value100
    //     <corrupted entry for user key x>
    //     <x,50,v> => value50
    // we will skip over the corrupted entry as well as value50.
    if (ParseKey(&ikey) && user_comparator_->Compare(ikey.user_key, k) != 0) {
      break;
    }
    iter_->Next();
  }
}

void DBIter::SeekToLast() {
  // Position iter_ at the last uncorrupted user key and then
  // let FindPrevUserEntry() do the heavy lifting to find
  // a user key that is live.
  iter_->SeekToLast();
  ParsedInternalKey current;
  while (iter_->Valid() && !ParseKey(&current)) {
    iter_->Prev();
  }
  if (iter_->Valid()) {
    SaveKey(current.user_key);
  }
  FindPrevUserEntry();
}

// Let X be the user key at which iter_ is currently positioned.
// Adjust DBIter to point at the last entry with a key <= X that
// has a live value.
void DBIter::FindPrevUserEntry() {
  // Consider the following example:
  //
  //     A@540
  //     A@400
  //
  //     B@300
  //     B@200
  //     B@100        <- iter_
  //
  //     C@301
  //     C@201
  //
  // The comments marked "(first iteration)" below relate what happens
  // for the preceding example in the first iteration of the while loop
  // below.  There may be more than one iteration either if there are
  // no live values for B, or if there is a corruption.
  while (iter_->Valid()) {
    std::string saved = key_;
    bool found_live;
    ScanUntilBeforeCurrentKey(&found_live);
    // (first iteration) iter_ at A@400
    if (found_live) {
      // Step forward into range of entries with user key >= saved
      if (!iter_->Valid()) {
        iter_->SeekToFirst();
      } else {
        iter_->Next();
      }
      // (first iteration) iter_ at B@300

      FindNextUserEntry();  // Sets key_ to the key of the next value it found
      if (valid_ && user_comparator_->Compare(key_, saved) == 0) {
        // (first iteration) iter_ at C@301
        return;
      }

      // FindNextUserEntry() could not find any entries under the
      // user key "saved".  This is probably a corruption since
      // ScanUntilBefore(saved) found a live value.  So we skip
      // backwards to an earlier key and ignore the corrupted
      // entries for "saved".
      //
      // (first iteration) iter_ at C@301 and saved == "B"
      key_ = saved;
      bool ignored;
      ScanUntilBeforeCurrentKey(&ignored);
      // (first iteration) iter_ at A@400
    }
  }
  valid_ = false;
  key_.clear();
  value_.clear();
}

void DBIter::ScanUntilBeforeCurrentKey(bool* found_live) {
  *found_live = false;
  if (!iter_->Valid()) {
    iter_->SeekToLast();
  }

  while (iter_->Valid()) {
    ParsedInternalKey current;
    if (!ParseKey(&current)) {
      iter_->Prev();
      continue;
    }

    if (current.sequence > sequence_) {
      // Ignore entries that are serialized after this read
      iter_->Prev();
      continue;
    }

    const int cmp = user_comparator_->Compare(current.user_key, key_);
    if (cmp < 0) {
      SaveKey(current.user_key);
      return;
    } else if (cmp == 0) {
      switch (current.type) {
        case kTypeDeletion:
          *found_live = false;
          break;

        case kTypeValue:
        case kTypeLargeValueRef:
          *found_live = true;
          break;
      }
    } else {  // cmp > 0
      *found_live = false;
    }

    iter_->Prev();
  }
}

void DBIter::ReadIndirectValue() const {
  assert(!large_->produced);
  large_->produced = true;
  LargeValueRef large_ref;
  if (value_.size() != LargeValueRef::ByteSize()) {
    large_->status = Status::Corruption("malformed large value reference");
    return;
  }
  memcpy(large_ref.data, value_.data(), LargeValueRef::ByteSize());
  std::string fname = LargeValueFileName(*dbname_, large_ref);
  RandomAccessFile* file;
  Status s = env_->NewRandomAccessFile(fname, &file);
  if (s.ok()) {
    uint64_t file_size = file->Size();
    uint64_t value_size = large_ref.ValueSize();
    large_->value.resize(value_size);
    Slice result;
    s = file->Read(0, file_size, &result,
                   const_cast<char*>(large_->value.data()));
    if (s.ok()) {
      if (result.size() == file_size) {
        switch (large_ref.compression_type()) {
          case kNoCompression: {
            if (result.data() != large_->value.data()) {
              large_->value.assign(result.data(), result.size());
            }
            break;
          }
          case kLightweightCompression: {
            std::string uncompressed;
            if (port::Lightweight_Uncompress(result.data(), result.size(),
                                       &uncompressed) &&
                uncompressed.size() == large_ref.ValueSize()) {
              swap(uncompressed, large_->value);
            } else {
              s = Status::Corruption(
                  "Unable to read entire compressed large value file");
            }
          }
        }
      } else {
        s = Status::Corruption("Unable to read entire large value file");
      }
    }
    delete file;        // Ignore errors on closing
  }
  if (!s.ok()) {
    large_->value.clear();
    large_->status = s;
  }
}

}  // anonymous namespace

Iterator* NewDBIterator(
    const std::string* dbname,
    Env* env,
    const Comparator* user_key_comparator,
    Iterator* internal_iter,
    const SequenceNumber& sequence) {
  return new DBIter(dbname, env, user_key_comparator, internal_iter, sequence);
}

}
