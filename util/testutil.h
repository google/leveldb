// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef STORAGE_LEVELDB_UTIL_TESTUTIL_H_
#define STORAGE_LEVELDB_UTIL_TESTUTIL_H_

#include "leveldb/env.h"
#include "leveldb/slice.h"
#include "db/db_impl.h"
#include "util/random.h"

namespace leveldb {

namespace test {

// Store in *dst a random string of length "len" and return a Slice that
// references the generated data.
extern Slice RandomString(Random* rnd, int len, std::string* dst);

// Return a random key with the specified length that may contain interesting
// characters (e.g. \x00, \xff, etc.).
extern std::string RandomKey(Random* rnd, int len);

// Store in *dst a string of length "len" that will compress to
// "N*compressed_fraction" bytes and return a Slice that references
// the generated data.
extern Slice CompressibleString(Random* rnd, double compressed_fraction,
                                size_t len, std::string* dst);



extern Status Open(const Options& options, const std::string& name,
                   DB** dbptr);

class DBImplTesting : public DBImpl {
public:
  DBImplTesting(const Options& options, const std::string& dbname)
  : DBImpl(options, dbname) { }

  // Compact any files in the named level that overlap [*begin,*end]
  void DoCompactRange(int level, const Slice* begin, const Slice* end) {
    DBImpl::DoCompactRange(level, begin, end);
  }

  // Force current memtable contents to be compacted.
  Status DoCompactMemTable() {
    return DBImpl::DoCompactMemTable();
  }

  // Return an internal iterator over the current state of the database.
  // The keys of this iterator are internal keys (see format.h).
  // The returned iterator should be deleted when no longer needed.
  Iterator* DoNewInternalIterator() {
    return DBImpl::DoNewInternalIterator();
  }

  // Return the maximum overlapping data (in bytes) at next level for any
  // file at a level >= 1.
  int64_t MaxNextLevelOverlappingBytes() {
    return DBImpl::MaxNextLevelOverlappingBytes();
  }
};

// A wrapper that allows injection of errors.
class ErrorEnv : public EnvWrapper {
 public:
  bool writable_file_error_;
  int num_writable_file_errors_;

  ErrorEnv() : EnvWrapper(Env::Default()),
               writable_file_error_(false),
               num_writable_file_errors_(0) { }

  virtual Status NewWritableFile(const std::string& fname,
                                 WritableFile** result) {
    if (writable_file_error_) {
      ++num_writable_file_errors_;
      *result = NULL;
      return Status::IOError(fname, "fake error");
    }
    return target()->NewWritableFile(fname, result);
  }

  virtual Status NewAppendableFile(const std::string& fname,
                                   WritableFile** result) {
    if (writable_file_error_) {
      ++num_writable_file_errors_;
      *result = NULL;
      return Status::IOError(fname, "fake error");
    }
    return target()->NewAppendableFile(fname, result);
  }
};

}  // namespace test
}  // namespace leveldb

#endif  // STORAGE_LEVELDB_UTIL_TESTUTIL_H_
