// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "table/format.h"

#include <cstring>
#include <string>

#include "gtest/gtest.h"
#include "leveldb/env.h"
#include "leveldb/options.h"
#include "port/port.h"

namespace leveldb {
namespace {

class StringSource1349 : public RandomAccessFile {
 public:
  explicit StringSource1349(std::string data) : data_(std::move(data)) {}
  Status Read(uint64_t offset, size_t n, Slice* result,
              char* scratch) const override {
    if (offset > data_.size()) return Status::IOError("offset");
    const size_t available = data_.size() - static_cast<size_t>(offset);
    const size_t take = n < available ? n : available;
    std::memcpy(scratch, data_.data() + offset, take);
    *result = Slice(scratch, take);
    return Status::OK();
  }
 private:
  std::string data_;
};

Status ReadPayload(const std::string& payload, CompressionType type,
                   BlockContents* out) {
  std::string block = payload;
  const size_t n = block.size();
  block.push_back(static_cast<char>(type));
  block.append(4, '\0');
  StringSource1349 source(block);
  BlockHandle handle;
  handle.set_offset(0);
  handle.set_size(n);
  ReadOptions options;
  options.verify_checksums = false;
  return ReadBlock(&source, options, handle, out);
}

TEST(ReadBlockIssue1349Test, RejectsHugeMalformedSnappyBeforeAllocation) {
#if HAVE_SNAPPY
  std::string payload;
  for (unsigned char c : {0xfe, 0xff, 0xff, 0xff, 0x0f})
    payload.push_back(static_cast<char>(c));
  BlockContents out;
  Status s = ReadPayload(payload, kSnappyCompression, &out);
  EXPECT_TRUE(s.IsCorruption()) << s.ToString();
#else
  GTEST_SKIP() << "Snappy support unavailable";
#endif
}

TEST(ReadBlockIssue1349Test, RejectsHugeMalformedZstdBeforeAllocation) {
#if HAVE_ZSTD
  const unsigned char raw[] = {0x28,0xb5,0x2f,0xfd,0xa0,0xfe,
                               0xff,0xff,0xff,0x01,0x00,0x00};
  std::string payload(reinterpret_cast<const char*>(raw), sizeof(raw));
  BlockContents out;
  Status s = ReadPayload(payload, kZstdCompression, &out);
  EXPECT_TRUE(s.IsCorruption()) << s.ToString();
#else
  GTEST_SKIP() << "Zstd support unavailable";
#endif
}

TEST(ReadBlockIssue1349Test, PreservesValidSnappyFastPath) {
#if HAVE_SNAPPY
  std::string input(256 * 1024, 'S'), payload;
  ASSERT_TRUE(port::Snappy_Compress(input.data(), input.size(), &payload));
  BlockContents out;
  Status s = ReadPayload(payload, kSnappyCompression, &out);
  ASSERT_TRUE(s.ok()) << s.ToString();
  EXPECT_EQ(input, out.data.ToString());
  if (out.heap_allocated) delete[] out.data.data();
#else
  GTEST_SKIP() << "Snappy support unavailable";
#endif
}

TEST(ReadBlockIssue1349Test, PreservesValidZstdFastPath) {
#if HAVE_ZSTD
  std::string input(256 * 1024, 'Z'), payload;
  ASSERT_TRUE(port::Zstd_Compress(1, input.data(), input.size(), &payload));
  BlockContents out;
  Status s = ReadPayload(payload, kZstdCompression, &out);
  ASSERT_TRUE(s.ok()) << s.ToString();
  EXPECT_EQ(input, out.data.ToString());
  if (out.heap_allocated) delete[] out.data.data();
#else
  GTEST_SKIP() << "Zstd support unavailable";
#endif
}

TEST(ReadBlockIssue1349Test, PreservesValidSnappyFallback) {
#if HAVE_SNAPPY
  std::string input(9 * 1024 * 1024, 'L'), payload;
  ASSERT_TRUE(port::Snappy_Compress(input.data(), input.size(), &payload));
  BlockContents out;
  Status s = ReadPayload(payload, kSnappyCompression, &out);
  ASSERT_TRUE(s.ok()) << s.ToString();
  EXPECT_EQ(input.size(), out.data.size());
  EXPECT_EQ(0, std::memcmp(input.data(), out.data.data(), input.size()));
  if (out.heap_allocated) delete[] out.data.data();
#else
  GTEST_SKIP() << "Snappy support unavailable";
#endif
}

TEST(ReadBlockIssue1349Test, PreservesValidZstdFallback) {
#if HAVE_ZSTD
  std::string input(9 * 1024 * 1024, 'Q'), payload;
  ASSERT_TRUE(port::Zstd_Compress(1, input.data(), input.size(), &payload));
  BlockContents out;
  Status s = ReadPayload(payload, kZstdCompression, &out);
  ASSERT_TRUE(s.ok()) << s.ToString();
  EXPECT_EQ(input.size(), out.data.size());
  EXPECT_EQ(0, std::memcmp(input.data(), out.data.data(), input.size()));
  if (out.heap_allocated) delete[] out.data.data();
#else
  GTEST_SKIP() << "Zstd support unavailable";
#endif
}

TEST(ReadBlockIssue1349Test, AcceptsLargeWindowZstdFrame) {
#if HAVE_ZSTD
  std::string input(9 * 1024 * 1024, 'W'), payload;
  ASSERT_TRUE(port::Zstd_Compress(1, input.data(), input.size(), &payload));
  // Rewrite the window descriptor to declare a 256 MiB window.  The frame's
  // compressed blocks are unchanged, and the one-pass decompressor this
  // validation guards accepts it - so the pre-validation probe must not
  // reject it either.
  ASSERT_EQ(0, payload[4] & 0x20) << "expected a non-single-segment frame";
  payload[5] = static_cast<char>(0x90);
  BlockContents out;
  Status s = ReadPayload(payload, kZstdCompression, &out);
  ASSERT_TRUE(s.ok()) << s.ToString();
  EXPECT_EQ(input.size(), out.data.size());
  EXPECT_EQ(0, std::memcmp(input.data(), out.data.data(), input.size()));
  if (out.heap_allocated) delete[] out.data.data();
#else
  GTEST_SKIP() << "Zstd support unavailable";
#endif
}

TEST(ReadBlockIssue1349Test, AcceptsTrailingSkippableZstdFrame) {
#if HAVE_ZSTD
  std::string input(9 * 1024 * 1024, 'X'), payload;
  ASSERT_TRUE(port::Zstd_Compress(1, input.data(), input.size(), &payload));
  // A zero-payload skippable frame behind the data frame adds no decoded
  // bytes; one-pass decoding ignores it, so the probe must as well.
  const unsigned char skippable[] = {0x50, 0x2a, 0x4d, 0x18,
                                     0x00, 0x00, 0x00, 0x00};
  payload.append(reinterpret_cast<const char*>(skippable), sizeof(skippable));
  BlockContents out;
  Status s = ReadPayload(payload, kZstdCompression, &out);
  ASSERT_TRUE(s.ok()) << s.ToString();
  EXPECT_EQ(input.size(), out.data.size());
  EXPECT_EQ(0, std::memcmp(input.data(), out.data.data(), input.size()));
  if (out.heap_allocated) delete[] out.data.data();
#else
  GTEST_SKIP() << "Zstd support unavailable";
#endif
}

TEST(ReadBlockIssue1349Test, RejectsFrameSequenceBeyondDeclaredSize) {
#if HAVE_ZSTD
  std::string input(9 * 1024 * 1024, 'Y'), payload;
  ASSERT_TRUE(port::Zstd_Compress(1, input.data(), input.size(), &payload));
  // Appending a second data frame makes the sequence produce more bytes
  // than the first frame header declares; whatever rejected it before must
  // keep rejecting it, now by the output budget as well.
  std::string tail(100, 'z'), tail_payload;
  ASSERT_TRUE(port::Zstd_Compress(1, tail.data(), tail.size(), &tail_payload));
  payload += tail_payload;
  BlockContents out;
  Status s = ReadPayload(payload, kZstdCompression, &out);
  EXPECT_TRUE(s.IsCorruption()) << s.ToString();
#else
  GTEST_SKIP() << "Zstd support unavailable";
#endif
}

}  // namespace
}  // namespace leveldb
