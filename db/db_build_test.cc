// Copyright (c) 2020 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "gtest/gtest.h"
#include "db/dbformat.h"
#include "leveldb/db.h"
#include "leveldb/env.h"
#include "leveldb/sst_file_writer.h"
#include "util/testutil.h"

namespace leveldb {

class BuildDBFromSstFilesTest: public testing::Test {
 public:
  std::string dbname_;
  std::string sst_files_dir_;
  Env* env_;
  Options options_;

  BuildDBFromSstFilesTest() : env_(Env::Default()) {
    dbname_ = testing::TempDir() + "sst_file_test";
    sst_files_dir_ = testing::TempDir() + "sst_files/";
    DestroyDB(dbname_, Options());
    env_->CreateDir(sst_files_dir_);
    options_ = Options();
    options_.create_if_missing = true;
    options_.env = env_;
  }
};

static std::string Get(DB* db, const std::string& key) {
  std::string result;
  db->Get(ReadOptions(), key, &result);
  return result;
}

static std::string Key(int i) {
  char buf[100];
  snprintf(buf, sizeof(buf), "key%06d", i);
  return std::string(buf);
}

static std::string InternalValueKey(int i) {
  return InternalKey(Key(i), 0, ValueType::kTypeValue).Encode().ToString();
}

static std::string InternalDeletionKey(int i) {
  return InternalKey(Key(i), 0, ValueType::kTypeDeletion).Encode().ToString();
}

TEST_F(BuildDBFromSstFilesTest, Basic) {
  SstFileWriter sst_file_writer(options_);

  // Current file size should be 0 after sst_file_writer init and before open a
  // file.
  EXPECT_EQ(sst_file_writer.FileSize(), 0);

  // file1.sst(0 => 99)
  std::string file1 = sst_files_dir_ + "file1.sst";
  EXPECT_LEVELDB_OK(sst_file_writer.Open(file1));
  for (int k = 0; k < 100; ++k) {
    EXPECT_LEVELDB_OK(sst_file_writer.Put(Key(k), Key(k) + "_val"));
  }
  ExternalSstFileInfo file1_info;
  Status s = sst_file_writer.Finish(&file1_info);
  EXPECT_TRUE(s.ok());
  // Current file size should be non-zero after success write.
  EXPECT_GT(sst_file_writer.FileSize(), 0);
  EXPECT_EQ(file1_info.file_path, file1);
  EXPECT_EQ(file1_info.num_entries, 100);
  EXPECT_EQ(file1_info.smallest_internal_key, InternalValueKey(0));
  EXPECT_EQ(file1_info.largest_internal_key, InternalValueKey(99));
  // sst_file_writer already finished, cannot add this value
  s = sst_file_writer.Put(Key(100), "bad_val");
  EXPECT_TRUE(!s.ok());

  // file2.sst(100 => 199)
  std::string file2 = sst_files_dir_ + "file2.sst";
  EXPECT_LEVELDB_OK(sst_file_writer.Open(file2));
  for (int k = 100; k < 200; ++k) {
    EXPECT_LEVELDB_OK(sst_file_writer.Put(Key(k), Key(k) + "_val"));
  }
  // Cannot add this key because it's not after last added key
  s = sst_file_writer.Put(Key(99), "bad_val");
  EXPECT_TRUE(!s.ok());
  ExternalSstFileInfo file2_info;
  s = sst_file_writer.Finish(&file2_info);
  EXPECT_TRUE(s.ok());
  // Current file size should be non-zero after success write.
  EXPECT_GT(sst_file_writer.FileSize(), 0);
  EXPECT_EQ(file2_info.file_path, file2);
  EXPECT_EQ(file2_info.num_entries, 100);
  EXPECT_EQ(file2_info.smallest_internal_key, InternalValueKey(100));
  EXPECT_EQ(file2_info.largest_internal_key, InternalValueKey(199));

  // file3.sst (195 => 299)
  // This file values overlap with file2 values
  std::string file3 = sst_files_dir_ + "file3.sst";
  EXPECT_LEVELDB_OK(sst_file_writer.Open(file3));
  for (int k = 195; k < 300; k++) {
    EXPECT_LEVELDB_OK(sst_file_writer.Put(Key(k), Key(k) + "_val_overlap"));
  }
  ExternalSstFileInfo file3_info;
  s = sst_file_writer.Finish(&file3_info);
  EXPECT_TRUE(s.ok());
  // Current file size should be non-zero after success finish.
  EXPECT_GT(sst_file_writer.FileSize(), 0);
  EXPECT_EQ(file3_info.file_path, file3);
  EXPECT_EQ(file3_info.num_entries, 105);
  EXPECT_EQ(file3_info.smallest_internal_key, InternalValueKey(195));
  EXPECT_EQ(file3_info.largest_internal_key, InternalValueKey(299));

  // file4.sst(400 => 499) deletion
  std::string file4 = sst_files_dir_ + "file4.sst";
  EXPECT_LEVELDB_OK(sst_file_writer.Open(file4));
  for (int k = 400; k < 500; ++k) {
    EXPECT_LEVELDB_OK(sst_file_writer.Delete(Key(k)));
  }
  ExternalSstFileInfo file4_info;
  s = sst_file_writer.Finish(&file4_info);
  EXPECT_TRUE(s.ok());
  // Current file size should be non-zero after success write.
  EXPECT_GT(sst_file_writer.FileSize(), 0);
  EXPECT_EQ(file4_info.file_path, file4);
  EXPECT_EQ(file4_info.num_entries, 100);
  EXPECT_EQ(file4_info.smallest_internal_key, InternalDeletionKey(400));
  EXPECT_EQ(file4_info.largest_internal_key, InternalDeletionKey(499));

  // Cannot create an empty sst file
  std::string file_empty = sst_files_dir_ + "file_empty.sst";
  ExternalSstFileInfo file_empty_info;
  s = sst_file_writer.Finish(&file_empty_info);
  EXPECT_TRUE(!s.ok());

  // failed because of overlapping ranges
  s = BuildDBFromSstFiles(options_, dbname_,
                          {file1_info, file2_info, file3_info, file4_info});
  EXPECT_TRUE(!s.ok());

  EXPECT_TRUE(env_->FileExists(file1));
  EXPECT_TRUE(env_->FileExists(file2));
  EXPECT_TRUE(env_->FileExists(file3));
  EXPECT_TRUE(env_->FileExists(file4));
  // Succeeded
  s = BuildDBFromSstFiles(options_, dbname_,
                          {file1_info, file2_info, file4_info});
  EXPECT_LEVELDB_OK(s);
  EXPECT_TRUE(!env_->FileExists(file1));
  EXPECT_TRUE(!env_->FileExists(file2));
  EXPECT_TRUE(!env_->FileExists(file4));

  env_->DeleteFile(file3);

  DB* db;
  s = DB::Open(options_, dbname_, &db);
  EXPECT_LEVELDB_OK(s);
  for (int k = 0; k < 200; ++k) {
    EXPECT_EQ(Get(db, Key(k)), Key(k) + "_val");
  }
  delete db;
}

}  // namespace leveldb

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
