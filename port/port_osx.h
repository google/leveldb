// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
//
// See port_example.h for documentation for the following types/functions.

#ifndef STORAGE_LEVELDB_PORT_PORT_OSX_H_
#define STORAGE_LEVELDB_PORT_PORT_OSX_H_

#include <libkern/OSAtomic.h>
#include <machine/endian.h>
#include <pthread.h>
#include <stdint.h>

#include <string>

namespace leveldb {

// The following 4 methods implemented here for the benefit of env_posix.cc.
inline size_t fread_unlocked(void *a, size_t b, size_t c, FILE *d) {
  return fread(a, b, c, d);
}

inline size_t fwrite_unlocked(const void *a, size_t b, size_t c, FILE *d) {
  return fwrite(a, b, c, d);
}

inline int fflush_unlocked(FILE *f) {
  return fflush(f);
}

inline int fdatasync(int fd) {
  return fsync(fd);
}

namespace port {

static const bool kLittleEndian = (__DARWIN_BYTE_ORDER == __DARWIN_LITTLE_ENDIAN);

// ------------------ Threading -------------------

// A Mutex represents an exclusive lock.
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

inline void MemoryBarrier() {
#if defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))
  // See http://gcc.gnu.org/ml/gcc/2003-04/msg01180.html for a discussion on
  // this idiom. Also see http://en.wikipedia.org/wiki/Memory_ordering.
  __asm__ __volatile__("" : : : "memory");
#else
  OSMemoryBarrier();
#endif
}

class AtomicPointer {
 private:
  void* ptr_;
 public:
  AtomicPointer() { }
  explicit AtomicPointer(void* p) : ptr_(p) {}
  inline void* Acquire_Load() const {
    void* ptr = ptr_;
    MemoryBarrier();
    return ptr;
  }
  inline void Release_Store(void* v) {
    MemoryBarrier();
    ptr_ = v;
  }
  inline void* NoBarrier_Load() const {
    return ptr_;
  }
  inline void NoBarrier_Store(void* v) {
    ptr_ = v;
  }
};

inline bool Snappy_Compress(const char* input, size_t input_length,
                            std::string* output) {
  return false;
}

inline bool Snappy_Uncompress(const char* input_data, size_t input_length,
                              std::string* output) {
  return false;
}

inline bool GetHeapProfile(void (*func)(void*, const char*, int), void* arg) {
  return false;
}

}
}

#endif  // STORAGE_LEVELDB_PORT_PORT_OSX_H_
