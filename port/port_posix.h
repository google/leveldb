// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
//
// See port_example.h for documentation for the following types/functions.

#ifndef STORAGE_LEVELDB_PORT_PORT_POSIX_H_
#define STORAGE_LEVELDB_PORT_PORT_POSIX_H_

#include <endian.h>
#include <pthread.h>
#ifdef SNAPPY
#include <snappy.h>
#endif
#include <stdint.h>
#include <string>
#include <cstdatomic>
#include <cstring>

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

inline bool Snappy_Compress(const char* input, size_t input_length,
                            std::string* output) {
#ifdef SNAPPY
  output->resize(snappy::MaxCompressedLength(input_length));
  size_t outlen;
  snappy::RawCompress(input, input_length, &(*output)[0], &outlen);
  output->resize(outlen);
  return true;
#endif

  return false;
}

inline bool Snappy_Uncompress(const char* input_data, size_t input_length,
                              std::string* output) {
#ifdef SNAPPY
  size_t ulength;
  if (!snappy::GetUncompressedLength(input_data, ulength, &ulength)) {
    return false;
  }
  output->resize(ulength);
  return snappy::RawUncompress(input_data, input_length, &(*output)[0]);
#endif

  return false;
}

inline bool GetHeapProfile(void (*func)(void*, const char*, int), void* arg) {
  return false;
}

}
}

#endif  // STORAGE_LEVELDB_PORT_PORT_POSIX_H_
