// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef STORAGE_LEVELDB_DB_WRITE_BATCH_INTERNAL_H_
#define STORAGE_LEVELDB_DB_WRITE_BATCH_INTERNAL_H_

#include "include/write_batch.h"

namespace leveldb {

// WriteBatchInternal provides static methods for manipulating a
// WriteBatch that we don't want in the public WriteBatch interface.
class WriteBatchInternal {
 public:
  static void PutLargeValueRef(WriteBatch* batch,
                               const Slice& key,
                               const LargeValueRef& large_ref);

  // Return the number of entries in the batch.
  static int Count(const WriteBatch* batch);

  // Set the count for the number of entries in the batch.
  static void SetCount(WriteBatch* batch, int n);

  // Return the seqeunce number for the start of this batch.
  static SequenceNumber Sequence(const WriteBatch* batch);

  // Store the specified number as the seqeunce number for the start of
  // this batch.
  static void SetSequence(WriteBatch* batch, SequenceNumber seq);

  static Slice Contents(const WriteBatch* batch) {
    return Slice(batch->rep_);
  }

  static size_t ByteSize(const WriteBatch* batch) {
    return batch->rep_.size();
  }

  static void SetContents(WriteBatch* batch, const Slice& contents);

  static Status InsertInto(const WriteBatch* batch, MemTable* memtable);

  // Iterate over the contents of a write batch.
  class Iterator {
   public:
    explicit Iterator(const WriteBatch& batch);
    bool Done() const { return done_; }
    void Next();
    ValueType op() const { return op_; }
    const Slice& key() const { return key_; }
    const Slice& value() const { return value_; }
    SequenceNumber sequence_number() const { return seq_; }
    Status status() const { return status_; }

   private:
    void GetNextEntry();

    Slice input_;
    bool done_;
    ValueType op_;
    Slice key_;
    Slice value_;
    SequenceNumber seq_;
    Status status_;
  };
};

}


#endif  // STORAGE_LEVELDB_DB_WRITE_BATCH_INTERNAL_H_
