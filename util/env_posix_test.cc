// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "leveldb/env.h"
#include "leveldb/options.h"

#include "port/port.h"
#include "util/testharness.h"

namespace leveldb {

static const int kDelayMicros = 100000;
static const int kReadOnlyFileLimit = 4;
static const int kMMapLimit = 4;

class EnvPosixTest {
 public:
  Env* env_;
  static EnvOptions env_options;
  EnvPosixTest() : env_(Env::NewCustom(env_options)) { }
  ~EnvPosixTest() { delete env_; }

  static void SetFileLimits(int read_only_file_limit, int mmap_limit) {
    env_options.max_fds = read_only_file_limit;
    env_options.max_mmaps = mmap_limit;
  }
};

EnvOptions EnvPosixTest::env_options;

TEST(EnvPosixTest, TestOpenOnRead) {
  // Write some test data to a single file that will be opened |n| times.
  std::string test_dir;
  ASSERT_OK(env_->GetTestDirectory(&test_dir));
  std::string test_file = test_dir + "/open_on_read.txt";

  FILE* f = fopen(test_file.c_str(), "w");
  ASSERT_TRUE(f != nullptr);
  const char kFileData[] = "abcdefghijklmnopqrstuvwxyz";
  fputs(kFileData, f);
  fclose(f);

  // Open test file some number above the sum of the two limits to force
  // open-on-read behavior of POSIX Env leveldb::RandomAccessFile.
  const int kNumFiles = kReadOnlyFileLimit + kMMapLimit + 5;
  leveldb::RandomAccessFile* files[kNumFiles] = {0};
  for (int i = 0; i < kNumFiles; i++) {
    ASSERT_OK(env_->NewRandomAccessFile(test_file, &files[i]));
  }
  char scratch;
  Slice read_result;
  for (int i = 0; i < kNumFiles; i++) {
    ASSERT_OK(files[i]->Read(i, 1, &read_result, &scratch));
    ASSERT_EQ(kFileData[i], read_result[0]);
  }
  for (int i = 0; i < kNumFiles; i++) {
    delete files[i];
  }
  ASSERT_OK(env_->DeleteFile(test_file));
}

}  // namespace leveldb

int main(int argc, char** argv) {
  // All tests currently run with the same read-only file limits.
  leveldb::EnvPosixTest::SetFileLimits(leveldb::kReadOnlyFileLimit,
                                       leveldb::kMMapLimit);
  return leveldb::test::RunAllTests();
}
