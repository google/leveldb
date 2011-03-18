// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
//
// Thread-safe (provides internal synchronization)

#ifndef STORAGE_LEVELDB_DB_TABLE_CACHE_H_
#define STORAGE_LEVELDB_DB_TABLE_CACHE_H_

#include <string>
#include <stdint.h>
#include "db/dbformat.h"
#include "include/cache.h"
#include "include/table.h"
#include "port/port.h"

namespace leveldb {

class Env;

class TableCache {
 public:
  TableCache(const std::string& dbname, const Options* options, int entries);
  ~TableCache();

  // Get an iterator for the specified file number and return it.  If
  // "tableptr" is non-NULL, also sets "*tableptr" to point to the
  // Table object underlying the returned iterator, or NULL if no
  // Table object underlies the returned iterator.  The returned
  // "*tableptr" object is owned by the cache and should not be
  // deleted, and is valid for as long as the returned iterator is
  // live.
  Iterator* NewIterator(const ReadOptions& options,
                        uint64_t file_number,
                        Table** tableptr = NULL);

  // Evict any entry for the specified file number
  void Evict(uint64_t file_number);

 private:
  Env* const env_;
  const std::string dbname_;
  const Options* options_;
  Cache* cache_;
};

}

#endif  // STORAGE_LEVELDB_DB_TABLE_CACHE_H_
