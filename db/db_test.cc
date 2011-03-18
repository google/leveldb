// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "include/db.h"

#include "db/db_impl.h"
#include "db/filename.h"
#include "db/version_set.h"
#include "db/write_batch_internal.h"
#include "include/env.h"
#include "include/table.h"
#include "util/logging.h"
#include "util/testharness.h"
#include "util/testutil.h"

namespace leveldb {

static std::string RandomString(Random* rnd, int len) {
  std::string r;
  test::RandomString(rnd, len, &r);
  return r;
}

class DBTest {
 public:
  std::string dbname_;
  Env* env_;
  DB* db_;

  Options last_options_;

  DBTest() : env_(Env::Default()) {
    dbname_ = test::TmpDir() + "/db_test";
    DestroyDB(dbname_, Options());
    db_ = NULL;
    Reopen();
  }

  ~DBTest() {
    delete db_;
    DestroyDB(dbname_, Options());
  }

  DBImpl* dbfull() {
    return reinterpret_cast<DBImpl*>(db_);
  }

  void Reopen(Options* options = NULL) {
    ASSERT_OK(TryReopen(options));
  }

  void DestroyAndReopen(Options* options = NULL) {
    delete db_;
    db_ = NULL;
    DestroyDB(dbname_, Options());
    ASSERT_OK(TryReopen(options));
  }

  Status TryReopen(Options* options) {
    delete db_;
    db_ = NULL;
    Options opts;
    if (options != NULL) {
      opts = *options;
    } else {
      opts.create_if_missing = true;
    }
    last_options_ = opts;

    return DB::Open(opts, dbname_, &db_);
  }

  Status Put(const std::string& k, const std::string& v) {
    WriteBatch batch;
    batch.Put(k, v);
    return db_->Write(WriteOptions(), &batch);
  }

  Status Delete(const std::string& k) {
    WriteBatch batch;
    batch.Delete(k);
    return db_->Write(WriteOptions(), &batch);
  }

  std::string Get(const std::string& k, const Snapshot* snapshot = NULL) {
    ReadOptions options;
    options.snapshot = snapshot;
    std::string result;
    Status s = db_->Get(options, k, &result);
    if (s.IsNotFound()) {
      result = "NOT_FOUND";
    } else if (!s.ok()) {
      result = s.ToString();
    }
    return result;
  }

  std::string AllEntriesFor(const Slice& user_key) {
    Iterator* iter = dbfull()->TEST_NewInternalIterator();
    InternalKey target(user_key, kMaxSequenceNumber, kTypeValue);
    iter->Seek(target.Encode());
    std::string result;
    if (!iter->status().ok()) {
      result = iter->status().ToString();
    } else {
      result = "[ ";
      bool first = true;
      while (iter->Valid()) {
        ParsedInternalKey ikey;
        if (!ParseInternalKey(iter->key(), &ikey)) {
          result += "CORRUPTED";
        } else {
          if (last_options_.comparator->Compare(
                  ikey.user_key, user_key) != 0) {
            break;
          }
          if (!first) {
            result += ", ";
          }
          first = false;
          switch (ikey.type) {
            case kTypeValue:
              result += iter->value().ToString();
              break;
            case kTypeLargeValueRef:
              result += "LARGEVALUE(" + EscapeString(iter->value()) + ")";
              break;
            case kTypeDeletion:
              result += "DEL";
              break;
          }
        }
        iter->Next();
      }
      if (!first) {
        result += " ";
      }
      result += "]";
    }
    delete iter;
    return result;
  }

  int NumTableFilesAtLevel(int level) {
    uint64_t val;
    ASSERT_TRUE(
        db_->GetProperty("leveldb.num-files-at-level" + NumberToString(level),
                         &val));
    return val;
  }

  uint64_t Size(const Slice& start, const Slice& limit) {
    Range r(start, limit);
    uint64_t size;
    db_->GetApproximateSizes(&r, 1, &size);
    return size;
  }

  std::set<LargeValueRef> LargeValueFiles() const {
    // Return the set of large value files that exist in the database
    std::vector<std::string> filenames;
    env_->GetChildren(dbname_, &filenames); // Ignoring errors on purpose
    uint64_t number;
    LargeValueRef large_ref;
    FileType type;
    std::set<LargeValueRef> live;
    for (int i = 0; i < filenames.size(); i++) {
      if (ParseFileName(filenames[i], &number, &large_ref, &type) &&
          type == kLargeValueFile) {
        fprintf(stderr, "  live: %s\n",
                LargeValueRefToFilenameString(large_ref).c_str());
        live.insert(large_ref);
      }
    }
    fprintf(stderr, "Found %d live large value files\n", (int)live.size());
    return live;
  }
};

TEST(DBTest, Empty) {
  ASSERT_TRUE(db_ != NULL);
  ASSERT_EQ("NOT_FOUND", Get("foo"));
}

TEST(DBTest, ReadWrite) {
  ASSERT_OK(Put("foo", "v1"));
  ASSERT_EQ("v1", Get("foo"));
  ASSERT_OK(Put("bar", "v2"));
  ASSERT_OK(Put("foo", "v3"));
  ASSERT_EQ("v3", Get("foo"));
  ASSERT_EQ("v2", Get("bar"));
}

TEST(DBTest, PutDeleteGet) {
  ASSERT_OK(db_->Put(WriteOptions(), "foo", "v1"));
  ASSERT_EQ("v1", Get("foo"));
  ASSERT_OK(db_->Put(WriteOptions(), "foo", "v2"));
  ASSERT_EQ("v2", Get("foo"));
  ASSERT_OK(db_->Delete(WriteOptions(), "foo"));
  ASSERT_EQ("NOT_FOUND", Get("foo"));
}

TEST(DBTest, Recover) {
  ASSERT_OK(Put("foo", "v1"));
  ASSERT_OK(Put("baz", "v5"));

  Reopen();
  ASSERT_EQ("v1", Get("foo"));

  ASSERT_EQ("v1", Get("foo"));
  ASSERT_EQ("v5", Get("baz"));
  ASSERT_OK(Put("bar", "v2"));
  ASSERT_OK(Put("foo", "v3"));

  Reopen();
  ASSERT_EQ("v3", Get("foo"));
  ASSERT_OK(Put("foo", "v4"));
  ASSERT_EQ("v4", Get("foo"));
  ASSERT_EQ("v2", Get("bar"));
  ASSERT_EQ("v5", Get("baz"));
}

TEST(DBTest, RecoveryWithEmptyLog) {
  ASSERT_OK(Put("foo", "v1"));
  ASSERT_OK(Put("foo", "v2"));
  Reopen();
  Reopen();
  ASSERT_OK(Put("foo", "v3"));
  Reopen();
  ASSERT_EQ("v3", Get("foo"));
}

static std::string Key(int i) {
  char buf[100];
  snprintf(buf, sizeof(buf), "key%06d", i);
  return std::string(buf);
}

TEST(DBTest, MinorCompactionsHappen) {
  Options options;
  options.write_buffer_size = 10000;
  Reopen(&options);

  const int N = 100;

  int starting_num_tables = NumTableFilesAtLevel(0);
  for (int i = 0; i < N; i++) {
    ASSERT_OK(Put(Key(i), Key(i) + std::string(1000, 'v')));
  }
  int ending_num_tables = NumTableFilesAtLevel(0);
  ASSERT_GT(ending_num_tables, starting_num_tables);

  for (int i = 0; i < N; i++) {
    ASSERT_EQ(Key(i) + std::string(1000, 'v'), Get(Key(i)));
  }

  Reopen();

  for (int i = 0; i < N; i++) {
    ASSERT_EQ(Key(i) + std::string(1000, 'v'), Get(Key(i)));
  }
}

TEST(DBTest, RecoverWithLargeLog) {
  {
    Options options;
    options.large_value_threshold = 1048576;
    Reopen(&options);
    ASSERT_OK(Put("big1", std::string(200000, '1')));
    ASSERT_OK(Put("big2", std::string(200000, '2')));
    ASSERT_OK(Put("small3", std::string(10, '3')));
    ASSERT_OK(Put("small4", std::string(10, '4')));
    ASSERT_EQ(NumTableFilesAtLevel(0), 0);
  }

  // Make sure that if we re-open with a small write buffer size that
  // we flush table files in the middle of a large log file.
  Options options;
  options.write_buffer_size = 100000;
  options.large_value_threshold = 1048576;
  Reopen(&options);
  ASSERT_EQ(NumTableFilesAtLevel(0), 3);
  ASSERT_EQ(std::string(200000, '1'), Get("big1"));
  ASSERT_EQ(std::string(200000, '2'), Get("big2"));
  ASSERT_EQ(std::string(10, '3'), Get("small3"));
  ASSERT_EQ(std::string(10, '4'), Get("small4"));
  ASSERT_GT(NumTableFilesAtLevel(0), 1);
}

TEST(DBTest, CompactionsGenerateMultipleFiles) {
  Options options;
  options.write_buffer_size = 100000000;        // Large write buffer
  options.large_value_threshold = 1048576;
  Reopen(&options);

  Random rnd(301);

  // Write 8MB (80 values, each 100K)
  ASSERT_EQ(NumTableFilesAtLevel(0), 0);
  std::vector<std::string> values;
  for (int i = 0; i < 80; i++) {
    values.push_back(RandomString(&rnd, 100000));
    ASSERT_OK(Put(Key(i), values[i]));
  }

  // Reopening moves updates to level-0
  Reopen(&options);
  dbfull()->TEST_CompactRange(0, "", Key(100000));

  ASSERT_EQ(NumTableFilesAtLevel(0), 0);
  ASSERT_GT(NumTableFilesAtLevel(1), 1);
  for (int i = 0; i < 80; i++) {
    ASSERT_EQ(Get(Key(i)), values[i]);
  }
}

static bool Between(uint64_t val, uint64_t low, uint64_t high) {
  bool result = (val >= low) && (val <= high);
  if (!result) {
    fprintf(stderr, "Value %llu is not in range [%llu, %llu]\n",
            (unsigned long long)(val),
            (unsigned long long)(low),
            (unsigned long long)(high));
  }
  return result;
}

TEST(DBTest, ApproximateSizes) {
  for (int test = 0; test < 2; test++) {
    // test==0: default large_value_threshold
    // test==1: 1 MB large_value_threshold
    Options options;
    options.large_value_threshold = (test == 0) ? 65536 : 1048576;
    options.write_buffer_size = 100000000;        // Large write buffer
    options.compression = kNoCompression;
    DestroyAndReopen();

    ASSERT_TRUE(Between(Size("", "xyz"), 0, 0));
    Reopen(&options);
    ASSERT_TRUE(Between(Size("", "xyz"), 0, 0));

    // Write 8MB (80 values, each 100K)
    ASSERT_EQ(NumTableFilesAtLevel(0), 0);
    const int N = 80;
    Random rnd(301);
    for (int i = 0; i < N; i++) {
      ASSERT_OK(Put(Key(i), RandomString(&rnd, 100000)));
    }
    if (test == 1) {
      // 0 because GetApproximateSizes() does not account for memtable space for
      // non-large values
      ASSERT_TRUE(Between(Size("", Key(50)), 0, 0));
    } else {
      ASSERT_TRUE(Between(Size("", Key(50)), 100000*50, 100000*50 + 10000));
      ASSERT_TRUE(Between(Size(Key(20), Key(30)),
                          100000*10, 100000*10 + 10000));
    }

    // Check sizes across recovery by reopening a few times
    for (int run = 0; run < 3; run++) {
      Reopen(&options);

      for (int compact_start = 0; compact_start < N; compact_start += 10) {
        for (int i = 0; i < N; i += 10) {
          ASSERT_TRUE(Between(Size("", Key(i)), 100000*i, 100000*i + 10000));
          ASSERT_TRUE(Between(Size("", Key(i)+".suffix"),
                              100000 * (i+1), 100000 * (i+1) + 10000));
          ASSERT_TRUE(Between(Size(Key(i), Key(i+10)),
                              100000 * 10, 100000 * 10 + 10000));
        }
        ASSERT_TRUE(Between(Size("", Key(50)), 5000000, 5010000));
        ASSERT_TRUE(Between(Size("", Key(50)+".suffix"), 5100000, 5110000));

        dbfull()->TEST_CompactRange(0,
                                    Key(compact_start),
                                    Key(compact_start + 9));
      }

      ASSERT_EQ(NumTableFilesAtLevel(0), 0);
      ASSERT_GT(NumTableFilesAtLevel(1), 0);
    }
  }
}

TEST(DBTest, ApproximateSizes_MixOfSmallAndLarge) {
  Options options;
  options.large_value_threshold = 65536;
  options.compression = kNoCompression;
  Reopen();

  Random rnd(301);
  std::string big1 = RandomString(&rnd, 100000);
  ASSERT_OK(Put(Key(0), RandomString(&rnd, 10000)));
  ASSERT_OK(Put(Key(1), RandomString(&rnd, 10000)));
  ASSERT_OK(Put(Key(2), big1));
  ASSERT_OK(Put(Key(3), RandomString(&rnd, 10000)));
  ASSERT_OK(Put(Key(4), big1));
  ASSERT_OK(Put(Key(5), RandomString(&rnd, 10000)));
  ASSERT_OK(Put(Key(6), RandomString(&rnd, 300000)));
  ASSERT_OK(Put(Key(7), RandomString(&rnd, 10000)));

  // Check sizes across recovery by reopening a few times
  for (int run = 0; run < 3; run++) {
    Reopen(&options);

    ASSERT_TRUE(Between(Size("", Key(0)), 0, 0));
    ASSERT_TRUE(Between(Size("", Key(1)), 10000, 11000));
    ASSERT_TRUE(Between(Size("", Key(2)), 20000, 21000));
    ASSERT_TRUE(Between(Size("", Key(3)), 120000, 121000));
    ASSERT_TRUE(Between(Size("", Key(4)), 130000, 131000));
    ASSERT_TRUE(Between(Size("", Key(5)), 230000, 231000));
    ASSERT_TRUE(Between(Size("", Key(6)), 240000, 241000));
    ASSERT_TRUE(Between(Size("", Key(7)), 540000, 541000));
    ASSERT_TRUE(Between(Size("", Key(8)), 550000, 551000));

    ASSERT_TRUE(Between(Size(Key(3), Key(5)), 110000, 111000));

    dbfull()->TEST_CompactRange(0, Key(0), Key(100));
  }
}

TEST(DBTest, IteratorPinsRef) {
  Put("foo", "hello");

  // Get iterator that will yield the current contents of the DB.
  Iterator* iter = db_->NewIterator(ReadOptions());

  // Write to force compactions
  Put("foo", "newvalue1");
  for (int i = 0; i < 100; i++) {
    ASSERT_OK(Put(Key(i), Key(i) + std::string(100000, 'v'))); // 100K values
  }
  Put("foo", "newvalue2");

  iter->SeekToFirst();
  ASSERT_TRUE(iter->Valid());
  ASSERT_EQ("foo", iter->key().ToString());
  ASSERT_EQ("hello", iter->value().ToString());
  iter->Next();
  ASSERT_TRUE(!iter->Valid());
  delete iter;
}

TEST(DBTest, Snapshot) {
  Put("foo", "v1");
  const Snapshot* s1 = db_->GetSnapshot();
  Put("foo", "v2");
  const Snapshot* s2 = db_->GetSnapshot();
  Put("foo", "v3");
  const Snapshot* s3 = db_->GetSnapshot();

  Put("foo", "v4");
  ASSERT_EQ("v1", Get("foo", s1));
  ASSERT_EQ("v2", Get("foo", s2));
  ASSERT_EQ("v3", Get("foo", s3));
  ASSERT_EQ("v4", Get("foo"));

  db_->ReleaseSnapshot(s3);
  ASSERT_EQ("v1", Get("foo", s1));
  ASSERT_EQ("v2", Get("foo", s2));
  ASSERT_EQ("v4", Get("foo"));

  db_->ReleaseSnapshot(s1);
  ASSERT_EQ("v2", Get("foo", s2));
  ASSERT_EQ("v4", Get("foo"));

  db_->ReleaseSnapshot(s2);
  ASSERT_EQ("v4", Get("foo"));
}

TEST(DBTest, HiddenValuesAreRemoved) {
  Random rnd(301);
  std::string big = RandomString(&rnd, 50000);
  Put("foo", big);
  Put("pastfoo", "v");
  const Snapshot* snapshot = db_->GetSnapshot();
  Put("foo", "tiny");
  Put("pastfoo2", "v2");        // Advance sequence number one more

  ASSERT_OK(dbfull()->TEST_CompactMemTable());
  ASSERT_GT(NumTableFilesAtLevel(0), 0);

  ASSERT_EQ(big, Get("foo", snapshot));
  ASSERT_TRUE(Between(Size("", "pastfoo"), 50000, 60000));
  db_->ReleaseSnapshot(snapshot);
  ASSERT_EQ(AllEntriesFor("foo"), "[ tiny, " + big + " ]");
  dbfull()->TEST_CompactRange(0, "", "x");
  ASSERT_EQ(AllEntriesFor("foo"), "[ tiny ]");
  ASSERT_EQ(NumTableFilesAtLevel(0), 0);
  ASSERT_GE(NumTableFilesAtLevel(1), 1);
  dbfull()->TEST_CompactRange(1, "", "x");
  ASSERT_EQ(AllEntriesFor("foo"), "[ tiny ]");

  ASSERT_TRUE(Between(Size("", "pastfoo"), 0, 1000));
}

TEST(DBTest, DeletionMarkers1) {
  Put("foo", "v1");
  ASSERT_OK(dbfull()->TEST_CompactMemTable());
  dbfull()->TEST_CompactRange(0, "", "z");
  dbfull()->TEST_CompactRange(1, "", "z");
  ASSERT_EQ(NumTableFilesAtLevel(2), 1);   // foo => v1 is now in level 2 file
  Delete("foo");
  Put("foo", "v2");
  ASSERT_EQ(AllEntriesFor("foo"), "[ v2, DEL, v1 ]");
  ASSERT_OK(dbfull()->TEST_CompactMemTable());
  ASSERT_EQ(AllEntriesFor("foo"), "[ v2, DEL, v1 ]");
  dbfull()->TEST_CompactRange(0, "", "z");
  // DEL eliminated, but v1 remains because we aren't compacting that level
  // (DEL can be eliminated because v2 hides v1).
  ASSERT_EQ(AllEntriesFor("foo"), "[ v2, v1 ]");
  dbfull()->TEST_CompactRange(1, "", "z");
  // Merging L1 w/ L2, so we are the base level for "foo", so DEL is removed.
  // (as is v1).
  ASSERT_EQ(AllEntriesFor("foo"), "[ v2 ]");
}

TEST(DBTest, DeletionMarkers2) {
  Put("foo", "v1");
  ASSERT_OK(dbfull()->TEST_CompactMemTable());
  dbfull()->TEST_CompactRange(0, "", "z");
  dbfull()->TEST_CompactRange(1, "", "z");
  ASSERT_EQ(NumTableFilesAtLevel(2), 1);   // foo => v1 is now in level 2 file
  Delete("foo");
  ASSERT_EQ(AllEntriesFor("foo"), "[ DEL, v1 ]");
  ASSERT_OK(dbfull()->TEST_CompactMemTable());
  ASSERT_EQ(AllEntriesFor("foo"), "[ DEL, v1 ]");
  dbfull()->TEST_CompactRange(0, "", "z");
  // DEL kept: L2 file overlaps
  ASSERT_EQ(AllEntriesFor("foo"), "[ DEL, v1 ]");
  dbfull()->TEST_CompactRange(1, "", "z");
  // Merging L1 w/ L2, so we are the base level for "foo", so DEL is removed.
  // (as is v1).
  ASSERT_EQ(AllEntriesFor("foo"), "[ ]");
}

TEST(DBTest, ComparatorCheck) {
  class NewComparator : public Comparator {
   public:
    virtual const char* Name() const { return "leveldb.NewComparator"; }
    virtual int Compare(const Slice& a, const Slice& b) const {
      return BytewiseComparator()->Compare(a, b);
    }
    virtual void FindShortestSeparator(std::string* s, const Slice& l) const {
      BytewiseComparator()->FindShortestSeparator(s, l);
    }
    virtual void FindShortSuccessor(std::string* key) const {
      BytewiseComparator()->FindShortSuccessor(key);
    }
  };
  NewComparator cmp;
  Options new_options;
  new_options.comparator = &cmp;
  Status s = TryReopen(&new_options);
  ASSERT_TRUE(!s.ok());
  ASSERT_TRUE(s.ToString().find("comparator") != std::string::npos)
      << s.ToString();
}

static bool LargeValuesOK(DBTest* db,
                          const std::set<LargeValueRef>& expected) {
  std::set<LargeValueRef> actual = db->LargeValueFiles();
  if (actual.size() != expected.size()) {
    fprintf(stderr, "Sets differ in size: %d vs %d\n",
            (int)actual.size(), (int)expected.size());
    return false;
  }
  for (std::set<LargeValueRef>::const_iterator it = expected.begin();
       it != expected.end();
       ++it) {
    if (actual.count(*it) != 1) {
      fprintf(stderr, "  key '%s' not found in actual set\n",
              LargeValueRefToFilenameString(*it).c_str());
      return false;
    }
  }
  return true;
}

TEST(DBTest, LargeValues1) {
  Options options;
  options.large_value_threshold = 10000;
  Reopen(&options);

  Random rnd(301);

  std::string big1;
  test::CompressibleString(&rnd, 1.0, 100000, &big1); // Not compressible
  std::set<LargeValueRef> expected;

  ASSERT_OK(Put("big1", big1));
  expected.insert(LargeValueRef::Make(big1, kNoCompression));
  ASSERT_TRUE(LargeValuesOK(this, expected));

  ASSERT_OK(Delete("big1"));
  ASSERT_TRUE(LargeValuesOK(this, expected));
  ASSERT_OK(dbfull()->TEST_CompactMemTable());
  // No handling of deletion markers on memtable compactions, so big1 remains
  ASSERT_TRUE(LargeValuesOK(this, expected));

  dbfull()->TEST_CompactRange(0, "", "z");
  expected.erase(LargeValueRef::Make(big1, kNoCompression));
  ASSERT_TRUE(LargeValuesOK(this, expected));
}

TEST(DBTest, LargeValues2) {
  Options options;
  options.large_value_threshold = 10000;
  Reopen(&options);

  Random rnd(301);

  std::string big1, big2;
  test::CompressibleString(&rnd, 1.0, 20000, &big1);  // Not compressible
  test::CompressibleString(&rnd, 0.6, 40000, &big2);  // Compressible
  std::set<LargeValueRef> expected;
  ASSERT_TRUE(LargeValuesOK(this, expected));

  ASSERT_OK(Put("big1", big1));
  expected.insert(LargeValueRef::Make(big1, kNoCompression));
  ASSERT_EQ(big1, Get("big1"));
  ASSERT_TRUE(LargeValuesOK(this, expected));

  ASSERT_OK(Put("big2", big2));
  ASSERT_EQ(big2, Get("big2"));
#if defined(LEVELDB_PLATFORM_POSIX) || defined(LEVELDB_PLATFORM_CHROMIUM)
  // TODO(sanjay) Reenable after compression support is added
  expected.insert(LargeValueRef::Make(big2, kNoCompression));
#else
  expected.insert(LargeValueRef::Make(big2, kLightweightCompression));
#endif
  ASSERT_TRUE(LargeValuesOK(this, expected));

  ASSERT_OK(dbfull()->TEST_CompactMemTable());
  ASSERT_TRUE(LargeValuesOK(this, expected));

  dbfull()->TEST_CompactRange(0, "", "z");
  ASSERT_TRUE(LargeValuesOK(this, expected));

  ASSERT_OK(Put("big2", big2));
  ASSERT_OK(Put("big2_b", big2));
  ASSERT_EQ(big1, Get("big1"));
  ASSERT_EQ(big2, Get("big2"));
  ASSERT_EQ(big2, Get("big2_b"));
  ASSERT_TRUE(LargeValuesOK(this, expected));

  ASSERT_OK(Delete("big1"));
  ASSERT_EQ("NOT_FOUND", Get("big1"));
  ASSERT_TRUE(LargeValuesOK(this, expected));

  ASSERT_OK(dbfull()->TEST_CompactMemTable());
  ASSERT_TRUE(LargeValuesOK(this, expected));
  dbfull()->TEST_CompactRange(0, "", "z");
  expected.erase(LargeValueRef::Make(big1, kNoCompression));
  ASSERT_TRUE(LargeValuesOK(this, expected));
  dbfull()->TEST_CompactRange(1, "", "z");

  ASSERT_OK(Delete("big2"));
  ASSERT_EQ("NOT_FOUND", Get("big2"));
  ASSERT_EQ(big2, Get("big2_b"));
  ASSERT_OK(dbfull()->TEST_CompactMemTable());
  ASSERT_TRUE(LargeValuesOK(this, expected));
  dbfull()->TEST_CompactRange(0, "", "z");
  ASSERT_TRUE(LargeValuesOK(this, expected));

  // Make sure the large value refs survive a reload and compactions after
  // the reload.
  Reopen();
  ASSERT_TRUE(LargeValuesOK(this, expected));
  ASSERT_OK(Put("foo", "bar"));
  ASSERT_OK(dbfull()->TEST_CompactMemTable());
  dbfull()->TEST_CompactRange(0, "", "z");
  ASSERT_TRUE(LargeValuesOK(this, expected));
}

TEST(DBTest, LargeValues3) {
  // Make sure we don't compress values if
  Options options;
  options.large_value_threshold = 10000;
  options.compression = kNoCompression;
  Reopen(&options);

  Random rnd(301);

  std::string big1 = std::string(100000, 'x');       // Very compressible
  std::set<LargeValueRef> expected;

  ASSERT_OK(Put("big1", big1));
  ASSERT_EQ(big1, Get("big1"));
  expected.insert(LargeValueRef::Make(big1, kNoCompression));
  ASSERT_TRUE(LargeValuesOK(this, expected));
}


TEST(DBTest, DBOpen_Options) {
  std::string dbname = test::TmpDir() + "/db_options_test";
  DestroyDB(dbname, Options());

  // Does not exist, and create_if_missing == false: error
  DB* db = NULL;
  Options opts;
  opts.create_if_missing = false;
  Status s = DB::Open(opts, dbname, &db);
  ASSERT_TRUE(strstr(s.ToString().c_str(), "does not exist") != NULL);
  ASSERT_TRUE(db == NULL);

  // Does not exist, and create_if_missing == true: OK
  opts.create_if_missing = true;
  s = DB::Open(opts, dbname, &db);
  ASSERT_OK(s);
  ASSERT_TRUE(db != NULL);

  delete db;
  db = NULL;

  // Does exist, and error_if_exists == true: error
  opts.create_if_missing = false;
  opts.error_if_exists = true;
  s = DB::Open(opts, dbname, &db);
  ASSERT_TRUE(strstr(s.ToString().c_str(), "exists") != NULL);
  ASSERT_TRUE(db == NULL);

  // Does exist, and error_if_exists == false: OK
  opts.create_if_missing = true;
  opts.error_if_exists = false;
  s = DB::Open(opts, dbname, &db);
  ASSERT_OK(s);
  ASSERT_TRUE(db != NULL);

  delete db;
  db = NULL;
}

class ModelDB: public DB {
 public:
  explicit ModelDB(const Options& options): options_(options) { }
  ~ModelDB() { }
  virtual Status Put(const WriteOptions& o, const Slice& k, const Slice& v) {
    return DB::Put(o, k, v);
  }
  virtual Status Delete(const WriteOptions& o, const Slice& key) {
    return DB::Delete(o, key);
  }
  virtual Status Get(const ReadOptions& options,
                     const Slice& key, std::string* value) {
    assert(false);      // Not implemented
    return Status::NotFound(key);
  }
  virtual Iterator* NewIterator(const ReadOptions& options) {
    if (options.snapshot == NULL) {
      KVMap* saved = new KVMap;
      *saved = map_;
      return new ModelIter(saved, true);
    } else {
      const KVMap* snapshot_state =
          reinterpret_cast<const KVMap*>(options.snapshot->number_);
      return new ModelIter(snapshot_state, false);
    }
  }
  virtual const Snapshot* GetSnapshot() {
    KVMap* saved = new KVMap;
    *saved = map_;
    return snapshots_.New(
        reinterpret_cast<SequenceNumber>(saved));
  }

  virtual void ReleaseSnapshot(const Snapshot* snapshot) {
    const KVMap* saved = reinterpret_cast<const KVMap*>(snapshot->number_);
    delete saved;
    snapshots_.Delete(snapshot);
  }
  virtual Status Write(const WriteOptions& options, WriteBatch* batch) {
    assert(options.post_write_snapshot == NULL);   // Not supported
    for (WriteBatchInternal::Iterator it(*batch); !it.Done(); it.Next()) {
      switch (it.op()) {
        case kTypeValue:
          map_[it.key().ToString()] = it.value().ToString();
          break;
        case kTypeLargeValueRef:
          assert(false);        // Should not occur
          break;
        case kTypeDeletion:
          map_.erase(it.key().ToString());
          break;
      }
    }
    return Status::OK();
  }

  virtual bool GetProperty(const Slice& property, uint64_t* value) {
    return false;
  }
  virtual void GetApproximateSizes(const Range* r, int n, uint64_t* sizes) {
    for (int i = 0; i < n; i++) {
      sizes[i] = 0;
    }
  }
 private:
  typedef std::map<std::string, std::string> KVMap;
  class ModelIter: public Iterator {
   public:
    ModelIter(const KVMap* map, bool owned)
        : map_(map), owned_(owned), iter_(map_->end()) {
    }
    ~ModelIter() {
      if (owned_) delete map_;
    }
    virtual bool Valid() const { return iter_ != map_->end(); }
    virtual void SeekToFirst() { iter_ = map_->begin(); }
    virtual void SeekToLast() {
      if (map_->empty()) {
        iter_ = map_->end();
      } else {
        iter_ = map_->find(map_->rbegin()->first);
      }
    }
    virtual void Seek(const Slice& k) {
      iter_ = map_->lower_bound(k.ToString());
    }
    virtual void Next() { ++iter_; }
    virtual void Prev() { --iter_; }
    virtual Slice key() const { return iter_->first; }
    virtual Slice value() const { return iter_->second; }
    virtual Status status() const { return Status::OK(); }
   private:
    const KVMap* const map_;
    const bool owned_;  // Do we own map_
    KVMap::const_iterator iter_;
  };
  const Options options_;
  KVMap map_;
  SnapshotList snapshots_;
};

static std::string RandomKey(Random* rnd) {
  int len = (rnd->OneIn(3)
             ? 1                // Short sometimes to encourage collisions
             : (rnd->OneIn(100) ? rnd->Skewed(10) : rnd->Uniform(10)));
  return test::RandomKey(rnd, len);
}

static bool CompareIterators(int step,
                             DB* model,
                             DB* db,
                             const Snapshot* model_snap,
                             const Snapshot* db_snap) {
  ReadOptions options;
  options.snapshot = model_snap;
  Iterator* miter = model->NewIterator(options);
  options.snapshot = db_snap;
  Iterator* dbiter = db->NewIterator(options);
  bool ok = true;
  int count = 0;
  for (miter->SeekToFirst(), dbiter->SeekToFirst();
       ok && miter->Valid() && dbiter->Valid();
       miter->Next(), dbiter->Next()) {
    count++;
    if (miter->key().compare(dbiter->key()) != 0) {
      fprintf(stderr, "step %d: Key mismatch: '%s' vs. '%s'\n",
              step,
              EscapeString(miter->key()).c_str(),
              EscapeString(dbiter->key()).c_str());
      ok = false;
      break;
    }

    if (miter->value().compare(dbiter->value()) != 0) {
      fprintf(stderr, "step %d: Value mismatch for key '%s': '%s' vs. '%s'\n",
              step,
              EscapeString(miter->key()).c_str(),
              EscapeString(miter->value()).c_str(),
              EscapeString(miter->value()).c_str());
      ok = false;
    }
  }

  if (ok) {
    if (miter->Valid() != dbiter->Valid()) {
      fprintf(stderr, "step %d: Mismatch at end of iterators: %d vs. %d\n",
              step, miter->Valid(), dbiter->Valid());
      ok = false;
    }
  }
  fprintf(stderr, "%d entries compared: ok=%d\n", count, ok);
  delete miter;
  delete dbiter;
  return ok;
}

TEST(DBTest, Randomized) {
  Random rnd(test::RandomSeed());
  ModelDB model(last_options_);
  const int N = 10000;
  const Snapshot* model_snap = NULL;
  const Snapshot* db_snap = NULL;
  std::string k, v;
  for (int step = 0; step < N; step++) {
    if (step % 100 == 0) {
      fprintf(stderr, "Step %d of %d\n", step, N);
    }
    int p = rnd.Uniform(100);
    if (p < 45) {                               // Put
      k = RandomKey(&rnd);
      v = RandomString(&rnd,
                       rnd.OneIn(20)
                       ? 100 + rnd.Uniform(100)
                       : rnd.Uniform(8));
      ASSERT_OK(model.Put(WriteOptions(), k, v));
      ASSERT_OK(db_->Put(WriteOptions(), k, v));

    } else if (p < 90) {                        // Delete
      k = RandomKey(&rnd);
      ASSERT_OK(model.Delete(WriteOptions(), k));
      ASSERT_OK(db_->Delete(WriteOptions(), k));


    } else {                                    // Multi-element batch
      WriteBatch b;
      const int num = rnd.Uniform(8);
      for (int i = 0; i < num; i++) {
        if (i == 0 || !rnd.OneIn(10)) {
          k = RandomKey(&rnd);
        } else {
          // Periodically re-use the same key from the previous iter, so
          // we have multiple entries in the write batch for the same key
        }
        if (rnd.OneIn(2)) {
          v = RandomString(&rnd, rnd.Uniform(10));
          b.Put(k, v);
        } else {
          b.Delete(k);
        }
      }
      ASSERT_OK(model.Write(WriteOptions(), &b));
      ASSERT_OK(db_->Write(WriteOptions(), &b));
    }

    if ((step % 100) == 0) {
      ASSERT_TRUE(CompareIterators(step, &model, db_, NULL, NULL));
      ASSERT_TRUE(CompareIterators(step, &model, db_, model_snap, db_snap));
      // Save a snapshot from each DB this time that we'll use next
      // time we compare things, to make sure the current state is
      // preserved with the snapshot
      if (model_snap != NULL) model.ReleaseSnapshot(model_snap);
      if (db_snap != NULL) db_->ReleaseSnapshot(db_snap);

      Reopen();
      ASSERT_TRUE(CompareIterators(step, &model, db_, NULL, NULL));

      model_snap = model.GetSnapshot();
      db_snap = db_->GetSnapshot();
    }
  }
  if (model_snap != NULL) model.ReleaseSnapshot(model_snap);
  if (db_snap != NULL) db_->ReleaseSnapshot(db_snap);
}

}

int main(int argc, char** argv) {
  return leveldb::test::RunAllTests();
}
