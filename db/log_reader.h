// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef STORAGE_LEVELDB_DB_LOG_READER_H_
#define STORAGE_LEVELDB_DB_LOG_READER_H_

#include "db/log_format.h"
#include "include/slice.h"
#include "include/status.h"

namespace leveldb {

class SequentialFile;

namespace log {

class Reader {
 public:
  // Interface for reporting errors.
  class Reporter {
   public:
    virtual ~Reporter();

    // Some corruption was detected.  "size" is the approximate number
    // of bytes dropped due to the corruption.
    virtual void Corruption(size_t bytes, const Status& status) = 0;
  };

  // Create a reader that will return log records from "*file".
  // "*file" must remain live while this Reader is in use.
  //
  // If "reporter" is non-NULL, it is notified whenever some data is
  // dropped due to a detected corruption.  "*reporter" must remain
  // live while this Reader is in use.
  //
  // If "checksum" is true, verify checksums if available.
  Reader(SequentialFile* file, Reporter* reporter, bool checksum);

  ~Reader();

  // Read the next record into *record.  Returns true if read
  // successfully, false if we hit end of the input.  May use
  // "*scratch" as temporary storage.  The contents filled in *record
  // will only be valid until the next mutating operation on this
  // reader or the next mutation to *scratch.
  bool ReadRecord(Slice* record, std::string* scratch);

 private:
  SequentialFile* const file_;
  Reporter* const reporter_;
  bool const checksum_;
  char* const backing_store_;
  Slice buffer_;
  bool eof_;   // Last Read() indicated EOF by returning < kBlockSize

  // Extend record types with the following special values
  enum {
    kEof = kMaxRecordType + 1,
    kBadRecord = kMaxRecordType + 2
  };

  // Return type, or one of the preceding special values
  unsigned int ReadPhysicalRecord(Slice* result);
  void ReportDrop(size_t bytes, const char* reason);

  // No copying allowed
  Reader(const Reader&);
  void operator=(const Reader&);
};

}
}

#endif  // STORAGE_LEVELDB_DB_LOG_READER_H_
