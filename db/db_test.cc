// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "leveldb/db.h"
#include "db/db_impl.h"
#include "db/filename.h"
#include "db/version_set.h"
#include "db/write_batch_internal.h"
#include "leveldb/env.h"
#include "leveldb/table.h"
#include "util/logging.h"
#include "util/mutexlock.h"
#include "util/testharness.h"
#include "util/testutil.h"

namespace leveldb {

static std::string RandomString(Random* rnd, int len) {
  std::string r;
  test::RandomString(rnd, len, &r);
  return r;
}

// Special Env used to delay background operations
class SpecialEnv : public EnvWrapper {
 public:
  // sstable Sync() calls are blocked while this pointer is non-NULL.
  port::AtomicPointer delay_sstable_sync_;

  explicit SpecialEnv(Env* base) : EnvWrapper(base) {
    delay_sstable_sync_.Release_Store(NULL);
  }

  Status NewWritableFile(const std::string& f, WritableFile** r) {
    class SSTableFile : public WritableFile {
     private:
      SpecialEnv* env_;
      WritableFile* base_;

     public:
      SSTableFile(SpecialEnv* env, WritableFile* base)
          : env_(env),
            base_(base) {
      }
      ~SSTableFile() { delete base_; }
      Status Append(const Slice& data) { return base_->Append(data); }
      Status Close() { return base_->Close(); }
      Status Flush() { return base_->Flush(); }
      Status Sync() {
        while (env_->delay_sstable_sync_.Acquire_Load() != NULL) {
          env_->SleepForMicroseconds(100000);
        }
        return base_->Sync();
      }
    };

    Status s = target()->NewWritableFile(f, r);
    if (s.ok()) {
      if (strstr(f.c_str(), ".sst") != NULL) {
        *r = new SSTableFile(this, *r);
      }
    }
    return s;
  }
};

class DBTest {
 public:
  std::string dbname_;
  SpecialEnv* env_;
  DB* db_;

  Options last_options_;

  DBTest() : env_(new SpecialEnv(Env::Default())) {
    dbname_ = test::TmpDir() + "/db_test";
    DestroyDB(dbname_, Options());
    db_ = NULL;
    Reopen();
  }

  ~DBTest() {
    delete db_;
    DestroyDB(dbname_, Options());
    delete env_;
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
    return db_->Put(WriteOptions(), k, v);
  }

  Status Delete(const std::string& k) {
    return db_->Delete(WriteOptions(), k);
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
    std::string property;
    ASSERT_TRUE(
        db_->GetProperty("leveldb.num-files-at-level" + NumberToString(level),
                         &property));
    return atoi(property.c_str());
  }

  int TotalTableFiles() {
    int result = 0;
    for (int level = 0; level < config::kNumLevels; level++) {
      result += NumTableFilesAtLevel(level);
    }
    return result;
  }

  uint64_t Size(const Slice& start, const Slice& limit) {
    Range r(start, limit);
    uint64_t size;
    db_->GetApproximateSizes(&r, 1, &size);
    return size;
  }

  void Compact(const Slice& start, const Slice& limit) {
    dbfull()->TEST_CompactMemTable();
    int max_level_with_files = 1;
    for (int level = 1; level < config::kNumLevels; level++) {
      if (NumTableFilesAtLevel(level) > 0) {
        max_level_with_files = level;
      }
    }
    for (int level = 0; level < max_level_with_files; level++) {
      dbfull()->TEST_CompactRange(level, "", "~");
    }
  }

  // Prevent pushing of new sstables into deeper levels by adding
  // tables that cover a specified range to all levels.
  void FillLevels(const std::string& smallest, const std::string& largest) {
    for (int level = 0; level < config::kNumLevels; level++) {
      Put(smallest, "begin");
      Put(largest, "end");
      dbfull()->TEST_CompactMemTable();
    }
  }

  void DumpFileCounts(const char* label) {
    fprintf(stderr, "---\n%s:\n", label);
    fprintf(stderr, "maxoverlap: %lld\n",
            static_cast<long long>(
                dbfull()->TEST_MaxNextLevelOverlappingBytes()));
    for (int level = 0; level < config::kNumLevels; level++) {
      int num = NumTableFilesAtLevel(level);
      if (num > 0) {
        fprintf(stderr, "  level %3d : %d files\n", level, num);
      }
    }
  }

  std::string IterStatus(Iterator* iter) {
    std::string result;
    if (iter->Valid()) {
      result = iter->key().ToString() + "->" + iter->value().ToString();
    } else {
      result = "(invalid)";
    }
    return result;
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

TEST(DBTest, GetFromImmutableLayer) {
  Options options;
  options.env = env_;
  options.write_buffer_size = 100000;  // Small write buffer
  Reopen(&options);

  ASSERT_OK(Put("foo", "v1"));
  ASSERT_EQ("v1", Get("foo"));

  env_->delay_sstable_sync_.Release_Store(env_);   // Block sync calls
  Put("k1", std::string(100000, 'x'));             // Fill memtable
  Put("k2", std::string(100000, 'y'));             // Trigger compaction
  ASSERT_EQ("v1", Get("foo"));
  env_->delay_sstable_sync_.Release_Store(NULL);   // Release sync calls
}

TEST(DBTest, GetFromVersions) {
  ASSERT_OK(Put("foo", "v1"));
  dbfull()->TEST_CompactMemTable();
  ASSERT_EQ("v1", Get("foo"));
}

TEST(DBTest, GetSnapshot) {
  // Try with both a short key and a long key
  for (int i = 0; i < 2; i++) {
    std::string key = (i == 0) ? std::string("foo") : std::string(200, 'x');
    ASSERT_OK(Put(key, "v1"));
    const Snapshot* s1 = db_->GetSnapshot();
    ASSERT_OK(Put(key, "v2"));
    ASSERT_EQ("v2", Get(key));
    ASSERT_EQ("v1", Get(key, s1));
    dbfull()->TEST_CompactMemTable();
    ASSERT_EQ("v2", Get(key));
    ASSERT_EQ("v1", Get(key, s1));
    db_->ReleaseSnapshot(s1);
  }
}

TEST(DBTest, GetLevel0Ordering) {
  // Check that we process level-0 files in correct order.  The code
  // below generates two level-0 files where the earlier one comes
  // before the later one in the level-0 file list since the earlier
  // one has a smaller "smallest" key.
  ASSERT_OK(Put("bar", "b"));
  ASSERT_OK(Put("foo", "v1"));
  dbfull()->TEST_CompactMemTable();
  ASSERT_OK(Put("foo", "v2"));
  dbfull()->TEST_CompactMemTable();
  ASSERT_EQ("v2", Get("foo"));
}

TEST(DBTest, GetOrderedByLevels) {
  ASSERT_OK(Put("foo", "v1"));
  Compact("a", "z");
  ASSERT_EQ("v1", Get("foo"));
  ASSERT_OK(Put("foo", "v2"));
  ASSERT_EQ("v2", Get("foo"));
  dbfull()->TEST_CompactMemTable();
  ASSERT_EQ("v2", Get("foo"));
}

TEST(DBTest, GetPicksCorrectFile) {
  // Arrange to have multiple files in a non-level-0 level.
  ASSERT_OK(Put("a", "va"));
  Compact("a", "b");
  ASSERT_OK(Put("x", "vx"));
  Compact("x", "y");
  ASSERT_OK(Put("f", "vf"));
  Compact("f", "g");
  ASSERT_EQ("va", Get("a"));
  ASSERT_EQ("vf", Get("f"));
  ASSERT_EQ("vx", Get("x"));
}

TEST(DBTest, GetEncountersEmptyLevel) {
  // Arrange for the following to happen:
  //   * sstable A in level 0
  //   * nothing in level 1
  //   * sstable B in level 2
  // Then do enough Get() calls to arrange for an automatic compaction
  // of sstable A.  A bug would cause the compaction to be marked as
  // occuring at level 1 (instead of the correct level 0).

  // Step 1: First place sstables in levels 0 and 2
  int compaction_count = 0;
  while (NumTableFilesAtLevel(0) == 0 ||
         NumTableFilesAtLevel(2) == 0) {
    ASSERT_LE(compaction_count, 100) << "could not fill levels 0 and 2";
    compaction_count++;
    Put("a", "begin");
    Put("z", "end");
    dbfull()->TEST_CompactMemTable();
  }

  // Step 2: clear level 1 if necessary.
  dbfull()->TEST_CompactRange(1, "a", "z");
  ASSERT_EQ(NumTableFilesAtLevel(0), 1);
  ASSERT_EQ(NumTableFilesAtLevel(1), 0);
  ASSERT_EQ(NumTableFilesAtLevel(2), 1);

  // Step 3: read until level 0 compaction disappears.
  int read_count = 0;
  while (NumTableFilesAtLevel(0) > 0) {
    ASSERT_LE(read_count, 10000) << "did not trigger level 0 compaction";
    read_count++;
    ASSERT_EQ("NOT_FOUND", Get("missing"));
  }
}

TEST(DBTest, IterEmpty) {
  Iterator* iter = db_->NewIterator(ReadOptions());

  iter->SeekToFirst();
  ASSERT_EQ(IterStatus(iter), "(invalid)");

  iter->SeekToLast();
  ASSERT_EQ(IterStatus(iter), "(invalid)");

  iter->Seek("foo");
  ASSERT_EQ(IterStatus(iter), "(invalid)");

  delete iter;
}

TEST(DBTest, IterSingle) {
  ASSERT_OK(Put("a", "va"));
  Iterator* iter = db_->NewIterator(ReadOptions());

  iter->SeekToFirst();
  ASSERT_EQ(IterStatus(iter), "a->va");
  iter->Next();
  ASSERT_EQ(IterStatus(iter), "(invalid)");
  iter->SeekToFirst();
  ASSERT_EQ(IterStatus(iter), "a->va");
  iter->Prev();
  ASSERT_EQ(IterStatus(iter), "(invalid)");

  iter->SeekToLast();
  ASSERT_EQ(IterStatus(iter), "a->va");
  iter->Next();
  ASSERT_EQ(IterStatus(iter), "(invalid)");
  iter->SeekToLast();
  ASSERT_EQ(IterStatus(iter), "a->va");
  iter->Prev();
  ASSERT_EQ(IterStatus(iter), "(invalid)");

  iter->Seek("");
  ASSERT_EQ(IterStatus(iter), "a->va");
  iter->Next();
  ASSERT_EQ(IterStatus(iter), "(invalid)");

  iter->Seek("a");
  ASSERT_EQ(IterStatus(iter), "a->va");
  iter->Next();
  ASSERT_EQ(IterStatus(iter), "(invalid)");

  iter->Seek("b");
  ASSERT_EQ(IterStatus(iter), "(invalid)");

  delete iter;
}

TEST(DBTest, IterMulti) {
  ASSERT_OK(Put("a", "va"));
  ASSERT_OK(Put("b", "vb"));
  ASSERT_OK(Put("c", "vc"));
  Iterator* iter = db_->NewIterator(ReadOptions());

  iter->SeekToFirst();
  ASSERT_EQ(IterStatus(iter), "a->va");
  iter->Next();
  ASSERT_EQ(IterStatus(iter), "b->vb");
  iter->Next();
  ASSERT_EQ(IterStatus(iter), "c->vc");
  iter->Next();
  ASSERT_EQ(IterStatus(iter), "(invalid)");
  iter->SeekToFirst();
  ASSERT_EQ(IterStatus(iter), "a->va");
  iter->Prev();
  ASSERT_EQ(IterStatus(iter), "(invalid)");

  iter->SeekToLast();
  ASSERT_EQ(IterStatus(iter), "c->vc");
  iter->Prev();
  ASSERT_EQ(IterStatus(iter), "b->vb");
  iter->Prev();
  ASSERT_EQ(IterStatus(iter), "a->va");
  iter->Prev();
  ASSERT_EQ(IterStatus(iter), "(invalid)");
  iter->SeekToLast();
  ASSERT_EQ(IterStatus(iter), "c->vc");
  iter->Next();
  ASSERT_EQ(IterStatus(iter), "(invalid)");

  iter->Seek("");
  ASSERT_EQ(IterStatus(iter), "a->va");
  iter->Seek("a");
  ASSERT_EQ(IterStatus(iter), "a->va");
  iter->Seek("ax");
  ASSERT_EQ(IterStatus(iter), "b->vb");
  iter->Seek("b");
  ASSERT_EQ(IterStatus(iter), "b->vb");
  iter->Seek("z");
  ASSERT_EQ(IterStatus(iter), "(invalid)");

  // Switch from reverse to forward
  iter->SeekToLast();
  iter->Prev();
  iter->Prev();
  iter->Next();
  ASSERT_EQ(IterStatus(iter), "b->vb");

  // Switch from forward to reverse
  iter->SeekToFirst();
  iter->Next();
  iter->Next();
  iter->Prev();
  ASSERT_EQ(IterStatus(iter), "b->vb");

  // Make sure iter stays at snapshot
  ASSERT_OK(Put("a",  "va2"));
  ASSERT_OK(Put("a2", "va3"));
  ASSERT_OK(Put("b",  "vb2"));
  ASSERT_OK(Put("c",  "vc2"));
  ASSERT_OK(Delete("b"));
  iter->SeekToFirst();
  ASSERT_EQ(IterStatus(iter), "a->va");
  iter->Next();
  ASSERT_EQ(IterStatus(iter), "b->vb");
  iter->Next();
  ASSERT_EQ(IterStatus(iter), "c->vc");
  iter->Next();
  ASSERT_EQ(IterStatus(iter), "(invalid)");
  iter->SeekToLast();
  ASSERT_EQ(IterStatus(iter), "c->vc");
  iter->Prev();
  ASSERT_EQ(IterStatus(iter), "b->vb");
  iter->Prev();
  ASSERT_EQ(IterStatus(iter), "a->va");
  iter->Prev();
  ASSERT_EQ(IterStatus(iter), "(invalid)");

  delete iter;
}

TEST(DBTest, IterSmallAndLargeMix) {
  ASSERT_OK(Put("a", "va"));
  ASSERT_OK(Put("b", std::string(100000, 'b')));
  ASSERT_OK(Put("c", "vc"));
  ASSERT_OK(Put("d", std::string(100000, 'd')));
  ASSERT_OK(Put("e", std::string(100000, 'e')));

  Iterator* iter = db_->NewIterator(ReadOptions());

  iter->SeekToFirst();
  ASSERT_EQ(IterStatus(iter), "a->va");
  iter->Next();
  ASSERT_EQ(IterStatus(iter), "b->" + std::string(100000, 'b'));
  iter->Next();
  ASSERT_EQ(IterStatus(iter), "c->vc");
  iter->Next();
  ASSERT_EQ(IterStatus(iter), "d->" + std::string(100000, 'd'));
  iter->Next();
  ASSERT_EQ(IterStatus(iter), "e->" + std::string(100000, 'e'));
  iter->Next();
  ASSERT_EQ(IterStatus(iter), "(invalid)");

  iter->SeekToLast();
  ASSERT_EQ(IterStatus(iter), "e->" + std::string(100000, 'e'));
  iter->Prev();
  ASSERT_EQ(IterStatus(iter), "d->" + std::string(100000, 'd'));
  iter->Prev();
  ASSERT_EQ(IterStatus(iter), "c->vc");
  iter->Prev();
  ASSERT_EQ(IterStatus(iter), "b->" + std::string(100000, 'b'));
  iter->Prev();
  ASSERT_EQ(IterStatus(iter), "a->va");
  iter->Prev();
  ASSERT_EQ(IterStatus(iter), "(invalid)");

  delete iter;
}

TEST(DBTest, IterMultiWithDelete) {
  ASSERT_OK(Put("a", "va"));
  ASSERT_OK(Put("b", "vb"));
  ASSERT_OK(Put("c", "vc"));
  ASSERT_OK(Delete("b"));
  ASSERT_EQ("NOT_FOUND", Get("b"));

  Iterator* iter = db_->NewIterator(ReadOptions());
  iter->Seek("c");
  ASSERT_EQ(IterStatus(iter), "c->vc");
  iter->Prev();
  ASSERT_EQ(IterStatus(iter), "a->va");
  delete iter;
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

// Check that writes done during a memtable compaction are recovered
// if the database is shutdown during the memtable compaction.
TEST(DBTest, RecoverDuringMemtableCompaction) {
  Options options;
  options.env = env_;
  options.write_buffer_size = 1000000;
  Reopen(&options);

  // Trigger a long memtable compaction and reopen the database during it
  ASSERT_OK(Put("foo", "v1"));                         // Goes to 1st log file
  ASSERT_OK(Put("big1", std::string(10000000, 'x')));  // Fills memtable
  ASSERT_OK(Put("big2", std::string(1000, 'y')));      // Triggers compaction
  ASSERT_OK(Put("bar", "v2"));                         // Goes to new log file

  Reopen(&options);
  ASSERT_EQ("v1", Get("foo"));
  ASSERT_EQ("v2", Get("bar"));
  ASSERT_EQ(std::string(10000000, 'x'), Get("big1"));
  ASSERT_EQ(std::string(1000, 'y'), Get("big2"));
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

  const int N = 500;

  int starting_num_tables = TotalTableFiles();
  for (int i = 0; i < N; i++) {
    ASSERT_OK(Put(Key(i), Key(i) + std::string(1000, 'v')));
  }
  int ending_num_tables = TotalTableFiles();
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

TEST(DBTest, RepeatedWritesToSameKey) {
  Options options;
  options.env = env_;
  options.write_buffer_size = 100000;  // Small write buffer
  Reopen(&options);

  // We must have at most one file per level except for level-0,
  // which may have up to kL0_StopWritesTrigger files.
  const int kMaxFiles = config::kNumLevels + config::kL0_StopWritesTrigger;

  Random rnd(301);
  std::string value = RandomString(&rnd, 2 * options.write_buffer_size);
  for (int i = 0; i < 5 * kMaxFiles; i++) {
    Put("key", value);
    ASSERT_LE(TotalTableFiles(), kMaxFiles);
    fprintf(stderr, "after %d: %d files\n", int(i+1), TotalTableFiles());
  }
}

TEST(DBTest, SparseMerge) {
  Options options;
  options.compression = kNoCompression;
  Reopen(&options);

  FillLevels("A", "Z");

  // Suppose there is:
  //    small amount of data with prefix A
  //    large amount of data with prefix B
  //    small amount of data with prefix C
  // and that recent updates have made small changes to all three prefixes.
  // Check that we do not do a compaction that merges all of B in one shot.
  const std::string value(1000, 'x');
  Put("A", "va");
  // Write approximately 100MB of "B" values
  for (int i = 0; i < 100000; i++) {
    char key[100];
    snprintf(key, sizeof(key), "B%010d", i);
    Put(key, value);
  }
  Put("C", "vc");
  dbfull()->TEST_CompactMemTable();
  dbfull()->TEST_CompactRange(0, "A", "Z");

  // Make sparse update
  Put("A",    "va2");
  Put("B100", "bvalue2");
  Put("C",    "vc2");
  dbfull()->TEST_CompactMemTable();

  // Compactions should not cause us to create a situation where
  // a file overlaps too much data at the next level.
  ASSERT_LE(dbfull()->TEST_MaxNextLevelOverlappingBytes(), 20*1048576);
  dbfull()->TEST_CompactRange(0, "", "z");
  ASSERT_LE(dbfull()->TEST_MaxNextLevelOverlappingBytes(), 20*1048576);
  dbfull()->TEST_CompactRange(1, "", "z");
  ASSERT_LE(dbfull()->TEST_MaxNextLevelOverlappingBytes(), 20*1048576);
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
  Options options;
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

  // 0 because GetApproximateSizes() does not account for memtable space
  ASSERT_TRUE(Between(Size("", Key(50)), 0, 0));

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

TEST(DBTest, ApproximateSizes_MixOfSmallAndLarge) {
  Options options;
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
  FillLevels("a", "z");

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
  const int last = config::kMaxMemCompactLevel;
  ASSERT_EQ(NumTableFilesAtLevel(last), 1);   // foo => v1 is now in last level

  // Place a table at level last-1 to prevent merging with preceding mutation
  Put("a", "begin");
  Put("z", "end");
  dbfull()->TEST_CompactMemTable();
  ASSERT_EQ(NumTableFilesAtLevel(last), 1);
  ASSERT_EQ(NumTableFilesAtLevel(last-1), 1);

  Delete("foo");
  Put("foo", "v2");
  ASSERT_EQ(AllEntriesFor("foo"), "[ v2, DEL, v1 ]");
  ASSERT_OK(dbfull()->TEST_CompactMemTable());  // Moves to level last-2
  ASSERT_EQ(AllEntriesFor("foo"), "[ v2, DEL, v1 ]");
  dbfull()->TEST_CompactRange(last-2, "", "z");
  // DEL eliminated, but v1 remains because we aren't compacting that level
  // (DEL can be eliminated because v2 hides v1).
  ASSERT_EQ(AllEntriesFor("foo"), "[ v2, v1 ]");
  dbfull()->TEST_CompactRange(last-1, "", "z");
  // Merging last-1 w/ last, so we are the base level for "foo", so
  // DEL is removed.  (as is v1).
  ASSERT_EQ(AllEntriesFor("foo"), "[ v2 ]");
}

TEST(DBTest, DeletionMarkers2) {
  Put("foo", "v1");
  ASSERT_OK(dbfull()->TEST_CompactMemTable());
  const int last = config::kMaxMemCompactLevel;
  ASSERT_EQ(NumTableFilesAtLevel(last), 1);   // foo => v1 is now in last level

  // Place a table at level last-1 to prevent merging with preceding mutation
  Put("a", "begin");
  Put("z", "end");
  dbfull()->TEST_CompactMemTable();
  ASSERT_EQ(NumTableFilesAtLevel(last), 1);
  ASSERT_EQ(NumTableFilesAtLevel(last-1), 1);

  Delete("foo");
  ASSERT_EQ(AllEntriesFor("foo"), "[ DEL, v1 ]");
  ASSERT_OK(dbfull()->TEST_CompactMemTable());  // Moves to level last-2
  ASSERT_EQ(AllEntriesFor("foo"), "[ DEL, v1 ]");
  dbfull()->TEST_CompactRange(last-2, "", "z");
  // DEL kept: "last" file overlaps
  ASSERT_EQ(AllEntriesFor("foo"), "[ DEL, v1 ]");
  dbfull()->TEST_CompactRange(last-1, "", "z");
  // Merging last-1 w/ last, so we are the base level for "foo", so
  // DEL is removed.  (as is v1).
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

// Multi-threaded test:
namespace {

static const int kNumThreads = 4;
static const int kTestSeconds = 10;
static const int kNumKeys = 1000;

struct MTState {
  DBTest* test;
  port::AtomicPointer stop;
  port::AtomicPointer counter[kNumThreads];
  port::AtomicPointer thread_done[kNumThreads];
};

struct MTThread {
  MTState* state;
  int id;
};

static void MTThreadBody(void* arg) {
  MTThread* t = reinterpret_cast<MTThread*>(arg);
  DB* db = t->state->test->db_;
  uintptr_t counter = 0;
  fprintf(stderr, "... starting thread %d\n", t->id);
  Random rnd(1000 + t->id);
  std::string value;
  char valbuf[1500];
  while (t->state->stop.Acquire_Load() == NULL) {
    t->state->counter[t->id].Release_Store(reinterpret_cast<void*>(counter));

    int key = rnd.Uniform(kNumKeys);
    char keybuf[20];
    snprintf(keybuf, sizeof(keybuf), "%016d", key);

    if (rnd.OneIn(2)) {
      // Write values of the form <key, my id, counter>.
      // We add some padding for force compactions.
      snprintf(valbuf, sizeof(valbuf), "%d.%d.%-1000d",
               key, t->id, static_cast<int>(counter));
      ASSERT_OK(db->Put(WriteOptions(), Slice(keybuf), Slice(valbuf)));
    } else {
      // Read a value and verify that it matches the pattern written above.
      Status s = db->Get(ReadOptions(), Slice(keybuf), &value);
      if (s.IsNotFound()) {
        // Key has not yet been written
      } else {
        // Check that the writer thread counter is >= the counter in the value
        ASSERT_OK(s);
        int k, w, c;
        ASSERT_EQ(3, sscanf(value.c_str(), "%d.%d.%d", &k, &w, &c)) << value;
        ASSERT_EQ(k, key);
        ASSERT_GE(w, 0);
        ASSERT_LT(w, kNumThreads);
        ASSERT_LE(c, reinterpret_cast<uintptr_t>(
            t->state->counter[w].Acquire_Load()));
      }
    }
    counter++;
  }
  t->state->thread_done[t->id].Release_Store(t);
  fprintf(stderr, "... stopping thread %d after %d ops\n", t->id, int(counter));
}

}

TEST(DBTest, MultiThreaded) {
  // Initialize state
  MTState mt;
  mt.test = this;
  mt.stop.Release_Store(0);
  for (int id = 0; id < kNumThreads; id++) {
    mt.counter[id].Release_Store(0);
    mt.thread_done[id].Release_Store(0);
  }

  // Start threads
  MTThread thread[kNumThreads];
  for (int id = 0; id < kNumThreads; id++) {
    thread[id].state = &mt;
    thread[id].id = id;
    env_->StartThread(MTThreadBody, &thread[id]);
  }

  // Let them run for a while
  env_->SleepForMicroseconds(kTestSeconds * 1000000);

  // Stop the threads and wait for them to finish
  mt.stop.Release_Store(&mt);
  for (int id = 0; id < kNumThreads; id++) {
    while (mt.thread_done[id].Acquire_Load() == NULL) {
      env_->SleepForMicroseconds(100000);
    }
  }
}

namespace {
typedef std::map<std::string, std::string> KVMap;
}

class ModelDB: public DB {
 public:
  class ModelSnapshot : public Snapshot {
   public:
    KVMap map_;
  };

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
          &(reinterpret_cast<const ModelSnapshot*>(options.snapshot)->map_);
      return new ModelIter(snapshot_state, false);
    }
  }
  virtual const Snapshot* GetSnapshot() {
    ModelSnapshot* snapshot = new ModelSnapshot;
    snapshot->map_ = map_;
    return snapshot;
  }

  virtual void ReleaseSnapshot(const Snapshot* snapshot) {
    delete reinterpret_cast<const ModelSnapshot*>(snapshot);
  }
  virtual Status Write(const WriteOptions& options, WriteBatch* batch) {
    assert(options.post_write_snapshot == NULL);   // Not supported
    class Handler : public WriteBatch::Handler {
     public:
      KVMap* map_;
      virtual void Put(const Slice& key, const Slice& value) {
        (*map_)[key.ToString()] = value.ToString();
      }
      virtual void Delete(const Slice& key) {
        map_->erase(key.ToString());
      }
    };
    Handler handler;
    handler.map_ = &map_;
    return batch->Iterate(&handler);
  }

  virtual bool GetProperty(const Slice& property, std::string* value) {
    return false;
  }
  virtual void GetApproximateSizes(const Range* r, int n, uint64_t* sizes) {
    for (int i = 0; i < n; i++) {
      sizes[i] = 0;
    }
  }
 private:
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

std::string MakeKey(unsigned int num) {
  char buf[30];
  snprintf(buf, sizeof(buf), "%016u", num);
  return std::string(buf);
}

void BM_LogAndApply(int iters, int num_base_files) {
  std::string dbname = test::TmpDir() + "/leveldb_test_benchmark";
  DestroyDB(dbname, Options());

  DB* db = NULL;
  Options opts;
  opts.create_if_missing = true;
  Status s = DB::Open(opts, dbname, &db);
  ASSERT_OK(s);
  ASSERT_TRUE(db != NULL);

  delete db;
  db = NULL;

  Env* env = Env::Default();

  port::Mutex mu;
  MutexLock l(&mu);

  InternalKeyComparator cmp(BytewiseComparator());
  Options options;
  VersionSet vset(dbname, &options, NULL, &cmp);
  ASSERT_OK(vset.Recover());
  VersionEdit vbase;
  uint64_t fnum = 1;
  for (int i = 0; i < num_base_files; i++) {
    InternalKey start(MakeKey(2*fnum), 1, kTypeValue);
    InternalKey limit(MakeKey(2*fnum+1), 1, kTypeDeletion);
    vbase.AddFile(2, fnum++, 1 /* file size */, start, limit);
  }
  ASSERT_OK(vset.LogAndApply(&vbase, &mu));

  uint64_t start_micros = env->NowMicros();

  for (int i = 0; i < iters; i++) {
    VersionEdit vedit;
    vedit.DeleteFile(2, fnum);
    InternalKey start(MakeKey(2*fnum), 1, kTypeValue);
    InternalKey limit(MakeKey(2*fnum+1), 1, kTypeDeletion);
    vedit.AddFile(2, fnum++, 1 /* file size */, start, limit);
    vset.LogAndApply(&vedit, &mu);
  }
  uint64_t stop_micros = env->NowMicros();
  unsigned int us = stop_micros - start_micros;
  char buf[16];
  snprintf(buf, sizeof(buf), "%d", num_base_files);
  fprintf(stderr,
          "BM_LogAndApply/%-6s   %8d iters : %9u us (%7.0f us / iter)\n",
          buf, iters, us, ((float)us) / iters);
}

}

int main(int argc, char** argv) {
  if (argc > 1 && std::string(argv[1]) == "--benchmark") {
    leveldb::BM_LogAndApply(1000, 1);
    leveldb::BM_LogAndApply(1000, 100);
    leveldb::BM_LogAndApply(1000, 10000);
    leveldb::BM_LogAndApply(100, 100000);
    return 0;
  }

  return leveldb::test::RunAllTests();
}
