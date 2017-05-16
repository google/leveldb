// LevelDB Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
//
// See port_example.h for documentation for the following types/functions.

// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//  * Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//  * Neither the name of the University of California, Berkeley nor the
//    names of its contributors may be used to endorse or promote products
//    derived from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE REGENTS AND CONTRIBUTORS BE LIABLE FOR ANY
// DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//

#ifndef STORAGE_LEVELDB_PORT_PORT_WIN_H_
#define STORAGE_LEVELDB_PORT_PORT_WIN_H_

#if defined(_MSC_VER) && _MSC_VER < 1900
#define snprintf _snprintf
#endif
#define close _close
#define fread_unlocked _fread_nolock

#if defined(_MSC_VER)
	#include <BaseTsd.h>
	typedef SSIZE_T ssize_t;
#endif


#ifdef SNAPPY
#include <snappy.h>
#endif

#include <string>

#include <stdint.h>

namespace leveldb {
namespace port {

// Windows is little endian (for now :p)
static const bool kLittleEndian = true;

class CondVar;

class Mutex {
 public:
  Mutex();
  ~Mutex();

  void Lock();
  void Unlock();
  void AssertHeld();

 private:
  friend class CondVar;
  // critical sections are more efficient than mutexes
  // but they are not recursive and can only be used to synchronize threads
  // within the same process
  // additionnaly they cannot be used with SignalObjectAndWait that we use for
  // CondVar
  // we use opaque void * to avoid including windows.h in port_win.h
  void* mutex_;

  // No copying
  Mutex(const Mutex&);
  void operator=(const Mutex&);
};

// the Win32 API offers a dependable condition variable mechanism, but only
// starting with
// Windows 2008 and Vista
// no matter what we will implement our own condition variable with a semaphore
// implementation as described in a paper written by Douglas C. Schmidt and
// Irfan Pyarali
class CondVar {
 public:
  explicit CondVar(Mutex* mu);
  ~CondVar();
  void Wait();
  void Signal();
  void SignalAll();

 private:
  Mutex* mu_;

  Mutex wait_mtx_;
  long waiting_;

  void* sema_;
  void* event_;

  bool broadcasted_;
};

// Storage for a lock-free pointer
class AtomicPointer {
 private:
  void* rep_;

 public:
  AtomicPointer() : rep_(nullptr) {}
  explicit AtomicPointer(void* v);
  void* Acquire_Load() const;

  void Release_Store(void* v);

  void* NoBarrier_Load() const;

  void NoBarrier_Store(void* v);
};

typedef volatile long OnceType;
#define LEVELDB_ONCE_INIT (0)

extern void InitOnce(OnceType* once, void (*initializer)());

inline bool Snappy_Compress(const char* input, size_t length,
                            ::std::string* output) {
#ifdef SNAPPY
  output->resize(snappy::MaxCompressedLength(length));
  size_t outlen;
  snappy::RawCompress(input, length, &(*output)[0], &outlen);
  output->resize(outlen);
  return true;
#endif

  return false;
}

inline bool Snappy_GetUncompressedLength(const char* input, size_t length,
                                         size_t* result) {
#ifdef SNAPPY
  return snappy::GetUncompressedLength(input, length, result);
#else
  return false;
#endif
}

inline bool Snappy_Uncompress(const char* input, size_t length, char* output) {
#ifdef SNAPPY
  return snappy::RawUncompress(input, length, output);
#else
  return false;
#endif
}

inline bool GetHeapProfile(void (*func)(void*, const char*, int), void* arg) {
  return false;
}

// Extend the CRC to include the first n bytes of buf.
//
// Returns zero if the CRC cannot be extended using acceleration, else returns
// the newly extended CRC value (which may also be zero).
uint32_t AcceleratedCRC32C(uint32_t crc, const char* buf, size_t size);

}  // namespace port
}  // namespace leveldb


#endif  // STORAGE_LEVELDB_PORT_PORT_WIN_H_
