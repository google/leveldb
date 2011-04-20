// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
//
// See port_example.h for documentation for the following types/functions.

#ifndef STORAGE_LEVELDB_PORT_PORT_CHROMIUM_H_
#define STORAGE_LEVELDB_PORT_PORT_CHROMIUM_H_

#include <stdint.h>
#include <string>
#include <cstring>
#include "base/atomicops.h"
#include "base/basictypes.h"
#include "base/logging.h"
#include "base/synchronization/condition_variable.h"
#include "base/synchronization/lock.h"

// Linux's ThreadIdentifier() needs this.
#if defined(OS_LINUX)
#  include <linux/unistd.h>
#endif

#if defined(OS_WIN)
#define snprintf _snprintf
#define va_copy(a, b) do { (a) = (b); } while (0)
#endif

namespace leveldb {
namespace port {

// Chromium only supports little endian.
static const bool kLittleEndian = true;

class Mutex {
 public:
  Mutex();
  ~Mutex();
  void Lock();
  void Unlock();
  void AssertHeld();

 private:
  base::Lock mu_;

  friend class CondVar;
  DISALLOW_COPY_AND_ASSIGN(Mutex);
};

class CondVar {
 public:
  explicit CondVar(Mutex* mu);
  ~CondVar();
  void Wait();
  void Signal();
  void SignalAll();

 private:
  base::ConditionVariable cv_;

  DISALLOW_COPY_AND_ASSIGN(CondVar);
};

class AtomicPointer {
 private:
  typedef base::subtle::AtomicWord Rep;
  Rep rep_;
 public:
  AtomicPointer() { }
  explicit AtomicPointer(void* p) : rep_(reinterpret_cast<Rep>(p)) {}
  inline void* Acquire_Load() const {
    return reinterpret_cast<void*>(::base::subtle::Acquire_Load(&rep_));
  }
  inline void Release_Store(void* v) {
    ::base::subtle::Release_Store(&rep_, reinterpret_cast<Rep>(v));
  }
  inline void* NoBarrier_Load() const {
    return reinterpret_cast<void*>(::base::subtle::NoBarrier_Load(&rep_));
  }
  inline void NoBarrier_Store(void* v) {
    ::base::subtle::NoBarrier_Store(&rep_, reinterpret_cast<Rep>(v));
  }
};

bool Snappy_Compress(const char* input, size_t input_length,
                     std::string* output);
bool Snappy_Uncompress(const char* input_data, size_t input_length,
                       std::string* output);

inline bool GetHeapProfile(void (*func)(void*, const char*, int), void* arg) {
  return false;
}

}
}

#endif  // STORAGE_LEVELDB_PORT_PORT_CHROMIUM_H_
