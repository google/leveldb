// Copyright (c) 2019 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

// Test for issue 718: leveldb crashes when the values's size is bigger
// than 1ull << 32.

#include <cstdlib>
#include <string>

#include "gtest/gtest.h"
#include "leveldb/db.h"
#include "util/testutil.h"

namespace {

TEST(Issue718, Test) {
  std::srand(std::time(0));
  leveldb::DB* db;
  leveldb::Options options;
  options.create_if_missing = true;
  options.compression = leveldb::kNoCompression;
  std::string dbpath = testing::TempDir() + "leveldb_issue718_test";
  ASSERT_LEVELDB_OK(leveldb::DB::Open(options, dbpath, &db));

  size_t val_size = (1ull << 32) + (rand() % 1024);
  char c = static_cast<char>(rand() % 256);
  std::string key("test_issue718");
  std::string value(val_size, c);
  std::string get_value;
  ASSERT_LEVELDB_OK(db->Put(leveldb::WriteOptions(), key, value));
  ASSERT_LEVELDB_OK(db->Get(leveldb::ReadOptions(), key, &get_value));
  ASSERT_EQ(value, get_value);

  delete db;

  ASSERT_LEVELDB_OK(leveldb::DB::Open(options, dbpath, &db));
  get_value.clear();
  ASSERT_LEVELDB_OK(db->Get(leveldb::ReadOptions(), key, &get_value));
  ASSERT_EQ(value, get_value);

  DestroyDB(dbpath, options);
}

}  // namespace

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}