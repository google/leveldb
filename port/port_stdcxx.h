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
#if HAVE_ZSTD
#define ZSTD_STATIC_LINKING_ONLY  // For ZSTD_compressionParameters.
#include <zstd.h>
#endif  // HAVE_ZSTD

#if HAVE_LZ4
#include <lz4.h>
#endif  // HAVE_LZ4

#include <cassert>
#include <condition_variable>  // NOLINT
#include <cstddef>
#include <cstdint>
#include <mutex>  // NOLINT
#include <string>

#include "port/thread_annotations.h"

namespace leveldb {
namespace port {

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

inline bool Zstd_Compress(int level, const char* input, size_t length,
                          std::string* output) {
#if HAVE_ZSTD
  // Get the MaxCompressedLength.
  size_t outlen = ZSTD_compressBound(length);
  if (ZSTD_isError(outlen)) {
    return false;
  }
  output->resize(outlen);
  ZSTD_CCtx* ctx = ZSTD_createCCtx();
  ZSTD_compressionParameters parameters =
      ZSTD_getCParams(level, std::max(length, size_t{1}), /*dictSize=*/0);
  ZSTD_CCtx_setCParams(ctx, parameters);
  outlen = ZSTD_compress2(ctx, &(*output)[0], output->size(), input, length);
  ZSTD_freeCCtx(ctx);
  if (ZSTD_isError(outlen)) {
    return false;
  }
  output->resize(outlen);
  return true;
#else
  // Silence compiler warnings about unused arguments.
  (void)level;
  (void)input;
  (void)length;
  (void)output;
  return false;
#endif  // HAVE_ZSTD
}

inline bool Zstd_GetUncompressedLength(const char* input, size_t length,
                                       size_t* result) {
#if HAVE_ZSTD
  size_t size = ZSTD_getFrameContentSize(input, length);
  if (size == 0) return false;
  *result = size;
  return true;
#else
  // Silence compiler warnings about unused arguments.
  (void)input;
  (void)length;
  (void)result;
  return false;
#endif  // HAVE_ZSTD
}

inline bool Zstd_Uncompress(const char* input, size_t length, char* output) {
#if HAVE_ZSTD
  size_t outlen;
  if (!Zstd_GetUncompressedLength(input, length, &outlen)) {
    return false;
  }
  ZSTD_DCtx* ctx = ZSTD_createDCtx();
  outlen = ZSTD_decompressDCtx(ctx, output, outlen, input, length);
  ZSTD_freeDCtx(ctx);
  if (ZSTD_isError(outlen)) {
    return false;
  }
  return true;
#else
  // Silence compiler warnings about unused arguments.
  (void)input;
  (void)length;
  (void)output;
  return false;
#endif  // HAVE_ZSTD
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

inline bool Lz4_Compress(const char* input, size_t length,
                         std::string* output) {
#if HAVE_LZ4
  // std::fprintf(stdout, "Lz4_Compress start\n");
  int src_size = (int)(length);

  // LZ4 provides a function that will tell you the maximum size of compressed
  // output based on input data via LZ4_compressBound().
  const int max_dst_size = LZ4_compressBound(src_size);
  output->resize(max_dst_size);
  int compressed_data_size =
      LZ4_compress_default(input, &(*output)[0], src_size, max_dst_size);
  // std::fprintf(stdout, "Lz4_Compress
  // max_dst_size:%d,compressed_data_size:%d,output:%s\n",max_dst_size,compressed_data_size,(*output).c_str());
  // Check return_value to determine what happened.
  if (compressed_data_size <= 0) return false;
  if (compressed_data_size > 0) {
    output->resize(compressed_data_size);
    return true;
  }
#else
  // Silence compiler warnings about unused arguments.
  (void)input;
  (void)length;
  (void)output;
  return 0;
#endif  // HAVE_LZ4
}

inline bool Lz4_UnCompress(const char* input, size_t length, char* output,
                           size_t rawsize) {
#if HAVE_LZ4
  // std::fprintf(stdout, "Lz4_UnCompress start\n");
  // The LZ4_decompress_safe function needs to know where the compressed data
  // is, how many bytes long it is, where the regen_buffer memory location is,
  // and how large regen_buffer (uncompressed) output will be. Again, save the
  // return_value.
  const int decompressed_size =
      LZ4_decompress_safe(input, output, length, rawsize);

  // Check return_value to determine what happened.
  if (decompressed_size <= 0) return false;
  if (decompressed_size > 0) {
    // std::fprintf(stdout, "Lz4_UnCompress end\n");
    return true;
  }
#else
  // Silence compiler warnings about unused arguments.
  (void)input;
  (void)length;
  (void)output;
  return false;
#endif  // HAVE_LZ4
}

}  // namespace port
}  // namespace leveldb

#endif  // STORAGE_LEVELDB_PORT_PORT_STDCXX_H_
