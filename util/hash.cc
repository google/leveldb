// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "util/hash.h"

#include <cstring>

#include "util/coding.h"

// The FALLTHROUGH_INTENDED macro can be used to annotate implicit fall-through
// between switch labels. The real definition should be provided externally.
// This one is a fallback version for unsupported compilers.
#ifndef FALLTHROUGH_INTENDED
#define FALLTHROUGH_INTENDED \
  do {                       \
  } while (0)
#endif

namespace leveldb {

uint32_t Hash(const char* data, size_t n, uint32_t seed) {
  // Similar to murmur hash
  const uint32_t m = 0xc6a4a793;
  const uint32_t r = 24;
  uint32_t h = seed ^ (n * m);
  size_t left = n;

  // Pick up four bytes at a time
  while (left >= 4) {
    uint32_t w = DecodeFixed32(data);
    h += w;
    h *= m;
    h ^= (h >> 16);
    data += 4;
    left -= 4;
  }

  // Pick up remaining bytes
  switch (left) {
    case 3:
      h += static_cast<uint8_t>(data[2]) << 16;
      FALLTHROUGH_INTENDED;
    case 2:
      h += static_cast<uint8_t>(data[1]) << 8;
      FALLTHROUGH_INTENDED;
    case 1:
      h += static_cast<uint8_t>(data[0]);
      h *= m;
      h ^= (h >> r);
      break;
  }
  return h;
}

}  // namespace leveldb
