// Copyright (c) 2018 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef STORAGE_LEVELDB_PORT_PORT_STDCXX_H_
#define STORAGE_LEVELDB_PORT_PORT_STDCXX_H_

// port/port_config.h availability is automatically detected via __has_include
// in newer compilers. If LEVELDB_HAS_PORT_CONFIG_H is defined, it overrides the
// configuration detection.
#if defined(LEVELDB_HAS_PORT_CONFIG_H)

#if LEVELDB_HAS_PORT_CONFIG_H
#include "port/port_config.h"
#endif  // LEVELDB_HAS_PORT_CONFIG_H

#elif defined(__has_include)

#if __has_include("port/port_config.h")
#include "port/port_config.h"
#endif  // __has_include("port/port_config.h")

#endif  // defined(LEVELDB_HAS_PORT_CONFIG_H)

#if HAVE_CRC32C
#include <crc32c/crc32c.h>
#endif  // HAVE_CRC32C
#if HAVE_SNAPPY
#include <snappy.h>
#endif  // HAVE_SNAPPY
#if HAVE_ZLIB
#include <zlib.h>
#endif  // HAV_ZLIB

#include <cassert>
#include <condition_variable>  // NOLINT
#include <cstddef>
#include <cstdint>
#include <mutex>  // NOLINT
#include <string>

#include "port/thread_annotations.h"

namespace leveldb {
namespace port {

static const bool kLittleEndian = !LEVELDB_IS_BIG_ENDIAN;

class CondVar;

// Thinly wraps std::mutex.
class LOCKABLE Mutex {
 public:
  Mutex() = default;
  ~Mutex() = default;

  Mutex(const Mutex&) = delete;
  Mutex& operator=(const Mutex&) = delete;

  void Lock() EXCLUSIVE_LOCK_FUNCTION() { mu_.lock(); }
  void Unlock() UNLOCK_FUNCTION() { mu_.unlock(); }
  void AssertHeld() ASSERT_EXCLUSIVE_LOCK() {}

 private:
  friend class CondVar;
  std::mutex mu_;
};

// Thinly wraps std::condition_variable.
class CondVar {
 public:
  explicit CondVar(Mutex* mu) : mu_(mu) { assert(mu != nullptr); }
  ~CondVar() = default;

  CondVar(const CondVar&) = delete;
  CondVar& operator=(const CondVar&) = delete;

  void Wait() {
    std::unique_lock<std::mutex> lock(mu_->mu_, std::adopt_lock);
    cv_.wait(lock);
    lock.release();
  }
  void Signal() { cv_.notify_one(); }
  void SignalAll() { cv_.notify_all(); }

 private:
  std::condition_variable cv_;
  Mutex* const mu_;
};

inline bool Snappy_Compress(const char* input, size_t length,
                            std::string* output) {
#if HAVE_SNAPPY
  output->resize(snappy::MaxCompressedLength(length));
  size_t outlen;
  snappy::RawCompress(input, length, &(*output)[0], &outlen);
  output->resize(outlen);
  return true;
#else
  // Silence compiler warnings about unused arguments.
  (void)input;
  (void)length;
  (void)output;
#endif  // HAVE_SNAPPY

  return false;
}

inline bool Snappy_GetUncompressedLength(const char* input, size_t length,
                                         size_t* result) {
#if HAVE_SNAPPY
  return snappy::GetUncompressedLength(input, length, result);
#else
  // Silence compiler warnings about unused arguments.
  (void)input;
  (void)length;
  (void)result;
  return false;
#endif  // HAVE_SNAPPY
}

inline bool Snappy_Uncompress(const char* input, size_t length, char* output) {
#if HAVE_SNAPPY
  return snappy::RawUncompress(input, length, output);
#else
  // Silence compiler warnings about unused arguments.
  (void)input;
  (void)length;
  (void)output;
  return false;
#endif  // HAVE_SNAPPY
}

inline bool ZLib_Compress(const char* input, size_t length,
                          std::string* output) {
#ifdef HAVE_ZLIB

  z_stream stream;
  size_t currentOutSize = 0;
  stream.zalloc = Z_NULL;
  stream.zfree = Z_NULL;
  stream.opaque = Z_NULL;

  if (Z_OK != deflateInit(&stream, Z_BEST_COMPRESSION)) return false;

  stream.next_in = (Bytef*)input;
  stream.avail_in = length;
  output->clear();

  // Now, Compress...
  do {
    size_t currentOutSize = output->size();
    // grow output size by `length`
    output->resize(output->size() + length);
    stream.next_out = (Bytef*)(output->data() + currentOutSize);
    stream.avail_out = length;

    if (Z_OK != deflate(&stream, Z_FINISH)) return false;
    if (stream.avail_in == 0) {
      // free unneeded `out` bytes
      output->resize(output->size() - stream.avail_out);
    } else
      currentOutSize += length;
  } while (stream.avail_in > 0);

  if (Z_OK != deflateEnd(&stream)) return false;
  return true;
  //#else
  // Silence compiler warnings about unused arguments.
  (void)input;
  (void)length;
  (void)output;

  return false;
#endif  // HAVE_ZLIB
}

/*inline bool ZLib_GetUncompressedLength(const char* input, size_t length,
                                       size_t* result) {
#ifdef HAVE_ZLIB
  z_stream stream;
  *result = 0;
  std::string tmp;
  tmp.resize(length);

  stream.zalloc = Z_NULL;
  stream.zfree = Z_NULL;
  stream.opaque = Z_NULL;

  if (Z_OK != inflateInit(&stream)) return false;

  stream.avail_in = length;
  stream.next_in = (Bytef*)input;

  do {
    stream.avail_out = length;
    stream.next_out = (Bytef*)tmp.data();
    if (Z_OK != inflate(&stream, Z_FINISH)) return false;

    if (stream.avail_in == 0) {
      *result += (length - stream.avail_out);
    } else {
      *result += length;
    }

  } while (stream.avail_in > 0);
  if (Z_OK != inflateEnd(&stream)) return false;
  return true;
#else
  (void)input;
  (void)length;
  (void)result;
  return false;
#endif  // HAVE_ZLIB
}*/

inline bool ZLib_Uncompress(const char* input, size_t length, char* output,
                            size_t* outSize) {
#ifdef HAVE_ZLIB
  z_stream stream;
  *outSize = 0;
  output = (char*)malloc(length);

  stream.zalloc = Z_NULL;
  stream.zfree = Z_NULL;
  stream.opaque = Z_NULL;

  if (Z_OK != inflateInit(&stream)) return false;

  stream.next_in = (Bytef*)input;
  stream.avail_in = length;

  do {
    stream.next_out = (Bytef*)(output + (*outSize));
    stream.avail_out = length;

    if (Z_OK != inflate(&stream, Z_FINISH)) return false;

    (*outSize) += length - stream.avail_out;
    if (stream.avail_in > 0) {
      output = (char*)realloc(output, *(outSize) + length);
    }
  } while (stream.avail_in > 0);
  if (Z_OK != inflateEnd(&stream)) return false;
  return true;
#else
  (void)input;

  (void)length;
  (void)output;
  return false;
#endif  // HAVE_ZLIB
}

inline bool GetHeapProfile(void (*func)(void*, const char*, int), void* arg) {
  // Silence compiler warnings about unused arguments.
  (void)func;
  (void)arg;
  return false;
}

inline uint32_t AcceleratedCRC32C(uint32_t crc, const char* buf, size_t size) {
#if HAVE_CRC32C
  return ::crc32c::Extend(crc, reinterpret_cast<const uint8_t*>(buf), size);
#else
  // Silence compiler warnings about unused arguments.
  (void)crc;
  (void)buf;
  (void)size;
  return 0;
#endif  // HAVE_CRC32C
}

}  // namespace port
}  // namespace leveldb

#endif  // STORAGE_LEVELDB_PORT_PORT_STDCXX_H_
