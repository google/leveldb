// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
//
// A Status encapsulates the result of an operation.  It may indicate success,
// or it may indicate an error with an associated error message.

#ifndef STORAGE_LEVELDB_INCLUDE_STATUS_H_
#define STORAGE_LEVELDB_INCLUDE_STATUS_H_

#include <string>
#include <utility>
#include "leveldb/slice.h"

namespace leveldb {

class Status {
 public:
  // Create a success status.
  Status() : state_(NULL) { }
  ~Status() { delete state_; }

  // Copy the specified status.
  Status(const Status& s);
  void operator=(const Status& s);

  // Return a success status.
  static Status OK() { return Status(); }

  // Return error status of an appropriate type.
  static Status NotFound(const Slice& msg, const Slice& msg2 = Slice()) {
    return Status(kNotFound, msg, Slice());
  }
  static Status Corruption(const Slice& msg, const Slice& msg2 = Slice()) {
    return Status(kCorruption, msg, msg2);
  }
  static Status NotSupported(const Slice& msg, const Slice& msg2 = Slice()) {
    return Status(kNotSupported, msg, msg2);
  }
  static Status InvalidArgument(const Slice& msg, const Slice& msg2 = Slice()) {
    return Status(kInvalidArgument, msg, msg2);
  }
  static Status IOError(const Slice& msg, const Slice& msg2 = Slice()) {
    return Status(kIOError, msg, msg2);
  }

  // Returns true iff the status indicates success.
  bool ok() const { return (state_ == NULL); }

  // Returns true iff the status indicates a NotFound error.
  bool IsNotFound() const { return code() == kNotFound; }

  // Return a string representation of this status suitable for printing.
  // Returns the string "OK" for success.
  std::string ToString() const;

 private:
  enum Code {
    kOk = 0,
    kNotFound = 1,
    kCorruption = 2,
    kNotSupported = 3,
    kInvalidArgument = 4,
    kIOError = 5,
  };
  Code code() const { return (state_ == NULL) ? kOk : state_->first; }

  Status(Code code, const Slice& msg, const Slice& msg2);

  typedef std::pair<Code, std::string> State;
  State* state_;
};

inline Status::Status(const Status& s) {
  state_ = (s.state_ == NULL) ? NULL : new State(*s.state_);
}
inline void Status::operator=(const Status& s) {
  if (this != &s) {
    delete state_;
    state_ = (s.state_ == NULL) ? NULL : new State(*s.state_);
  }
}

}

#endif  // STORAGE_LEVELDB_INCLUDE_STATUS_H_
