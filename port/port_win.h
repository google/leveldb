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

#define snprintf _snprintf

#include <windows.h>
// Undo various #define in windows headers that interfere with the code
#ifdef min
#undef min
#endif
#ifdef small
#undef small
#endif
#ifdef DeleteFile
#undef DeleteFile
#endif

#include <string>

#include <stdint.h>

namespace leveldb {
namespace port {

    // Thread-safe initialization.
    // Used as follows:
    // static port::OnceType init_control = LEVELDB_ONCE_INIT;
    // static void Initializer() { ... do something ...; }
    // ...
    // port::InitOnce(&init_control, &Initializer);
    typedef long OnceType;
#define LEVELDB_ONCE_INIT 0
    extern void InitOnce(port::OnceType*, void(*initializer)());



// Windows is little endian (for now :p)
static const bool kLittleEndian = true;

// based on lock_impl_win.cc from chrome:
// http://src.chromium.org/viewvc/chrome/trunk/src/base/synchronization/lock_impl_win.cc?revision=70363&view=markup
class Mutex {
 public:
  Mutex() {
      // The second parameter is the spin count, for short-held locks it avoid the
      // contending thread from going to sleep which helps performance greatly.
      ::InitializeCriticalSectionAndSpinCount(&os_lock_, 2000);
  }

  ~Mutex(){
    ::DeleteCriticalSection(&os_lock_);
  }

  void Lock() {
    ::EnterCriticalSection(&os_lock_);
  }

  void Unlock() {
     ::LeaveCriticalSection(&os_lock_);
  }

  void AssertHeld() { }

private:
  CRITICAL_SECTION os_lock_;

  // No copying
  Mutex(const Mutex&);
  void operator=(const Mutex&);
};

// based on condition_variable_win.cc from chrome:
// http://src.chromium.org/viewvc/chrome/trunk/src/base/synchronization/condition_variable_win.cc?revision=70363&view=markup
class CondVar {
 public:
  explicit CondVar(Mutex* user_lock);
  ~CondVar();

  void Wait() {
      // Default to "wait forever" timing, which means have to get a Signal()
      // or SignalAll() to come out of this wait state.
      TimedWait(INFINITE);
  }

  void TimedWait(const int64_t max_time_in_ms);
  void Signal();
  void SignalAll();
 private:
   // Define Event class that is used to form circularly linked lists.
   // The list container is an element with NULL as its handle_ value.
   // The actual list elements have a non-zero handle_ value.
   // All calls to methods MUST be done under protection of a lock so that links
   // can be validated.  Without the lock, some links might asynchronously
   // change, and the assertions would fail (as would list change operations).
   class Event {
    public:
     // Default constructor with no arguments creates a list container.
     Event();
     ~Event();
   
     // InitListElement transitions an instance from a container, to an element.
     void InitListElement();
   
     // Methods for use on lists.
     bool IsEmpty() const;
     void PushBack(Event* other);
     Event* PopFront();
     Event* PopBack();
   
     // Methods for use on list elements.
     // Accessor method.
     HANDLE handle() const;
     // Pull an element from a list (if it's in one).
     Event* Extract();
   
     // Method for use on a list element or on a list.
     bool IsSingleton() const;
   
    private:
     // Provide pre/post conditions to validate correct manipulations.
     bool ValidateAsDistinct(Event* other) const;
     bool ValidateAsItem() const;
     bool ValidateAsList() const;
     bool ValidateLinks() const;
   
     HANDLE handle_;
     Event* next_;
     Event* prev_;
     //DISALLOW_COPY_AND_ASSIGN(Event);
   };
   
   // Note that RUNNING is an unlikely number to have in RAM by accident.
   // This helps with defensive destructor coding in the face of user error.
   enum RunState { SHUTDOWN = 0, RUNNING = 64213 };
   
   // Internal implementation methods supporting Wait().
   Event* GetEventForWaiting();
   void RecycleEvent(Event* used_event);
   
   RunState run_state_;
   
   // Private critical section for access to member data.
   Mutex internal_lock_;
   
   // Lock that is acquired before calling Wait().
   Mutex& user_lock_;
   
   // Events that threads are blocked on.
   Event waiting_list_;
   
   // Free list for old events.
   Event recycling_list_;
   int recycling_list_size_;
   
   // The number of allocated, but not yet deleted events.
   int allocation_counter_;
};

// Storage for a lock-free pointer
class AtomicPointer {
 private:
  void * rep_;
 public:
  AtomicPointer() : rep_(nullptr) { }
  explicit AtomicPointer(void* v) {
      Release_Store(v);
  }
  void* Acquire_Load() const {
    void * p = nullptr;
    InterlockedExchangePointer(&p, rep_);
    return p;
  }

  void Release_Store(void* v) {
    InterlockedExchangePointer(&rep_, v);
  }

  void* NoBarrier_Load() const {
    return rep_;
  }

  void NoBarrier_Store(void* v) {
    rep_ = v;
  }
};

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

inline bool Snappy_Uncompress(const char* input, size_t length,
                              char* output) {
#ifdef SNAPPY
  return snappy::RawUncompress(input, length, output);
#else
  return false;
#endif
}

inline bool GetHeapProfile(void (*func)(void*, const char*, int), void* arg) {
  return false;
}

}
}

#endif  // STORAGE_LEVELDB_PORT_PORT_WIN_H_
