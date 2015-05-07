// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

// AtomicPointer provides storage for a lock-free pointer.
// Platform-dependent implementation of AtomicPointer:
// - If the platform provides a cheap barrier, we use it with raw pointers
// - If <atomic> is present (on newer versions of gcc, it is), we use
//   a <atomic>-based AtomicPointer.  However we prefer the memory
//   barrier based version, because at least on a gcc 4.4 32-bit build
//   on linux, we have encountered a buggy <atomic> implementation.
//   Also, some <atomic> implementations are much slower than a memory-barrier
//   based implementation (~16ns for <atomic> based acquire-load vs. ~1ns for
//   a barrier based acquire-load).
// This code is based on atomicops-internals-* in Google's perftools:
// http://code.google.com/p/google-perftools/source/browse/#svn%2Ftrunk%2Fsrc%2Fbase

#ifndef PORT_ATOMIC_POINTER_H_
#define PORT_ATOMIC_POINTER_H_

#include <stdint.h>
#ifdef LEVELDB_ATOMIC_PRESENT
#include <atomic>
#endif
#ifdef OS_WIN
#include <windows.h>
#endif
#ifdef OS_MACOSX
#include <libkern/OSAtomic.h>
#endif

#if defined(_M_X64) || defined(__x86_64__)
#define ARCH_CPU_X86_FAMILY 1
#elif defined(_M_IX86) || defined(__i386__) || defined(__i386)
#define ARCH_CPU_X86_FAMILY 1
#elif defined(__ARMEL__)
#define ARCH_CPU_ARM_FAMILY 1
#elif defined(__aarch64__)
#define ARCH_CPU_ARM64_FAMILY 1
#elif defined(__ppc__) || defined(__powerpc__) || defined(__powerpc64__)
#define ARCH_CPU_PPC_FAMILY 1
#elif defined(__ia64__)
#define ARCH_CPU_IA64_FAMILY 1
#elif defined(__alpha__)
#define ARCH_CPU_ALPHA_FAMILY 1
#elif defined(__s390x__) || defined(__s390__)
#define ARCH_CPU_S390_FAMILY 1
#elif defined(__sparc__) || defined(__sparc64__)
#define ARCH_CPU_SPARC_FAMILY 1
#elif defined(__sh__)
#define ARCH_CPU_SH_FAMILY 1
#elif defined(__hppa__) || defined(__parisc__)
#define ARCH_CPU_PARISC_FAMILY 1
#endif

namespace leveldb {
namespace port {

// Define MemoryBarrier() if available
// Windows on x86
#if defined(OS_WIN) && defined(COMPILER_MSVC) && defined(ARCH_CPU_X86_FAMILY)
// windows.h already provides a MemoryBarrier(void) macro
// http://msdn.microsoft.com/en-us/library/ms684208(v=vs.85).aspx
#define LEVELDB_HAVE_MEMORY_BARRIER

// Mac OS
#elif defined(OS_MACOSX)
inline void ReadMemoryBarrier() {
  OSMemoryBarrier();
}
inline void WriteMemoryBarrier() {
  OSMemoryBarrier();
}
#define LEVELDB_HAVE_MEMORY_BARRIER

#define ReadMemoryBarrier MemoryBarrier()
#define WriteMemoryBarrier MemoryBarrier()

// Gcc on x86
#elif defined(ARCH_CPU_X86_FAMILY) && defined(__GNUC__)
inline void ReadMemoryBarrier() {
  // See http://gcc.gnu.org/ml/gcc/2003-04/msg01180.html for a discussion on
  // this idiom. Also see http://en.wikipedia.org/wiki/Memory_ordering.
  __asm__ __volatile__("" : : : "memory");
}
inline void WriteMemoryBarrier() {
  // See http://gcc.gnu.org/ml/gcc/2003-04/msg01180.html for a discussion on
  // this idiom. Also see http://en.wikipedia.org/wiki/Memory_ordering.
  __asm__ __volatile__("" : : : "memory");
}
#define LEVELDB_HAVE_MEMORY_BARRIER

// Sun Studio
#elif defined(ARCH_CPU_X86_FAMILY) && defined(__SUNPRO_CC)
inline void ReadMemoryBarrier() {
  // See http://gcc.gnu.org/ml/gcc/2003-04/msg01180.html for a discussion on
  // this idiom. Also see http://en.wikipedia.org/wiki/Memory_ordering.
  asm volatile("" : : : "memory");
}
inline void WriteMemoryBarrier() {
  // See http://gcc.gnu.org/ml/gcc/2003-04/msg01180.html for a discussion on
  // this idiom. Also see http://en.wikipedia.org/wiki/Memory_ordering.
  asm volatile("" : : : "memory");
}
#define LEVELDB_HAVE_MEMORY_BARRIER

// ARM Linux
#elif defined(ARCH_CPU_ARM_FAMILY) && defined(__linux__)
typedef void (*LinuxKernelMemoryBarrierFunc)(void);
// The Linux ARM kernel provides a highly optimized device-specific memory
// barrier function at a fixed memory address that is mapped in every
// user-level process.
//
// This beats using CPU-specific instructions which are, on single-core
// devices, un-necessary and very costly (e.g. ARMv7-A "dmb" takes more
// than 180ns on a Cortex-A8 like the one on a Nexus One). Benchmarking
// shows that the extra function call cost is completely negligible on
// multi-core devices.
//
inline void ReadMemoryBarrier() {
  (*(LinuxKernelMemoryBarrierFunc)0xffff0fa0)();
}
inline void WriteMemoryBarrier() {
  (*(LinuxKernelMemoryBarrierFunc)0xffff0fa0)();
}
#define LEVELDB_HAVE_MEMORY_BARRIER

// ARM64
#elif defined(ARCH_CPU_ARM64_FAMILY)
inline void MemoryBarrier() {
  asm volatile("dmb sy" : : : "memory");
}
#define LEVELDB_HAVE_MEMORY_BARRIER

// PPC
#elif defined(ARCH_CPU_PPC_FAMILY) && defined(__GNUC__)

inline void ReadMemoryBarrier() {
#ifdef __powerpc64__
  __asm__ __volatile__ ("lwsync" : : : "memory");
#else
  __asm__ __volatile__ ("sync" : : : "memory");
#endif
}
inline void WriteMemoryBarrier() {
  __asm__ __volatile__ ("sync" : : : "memory");
}
#define LEVELDB_HAVE_MEMORY_BARRIER

// IA64
#elif defined(ARCH_CPU_IA64_FAMILY)
inline void ReadMemoryBarrier() {
  __asm__ __volatile__ ("mf" : : : "memory");
}
inline void WriteMemoryBarrier() {
  __asm__ __volatile__ ("mf" : : : "memory");
}
#define LEVELDB_HAVE_MEMORY_BARRIER

// ALPHA
#elif defined(ARCH_CPU_ALPHA_FAMILY)

inline void ReadMemoryBarrier() {
  __asm__ __volatile__("mb" : : : "memory");
}
inline void WriteMemoryBarrier() {
  __asm__ __volatile__("wmb" : : : "memory");
}
#define LEVELDB_HAVE_MEMORY_BARRIER

// S390
#elif defined(ARCH_CPU_S390_FAMILY)

inline void ReadMemoryBarrier() {
  asm volatile("bcr 15,0" : : : "memory");
}
inline void WriteMemoryBarrier() {
  asm volatile("bcr 15,0" : : : "memory");
}
#define LEVELDB_HAVE_MEMORY_BARRIER

// SPARC
#elif defined(ARCH_CPU_SPARC_FAMILY)

inline void ReadMemoryBarrier() {
  __asm__ __volatile__("" : : : "memory");
}
inline void WriteMemoryBarrier() {
  __asm__ __volatile__("" : : : "memory");
}
#define LEVELDB_HAVE_MEMORY_BARRIER

// SH
#elif defined(ARCH_CPU_SH_FAMILY)
#if defined(__SH4A__) || defined(__SH5__)
inline void ReadMemoryBarrier() {
  __asm__ __volatile__ ("synco": : :"memory");
}
inline void WriteMemoryBarrier() {
  __asm__ __volatile__ ("synco": : :"memory");
}
#else
inline void ReadMemoryBarrier() {
  __asm__ __volatile__ ("": : :"memory");
}
inline void WriteMemoryBarrier() {
  __asm__ __volatile__ ("": : :"memory");
}
#endif
#define LEVELDB_HAVE_MEMORY_BARRIER

// PARISC
#elif defined(ARCH_CPU_PARISC_FAMILY)

inline void ReadMemoryBarrier() {
  __asm__ __volatile__("" : : : "memory");
}
inline void WriteMemoryBarrier() {
  __asm__ __volatile__("" : : : "memory");
}
#define LEVELDB_HAVE_MEMORY_BARRIER

#endif

// AtomicPointer built using platform-specific MemoryBarrier()
#if defined(LEVELDB_HAVE_MEMORY_BARRIER)
class AtomicPointer {
 private:
  void* rep_;
 public:
  AtomicPointer() { }
  explicit AtomicPointer(void* p) : rep_(p) {}
  inline void* NoBarrier_Load() const { return rep_; }
  inline void NoBarrier_Store(void* v) { rep_ = v; }
  inline void* Acquire_Load() const {
    void* result = rep_;
    ReadMemoryBarrier();
    return result;
  }
  inline void Release_Store(void* v) {
    WriteMemoryBarrier();
    rep_ = v;
  }
};

// AtomicPointer based on <cstdatomic>
#elif defined(LEVELDB_ATOMIC_PRESENT)
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

// Atomic pointer based on sparc memory barriers
#elif defined(__sparcv9) && defined(__GNUC__)
class AtomicPointer {
 private:
  void* rep_;
 public:
  AtomicPointer() { }
  explicit AtomicPointer(void* v) : rep_(v) { }
  inline void* Acquire_Load() const {
    void* val;
    __asm__ __volatile__ (
        "ldx [%[rep_]], %[val] \n\t"
         "membar #LoadLoad|#LoadStore \n\t"
        : [val] "=r" (val)
        : [rep_] "r" (&rep_)
        : "memory");
    return val;
  }
  inline void Release_Store(void* v) {
    __asm__ __volatile__ (
        "membar #LoadStore|#StoreStore \n\t"
        "stx %[v], [%[rep_]] \n\t"
        :
        : [rep_] "r" (&rep_), [v] "r" (v)
        : "memory");
  }
  inline void* NoBarrier_Load() const { return rep_; }
  inline void NoBarrier_Store(void* v) { rep_ = v; }
};

// Atomic pointer based on ia64 acq/rel
#elif defined(__ia64) && defined(__GNUC__)
class AtomicPointer {
 private:
  void* rep_;
 public:
  AtomicPointer() { }
  explicit AtomicPointer(void* v) : rep_(v) { }
  inline void* Acquire_Load() const {
    void* val    ;
    __asm__ __volatile__ (
        "ld8.acq %[val] = [%[rep_]] \n\t"
        : [val] "=r" (val)
        : [rep_] "r" (&rep_)
        : "memory"
        );
    return val;
  }
  inline void Release_Store(void* v) {
    __asm__ __volatile__ (
        "st8.rel [%[rep_]] = %[v]  \n\t"
        :
        : [rep_] "r" (&rep_), [v] "r" (v)
        : "memory"
        );
  }
  inline void* NoBarrier_Load() const { return rep_; }
  inline void NoBarrier_Store(void* v) { rep_ = v; }
};

// We have neither MemoryBarrier(), nor <atomic>
#else
#error Please implement AtomicPointer for this platform.

#endif

#undef LEVELDB_HAVE_MEMORY_BARRIER
#undef ARCH_CPU_X86_FAMILY
#undef ARCH_CPU_ARM_FAMILY
#undef ARCH_CPU_ARM64_FAMILY
#undef ARCH_CPU_PPC_FAMILY
#undef ARCH_CPU_IA64_FAMILY
#undef ARCH_CPU_ALPHA_FAMILY
#undef ARCH_CPU_S390_FAMILY
#undef ARCH_CPU_SPARC_FAMILY
#undef ARCH_CPU_SH_FAMILY
#undef ARCH_CPU_PARISC_FAMILY

}  // namespace port
}  // namespace leveldb

#endif  // PORT_ATOMIC_POINTER_H_
