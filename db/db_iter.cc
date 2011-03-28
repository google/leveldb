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
  // Which direction is the iterator currently moving?
  // (1) When moving forward, the internal iterator is positioned at
  //     the exact entry that yields this->key(), this->value()
  // (2) When moving backwards, the internal iterator is positioned
  //     just before all entries whose user key == this->key().
  enum Direction {
    kForward,
    kReverse
  };

  DBIter(const std::string* dbname, Env* env,
         const Comparator* cmp, Iterator* iter, SequenceNumber s)
      : dbname_(dbname),
        env_(env),
        user_comparator_(cmp),
        iter_(iter),
        sequence_(s),
        large_(NULL),
        direction_(kForward),
        valid_(false) {
  }
  virtual ~DBIter() {
    delete iter_;
    delete large_;
  }
  virtual bool Valid() const { return valid_; }
  virtual Slice key() const {
    assert(valid_);
    return (direction_ == kForward) ? ExtractUserKey(iter_->key()) : saved_key_;
  }
  virtual Slice value() const {
    assert(valid_);
    Slice raw_value = (direction_ == kForward) ? iter_->value() : saved_value_;
    if (large_ == NULL) {
      return raw_value;
    } else {
      MutexLock l(&large_->mutex);
      if (!large_->produced) {
        ReadIndirectValue(raw_value);
      }
      return large_->value;
    }
  }
  virtual Status status() const {
    if (status_.ok()) {
      if (large_ != NULL && !large_->status.ok()) return large_->status;
      return iter_->status();
    } else {
      return status_;
    }
  }

  virtual void Next();
  virtual void Prev();
  virtual void Seek(const Slice& target);
  virtual void SeekToFirst();
  virtual void SeekToLast();

 private:
  struct Large {
    port::Mutex mutex;
    std::string value;
    bool produced;
    Status status;
  };

  void FindNextUserEntry(bool skipping, std::string* skip);
  void FindPrevUserEntry();
  bool ParseKey(ParsedInternalKey* key);
  void ReadIndirectValue(Slice ref) const;

  inline void SaveKey(const Slice& k, std::string* dst) {
    dst->assign(k.data(), k.size());
  }

  inline void ForgetLargeValue() {
    if (large_ != NULL) {
      delete large_;
      large_ = NULL;
    }
  }

  inline void ClearSavedValue() {
    if (saved_value_.capacity() > 1048576) {
      std::string empty;
      swap(empty, saved_value_);
    } else {
      saved_value_.clear();
    }
  }

  const std::string* const dbname_;
  Env* const env_;
  const Comparator* const user_comparator_;
  Iterator* const iter_;
  SequenceNumber const sequence_;

  Status status_;
  std::string saved_key_;     // == current key when direction_==kReverse
  std::string saved_value_;   // == current raw value when direction_==kReverse
  Large* large_;              // Non-NULL if value is an indirect reference
  Direction direction_;
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

void DBIter::Next() {
  assert(valid_);
  ForgetLargeValue();

  if (direction_ == kReverse) {  // Switch directions?
    direction_ = kForward;
    // iter_ is pointing just before the entries for this->key(),
    // so advance into the range of entries for this->key() and then
    // use the normal skipping code below.
    if (!iter_->Valid()) {
      iter_->SeekToFirst();
    } else {
      iter_->Next();
    }
    if (!iter_->Valid()) {
      valid_ = false;
      saved_key_.clear();
      return;
    }
  }

  // Temporarily use saved_key_ as storage for key to skip.
  std::string* skip = &saved_key_;
  SaveKey(ExtractUserKey(iter_->key()), skip);
  FindNextUserEntry(true, skip);
}

void DBIter::FindNextUserEntry(bool skipping, std::string* skip) {
  // Loop until we hit an acceptable entry to yield
  assert(iter_->Valid());
  assert(direction_ == kForward);
  assert(large_ == NULL);
  do {
    ParsedInternalKey ikey;
    if (ParseKey(&ikey) && ikey.sequence <= sequence_) {
      switch (ikey.type) {
        case kTypeDeletion:
          // Arrange to skip all upcoming entries for this key since
          // they are hidden by this deletion.
          SaveKey(ikey.user_key, skip);
          skipping = true;
          break;
        case kTypeValue:
        case kTypeLargeValueRef:
          if (skipping &&
              user_comparator_->Compare(ikey.user_key, *skip) <= 0) {
            // Entry hidden
          } else {
            valid_ = true;
            saved_key_.clear();
            if (ikey.type == kTypeLargeValueRef) {
              large_ = new Large;
              large_->produced = false;
            }
            return;
          }
          break;
      }
    }
    iter_->Next();
  } while (iter_->Valid());
  saved_key_.clear();
  valid_ = false;
}

void DBIter::Prev() {
  assert(valid_);
  ForgetLargeValue();

  if (direction_ == kForward) {  // Switch directions?
    // iter_ is pointing at the current entry.  Scan backwards until
    // the key changes so we can use the normal reverse scanning code.
    assert(iter_->Valid());  // Otherwise valid_ would have been false
    SaveKey(ExtractUserKey(iter_->key()), &saved_key_);
    while (true) {
      iter_->Prev();
      if (!iter_->Valid()) {
        valid_ = false;
        saved_key_.clear();
        ClearSavedValue();
        return;
      }
      if (user_comparator_->Compare(ExtractUserKey(iter_->key()),
                                    saved_key_) < 0) {
        break;
      }
    }
    direction_ = kReverse;
  }

  FindPrevUserEntry();
}

void DBIter::FindPrevUserEntry() {
  assert(direction_ == kReverse);
  assert(large_ == NULL);

  ValueType value_type = kTypeDeletion;
  if (iter_->Valid()) {
    SaveKey(ExtractUserKey(iter_->key()), &saved_key_);
    do {
      ParsedInternalKey ikey;
      if (ParseKey(&ikey) && ikey.sequence <= sequence_) {
        if ((value_type != kTypeDeletion) &&
            user_comparator_->Compare(ikey.user_key, saved_key_) < 0) {
          // We encountered a non-deleted value in entries for previous keys,
          break;
        }
        value_type = ikey.type;
        if (value_type == kTypeDeletion) {
          ClearSavedValue();
        } else {
          Slice raw_value = iter_->value();
          if (saved_value_.capacity() > raw_value.size() + 1048576) {
            std::string empty;
            swap(empty, saved_value_);
          }
          saved_value_.assign(raw_value.data(), raw_value.size());
        }
      }
      iter_->Prev();
    } while (iter_->Valid());
  }

  if (value_type == kTypeDeletion) {
    // End
    valid_ = false;
    saved_key_.clear();
    ClearSavedValue();
    direction_ = kForward;
  } else {
    valid_ = true;
    if (value_type == kTypeLargeValueRef) {
      large_ = new Large;
      large_->produced = false;
    }
  }
}

void DBIter::Seek(const Slice& target) {
  direction_ = kForward;
  ForgetLargeValue();
  ClearSavedValue();
  saved_key_.clear();
  AppendInternalKey(
      &saved_key_, ParsedInternalKey(target, sequence_, kValueTypeForSeek));
  iter_->Seek(saved_key_);
  if (iter_->Valid()) {
    FindNextUserEntry(false, &saved_key_ /* temporary storage */);
  } else {
    valid_ = false;
  }
}

void DBIter::SeekToFirst() {
  direction_ = kForward;
  ForgetLargeValue();
  ClearSavedValue();
  iter_->SeekToFirst();
  if (iter_->Valid()) {
    FindNextUserEntry(false, &saved_key_ /* temporary storage */);
  } else {
    valid_ = false;
  }
}

void DBIter::SeekToLast() {
  direction_ = kReverse;
  ForgetLargeValue();
  ClearSavedValue();
  iter_->SeekToLast();
  FindPrevUserEntry();
}

void DBIter::ReadIndirectValue(Slice ref) const {
  assert(!large_->produced);
  large_->produced = true;
  LargeValueRef large_ref;
  if (ref.size() != LargeValueRef::ByteSize()) {
    large_->status = Status::Corruption("malformed large value reference");
    return;
  }
  memcpy(large_ref.data, ref.data(), LargeValueRef::ByteSize());
  std::string fname = LargeValueFileName(*dbname_, large_ref);
  RandomAccessFile* file;
  Status s = env_->NewRandomAccessFile(fname, &file);
  uint64_t file_size = 0;
  if (s.ok()) {
    s = env_->GetFileSize(fname, &file_size);
  }
  if (s.ok()) {
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
          case kSnappyCompression: {
            std::string uncompressed;
            if (port::Snappy_Uncompress(result.data(), result.size(),
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
