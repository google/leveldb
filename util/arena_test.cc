// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "util/arena.h"

#include <thread>
#include <cstring>

#include "gtest/gtest.h"
#include "util/random.h"

namespace leveldb {

TEST(ArenaTest, Empty) { Arena arena; }

TEST(ArenaTest, Simple) {
  std::vector<std::pair<size_t, char*>> allocated;
  Arena arena;
  const int N = 100000;
  size_t bytes = 0;
  Random rnd(301);
  for (int i = 0; i < N; i++) {
    size_t s;
    if (i % (N / 10) == 0) {
      s = i;
    } else {
      s = rnd.OneIn(4000)
              ? rnd.Uniform(6000)
              : (rnd.OneIn(10) ? rnd.Uniform(100) : rnd.Uniform(20));
    }
    if (s == 0) {
      // Our arena disallows size 0 allocations.
      s = 1;
    }
    char* r;
    if (rnd.OneIn(10)) {
      r = arena.AllocateAligned(s);
    } else {
      r = arena.Allocate(s);
    }

    for (size_t b = 0; b < s; b++) {
      // Fill the "i"th allocation with a known bit pattern
      r[b] = i % 256;
    }
    bytes += s;
    allocated.push_back(std::make_pair(s, r));
    ASSERT_GE(arena.MemoryUsage(), bytes);
    if (i > N / 10) {
      ASSERT_LE(arena.MemoryUsage(), bytes * 1.10);
    }
  }
  for (size_t i = 0; i < allocated.size(); i++) {
    size_t num_bytes = allocated[i].first;
    const char* p = allocated[i].second;
    for (size_t b = 0; b < num_bytes; b++) {
      // Check the "i"th allocation for the known bit pattern
      ASSERT_EQ(int(p[b]) & 0xff, i % 256);
    }
  }
}

void ThreadedAllocation(Arena* arena, std::atomic<size_t>* bytes_allocated, Random* rnd) {
  const int N = 10000; // Number of allocations per thread
  for (int i = 0; i < N; ++i) {
    size_t s = rnd->OneIn(4000) ? rnd->Uniform(1000) : (rnd->OneIn(10) ? rnd->Uniform(100) : rnd->Uniform(20));
    if (s == 0) {
      s = 1; // Ensure we never allocate 0 bytes.
    }
    char* r = arena->Allocate(s);
    std::memset(r, 0, s); // Fill allocated memory with zeros.
    *bytes_allocated += s;
  }
}

TEST(ArenaTest, ThreadSafety) {
  Arena arena;
  std::atomic<size_t> bytes_allocated(0);
  const int kNumThreads = 4; // Adjust based on how many threads you want to test with.
  std::vector<std::thread> threads;
  Random rnd(301);

  for (int i = 0; i < kNumThreads; ++i) {
    threads.emplace_back(std::thread(ThreadedAllocation, &arena, &bytes_allocated, &rnd));
  }

  for (auto& t : threads) {
    t.join();
  }

  ASSERT_GE(arena.MemoryUsage(), bytes_allocated.load());
}

}  // namespace leveldb
