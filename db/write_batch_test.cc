// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include <limits>

#include "gtest/gtest.h"
#include "db/memtable.h"
#include "db/write_batch_internal.h"
#include "leveldb/db.h"
#include "leveldb/env.h"
#include "util/logging.h"

namespace leveldb {

static std::string PrintContents(WriteBatch* b) {
  InternalKeyComparator cmp(BytewiseComparator());
  MemTable* mem = new MemTable(cmp);
  mem->Ref();
  std::string state;
  Status s = WriteBatchInternal::InsertInto(b, mem);
  int count = 0;
  Iterator* iter = mem->NewIterator();
  for (iter->SeekToFirst(); iter->Valid(); iter->Next()) {
    ParsedInternalKey ikey;
    EXPECT_TRUE(ParseInternalKey(iter->key(), &ikey));
    switch (ikey.type) {
      case kTypeValue:
        state.append("Put(");
        state.append(ikey.user_key.ToString());
        state.append(", ");
        state.append(iter->value().ToString());
        state.append(")");
        count++;
        break;
      case kTypeDeletion:
        state.append("Delete(");
        state.append(ikey.user_key.ToString());
        state.append(")");
        count++;
        break;
    }
    state.append("@");
    state.append(NumberToString(ikey.sequence));
  }
  delete iter;
  if (!s.ok()) {
    state.append("ParseError()");
  } else if (count != WriteBatchInternal::Count(b)) {
    state.append("CountMismatch()");
  }
  mem->Unref();
  return state;
}

TEST(WriteBatchTest, Empty) {
  WriteBatch batch;
  ASSERT_EQ("", PrintContents(&batch));
  ASSERT_EQ(0, WriteBatchInternal::Count(&batch));
}

TEST(WriteBatchTest, Multiple) {
  WriteBatch batch;
  batch.Put(Slice("foo"), Slice("bar"));
  batch.Delete(Slice("box"));
  batch.Put(Slice("baz"), Slice("boo"));
  WriteBatchInternal::SetSequence(&batch, 100);
  ASSERT_EQ(100, WriteBatchInternal::Sequence(&batch));
  ASSERT_EQ(3, WriteBatchInternal::Count(&batch));
  ASSERT_EQ(
      "Put(baz, boo)@102"
      "Delete(box)@101"
      "Put(foo, bar)@100",
      PrintContents(&batch));
}

TEST(WriteBatchTest, Corruption) {
  WriteBatch batch;
  batch.Put(Slice("foo"), Slice("bar"));
  batch.Delete(Slice("box"));
  WriteBatchInternal::SetSequence(&batch, 200);
  Slice contents = WriteBatchInternal::Contents(&batch);
  WriteBatchInternal::SetContents(&batch,
                                  Slice(contents.data(), contents.size() - 1));
  ASSERT_EQ(
      "Put(foo, bar)@200"
      "ParseError()",
      PrintContents(&batch));
}

TEST(WriteBatchTest, Append) {
  WriteBatch b1, b2;
  WriteBatchInternal::SetSequence(&b1, 200);
  WriteBatchInternal::SetSequence(&b2, 300);
  b1.Append(b2);
  ASSERT_EQ("", PrintContents(&b1));
  b2.Put("a", "va");
  b1.Append(b2);
  ASSERT_EQ("Put(a, va)@200", PrintContents(&b1));
  b2.Clear();
  b2.Put("b", "vb");
  b1.Append(b2);
  ASSERT_EQ(
      "Put(a, va)@200"
      "Put(b, vb)@201",
      PrintContents(&b1));
  b2.Delete("foo");
  b1.Append(b2);
  ASSERT_EQ(
      "Put(a, va)@200"
      "Put(b, vb)@202"
      "Put(b, vb)@201"
      "Delete(foo)@203",
      PrintContents(&b1));
}

TEST(WriteBatchTest, ApproximateSize) {
  WriteBatch batch;
  size_t empty_size = batch.ApproximateSize();

  batch.Put(Slice("foo"), Slice("bar"));
  size_t one_key_size = batch.ApproximateSize();
  ASSERT_LT(empty_size, one_key_size);

  batch.Put(Slice("baz"), Slice("boo"));
  size_t two_keys_size = batch.ApproximateSize();
  ASSERT_LT(one_key_size, two_keys_size);

  batch.Delete(Slice("box"));
  size_t post_delete_size = batch.ApproximateSize();
  ASSERT_LT(two_keys_size, post_delete_size);
}

TEST(WriteBatchTest, OversizedKeyRejected) {
  Options options;
  options.create_if_missing = true;
  std::string dbname = testing::TempDir() + "write_batch_oversized_key_test";
  DestroyDB(dbname, options);
  DB* db = nullptr;
  ASSERT_TRUE(DB::Open(options, dbname, &db).ok());

  size_t oversized =
      static_cast<size_t>(std::numeric_limits<uint32_t>::max()) - 7;
  Slice oversized_key(nullptr, oversized);

  Status s = db->Put(WriteOptions(), oversized_key, "value");
  EXPECT_TRUE(s.IsInvalidArgument());

  s = db->Delete(WriteOptions(), oversized_key);
  EXPECT_TRUE(s.IsInvalidArgument());

  if (sizeof(size_t) > sizeof(uint32_t)) {
    size_t oversized_64 =
        static_cast<size_t>(std::numeric_limits<uint32_t>::max()) + 1;
    Slice oversized_key_64(nullptr, oversized_64);

    s = db->Put(WriteOptions(), oversized_key_64, "value");
    EXPECT_TRUE(s.IsInvalidArgument());

    s = db->Delete(WriteOptions(), oversized_key_64);
    EXPECT_TRUE(s.IsInvalidArgument());
  }

  delete db;
  DestroyDB(dbname, options);
}

TEST(WriteBatchTest, ShortInternalKeyUnderflow) {
  Slice short_key("short");
  ASSERT_EQ(0, ExtractUserKey(short_key).size());

  InternalKeyComparator cmp(BytewiseComparator());
  ASSERT_EQ(0, cmp.Compare(short_key, short_key));

  // Compare two distinct short keys (< 8 bytes)
  Slice short_a("abc");
  Slice short_b("xyz");
  ASSERT_LT(cmp.Compare(short_a, short_b), 0);
  ASSERT_GT(cmp.Compare(short_b, short_a), 0);

  // Compare short key (< 8 bytes) against a valid 8-byte key with empty user key.
  // Both ExtractUserKey calls return empty slices, so r == 0, exercising the
  // guard that avoids underflowing akey.size() - 8 / bkey.size() - 8.
  std::string valid_8_byte(8, '\0');
  ASSERT_LT(cmp.Compare(short_key, Slice(valid_8_byte)), 0);
  ASSERT_GT(cmp.Compare(Slice(valid_8_byte), short_key), 0);

  // Compare short key (< 8 bytes) against a key with non-empty user key.
  ASSERT_LT(cmp.Compare(short_key, Slice("longer_key_1234")), 0);
  ASSERT_GT(cmp.Compare(Slice("longer_key_1234"), short_key), 0);
}

}  // namespace leveldb
