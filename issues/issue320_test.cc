// Copyright (c) 2019 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <math.h>

#include "leveldb/db.h"
#include "leveldb/write_batch.h"
#include "util/testharness.h"

using namespace std;

namespace leveldb {

namespace {

unsigned int random(unsigned int max) {
  return std::rand() % max;
}

string newString(int32_t index) {
  const unsigned int len = 1024;
  char bytes[len];
  unsigned int i = 0;
  while (i < 8) {
    bytes[i] = 'a' + ((index >> (4 * i)) & 0xf);
    ++i;
  }
  while (i < sizeof(bytes)) {
    bytes[i] = 'a' + random(26);
    ++i;
  }
  return string(bytes, sizeof(bytes));
}

}  // namespace

class Issue320 { };

TEST(Issue320, Test) {
  std::srand(0);

  bool delete_before_put = false;
  bool keep_snapshots = true;

  vector<pair<string, string>*> test_map(10000, nullptr);
  vector<Snapshot const*> snapshots(100, nullptr);

  DB* db;
  Options options;
  options.create_if_missing = true;

  std::string dbpath = test::TmpDir() + "/leveldb_issue320_test";
  ASSERT_OK(DB::Open(options, dbpath, &db));

  unsigned int target_size = 10000;
  unsigned int num_items = 0;
  unsigned long count = 0;
  string key;
  string value, old_value;

  WriteOptions writeOptions;
  ReadOptions readOptions;
  while (count < 200000) {
    if ((++count % 1000) == 0) {
      cout << "count: " << count << endl;
    }

    unsigned int index = random(test_map.size());
    WriteBatch batch;

    if (test_map[index] == nullptr) {
      num_items++;
      test_map[index] =
          new pair<string, string>(newString(index), newString(index));
      batch.Put(test_map[index]->first, test_map[index]->second);
    } else {
      ASSERT_OK(db->Get(readOptions, test_map[index]->first, &old_value));
      if (old_value != test_map[index]->second) {
        cout << "ERROR incorrect value returned by Get" << endl;
        cout << "  count=" << count << endl;
        cout << "  old value=" << old_value << endl;
        cout << "  test_map[index]->second=" << test_map[index]->second << endl;
        cout << "  test_map[index]->first=" << test_map[index]->first << endl;
        cout << "  index=" << index << endl;
        ASSERT_EQ(old_value, test_map[index]->second);
      }

      if (num_items >= target_size && random(100) > 30) {
        batch.Delete(test_map[index]->first);
        delete test_map[index];
        test_map[index] = nullptr;
        --num_items;
      } else {
        test_map[index]->second = newString(index);
        if (delete_before_put) batch.Delete(test_map[index]->first);
        batch.Put(test_map[index]->first, test_map[index]->second);
      }
    }

    ASSERT_OK(db->Write(writeOptions, &batch));

    if (keep_snapshots && random(10) == 0) {
      unsigned int i = random(snapshots.size());
      if (snapshots[i] != nullptr) {
        db->ReleaseSnapshot(snapshots[i]);
      }
      snapshots[i] = db->GetSnapshot();
    }
  }

  for (Snapshot const* snapshot : snapshots) {
    if (snapshot) {
      db->ReleaseSnapshot(snapshot);
    }
  }

  for (size_t i = 0; i < test_map.size(); ++i) {
    if (test_map[i] != nullptr) {
      delete test_map[i];
      test_map[i] = nullptr;
    }
  }

  delete db;
  DestroyDB(dbpath, options);
}

}  // namespace leveldb

int main(int argc, char** argv) {
  return leveldb::test::RunAllTests();
}
