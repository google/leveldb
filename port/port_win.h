// LevelDB Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
//
// See port_example.h for documentation for the following types/functions.

// This file Copyright (c) 2011 Bureau 14 SARL. All rights reserved.
//
//
// All rights reserved.
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

#include <boost/cstdint.hpp>
#include <boost/atomic.hpp>
#include <boost/thread/thread.hpp>
#include <boost/thread/once.hpp>
#include <string>
#include <stdint.h>

#define snprintf _snprintf
#define ssize_t int64_t

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
  // but they are not recursive and can only be used to synchronize threads within the same process
  // we use opaque void * to avoid including windows.h in port_win.h
  void * cs_;

  // No copying
  Mutex(const Mutex&);
  void operator=(const Mutex&);
};

// the Win32 API offers a dependable condition variable mechanism, but only starting with
// Windows 2008 and Vista
// no matter what we will implement our own condition variable with a semaphore
// implementation as described in a paper written by Andrew D. Birrell in 2003
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
  
  void * sem1_;
  void * sem2_;
  
  
};

typedef boost::once_flag OnceType;
#define LEVELDB_ONCE_INIT BOOST_ONCE_INIT

inline void InitOnce(port::OnceType* once, void (*initializer)())
{
	boost::call_once(*once,initializer);
}
// Storage for a lock-free pointer
class AtomicPointer {
 private:
	 boost::atomic<void*> rep_;
 public:
  AtomicPointer() { }
  explicit AtomicPointer(void* v)  { rep_ = v; }
  inline void* Acquire_Load() const {
    return rep_;
  }
  inline void Release_Store(void* v) {
    rep_ = v;
  }
  inline void* NoBarrier_Load() const {
    return rep_;
  }
  inline void NoBarrier_Store(void* v) {
    rep_ = v;
  }
};

// If input[0,input_length-1] looks like a valid snappy compressed
// buffer, store the size of the uncompressed data in *result and
// return true.  Else return false.
inline bool Snappy_GetUncompressedLength(const char* input, size_t length,
                                         size_t* result)
{
	return false;
}
// TODO(gabor): Implement actual compress
inline bool Snappy_Compress(const char* input, size_t input_length,
                            std::string* output) {
  return false;
}

// TODO(gabor): Implement actual uncompress
inline bool Snappy_Uncompress(const char* input_data, size_t input_length,
                              std::string output) {
  return false;
}

inline bool GetHeapProfile(void (*func)(void*, const char*, int), void* arg) {
  return false;
}

}
}

#endif  // STORAGE_LEVELDB_PORT_PORT_WIN_H_
