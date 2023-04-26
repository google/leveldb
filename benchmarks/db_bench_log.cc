// Copyright (c) 2019 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include <cinttypes>
#include <cstdio>
#include <string>

#include "gtest/gtest.h"
#include "benchmark/benchmark.h"
#include "db/version_set.h"
#include "leveldb/comparator.h"
#include "leveldb/db.h"
#include "leveldb/env.h"
#include "leveldb/options.h"
#include "port/port.h"
#include "util/mutexlock.h"
#include "util/testutil.h"

namespace leveldb {

namespace {

std::string MakeKey(unsigned int num) {
  char buf[30];
  std::snprintf(buf, sizeof(buf), "%016u", num);
  return std::string(buf);
}

void BM_LogAndApply(benchmark::State& state) {
  const int num_base_files = state.range(0);

  std::string dbname = testing::TempDir() + "leveldb_test_benchmark";
  DestroyDB(dbname, Options());

  DB* db = nullptr;
  Options opts;
  opts.create_if_missing = true;
  Status s = DB::Open(opts, dbname, &db);
  ASSERT_LEVELDB_OK(s);
  ASSERT_TRUE(db != nullptr);

  delete db;
  db = nullptr;

  Env* env = Env::Default();

  port::Mutex mu;
  MutexLock l(&mu);

  InternalKeyComparator cmp(BytewiseComparator());
  Options options;
  VersionSet vset(dbname, &options, nullptr, &cmp);
  bool save_manifest;
  ASSERT_LEVELDB_OK(vset.Recover(&save_manifest));
  VersionEdit vbase;
  uint64_t fnum = 1;
  for (int i = 0; i < num_base_files; i++) {
    InternalKey start(MakeKey(2 * fnum), 1, kTypeValue);
    InternalKey limit(MakeKey(2 * fnum + 1), 1, kTypeDeletion);
    vbase.AddFile(2, fnum++, 1 /* file size */, start, limit);
  }
  ASSERT_LEVELDB_OK(vset.LogAndApply(&vbase, &mu));

  uint64_t start_micros = env->NowMicros();

  for (auto st : state) {
    VersionEdit vedit;
    vedit.RemoveFile(2, fnum);
    InternalKey start(MakeKey(2 * fnum), 1, kTypeValue);
    InternalKey limit(MakeKey(2 * fnum + 1), 1, kTypeDeletion);
    vedit.AddFile(2, fnum++, 1 /* file size */, start, limit);
    vset.LogAndApply(&vedit, &mu);
  }

  uint64_t stop_micros = env->NowMicros();
  unsigned int us = stop_micros - start_micros;
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%d", num_base_files);
  std::fprintf(stderr,
               "BM_LogAndApply/%-6s   %8" PRIu64
               " iters : %9u us (%7.0f us / iter)\n",
               buf, state.iterations(), us, ((float)us) / state.iterations());
}

BENCHMARK(BM_LogAndApply)->Arg(1)->Arg(100)->Arg(10000)->Arg(100000);

}  // namespace

}  // namespace leveldb

BENCHMARK_MAIN();
