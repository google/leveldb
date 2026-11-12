// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "table/format.h"

#include <cstdint>
#include <string>

#include "gtest/gtest.h"
#include "leveldb/env.h"
#include "leveldb/options.h"
#include "util/coding.h"
#include "util/crc32c.h"
#include "util/testutil.h"

namespace leveldb {

// A fake RandomAccessFile that returns crafted data
class FakeFile : public RandomAccessFile {
 public:
  explicit FakeFile(const std::string& contents) : contents_(contents) {}
  ~FakeFile() override = default;

  Status Read(uint64_t offset, size_t n, Slice* result,
              char* scratch) const override {
    if (offset >= contents_.size()) {
      return Status::InvalidArgument("read offset out of bounds");
    }
    size_t available = contents_.size() - offset;
    size_t to_read = std::min(n, available);
    std::memcpy(scratch, contents_.data() + offset, to_read);
    *result = Slice(scratch, to_read);
    return Status::OK();
  }

 private:
  std::string contents_;
};

class ReadBlockOomTest : public testing::Test {
 protected:
  ReadOptions options_;
};

// Test that a block handle with an excessively large size is rejected
// before attempting allocation (prevents OOM DoS attack).
// See: https://github.com/google/leveldb/issues/XXX
TEST_F(ReadBlockOomTest, RejectOversizedBlockHandle) {
  // Construct a minimal file with a block handle that claims to have
  // a huge size (2 GiB) but the actual file is tiny. This simulates
  // a malicious SSTable crafted to cause OOM during ReadBlock().
  
  // The 128 MiB limit is enforced in ReadBlock(), so test with size > 128 MiB
  const uint64_t kOversizeSize = 129ULL * 1024 * 1024;  // 129 MiB
  
  // Build a fake BlockHandle with offset=0 and size=129MB
  BlockHandle handle;
  handle.set_offset(0);
  handle.set_size(kOversizeSize);
  
  // Create a tiny fake file (the handle claims 129MB but file is only 10 bytes)
  std::string fake_contents = "small file";
  FakeFile fake_file(fake_contents);
  
  BlockContents result;
  Status s = ReadBlock(&fake_file, options_, handle, &result);
  
  // The read should fail with corruption, not crash or allocate huge memory
  ASSERT_TRUE(!s.ok());
  ASSERT_TRUE(s.IsCorruption());
  ASSERT_TRUE(s.ToString().find("size exceeds sanity limit") != std::string::npos)
      << "Expected error about size limit, got: " << s.ToString();
}

// Test that a block handle with size just above the limit is rejected
// (boundary condition test).
TEST_F(ReadBlockOomTest, RejectBlockHandleAboveLimit) {
  // Test with size = 128 MiB + 1 (should be rejected)
  const uint64_t kMaxSize = 128ULL * 1024 * 1024;
  BlockHandle over_limit_handle;
  over_limit_handle.set_offset(0);
  over_limit_handle.set_size(kMaxSize + 1);
  
  FakeFile fake_file("small");
  BlockContents result;
  Status s = ReadBlock(&fake_file, options_, over_limit_handle, &result);
  
  ASSERT_TRUE(!s.ok());
  ASSERT_TRUE(s.IsCorruption());
  ASSERT_TRUE(s.ToString().find("size exceeds sanity limit") != std::string::npos)
      << "Expected error about size limit, got: " << s.ToString();
}

// Test that normal-sized block handles work correctly (sanity check).
TEST_F(ReadBlockOomTest, NormalBlockHandleWorks) {
  // Build a valid small block
  const size_t kDataSize = 100;
  std::string data(kDataSize, 'x');
  
  // Build block trailer: compression type + crc
  char trailer[kBlockTrailerSize];
  trailer[0] = static_cast<char>(kNoCompression);
  uint32_t crc = crc32c::Value(data.data(), kDataSize);
  crc = crc32c::Mask(crc);
  EncodeFixed32(trailer + 1, crc);
  
  std::string contents = data;
  contents.append(trailer, kBlockTrailerSize);
  
  FakeFile fake_file(contents);
  BlockHandle handle;
  handle.set_offset(0);
  handle.set_size(kDataSize);
  
  BlockContents result;
  Status s = ReadBlock(&fake_file, options_, handle, &result);
  
  ASSERT_LEVELDB_OK(s);
  ASSERT_EQ(result.data.size(), kDataSize);
  ASSERT_TRUE(result.heap_allocated);
  ASSERT_TRUE(result.cachable);
}

// Test with a very large but still valid size (edge case).
TEST_F(ReadBlockOomTest, RejectExtremelyLargeBlockHandle) {
  // Test with size close to UINT64_MAX (malicious input)
  const uint64_t kHugeSize = ~static_cast<uint64_t>(0) - 1000;
  
  BlockHandle handle;
  handle.set_offset(0);
  handle.set_size(kHugeSize);
  
  FakeFile fake_file("tiny");
  
  BlockContents result;
  Status s = ReadBlock(&fake_file, options_, handle, &result);
  
  ASSERT_TRUE(!s.ok());
  ASSERT_TRUE(s.IsCorruption());
  ASSERT_TRUE(s.ToString().find("size exceeds sanity limit") != std::string::npos)
      << "Expected error about size limit, got: " << s.ToString();
}

}  // namespace leveldb
