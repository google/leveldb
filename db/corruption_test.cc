// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include <sys/types.h>

#include "gtest/gtest.h"
#include "db/db_impl.h"
#include "db/dbformat.h"
#include "db/filename.h"
#include "db/log_format.h"
#include "db/version_set.h"
#include "leveldb/cache.h"
#include "leveldb/db.h"
#include "leveldb/table.h"
#include "leveldb/table_builder.h"
#include "leveldb/write_batch.h"
#include "util/logging.h"
#include "util/testutil.h"

namespace leveldb {

static const int kValueSize = 1000;

class CorruptionTest : public testing::Test {
 public:
  CorruptionTest()
      : db_(nullptr),
        dbname_("/memenv/corruption_test"),
        tiny_cache_(NewLRUCache(100)) {
    options_.env = &env_;
    options_.block_cache = tiny_cache_;
    DestroyDB(dbname_, options_);

    options_.create_if_missing = true;
    Reopen();
    options_.create_if_missing = false;
  }

  ~CorruptionTest() {
    delete db_;
    delete tiny_cache_;
  }

  Status TryReopen() {
    delete db_;
    db_ = nullptr;
    return DB::Open(options_, dbname_, &db_);
  }

  void Reopen() { ASSERT_LEVELDB_OK(TryReopen()); }

  void RepairDB() {
    delete db_;
    db_ = nullptr;
    ASSERT_LEVELDB_OK(::leveldb::RepairDB(dbname_, options_));
  }

  void Build(int n) {
    std::string key_space, value_space;
    WriteBatch batch;
    for (int i = 0; i < n; i++) {
      // if ((i % 100) == 0) std::fprintf(stderr, "@ %d of %d\n", i, n);
      Slice key = Key(i, &key_space);
      batch.Clear();
      batch.Put(key, Value(i, &value_space));
      WriteOptions options;
      // Corrupt() doesn't work without this sync on windows; stat reports 0 for
      // the file size.
      if (i == n - 1) {
        options.sync = true;
      }
      ASSERT_LEVELDB_OK(db_->Write(options, &batch));
    }
  }

  void Check(int min_expected, int max_expected) {
    int next_expected = 0;
    int missed = 0;
    int bad_keys = 0;
    int bad_values = 0;
    int correct = 0;
    std::string value_space;
    Iterator* iter = db_->NewIterator(ReadOptions());
    for (iter->SeekToFirst(); iter->Valid(); iter->Next()) {
      uint64_t key;
      Slice in(iter->key());
      if (in == "" || in == "~") {
        // Ignore boundary keys.
        continue;
      }
      if (!ConsumeDecimalNumber(&in, &key) || !in.empty() ||
          key < next_expected) {
        bad_keys++;
        continue;
      }
      missed += (key - next_expected);
      next_expected = key + 1;
      if (iter->value() != Value(key, &value_space)) {
        bad_values++;
      } else {
        correct++;
      }
    }
    delete iter;

    std::fprintf(
        stderr,
        "expected=%d..%d; got=%d; bad_keys=%d; bad_values=%d; missed=%d\n",
        min_expected, max_expected, correct, bad_keys, bad_values, missed);
    ASSERT_LE(min_expected, correct);
    ASSERT_GE(max_expected, correct);
  }

  void Corrupt(FileType filetype, int offset, int bytes_to_corrupt) {
    // Pick file to corrupt
    std::vector<std::string> filenames;
    ASSERT_LEVELDB_OK(env_.target()->GetChildren(dbname_, &filenames));
    uint64_t number;
    FileType type;
    std::string fname;
    int picked_number = -1;
    for (size_t i = 0; i < filenames.size(); i++) {
      if (ParseFileName(filenames[i], &number, &type) && type == filetype &&
          int(number) > picked_number) {  // Pick latest file
        fname = dbname_ + "/" + filenames[i];
        picked_number = number;
      }
    }
    ASSERT_TRUE(!fname.empty()) << filetype;

    uint64_t file_size;
    ASSERT_LEVELDB_OK(env_.target()->GetFileSize(fname, &file_size));

    if (offset < 0) {
      // Relative to end of file; make it absolute
      if (-offset > file_size) {
        offset = 0;
      } else {
        offset = file_size + offset;
      }
    }
    if (offset > file_size) {
      offset = file_size;
    }
    if (offset + bytes_to_corrupt > file_size) {
      bytes_to_corrupt = file_size - offset;
    }

    // Do it
    std::string contents;
    Status s = ReadFileToString(env_.target(), fname, &contents);
    ASSERT_TRUE(s.ok()) << s.ToString();
    for (int i = 0; i < bytes_to_corrupt; i++) {
      contents[i + offset] ^= 0x80;
    }
    s = WriteStringToFile(env_.target(), contents, fname);
    ASSERT_TRUE(s.ok()) << s.ToString();
  }

  int Property(const std::string& name) {
    std::string property;
    int result;
    if (db_->GetProperty(name, &property) &&
        sscanf(property.c_str(), "%d", &result) == 1) {
      return result;
    } else {
      return -1;
    }
  }

  const std::string& dbname() const { return dbname_; }

  // Return the ith key
  Slice Key(int i, std::string* storage) {
    char buf[100];
    std::snprintf(buf, sizeof(buf), "%016d", i);
    storage->assign(buf, strlen(buf));
    return Slice(*storage);
  }

  // Return the value to associate with the specified key
  Slice Value(int k, std::string* storage) {
    Random r(k);
    return test::RandomString(&r, kValueSize, storage);
  }

  test::ErrorEnv env_;
  Options options_;
  DB* db_;

 private:
  std::string dbname_;
  Cache* tiny_cache_;
};

TEST_F(CorruptionTest, Recovery) {
  Build(100);
  Check(100, 100);
  Corrupt(kLogFile, 19, 1);  // WriteBatch tag for first record
  Corrupt(kLogFile, log::kBlockSize + 1000, 1);  // Somewhere in second block
  Reopen();

  // The 64 records in the first two log blocks are completely lost.
  Check(36, 36);
}

TEST_F(CorruptionTest, RecoverWriteError) {
  env_.writable_file_error_ = true;
  Status s = TryReopen();
  ASSERT_TRUE(!s.ok());
}

TEST_F(CorruptionTest, NewFileErrorDuringWrite) {
  // Do enough writing to force minor compaction
  env_.writable_file_error_ = true;
  const int num = 3 + (Options().write_buffer_size / kValueSize);
  std::string value_storage;
  Status s;
  for (int i = 0; s.ok() && i < num; i++) {
    WriteBatch batch;
    batch.Put("a", Value(100, &value_storage));
    s = db_->Write(WriteOptions(), &batch);
  }
  ASSERT_TRUE(!s.ok());
  ASSERT_GE(env_.num_writable_file_errors_, 1);
  env_.writable_file_error_ = false;
  Reopen();
}

TEST_F(CorruptionTest, TableFile) {
  Build(100);
  DBImpl* dbi = reinterpret_cast<DBImpl*>(db_);
  dbi->TEST_CompactMemTable();
  dbi->TEST_CompactRange(0, nullptr, nullptr);
  dbi->TEST_CompactRange(1, nullptr, nullptr);

  Corrupt(kTableFile, 100, 1);
  Check(90, 99);
}

TEST_F(CorruptionTest, TableFileRepair) {
  options_.block_size = 2 * kValueSize;  // Limit scope of corruption
  options_.paranoid_checks = true;
  Reopen();
  Build(100);
  DBImpl* dbi = reinterpret_cast<DBImpl*>(db_);
  dbi->TEST_CompactMemTable();
  dbi->TEST_CompactRange(0, nullptr, nullptr);
  dbi->TEST_CompactRange(1, nullptr, nullptr);

  Corrupt(kTableFile, 100, 1);
  RepairDB();
  Reopen();
  Check(95, 99);
}

TEST_F(CorruptionTest, TableFileIndexData) {
  Build(10000);  // Enough to build multiple Tables
  DBImpl* dbi = reinterpret_cast<DBImpl*>(db_);
  dbi->TEST_CompactMemTable();

  Corrupt(kTableFile, -2000, 500);
  Reopen();
  Check(5000, 9999);
}

TEST_F(CorruptionTest, MissingDescriptor) {
  Build(1000);
  RepairDB();
  Reopen();
  Check(1000, 1000);
}

TEST_F(CorruptionTest, SequenceNumberRecovery) {
  ASSERT_LEVELDB_OK(db_->Put(WriteOptions(), "foo", "v1"));
  ASSERT_LEVELDB_OK(db_->Put(WriteOptions(), "foo", "v2"));
  ASSERT_LEVELDB_OK(db_->Put(WriteOptions(), "foo", "v3"));
  ASSERT_LEVELDB_OK(db_->Put(WriteOptions(), "foo", "v4"));
  ASSERT_LEVELDB_OK(db_->Put(WriteOptions(), "foo", "v5"));
  RepairDB();
  Reopen();
  std::string v;
  ASSERT_LEVELDB_OK(db_->Get(ReadOptions(), "foo", &v));
  ASSERT_EQ("v5", v);
  // Write something.  If sequence number was not recovered properly,
  // it will be hidden by an earlier write.
  ASSERT_LEVELDB_OK(db_->Put(WriteOptions(), "foo", "v6"));
  ASSERT_LEVELDB_OK(db_->Get(ReadOptions(), "foo", &v));
  ASSERT_EQ("v6", v);
  Reopen();
  ASSERT_LEVELDB_OK(db_->Get(ReadOptions(), "foo", &v));
  ASSERT_EQ("v6", v);
}

TEST_F(CorruptionTest, CorruptedDescriptor) {
  ASSERT_LEVELDB_OK(db_->Put(WriteOptions(), "foo", "hello"));
  DBImpl* dbi = reinterpret_cast<DBImpl*>(db_);
  dbi->TEST_CompactMemTable();
  dbi->TEST_CompactRange(0, nullptr, nullptr);

  Corrupt(kDescriptorFile, 0, 1000);
  Status s = TryReopen();
  ASSERT_TRUE(!s.ok());

  RepairDB();
  Reopen();
  std::string v;
  ASSERT_LEVELDB_OK(db_->Get(ReadOptions(), "foo", &v));
  ASSERT_EQ("hello", v);
}

TEST_F(CorruptionTest, CompactionInputError) {
  Build(10);
  DBImpl* dbi = reinterpret_cast<DBImpl*>(db_);
  dbi->TEST_CompactMemTable();
  const int last = config::kMaxMemCompactLevel;
  ASSERT_EQ(1, Property("leveldb.num-files-at-level" + NumberToString(last)));

  Corrupt(kTableFile, 100, 1);
  Check(5, 9);

  // Force compactions by writing lots of values
  Build(10000);
  Check(10000, 10000);
}

TEST_F(CorruptionTest, CompactionInputErrorParanoid) {
  options_.paranoid_checks = true;
  options_.write_buffer_size = 512 << 10;
  Reopen();
  DBImpl* dbi = reinterpret_cast<DBImpl*>(db_);

  // Make multiple inputs so we need to compact.
  for (int i = 0; i < 2; i++) {
    Build(10);
    dbi->TEST_CompactMemTable();
    Corrupt(kTableFile, 100, 1);
    env_.SleepForMicroseconds(100000);
  }
  dbi->CompactRange(nullptr, nullptr);

  // Write must fail because of corrupted table
  std::string tmp1, tmp2;
  Status s = db_->Put(WriteOptions(), Key(5, &tmp1), Value(5, &tmp2));
  ASSERT_TRUE(!s.ok()) << "write did not fail in corrupted paranoid db";
}

namespace {
// A WritableFile that collects into a string, for building tables in tests.
class MemorySink : public WritableFile {
 public:
  Status Close() override { return Status::OK(); }
  Status Flush() override { return Status::OK(); }
  Status Sync() override { return Status::OK(); }
  Status Append(const Slice& data) override {
    contents_.append(data.data(), data.size());
    return Status::OK();
  }

  std::string contents_;
};
}  // namespace

TEST_F(CorruptionTest, RepairTableRecomputesMetadataAfterTransientError) {
  // A table whose first read fails (injected once): ScanTable() records the
  // error and hands the file to RepairTable(), whose re-scan succeeds.  The
  // rebuilt table must publish the metadata of the records that actually
  // made it in - not the empty boundaries the failed first scan collected.
  delete db_;
  db_ = nullptr;

  // Hand-build a valid table under the old ".sst" name.
  const std::string sst_name = SSTTableFileName(dbname(), 7);
  std::string table_bytes;
  {
    Options builder_options;
    builder_options.compression = kNoCompression;
    MemorySink sink;
    TableBuilder builder(builder_options, &sink);
    InternalKey key("key", 5, kTypeValue);
    builder.Add(key.Encode(), "v1");
    ASSERT_LEVELDB_OK(builder.Finish());
    table_bytes = sink.contents_;
  }
  {
    WritableFile* file = nullptr;
    ASSERT_LEVELDB_OK(env_.target()->NewWritableFile(sst_name, &file));
    ASSERT_LEVELDB_OK(file->Append(table_bytes));
    ASSERT_LEVELDB_OK(file->Close());
    delete file;
  }

  class FlakyEnv : public EnvWrapper {
   public:
    explicit FlakyEnv(Env* base) : EnvWrapper(base) {}
    Status NewRandomAccessFile(const std::string& fname,
                               RandomAccessFile** result) override {
      if (fail_next_table_read_ && fname.size() > 4 &&
          fname.compare(fname.size() - 4, 4, ".sst") == 0) {
        fail_next_table_read_ = false;
        return Status::IOError("injected first-table-read failure");
      }
      return target()->NewRandomAccessFile(fname, result);
    }
    bool fail_next_table_read_ = true;
  } flaky(env_.target());

  Options repair_options = options_;
  repair_options.env = &flaky;
  ASSERT_LEVELDB_OK(::leveldb::RepairDB(dbname(), repair_options));

  // The rebuilt table must be registered with real boundaries: the DB opens
  // and serves the record.
  Reopen();
  std::string value;
  ASSERT_LEVELDB_OK(db_->Get(ReadOptions(), "key", &value));
  ASSERT_EQ("v1", value);

  // The recovered LastSequence must cover the repaired records: a new write
  // lands above them and wins over the repaired value.
  ASSERT_LEVELDB_OK(db_->Put(WriteOptions(), "key", "v2"));
  Reopen();
  ASSERT_LEVELDB_OK(db_->Get(ReadOptions(), "key", &value));
  ASSERT_EQ("v2", value);
}

TEST_F(CorruptionTest, UnrelatedKeys) {
  Build(10);
  DBImpl* dbi = reinterpret_cast<DBImpl*>(db_);
  dbi->TEST_CompactMemTable();
  Corrupt(kTableFile, 100, 1);

  std::string tmp1, tmp2;
  ASSERT_LEVELDB_OK(
      db_->Put(WriteOptions(), Key(1000, &tmp1), Value(1000, &tmp2)));
  std::string v;
  ASSERT_LEVELDB_OK(db_->Get(ReadOptions(), Key(1000, &tmp1), &v));
  ASSERT_EQ(Value(1000, &tmp2).ToString(), v);
  dbi->TEST_CompactMemTable();
  ASSERT_LEVELDB_OK(db_->Get(ReadOptions(), Key(1000, &tmp1), &v));
  ASSERT_EQ(Value(1000, &tmp2).ToString(), v);
}

}  // namespace leveldb
