// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef STORAGE_LEVELDB_PORT_SHA1_PORTABLE_H_
#define STORAGE_LEVELDB_PORT_SHA1_PORTABLE_H_

#include <stddef.h>

namespace leveldb {
namespace port {

// Compute the SHA1 hash value of "data[0..len-1]" and store it in
// "hash_array[0..19]".  hash_array must have 20 bytes of space available.
//
// This function is portable but may not be as fast as a version
// optimized for your platform.  It is provided as a default method
// that can be used when porting leveldb to a new platform if no
// better SHA1 hash implementation is available.
void SHA1_Hash_Portable(const char* data, size_t len, char* hash_array);

}
}

#endif  // STORAGE_LEVELDB_PORT_SHA1_PORTABLE_H_
