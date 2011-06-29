// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

// AtomicPointer provides storage for a lock-free pointer.
// Platform-dependent implementation of AtomicPointer:
// - If cstdatomic is present (on newer versions of gcc, it is), we use
//   a cstdatomic-based AtomicPointer
// - If it is not, we define processor-dependent AtomicWord operations,
//   and then use them to build AtomicPointer
//
// This code is based on atomicops-internals-* in Google's perftools:
// http://code.google.com/p/google-perftools/source/browse/#svn%2Ftrunk%2Fsrc%2Fbase

#ifndef PORT_ATOMIC_POINTER_H_
#define PORT_ATOMIC_POINTER_H_

#ifdef LEVELDB_CSTDATOMIC_PRESENT

///////////////////////////////////////////////////////////////////////////////
// WE HAVE <cstdatomic>
// Use a <cstdatomic>-based AtomicPointer

#include <cstdatomic>
#include <stdint.h>

namespace leveldb {
namespace port {

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

} // namespace leveldb::port
} // namespace leveldb

#else
///////////////////////////////////////////////////////////////////////////////
// NO <cstdatomic>
// The entire rest of this file covers that case

#if defined(_M_X64) || defined(__x86_64__)
#define ARCH_CPU_X86_FAMILY 1
#elif defined(_M_IX86) || defined(__i386__) || defined(__i386)
#define ARCH_CPU_X86_FAMILY 1
#elif defined(__ARMEL__)
#define ARCH_CPU_ARM_FAMILY 1
#else
#warning Please add support for your architecture in atomicpointer.h
#endif

namespace leveldb {
namespace port {
namespace internal {

// AtomicWord is a machine-sized pointer.
typedef intptr_t AtomicWord;

} // namespace leveldb::port::internal
} // namespace leveldb::port
} // namespace leveldb

// Include our platform specific implementation.
///////////////////////////////////////////////////////////////////////////////
// Windows on x86
#if defined(OS_WIN) && defined(COMPILER_MSVC) && defined(ARCH_CPU_X86_FAMILY)

// void MemoryBarrier(void) macro is defined in windows.h:
// http://msdn.microsoft.com/en-us/library/ms684208(v=vs.85).aspx
// Including windows.h here; MemoryBarrier() gets used below.
#include <windows.h>

///////////////////////////////////////////////////////////////////////////////
// Mac OS on x86
#elif defined(OS_MACOSX) && defined(ARCH_CPU_X86_FAMILY)

#include <libkern/OSAtomic.h>

namespace leveldb {
namespace port {
namespace internal {

inline void MemoryBarrier() {
#if defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))
  // See http://gcc.gnu.org/ml/gcc/2003-04/msg01180.html for a discussion on
  // this idiom. Also see http://en.wikipedia.org/wiki/Memory_ordering.
  __asm__ __volatile__("" : : : "memory");
#else
  OSMemoryBarrier();
#endif
}

} // namespace leveldb::port::internal
} // namespace leveldb::port
} // namespace leveldb

///////////////////////////////////////////////////////////////////////////////
// Any x86 CPU
#elif defined(ARCH_CPU_X86_FAMILY)

namespace leveldb {
namespace port {
namespace internal {

inline void MemoryBarrier() {
  __asm__ __volatile__("" : : : "memory");
}

} // namespace leveldb::port::internal
} // namespace leveldb::port
} // namespace leveldb

#undef ATOMICOPS_COMPILER_BARRIER

///////////////////////////////////////////////////////////////////////////////
// ARM
#elif defined(ARCH_CPU_ARM_FAMILY)

namespace leveldb {
namespace port {
namespace internal {

typedef void (*LinuxKernelMemoryBarrierFunc)(void);
LinuxKernelMemoryBarrierFunc pLinuxKernelMemoryBarrier __attribute__((weak)) =
    (LinuxKernelMemoryBarrierFunc) 0xffff0fa0;

inline void MemoryBarrier() {
  pLinuxKernelMemoryBarrier();
}

} // namespace leveldb::port::internal
} // namespace leveldb::port
} // namespace leveldb

#else
#error "Atomic operations are not supported on your platform"
#endif

///////////////////////////////////////////////////////////////////////////////
// Implementation of AtomicPointer based on MemoryBarriers above

namespace leveldb {
namespace port {
namespace internal {

// Atomic operations using per-system MemoryBarrier()s

inline AtomicWord Acquire_Load(volatile const AtomicWord* ptr) {
  AtomicWord value = *ptr;
  MemoryBarrier();
  return value;
}

inline void Release_Store(volatile AtomicWord* ptr, AtomicWord value) {
  MemoryBarrier();
  *ptr = value;
}

inline AtomicWord NoBarrier_Load(volatile const AtomicWord* ptr) {
  return *ptr;
}

inline void NoBarrier_Store(volatile AtomicWord* ptr, AtomicWord value) {
  *ptr = value;
}

} // namespace leveldb::port::internal

// AtomicPointer definition for systems without <cstdatomic>.
class AtomicPointer {
 private:
  typedef internal::AtomicWord Rep;
  Rep rep_;
 public:
  AtomicPointer() { }
  explicit AtomicPointer(void* p) : rep_(reinterpret_cast<Rep>(p)) {}
  inline void* Acquire_Load() const {
    return reinterpret_cast<void*>(internal::Acquire_Load(&rep_));
  }
  inline void Release_Store(void* v) {
    internal::Release_Store(&rep_, reinterpret_cast<Rep>(v));
  }
  inline void* NoBarrier_Load() const {
    return reinterpret_cast<void*>(internal::NoBarrier_Load(&rep_));
  }
  inline void NoBarrier_Store(void* v) {
    internal::NoBarrier_Store(&rep_, reinterpret_cast<Rep>(v));
  }
};

} // namespace leveldb::port
} // namespace leveldb

#endif // LEVELDB_CSTDATOMIC_PRESENT

#endif  // PORT_ATOMIC_POINTER_H_
