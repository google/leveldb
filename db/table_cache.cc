// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "db/table_cache.h"

#include <iostream>
#include "db/filename.h"
#include "leveldb/env.h"
#include "leveldb/table.h"
#include "util/coding.h"

namespace leveldb {

struct TableAndFile {
  RandomAccessFile* file;
  Table* table;
};

static void DeleteEntry(const Slice& key, void* value) {
  TableAndFile* tf = reinterpret_cast<TableAndFile*>(value);
  delete tf->table;
  delete tf->file;
  delete tf;
}

static void UnrefEntry(void* arg1, void* arg2) {
  Cache* cache = reinterpret_cast<Cache*>(arg1);
  Cache::Handle* h = reinterpret_cast<Cache::Handle*>(arg2);
  cache->Release(h);
}

// whc change: add ssdname
TableCache::TableCache(const std::string& dbname,
                       const Options* options,
                       int entries,
					   const std::string& ssdname)
    : env_(options->env),
      dbname_(dbname),
      options_(options),
      cache_(NewLRUCache(entries)),
	ssdname_(ssdname){
	pathname_ = dbname_;
}

TableCache::~TableCache() {
  delete cache_;
}

Status TableCache::FindTable(uint64_t file_number, uint64_t file_size,
                             Cache::Handle** handle) {
  Status s;
  char buf[sizeof(file_number)];
  EncodeFixed64(buf, file_number);
  Slice key(buf, sizeof(buf));

  // whc add
         //std::cout<<"find table come in"<<std::endl;

  // whc add
            //std::cout<<"in findtable path  is :" + pathname_<<std::endl;

  //whc change
  *handle = cache_->Lookup(key);

  // whc add
        // std::cout<<"table_cache look up  is right"<<std::endl;
  if (*handle == NULL) {
    std::string fname = TableFileName(pathname_, file_number);
    RandomAccessFile* file = NULL;
    Table* table = NULL;
    // whc add
                std::cout<<"in findtable path  is :" + pathname_<<std::endl;
                std::cout<<"in findtable fname  is :" + fname<<std::endl;
    s = env_->NewRandomAccessFile(fname, &file);

    // whc add
         // std::cout<<"table cache path  is :" + pathname_<<std::endl;

    if (!s.ok()) {
      std::string old_fname = SSTTableFileName(pathname_, file_number);
      if (env_->NewRandomAccessFile(old_fname, &file).ok()) {
        s = Status::OK();
      }
    }
    // add ssd cache

    // whc add
              //std::cout<<"find table new randomaccess  is right"<<std::endl;
    if (s.ok()) {
      s = Table::Open(*options_, file, file_size, &table);
    }

    // whc add
          // std::cout<<"table_cache open is right"<<std::endl;
    if (!s.ok()) {
      assert(table == NULL);
      delete file;
      // We do not cache error results so that if the error is transient,
      // or somebody repairs the file, we recover automatically.
    } else {
      TableAndFile* tf = new TableAndFile;
      tf->file = file;
      tf->table = table;
      *handle = cache_->Insert(key, tf, 1, &DeleteEntry);
      // whc add
            // std::cout<<"table_cache insert  is right"<<std::endl;

    }
  }
  return s;
}

Iterator* TableCache::NewIterator(const ReadOptions& options,
                                  uint64_t file_number,
                                  uint64_t file_size,
                                  Table** tableptr) {
  if (tableptr != NULL) {
    *tableptr = NULL;
  }
  // whc add
         //std::cout<<" in table_cache,pathname is" + pathname_<<std::endl;

  Cache::Handle* handle = NULL;
  Status s = FindTable(file_number, file_size, &handle);
  if (!s.ok()) {
    return NewErrorIterator(s);
  }

  // whc add
       // std::cout<<"table_cache is right"<<std::endl;
  Table* table = reinterpret_cast<TableAndFile*>(cache_->Value(handle))->table;
  Iterator* result = table->NewIterator(options);
  result->RegisterCleanup(&UnrefEntry, cache_, handle);
  if (tableptr != NULL) {
    *tableptr = table;
  }

  // whc add
         // std::cout<<"table_cache is end"<<std::endl;
  return result;
}

Status TableCache::Get(const ReadOptions& options,
                       uint64_t file_number,
                       uint64_t file_size,
                       const Slice& k,
                       void* arg,
                       void (*saver)(void*, const Slice&, const Slice&)) {
  Cache::Handle* handle = NULL;
  Status s = FindTable(file_number, file_size, &handle);
  if (s.ok()) {
    Table* t = reinterpret_cast<TableAndFile*>(cache_->Value(handle))->table;
    s = t->InternalGet(options, k, arg, saver);
    cache_->Release(handle);
  }
  return s;
}

void TableCache::Evict(uint64_t file_number) {
  char buf[sizeof(file_number)];
  EncodeFixed64(buf, file_number);
  cache_->Erase(Slice(buf, sizeof(buf)));
}

Status TableCache::GetFromSSD(const ReadOptions& options,
                       uint64_t file_number,
                       uint64_t file_size,
                       const Slice& k,
                       void* arg,
                       void (*saver)(void*, const Slice&, const Slice&)) {
  Cache::Handle* handle = NULL;
  Status s = FindTableFromSSD(file_number, file_size, &handle);
  if (s.ok()) {
    Table* t = reinterpret_cast<TableAndFile*>(cache_->Value(handle))->table;
    s = t->InternalGet(options, k, arg, saver);
    cache_->Release(handle);
  }
  return s;
}

Status TableCache::FindTableFromSSD(uint64_t file_number, uint64_t file_size,
                             Cache::Handle** handle) {
  Status s;
  char buf[sizeof(file_number)];
  EncodeFixed64(buf, file_number);
  Slice key(buf, sizeof(buf));

  // whc add
         //std::cout<<"find table come in"<<std::endl;

  // whc add
            //std::cout<<"in findtable path  is :" + pathname_<<std::endl;

  //whc change
  *handle = cache_->Lookup(key);

  // whc add
        // std::cout<<"table_cache look up  is right"<<std::endl;
  if (*handle == NULL) {
    std::string fname = TableFileName(ssdname_, file_number);
    RandomAccessFile* file = NULL;
    Table* table = NULL;
    // whc add
                std::cout<<"in findtable path  is :" + pathname_<<std::endl;
                std::cout<<"in findtable fname  is :" + fname<<std::endl;
    s = env_->NewRandomAccessFile(fname, &file);

    // whc add
         // std::cout<<"table cache path  is :" + pathname_<<std::endl;

    if (!s.ok()) {
      std::string old_fname = SSTTableFileName(ssdname_, file_number);
      if (env_->NewRandomAccessFile(old_fname, &file).ok()) {
        s = Status::OK();
      }
    }
    // add ssd cache

    // whc add
              //std::cout<<"find table new randomaccess  is right"<<std::endl;
    if (s.ok()) {
      s = Table::Open(*options_, file, file_size, &table);
    }

    // whc add
          // std::cout<<"table_cache open is right"<<std::endl;
    if (!s.ok()) {
      assert(table == NULL);
      delete file;
      // We do not cache error results so that if the error is transient,
      // or somebody repairs the file, we recover automatically.
    } else {
      TableAndFile* tf = new TableAndFile;
      tf->file = file;
      tf->table = table;
      *handle = cache_->Insert(key, tf, 1, &DeleteEntry);
      // whc add
            // std::cout<<"table_cache insert  is right"<<std::endl;

    }
  }
  return s;
}

}  // namespace leveldb
