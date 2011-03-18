// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
//
// See port_example.h for documentation for the following types/functions.

#ifndef STORAGE_LEVELDB_PORT_PORT_POSIX_H_
#define STORAGE_LEVELDB_PORT_PORT_POSIX_H_

#include <endian.h>
#include <pthread.h>
#include <stdint.h>
#include <string>
#include <cstdatomic>
#include <cstring>
#include "port/sha1_portable.h"

namespace leveldb {
namespace port {

static const bool kLittleEndian = (__BYTE_ORDER == __LITTLE_ENDIAN);

class CondVar;

class Mutex {
 public:
  Mutex();
  ~Mutex();

  void Lock();
  void Unlock();
  void AssertHeld() { }

 private:
  friend class CondVar;
  pthread_mutex_t mu_;

  // No copying
  Mutex(const Mutex&);
  void operator=(const Mutex&);
};

class CondVar {
 public:
  explicit CondVar(Mutex* mu);
  ~CondVar();
  void Wait();
  void Signal();
  void SignalAll();
 private:
  pthread_cond_t cv_;
  Mutex* mu_;
};

// Storage for a lock-free pointer
class AtomicPointer {
 private:
  std::atomic<void*> rep_;
 public:
  AtomicPointer() { }
  explicit AtomicPointer(void* v) : rep_(v) { }
  inline void* Acquire_Load() const {
    return rep_.load(std::memory_order_acquire);
  }
  inline void Release_Store(void* v) {
    rep_.store(v, std::memory_order_release);
  }
  inline void* NoBarrier_Load() const {
    return rep_.load(std::memory_order_relaxed);
  }
  inline void NoBarrier_Store(void* v) {
    rep_.store(v, std::memory_order_relaxed);
  }
};

inline void SHA1_Hash(const char* data, size_t len, char* hash_array) {
  SHA1_Hash_Portable(data, len, hash_array);
}

/**
 * TODO(gabor): Implement actual compress
 *     This is a hack - it just copies input to output.
 *     No actual compression occurs.
 */
inline void Lightweight_Compress(const char* input, size_t input_length,
                           std::string* output) {
  output->assign(input, input_length);
}

/**
 * TODO(gabor): Implement actual uncompress
 *     This is a hack - it just copies input to output.
 *     No actual uncompression occurs.
 */
inline bool Lightweight_Uncompress(const char* input_data, size_t input_length,
                             std::string* output) {
  output->assign(input_data, input_length);
  return true;
}

inline bool GetHeapProfile(void (*func)(void*, const char*, int), void* arg) {
  return false;
}

}
}

#endif  // STORAGE_LEVELDB_PORT_PORT_POSIX_H_
