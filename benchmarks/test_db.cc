#include <cassert>
#include <ctime>
#include <iostream>
#include <string>
#include <vector>
#include <cmath>
#include "leveldb/db.h"
#include "leveldb/filter_policy.h"

// #include <random.h>

std::string gen_random(const int len) {
  static const char alphanum[] =
      "0123456789"
      // "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
      // "abcdefghijklmnopqrstuvwxyz"
      ;
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

int write_data(leveldb::DB* db, int num_megabytes, int key_size) {
  for (int i = 0; i < num_megabytes * 1024; i++) {
    std::string value = gen_random(key_size);
    leveldb::Status status =
        db->Put(leveldb::WriteOptions(), leveldb::Slice(value), "");
    if (!status.ok()) {
      std::cout << "oops" << std::endl;
      return -1;
    }
  }
  return 0;
}

int read_data(leveldb::DB* db, int num_entries, int key_size) {
  for (int i = 0; i < num_entries; i++) {
    std::string value = gen_random(key_size);
    std::string result;
    leveldb::Status status = db->Get(leveldb::ReadOptions(), leveldb::Slice(value), &result);
    if (!(status.ok() || status.IsNotFound())) {
      std::cout << "oops" << std::endl;
      return -1;
    }
    if(status.ok()) {
      std::cout << "actually found" << std::endl;
    }
  }
  return 0;

}
int sanity_check(leveldb::DB* db) {
  std::string value = gen_random(100);
  db->Put(leveldb::WriteOptions(), leveldb::Slice(value), "");

  for(int i = 0; i < 10000; i++) {
    std::string random = gen_random(100);
    db->Put(leveldb::WriteOptions(), leveldb::Slice(random), "");
  }

  std::string result;
  leveldb::Status status =
      db->Get(leveldb::ReadOptions(), leveldb::Slice(value), &result);
  if (!status.ok()) {
    std::cout << "Error!!!!!" << std::endl;
  }
  else {
    std::cout << "All good" << std::endl;
  }
  return 0;
}

double eval(long run_bits, long runs_entries){
  return std::exp(run_bits / runs_entries * std::pow(std::log(2), 2) * -1);
}

double TrySwitch(long& run1_entries, long& run1_bits, long& run2_entries,
                 long& run2_bits, long delta, double R) {
  double R_new = R - eval(run1_bits, run1_entries) -
                 eval(run2_bits, run2_entries) +
                 eval(run1_bits + delta, run1_entries) +
                 eval(run2_bits - delta, run2_entries);

  if (R_new < R && run2_bits - delta > 0) {
    run1_bits += delta;
    run2_bits -= delta;
    return R_new;
  } else {
    return R;
  }
}

std::vector<long> run_algorithm_c(std::vector<long> entries_per_level,
                                    int key_size, int bits_per_entry_equivalent) {
  long total_entries = 0;
  for (auto& i : entries_per_level) {
    total_entries += i;
  }
  std::cout << "total bytes is " << total_entries * key_size << " which is entries: " << total_entries << std::endl;
  long delta = total_entries * bits_per_entry_equivalent;
  std::vector<long> runs_entries;
  std::vector<long> runs_bits;
  for (long i = 0; i < entries_per_level.size(); i++) {
    runs_entries.push_back(entries_per_level[i]);
    runs_bits.push_back(0);
  }
  runs_bits[0] = delta;
  double R = runs_entries.size() - 1 + eval(runs_bits[0], runs_entries[0]);
  
  while (delta >= 1) {
    double R_new = R;
    for(int i = 0; i < runs_entries.size(); i++) {
      for(int j = i+1; j < runs_entries.size(); j++) {
        R_new = TrySwitch(runs_entries[i], runs_bits[i], runs_entries[j], runs_bits[j], delta, R_new);
        R_new = TrySwitch(runs_entries[j], runs_bits[j], runs_entries[i], runs_bits[i], delta, R_new);
      }
    }
    if (R_new == R) {
      delta /= 2;
    }
    R = R_new;
  }
  std::vector<long> result;
  for(int i = 0; i < runs_bits.size(); i++) {
    result.push_back(runs_bits[i] / entries_per_level[i]);
  }
  return result;
}



int main() {
  leveldb::DB* db;
  leveldb::Options options;
  
  options.create_if_missing = true;
  options.block_size = 1024 * 1024;
  options.compression = leveldb::kNoCompression;
  int key_size = 500;
  leveldb::Status status = leveldb::DB::Open(options, "/tmp/testdb", &db);

  write_data(db, 512, key_size);
  std::vector<long> bytes_per_level_with_zeros = db->GetBytesPerLevel();
  std::vector<long> entries_per_level;
  for(long i = 0; i < bytes_per_level_with_zeros.size(); i++) {
    if (bytes_per_level_with_zeros[i] == 0) {
      break;
    }
    entries_per_level.push_back(bytes_per_level_with_zeros[i] / 8);
  }

  std::vector<long> bits_per_key_per_level = run_algorithm_c(entries_per_level, key_size, 5);
  for(int i = 0; i < bits_per_key_per_level.size(); i++) {
    std::cout << "Level " << i << " bits per key is " << bits_per_key_per_level[i] << std::endl;
  }
  delete db;

  options.filter_policy = leveldb::NewBloomFilterPolicy(bits_per_key_per_level);
  
  status = leveldb::DB::Open(options, "/tmp/testdb", &db);
  // sanity_check(db);
  // std::cout << "Status: " << status.ToString() << std::endl;
  read_data(db, 12000, key_size);
  

  assert(status.ok());

  
}
