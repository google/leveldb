// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "db/filename.h"

#include "db/dbformat.h"
#include "port/port.h"
#include "util/logging.h"
#include "util/testharness.h"

namespace leveldb {

class FileNameTest { };

TEST(FileNameTest, Parse) {
  Slice db;
  FileType type;
  uint64_t number;
  LargeValueRef large_ref;

  // Successful parses
  static struct {
    const char* fname;
    uint64_t number;
    const char* large_ref;
    FileType type;
  } cases[] = {
    { "100.log",            100,   "",         kLogFile },
    { "0.log",              0,     "",         kLogFile },
    { "0.sst",              0,     "",         kTableFile },
    { "CURRENT",            0,     "",         kCurrentFile },
    { "LOCK",               0,     "",         kDBLockFile },
    { "MANIFEST-2",         2,     "",         kDescriptorFile },
    { "MANIFEST-7",         7,     "",         kDescriptorFile },
    { "LOG",                0,     "",         kInfoLogFile },
    { "LOG.old",            0,     "",         kInfoLogFile },
    { "18446744073709551615.log", 18446744073709551615ull, "",
      kLogFile },
    { "2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2323-1234-0.val", 0,
      "2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2323-1234-0", kLargeValueFile },
    { "2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2323-10000000000-0.val", 0,
      "2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2323-10000000000-0",
      kLargeValueFile },
  };
  for (int i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    std::string f = cases[i].fname;
    ASSERT_TRUE(ParseFileName(f, &number, &large_ref, &type)) << f;
    ASSERT_EQ(cases[i].type, type) << f;
    if (type == kLargeValueFile) {
      ASSERT_EQ(cases[i].large_ref, LargeValueRefToFilenameString(large_ref))
          << f;
    } else {
      ASSERT_EQ(cases[i].number, number) << f;
    }
  }

  // Errors
  static const char* errors[] = {
    "",
    "foo",
    "foo-dx-100.log",
    ".log",
    "",
    "manifest",
    "CURREN",
    "CURRENTX",
    "MANIFES",
    "MANIFEST",
    "MANIFEST-",
    "XMANIFEST-3",
    "MANIFEST-3x",
    "LOC",
    "LOCKx",
    "LO",
    "LOGx",
    "18446744073709551616.log",
    "184467440737095516150.log",
    "100",
    "100.",
    "100.lop",
    "100.val",
    ".val",
    "123456789012345678901234567890123456789-12340.val",
    "1234567890123456789012345678901234567-123-0.val",
    "12345678901234567890123456789012345678902-100-1-.val",
    // Overflow on value size
    "2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2323-100000000000000000000-1.val",
    // '03.val' is a bad compression type
    "2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2e2323-100000-3.val" };
  for (int i = 0; i < sizeof(errors) / sizeof(errors[0]); i++) {
    std::string f = errors[i];
    ASSERT_TRUE(!ParseFileName(f, &number, &large_ref, &type)) << f;
  };
}

TEST(FileNameTest, Construction) {
  uint64_t number;
  FileType type;
  LargeValueRef large_ref;
  std::string fname;

  fname = CurrentFileName("foo");
  ASSERT_EQ("foo/", std::string(fname.data(), 4));
  ASSERT_TRUE(ParseFileName(fname.c_str() + 4, &number, &large_ref, &type));
  ASSERT_EQ(0, number);
  ASSERT_EQ(kCurrentFile, type);

  fname = LockFileName("foo");
  ASSERT_EQ("foo/", std::string(fname.data(), 4));
  ASSERT_TRUE(ParseFileName(fname.c_str() + 4, &number, &large_ref, &type));
  ASSERT_EQ(0, number);
  ASSERT_EQ(kDBLockFile, type);

  fname = LogFileName("foo", 192);
  ASSERT_EQ("foo/", std::string(fname.data(), 4));
  ASSERT_TRUE(ParseFileName(fname.c_str() + 4, &number, &large_ref, &type));
  ASSERT_EQ(192, number);
  ASSERT_EQ(kLogFile, type);

  fname = TableFileName("bar", 200);
  ASSERT_EQ("bar/", std::string(fname.data(), 4));
  ASSERT_TRUE(ParseFileName(fname.c_str() + 4, &number, &large_ref, &type));
  ASSERT_EQ(200, number);
  ASSERT_EQ(kTableFile, type);

  fname = DescriptorFileName("bar", 100);
  ASSERT_EQ("bar/", std::string(fname.data(), 4));
  ASSERT_TRUE(ParseFileName(fname.c_str() + 4, &number, &large_ref, &type));
  ASSERT_EQ(100, number);
  ASSERT_EQ(kDescriptorFile, type);

  fname = TempFileName("tmp", 999);
  ASSERT_EQ("tmp/", std::string(fname.data(), 4));
  ASSERT_TRUE(ParseFileName(fname.c_str() + 4, &number, &large_ref, &type));
  ASSERT_EQ(999, number);
  ASSERT_EQ(kTempFile, type);

  for (int i = 0; i <= kLightweightCompression; i++) {
    CompressionType ctype = static_cast<CompressionType>(i);
    std::string value = "abcdef";
    LargeValueRef real_large_ref = LargeValueRef::Make(Slice(value), ctype);
    fname = LargeValueFileName("tmp", real_large_ref);
    ASSERT_EQ("tmp/", std::string(fname.data(), 4));
    ASSERT_TRUE(ParseFileName(fname.c_str() + 4, &number, &large_ref, &type));
    ASSERT_TRUE(real_large_ref == large_ref);
    ASSERT_EQ(kLargeValueFile, type);
    ASSERT_EQ(large_ref.compression_type(), ctype);
  }
}

}

int main(int argc, char** argv) {
  return leveldb::test::RunAllTests();
}
