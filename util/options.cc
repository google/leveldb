// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "leveldb/options.h"

#include "leveldb/comparator.h"
#include "leveldb/env.h"

namespace leveldb {

// Up to 1000 mmaps for 64-bit binaries; none for 32-bit.
constexpr int kDefaultMmapLimit = (sizeof(void*) >= 8) ? 1000 : 0;

EnvOptions::EnvOptions() : readonly_mmap_files_limit(kDefaultMmapLimit) {}

Options::Options() : comparator(BytewiseComparator()), env(Env::Default()) {}

}  // namespace leveldb
