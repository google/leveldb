#include <cassert>
#include <ctime>
#include <iostream>
#include <string>

#include "leveldb/db.h"
#include "leveldb/filter_policy.h"

// #include <random.h>

std::string gen_random(const int len) {
  static const char alphanum[] =
      "0123456789"
      "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "abcdefghijklmnopqrstuvwxyz";
  std::string tmp_s;
  tmp_s.reserve(len);

  for (int i = 0; i < len; ++i) {
    tmp_s += alphanum[rand() % (sizeof(alphanum) - 1)];
  }

  return tmp_s;
}

void print_db(leveldb::DB* db) {
  leveldb::Iterator* it = db->NewIterator(leveldb::ReadOptions());
  for (it->SeekToFirst(); it->Valid(); it->Next()) {
    std::cout << it->key().ToString() << ": " << it->value().ToString()
              << std::endl;
  }
  assert(it->status().ok());  // Check for any errors found during the scan
  delete it;
}

void write_data(leveldb::DB* db) {

}

  int main() {
  leveldb::DB* db;
  leveldb::Options options;
  
  options.create_if_missing = true;
  // options.filter_policy = leveldb::NewBloomFilterPolicy(5);
  options.block_size = 1024 * 1024;
  options.compression = leveldb::kNoCompression;
  leveldb::WriteOptions w_options;

  leveldb::Status status = leveldb::DB::Open(options, "/tmp/testdb", &db);
  int num_megabytes = 2;
  for (int i = 0; i < num_megabytes * 1024; i++) {
    std::string value = gen_random(512);
    leveldb::Status status = db->Put(w_options, leveldb::Slice(value), value);
    if (!status.ok()) {
      std::cout << "oops" << std::endl;
      return -1;
    }
      
  }
  

  options.filter_policy = leveldb::NewBloomFilterPolicy(5);
  delete db;
  
  status = leveldb::DB::Open(options, "/tmp/testdb", &db);
  std::cout << "HERE" << std::endl;
  for (int i = 0; i < 10; i++) {
    std::string value = gen_random(512);
    std::string result;
    leveldb::Status status =
        db->Get(leveldb::ReadOptions(), leveldb::Slice(value), &result);
  }


  // print_db(db);

  assert(status.ok());

  
}
