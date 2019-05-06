// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include <sys/wait.h>
#include <unistd.h>

#include "leveldb/env.h"
#include "port/port.h"
#include "util/env_posix_test_helper.h"
#include "util/testharness.h"

namespace leveldb {

static const int kReadOnlyFileLimit = 4;
static const int kMMapLimit = 4;

class EnvPosixTest {
 public:
  static void SetFileLimits(int read_only_file_limit, int mmap_limit) {
    EnvPosixTestHelper::SetReadOnlyFDLimit(read_only_file_limit);
    EnvPosixTestHelper::SetReadOnlyMMapLimit(mmap_limit);
  }

  EnvPosixTest() : env_(Env::Default()) {}

  Env* env_;
};

TEST(EnvPosixTest, TestOpenOnRead) {
  // Write some test data to a single file that will be opened |n| times.
  std::string test_dir;
  ASSERT_OK(env_->GetTestDirectory(&test_dir));
  std::string test_file = test_dir + "/open_on_read.txt";

  FILE* f = fopen(test_file.c_str(), "we");
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

#if defined(HAVE_O_CLOEXEC)

TEST(EnvPosixTest, TestCloseOnExec) {
  // Test that file handles are not inherited by child processes.

  // Open file handles with each of the open methods.
  std::string test_dir;
  ASSERT_OK(env_->GetTestDirectory(&test_dir));
  std::vector<std::string> test_files = {
      test_dir + "/close_on_exec_seq.txt",
      test_dir + "/close_on_exec_rand.txt",
      test_dir + "/close_on_exec_write.txt",
      test_dir + "/close_on_exec_append.txt",
      test_dir + "/close_on_exec_lock.txt",
      test_dir + "/close_on_exec_log.txt",
  };
  for (const std::string& test_file : test_files) {
    const char kFileData[] = "0123456789";
    ASSERT_OK(WriteStringToFile(env_, kFileData, test_file));
  }
  leveldb::SequentialFile* seqFile = nullptr;
  leveldb::RandomAccessFile* randFile = nullptr;
  leveldb::WritableFile* writeFile = nullptr;
  leveldb::WritableFile* appendFile = nullptr;
  leveldb::FileLock* lockFile = nullptr;
  leveldb::Logger* logFile = nullptr;
  ASSERT_OK(env_->NewSequentialFile(test_files[0], &seqFile));
  ASSERT_OK(env_->NewRandomAccessFile(test_files[1], &randFile));
  ASSERT_OK(env_->NewWritableFile(test_files[2], &writeFile));
  ASSERT_OK(env_->NewAppendableFile(test_files[3], &appendFile));
  ASSERT_OK(env_->LockFile(test_files[4], &lockFile));
  ASSERT_OK(env_->NewLogger(test_files[5], &logFile));

  // Fork a child process and wait for it to complete.
  int pid = fork();
  if (pid == 0) {
    const char* const child[] = {"/proc/self/exe", "-cloexec-child", nullptr};
    execv(child[0], const_cast<char* const*>(child));
    printf("Error spawning child process: %s\n", strerror(errno));
    exit(6);
  }
  int status;
  waitpid(pid, &status, 0);
  ASSERT_EQ(0, WEXITSTATUS(status));

  // cleanup
  ASSERT_OK(env_->UnlockFile(lockFile));
  delete seqFile;
  delete randFile;
  delete writeFile;
  delete appendFile;
  delete logFile;
  for (const std::string& test_file : test_files) {
    ASSERT_OK(env_->DeleteFile(test_file));
  }
}

#endif  // defined(HAVE_O_CLOEXEC)

int cloexecChild() {
  // Checks for open file descriptors in the range 3..FD_SETSIZE.
  for (int i = 3; i < FD_SETSIZE; i++) {
    int dup_result = dup2(i, i);
    if (dup_result != -1) {
      printf("Unexpected open file %d\n", i);
      char nbuf[28];
      snprintf(nbuf, 28, "/proc/self/fd/%d", i);
      char dbuf[1024];
      int result = readlink(nbuf, dbuf, 1024);
      if (0 < result && result < 1024) {
        dbuf[result] = 0;
        printf("File descriptor %d is %s\n", i, dbuf);
        if (strstr(dbuf, "close_on_exec_") == nullptr) {
          continue;
        }
      } else if (result >= 1024) {
        printf("(file name length is too long)\n");
      } else {
        printf("Couldn't get file name: %s\n", strerror(errno));
      }
      return 3;
    } else {
      int e = errno;
      if (e != EBADF) {
        printf("Unexpected result reading file handle %d: %s\n", i,
               strerror(errno));
        return 4;
      }
    }
  }
  return 0;
}

}  // namespace leveldb

int main(int argc, char** argv) {
  // Check if this is the child process for TestCloseOnExec
  if (argc > 1 && strcmp(argv[1], "-cloexec-child") == 0) {
    return leveldb::cloexecChild();
  }
  // All tests currently run with the same read-only file limits.
  leveldb::EnvPosixTest::SetFileLimits(leveldb::kReadOnlyFileLimit,
                                       leveldb::kMMapLimit);
  return leveldb::test::RunAllTests();
}
