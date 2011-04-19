// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef STORAGE_LEVELDB_TABLE_BLOCK_H_
#define STORAGE_LEVELDB_TABLE_BLOCK_H_

#include <stddef.h>
#include <stdint.h>
#include "leveldb/iterator.h"

namespace leveldb {

class Comparator;

class Block {
 public:
  // Initialize the block with the specified contents.
  // Takes ownership of data[] and will delete[] it when done.
  Block(const char* data, size_t size);

  ~Block();

  size_t size() const { return size_; }
  Iterator* NewIterator(const Comparator* comparator);

 private:
  uint32_t NumRestarts() const;

  const char* data_;
  size_t size_;
  uint32_t restart_offset_;     // Offset in data_ of restart array

  // No copying allowed
  Block(const Block&);
  void operator=(const Block&);

  class Iter;
};

}

#endif  // STORAGE_LEVELDB_TABLE_BLOCK_H_
