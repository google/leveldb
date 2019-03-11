// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "leveldb/env.h"

#include <algorithm>
#include <atomic>

#include "port/port.h"
#include "port/thread_annotations.h"
#include "util/mutexlock.h"
#include "util/testharness.h"
#include "util/testutil.h"

namespace leveldb {

static const int kDelayMicros = 100000;
static const int kReadOnlyFileLimit = 4;
static const int kMMapLimit = 4;

class EnvTest {
 public:
  Env* env_;
  EnvTest() : env_(Env::Default()) { }
};

namespace {

static void SetAtomicBool(void* atomic_bool_ptr) {
  std::atomic<bool>* atomic_bool =
      reinterpret_cast<std::atomic<bool>*>(atomic_bool_ptr);
  atomic_bool->store(true, std::memory_order_relaxed);
}

}  // namespace

TEST(EnvTest, ReadWrite) {
  Random rnd(test::RandomSeed());

  // Get file to use for testing.
  std::string test_dir;
  ASSERT_OK(env_->GetTestDirectory(&test_dir));
  std::string test_file_name = test_dir + "/open_on_read.txt";
  WritableFile* writable_file;
  ASSERT_OK(env_->NewWritableFile(test_file_name, &writable_file));

  // Fill a file with data generated via a sequence of randomly sized writes.
  static const size_t kDataSize = 10 * 1048576;
  std::string data;
  while (data.size() < kDataSize) {
    int len = rnd.Skewed(18);  // Up to 2^18 - 1, but typically much smaller
    std::string r;
    test::RandomString(&rnd, len, &r);
    ASSERT_OK(writable_file->Append(r));
    data += r;
    if (rnd.OneIn(10)) {
      ASSERT_OK(writable_file->Flush());
    }
  }
  ASSERT_OK(writable_file->Sync());
  ASSERT_OK(writable_file->Close());
  delete writable_file;

  // Read all data using a sequence of randomly sized reads.
  SequentialFile* sequential_file;
  ASSERT_OK(env_->NewSequentialFile(test_file_name, &sequential_file));
  std::string read_result;
  std::string scratch;
  while (read_result.size() < data.size()) {
    int len = std::min<int>(rnd.Skewed(18), data.size() - read_result.size());
    scratch.resize(std::max(len, 1));  // at least 1 so &scratch[0] is legal
    Slice read;
    ASSERT_OK(sequential_file->Read(len, &read, &scratch[0]));
    if (len > 0) {
      ASSERT_GT(read.size(), 0);
    }
    ASSERT_LE(read.size(), len);
    read_result.append(read.data(), read.size());
  }
  ASSERT_EQ(read_result, data);
  delete sequential_file;
}

TEST(EnvTest, RunImmediately) {
  std::atomic<bool> called(false);
  env_->Schedule(&SetAtomicBool, &called);
  env_->SleepForMicroseconds(kDelayMicros);
  ASSERT_TRUE(called.load(std::memory_order_relaxed));
}

TEST(EnvTest, RunMany) {
  std::atomic<int> last_id(0);

  struct Callback {
    std::atomic<int>* const last_id_ptr_;  // Pointer to shared state.
    const int id_;  // Order# for the execution of this callback.

    Callback(std::atomic<int>* last_id_ptr, int id)
        : last_id_ptr_(last_id_ptr), id_(id) { }

    static void Run(void* arg) {
      Callback* callback = reinterpret_cast<Callback*>(arg);
      int current_id = callback->last_id_ptr_->load(std::memory_order_relaxed);
      ASSERT_EQ(callback->id_ - 1, current_id);
      callback->last_id_ptr_->store(callback->id_, std::memory_order_relaxed);
    }
  };

  Callback callback1(&last_id, 1);
  Callback callback2(&last_id, 2);
  Callback callback3(&last_id, 3);
  Callback callback4(&last_id, 4);
  env_->Schedule(&Callback::Run, &callback1);
  env_->Schedule(&Callback::Run, &callback2);
  env_->Schedule(&Callback::Run, &callback3);
  env_->Schedule(&Callback::Run, &callback4);

  env_->SleepForMicroseconds(kDelayMicros);
  ASSERT_EQ(4, last_id.load(std::memory_order_relaxed));
}

struct State {
  port::Mutex mu;
  int val GUARDED_BY(mu);
  int num_running GUARDED_BY(mu);

  State(int val, int num_running) : val(val), num_running(num_running) { }
};

static void ThreadBody(void* arg) {
  State* s = reinterpret_cast<State*>(arg);
  s->mu.Lock();
  s->val += 1;
  s->num_running -= 1;
  s->mu.Unlock();
}

TEST(EnvTest, StartThread) {
  State state(0, 3);
  for (int i = 0; i < 3; i++) {
    env_->StartThread(&ThreadBody, &state);
  }
  while (true) {
    state.mu.Lock();
    int num = state.num_running;
    state.mu.Unlock();
    if (num == 0) {
      break;
    }
    env_->SleepForMicroseconds(kDelayMicros);
  }

  MutexLock l(&state.mu);
  ASSERT_EQ(state.val, 3);
}

TEST(EnvTest, TestOpenNonExistentFile) {
  // Write some test data to a single file that will be opened |n| times.
  std::string test_dir;
  ASSERT_OK(env_->GetTestDirectory(&test_dir));

  std::string non_existent_file = test_dir + "/non_existent_file";
  ASSERT_TRUE(!env_->FileExists(non_existent_file));

  RandomAccessFile* random_access_file;
  Status status = env_->NewRandomAccessFile(
      non_existent_file, &random_access_file);
  ASSERT_TRUE(status.IsNotFound());

  SequentialFile* sequential_file;
  status = env_->NewSequentialFile(non_existent_file, &sequential_file);
  ASSERT_TRUE(status.IsNotFound());
}

TEST(EnvTest, ReopenWritableFile) {
  std::string test_dir;
  ASSERT_OK(env_->GetTestDirectory(&test_dir));
  std::string test_file_name = test_dir + "/reopen_writable_file.txt";
  env_->DeleteFile(test_file_name);

  WritableFile* writable_file;
  ASSERT_OK(env_->NewWritableFile(test_file_name, &writable_file));
  std::string data("hello world!");
  ASSERT_OK(writable_file->Append(data));
  ASSERT_OK(writable_file->Close());
  delete writable_file;

  ASSERT_OK(env_->NewWritableFile(test_file_name, &writable_file));
  data = "42";
  ASSERT_OK(writable_file->Append(data));
  ASSERT_OK(writable_file->Close());
  delete writable_file;

  ASSERT_OK(ReadFileToString(env_, test_file_name, &data));
  ASSERT_EQ(std::string("42"), data);
  env_->DeleteFile(test_file_name);
}

TEST(EnvTest, ReopenAppendableFile) {
  std::string test_dir;
  ASSERT_OK(env_->GetTestDirectory(&test_dir));
  std::string test_file_name = test_dir + "/reopen_appendable_file.txt";
  env_->DeleteFile(test_file_name);

  WritableFile* appendable_file;
  ASSERT_OK(env_->NewAppendableFile(test_file_name, &appendable_file));
  std::string data("hello world!");
  ASSERT_OK(appendable_file->Append(data));
  ASSERT_OK(appendable_file->Close());
  delete appendable_file;

  ASSERT_OK(env_->NewAppendableFile(test_file_name, &appendable_file));
  data = "42";
  ASSERT_OK(appendable_file->Append(data));
  ASSERT_OK(appendable_file->Close());
  delete appendable_file;

  ASSERT_OK(ReadFileToString(env_, test_file_name, &data));
  ASSERT_EQ(std::string("hello world!42"), data);
  env_->DeleteFile(test_file_name);
}

}  // namespace leveldb

int main(int argc, char** argv) {
  return leveldb::test::RunAllTests();
}
