// Copyright (c) 2012 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
//
// A filter block is stored near the end of a Table file.  It contains
// filters (e.g., bloom filters) for all data blocks in the table combined
// into a single filter block.

#ifndef STORAGE_LEVELDB_TABLE_META_H_
#define STORAGE_LEVELDB_TABLE_META_H_

#include "leveldb/slice.h"
#include "leveldb/status.h"

namespace leveldb {

class MetaBlock {
 public:
    virtual ~MetaBlock() {}

    virtual Slice data() const = 0;
};

class MetaBlockBuilder {
 public:
  virtual ~MetaBlockBuilder() {}

  virtual const char* Name() const = 0;

  virtual void Add(const Slice& key, const Slice& value) = 0;
  virtual Slice Finish() = 0;
};

class MetaBlockVisitor {
 public:
  virtual ~MetaBlockVisitor() {}

  virtual bool PreVisit(int level, uint64_t file_number) = 0;
  virtual Status Visit(int level, uint64_t file_number, const Slice& data) = 0;
};

}

#endif  // STORAGE_LEVELDB_TABLE_META_H_
