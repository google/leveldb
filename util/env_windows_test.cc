// Copyright (c) 2018 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include <windows.h>

#include <unordered_set>

#include "gtest/gtest.h"
#include "leveldb/env.h"
#include "port/port.h"
#include "util/env_windows_test_helper.h"
#include "util/testutil.h"

namespace {

// Returns void so the implementation can use ASSERT_EQ.
void GetOpenHandles(std::unordered_set<HANDLE>* open_handles) {
  constexpr int kHandleOffset = 4;
  const HANDLE kHandleUpperBound = reinterpret_cast<HANDLE>(1000 * kHandleOffset);

  for (HANDLE handle = nullptr; handle < kHandleUpperBound; reinterpret_cast<size_t&>(handle) += kHandleOffset) {
    DWORD dwFlags;
    if (!GetHandleInformation(handle, &dwFlags)) {
      ASSERT_EQ(ERROR_INVALID_HANDLE, ::GetLastError())
          << "GetHandleInformation() should return ERROR_INVALID_HANDLE error on invalid handles";
      continue;
    }
    open_handles->insert(handle);
  }
}

// Returns void so the implementation can use ASSERT_GT.
void GetOpenedFileHandlesByFileName(const char* name, std::unordered_set<HANDLE>* result_handles) {
  std::unordered_set<HANDLE> open_handles;
  GetOpenHandles(&open_handles);

  for (auto handle_it = open_handles.begin(); handle_it != open_handles.end(); ) {
    char handle_path[MAX_PATH];
    DWORD ret = ::GetFinalPathNameByHandleA(*handle_it, handle_path, sizeof handle_path, FILE_NAME_NORMALIZED);
    if (ret != 0) {
      ASSERT_GT(sizeof handle_path, ret) << "Path too long";

      const char* last_backslash = std::strrchr(handle_path, '\\');
      ASSERT_NE(last_backslash, nullptr);
      if (std::strcmp(name, last_backslash + 1) == 0) {
        ++handle_it;
        continue;  // matched
      }
    }

    // otherwise remove it
    handle_it = open_handles.erase(handle_it);
  }

  *result_handles = std::move(open_handles);
}

void CheckOpenedFileHandleNonInheritable(const char* name) {
  std::unordered_set<HANDLE> found_handles;
  GetOpenedFileHandlesByFileName(name, &found_handles);
  ASSERT_EQ(1, found_handles.size());

  DWORD dwFlags;
  ASSERT_TRUE(GetHandleInformation(*found_handles.begin(), &dwFlags));
  ASSERT_FALSE(dwFlags & HANDLE_FLAG_INHERIT);
}

}  // namespace

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

  FILE* f = std::fopen(test_file.c_str(), "wN");
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

TEST_F(EnvWindowsTest, TestHandleNotInheritedLogger) {
  std::string test_dir;
  ASSERT_LEVELDB_OK(env_->GetTestDirectory(&test_dir));
  const char kFileName[] = "handle_not_inherited_logger.txt";
  std::string file_path = test_dir + "/" + kFileName;
  ASSERT_LEVELDB_OK(WriteStringToFile(env_, "0123456789", file_path));

  leveldb::Logger* file = nullptr;
  ASSERT_LEVELDB_OK(env_->NewLogger(file_path, &file));
  CheckOpenedFileHandleNonInheritable(kFileName);
  delete file;

  ASSERT_LEVELDB_OK(env_->RemoveFile(file_path));
}

}  // namespace leveldb

int main(int argc, char** argv) {
  // All tests currently run with the same read-only file limits.
  leveldb::EnvWindowsTest::SetFileLimits(leveldb::kMMapLimit);
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
