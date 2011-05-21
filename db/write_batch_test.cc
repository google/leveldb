// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "leveldb/db.h"

#include "db/memtable.h"
#include "db/write_batch_internal.h"
#include "leveldb/env.h"
#include "util/logging.h"
#include "util/testharness.h"

namespace leveldb {

static std::string PrintContents(WriteBatch* b) {
  InternalKeyComparator cmp(BytewiseComparator());
  MemTable* mem = new MemTable(cmp);
  mem->Ref();
  std::string state;
  Status s = WriteBatchInternal::InsertInto(b, mem);
  Iterator* iter = mem->NewIterator();
  for (iter->SeekToFirst(); iter->Valid(); iter->Next()) {
    ParsedInternalKey ikey;
    ASSERT_TRUE(ParseInternalKey(iter->key(), &ikey));
    switch (ikey.type) {
      case kTypeValue:
        state.append("Put(");
        state.append(ikey.user_key.ToString());
        state.append(", ");
        state.append(iter->value().ToString());
        state.append(")");
        break;
      case kTypeDeletion:
        state.append("Delete(");
        state.append(ikey.user_key.ToString());
        state.append(")");
        break;
    }
    state.append("@");
    state.append(NumberToString(ikey.sequence));
  }
  delete iter;
  if (!s.ok()) {
    state.append("ParseError()");
  }
  mem->Unref();
  return state;
}

class WriteBatchTest { };

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
  ASSERT_EQ("Put(baz, boo)@102"
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
                                  Slice(contents.data(),contents.size()-1));
  ASSERT_EQ("Put(foo, bar)@200"
            "ParseError()",
            PrintContents(&batch));
}

}

int main(int argc, char** argv) {
  return leveldb::test::RunAllTests();
}
