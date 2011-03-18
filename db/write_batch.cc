// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
//
// WriteBatch::rep_ :=
//    sequence: fixed64
//    count: fixed32
//    data: record[count]
// record :=
//    kTypeValue varstring varstring         |
//    kTypeLargeValueRef varstring varstring |
//    kTypeDeletion varstring
// varstring :=
//    len: varint32
//    data: uint8[len]

#include "include/write_batch.h"

#include "include/db.h"
#include "db/dbformat.h"
#include "db/memtable.h"
#include "db/write_batch_internal.h"
#include "util/coding.h"

namespace leveldb {

WriteBatch::WriteBatch() {
  Clear();
}

WriteBatch::~WriteBatch() { }

void WriteBatch::Clear() {
  rep_.clear();
  rep_.resize(12);
}

int WriteBatchInternal::Count(const WriteBatch* b) {
  return DecodeFixed32(b->rep_.data() + 8);
}

void WriteBatchInternal::SetCount(WriteBatch* b, int n) {
  EncodeFixed32(&b->rep_[8], n);
}

SequenceNumber WriteBatchInternal::Sequence(const WriteBatch* b) {
  return SequenceNumber(DecodeFixed64(b->rep_.data()));
}

void WriteBatchInternal::SetSequence(WriteBatch* b, SequenceNumber seq) {
  EncodeFixed64(&b->rep_[0], seq);
}

void WriteBatch::Put(const Slice& key, const Slice& value) {
  WriteBatchInternal::SetCount(this, WriteBatchInternal::Count(this) + 1);
  rep_.push_back(static_cast<char>(kTypeValue));
  PutLengthPrefixedSlice(&rep_, key);
  PutLengthPrefixedSlice(&rep_, value);
}

void WriteBatchInternal::PutLargeValueRef(WriteBatch* b,
                                          const Slice& key,
                                          const LargeValueRef& large_ref) {
  WriteBatchInternal::SetCount(b, WriteBatchInternal::Count(b) + 1);
  b->rep_.push_back(static_cast<char>(kTypeLargeValueRef));
  PutLengthPrefixedSlice(&b->rep_, key);
  PutLengthPrefixedSlice(&b->rep_,
                         Slice(large_ref.data, sizeof(large_ref.data)));
}

void WriteBatch::Delete(const Slice& key) {
  WriteBatchInternal::SetCount(this, WriteBatchInternal::Count(this) + 1);
  rep_.push_back(static_cast<char>(kTypeDeletion));
  PutLengthPrefixedSlice(&rep_, key);
}

Status WriteBatchInternal::InsertInto(const WriteBatch* b,
                                      MemTable* memtable) {
  const int count = WriteBatchInternal::Count(b);
  int found = 0;
  Iterator it(*b);
  for (; !it.Done(); it.Next()) {
    switch (it.op()) {
      case kTypeDeletion:
        memtable->Add(it.sequence_number(), kTypeDeletion, it.key(), Slice());
        break;
      case kTypeValue:
        memtable->Add(it.sequence_number(), kTypeValue, it.key(), it.value());
        break;
      case kTypeLargeValueRef:
        memtable->Add(it.sequence_number(), kTypeLargeValueRef,
                      it.key(), it.value());
        break;
    }
    found++;
  }
  if (!it.status().ok()) {
    return it.status();
  } else if (found != count) {
    return Status::Corruption("wrong count in WriteBatch");
  }
  return Status::OK();
}

void WriteBatchInternal::SetContents(WriteBatch* b, const Slice& contents) {
  assert(contents.size() >= 12);
  b->rep_.assign(contents.data(), contents.size());
}

WriteBatchInternal::Iterator::Iterator(const WriteBatch& batch)
    : input_(WriteBatchInternal::Contents(&batch)),
      done_(false) {
  if (input_.size() < 12) {
    done_ = true;
  } else {
    seq_ = WriteBatchInternal::Sequence(&batch),
    input_.remove_prefix(12);
    GetNextEntry();
  }
}

void WriteBatchInternal::Iterator::Next() {
  assert(!done_);
  seq_++;
  GetNextEntry();
}

void WriteBatchInternal::Iterator::GetNextEntry() {
  if (input_.empty()) {
    done_ = true;
    return;
  }
  char tag = input_[0];
  input_.remove_prefix(1);
  switch (tag) {
    case kTypeValue:
    case kTypeLargeValueRef:
      if (GetLengthPrefixedSlice(&input_, &key_) &&
          GetLengthPrefixedSlice(&input_, &value_)) {
        op_ = static_cast<ValueType>(tag);
      } else {
        status_ = Status::Corruption("bad WriteBatch Put");
        done_ = true;
        input_.clear();
      }
      break;
    case kTypeDeletion:
      if (GetLengthPrefixedSlice(&input_, &key_)) {
        op_ = kTypeDeletion;
      } else {
        status_ = Status::Corruption("bad WriteBatch Delete");
        done_ = true;
        input_.clear();
      }
      break;
    default:
      status_ = Status::Corruption("unknown WriteBatch tag");
      done_ = true;
      input_.clear();
      break;
  }
}

}
