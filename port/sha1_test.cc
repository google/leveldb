// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "port/port.h"
#include "util/testharness.h"

namespace leveldb {
namespace port {

class SHA1 { };

static std::string TestSHA1(const char* data, size_t len) {
  char hash_val[20];
  SHA1_Hash(data, len, hash_val);
  char buf[41];
  for (int i = 0; i < 20; i++) {
    snprintf(buf + i * 2, 41 - i * 2,
             "%02x",
             static_cast<unsigned int>(static_cast<unsigned char>(
                 hash_val[i])));
  }
  return std::string(buf, 40);
}

TEST(SHA1, Simple) {
  ASSERT_EQ("da39a3ee5e6b4b0d3255bfef95601890afd80709", TestSHA1("", 0));
  ASSERT_EQ("aaf4c61ddcc5e8a2dabede0f3b482cd9aea9434d", TestSHA1("hello", 5));
  std::string x(10000, 'x');
  ASSERT_EQ("f8c5cde791c5056cf515881e701c8a9ecb439a75",
            TestSHA1(x.data(), x.size()));
}

}
}

int main(int argc, char** argv) {
  return leveldb::test::RunAllTests();
}
