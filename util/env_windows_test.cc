// Copyright (c) 2018 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "gtest/gtest.h"
#include "leveldb/env.h"
#include "port/port.h"
#include "util/env_windows_test_helper.h"
#include "util/testutil.h"

namespace leveldb {

static const int kMMapLimit = 4;

class EnvWindowsTest : public testing::Test {
 public:
  static void SetFileLimits(int mmap_limit) {
    EnvWindowsTestHelper::SetReadOnlyMMapLimit(mmap_limit);
  }

  EnvWindowsTest() : env_(Env::Default()) {}

  Env* env_;
};

TEST_F(EnvWindowsTest, TestOpenOnRead) {
  // Write some test data to a single file that will be opened |n| times.
  std::string test_dir;
  ASSERT_LEVELDB_OK(env_->GetTestDirectory(&test_dir));
  std::string test_file = test_dir + "/open_on_read.txt";

  FILE* f = std::fopen(test_file.c_str(), "w");
  ASSERT_TRUE(f != nullptr);
  const char kFileData[] = "abcdefghijklmnopqrstuvwxyz";
  fputs(kFileData, f);
  std::fclose(f);

  // Open test file some number above the sum of the two limits to force
  // leveldb::WindowsEnv to switch from mapping the file into memory
  // to basic file reading.
  const int kNumFiles = kMMapLimit + 5;
  leveldb::RandomAccessFile* files[kNumFiles] = {0};
  for (int i = 0; i < kNumFiles; i++) {
    ASSERT_LEVELDB_OK(env_->NewRandomAccessFile(test_file, &files[i]));
  }
  char scratch;
  Slice read_result;
  for (int i = 0; i < kNumFiles; i++) {
    ASSERT_LEVELDB_OK(files[i]->Read(i, 1, &read_result, &scratch));
    ASSERT_EQ(kFileData[i], read_result[0]);
  }
  for (int i = 0; i < kNumFiles; i++) {
    delete files[i];
  }
  ASSERT_LEVELDB_OK(env_->RemoveFile(test_file));
}

TEST_F(EnvWindowsTest, TestOpenOnRead_Unicode) {
  // Write some test data to a single file that will be opened |n| times.
  std::string test_dir;
  ASSERT_LEVELDB_OK(env_->GetTestDirectory(&test_dir));
  std::string test_file = test_dir + u8"/open_on_runðŸƒ_read.txt";

  std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
  std::wstring wideUtf8Path = converter.from_bytes(test_file);
  FILE* f = _wfopen(wideUtf8Path.c_str(), L"w");
  ASSERT_TRUE(f != nullptr);
  const char kFileData[] = "abcdefghijklmnopqrstuvwxyz";
  fputs(kFileData, f);
  fclose(f);

  // Open test file some number above the sum of the two limits to force
  // leveldb::WindowsEnv to switch from mapping the file into memory
  // to basic file reading.
  const int kNumFiles = kMMapLimit + 5;
  leveldb::RandomAccessFile* files[kNumFiles] = {0};
  for (int i = 0; i < kNumFiles; i++) {
    ASSERT_LEVELDB_OK(env_->NewRandomAccessFile(test_file, &files[i]));
  }
  char scratch;
  Slice read_result;
  for (int i = 0; i < kNumFiles; i++) {
    ASSERT_LEVELDB_OK(files[i]->Read(i, 1, &read_result, &scratch));
    ASSERT_EQ(kFileData[i], read_result[0]);
  }
  for (int i = 0; i < kNumFiles; i++) {
    delete files[i];
  }
  ASSERT_LEVELDB_OK(env_->DeleteFile(test_file));
}

TEST_F(EnvWindowsTest, TestGetChildrenEmpty) {
  // Create some dummy files.
  std::string test_dir;
  ASSERT_LEVELDB_OK(env_->GetTestDirectory(&test_dir));

  std::vector<std::string> result;
  ASSERT_LEVELDB_OK(env_->GetChildren(test_dir, &result));
  ASSERT_EQ(2, result.size()); // "." and ".." are always returned.
}

TEST_F(EnvWindowsTest, TestGetChildren_ChildFiles) {
  // Create some dummy files.
  std::string test_dir;
  ASSERT_LEVELDB_OK(env_->GetTestDirectory(&test_dir));

  int childFilesCount = 10;
  for (int i = 0; i < childFilesCount; i++) {
    std::string test_file = test_dir + u8"/runðŸƒ_and_jumpðŸ¦˜_" + std::to_string(i) + ".txt";
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    std::wstring wTest_file = converter.from_bytes(test_file);
    FILE* f = _wfopen(wTest_file.c_str(), L"w");
    ASSERT_TRUE(f != nullptr);
    fclose(f);
  }

  std::vector<std::string> result;
  ASSERT_LEVELDB_OK(env_->GetChildren(test_dir, &result));
  ASSERT_EQ(childFilesCount + 2, result.size()); // "." and ".." are returned.
}

}  // namespace leveldb

int main(int argc, char** argv) {
  // All tests currently run with the same read-only file limits.
  leveldb::EnvWindowsTest::SetFileLimits(leveldb::kMMapLimit);
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
