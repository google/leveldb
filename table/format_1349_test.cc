// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "table/format.h"

#include <cstdint>
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

TEST(ReadBlockIssue1349Test, ReportsAllocationFailureForHugeRawRead) {
  // A handle whose raw-read buffer allocation cannot possibly succeed: the
  // failure must come back as a Status, not as std::bad_alloc escaping a
  // Status-returning interface.  Nothing is ever read from the file.
  BlockHandle handle;
  handle.set_offset(0);
  handle.set_size((size_t(1) << 60) - kBlockTrailerSize);
  StringSource1349 source("");
  ReadOptions options;
  BlockContents out;
  Status s = ReadBlock(&source, options, handle, &out);
  EXPECT_TRUE(s.IsCorruption()) << s.ToString();
  EXPECT_FALSE(out.heap_allocated);
}


TEST(ReadBlockIssue1349Test, AcceptsMaxAllowedWindowWithSmallOutput) {
#if HAVE_ZSTD
  // Deliberately overstate the history window to 2^30 (1 GiB), while
  // the actual decoded output is just 9 MiB. This must work within a
  // separately enforced 384 MiB address-space cap on the test process.
  std::string input(9 * 1024 * 1024, 'M'), payload;
  ASSERT_TRUE(port::Zstd_Compress(1, input.data(), input.size(), &payload));
  ASSERT_EQ(0, payload[4] & 0x20) << "expected a non-single-segment frame";
  payload[5] = static_cast<char>(0xa0);  // windowLog 30
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

TEST(ReadBlockIssue1349Test, RejectsWindowAboveConfiguredLimit) {
#if HAVE_ZSTD
  // A forged frame declares a window of 2^32, exceeding our chosen
  // streaming bound of 2^30. Reject without attempting multi-GiB memory.
  std::string input(9 * 1024 * 1024, 'N'), payload;
  ASSERT_TRUE(port::Zstd_Compress(1, input.data(), input.size(), &payload));
  ASSERT_EQ(0, payload[4] & 0x20) << "expected a non-single-segment frame";
  payload[5] = static_cast<char>(0xb0);  // windowLog 32
  BlockContents out;
  Status s = ReadPayload(payload, kZstdCompression, &out);
  EXPECT_TRUE(s.IsCorruption()) << s.ToString();
#else
  GTEST_SKIP() << "Zstd support unavailable";
#endif
}

#if HAVE_SNAPPY
// Appends the Snappy varint32 preamble (the claimed uncompressed length).
void AppendSnappyLength(std::string* dst, uint32_t v) {
  while (v >= 0x80) {
    dst->push_back(static_cast<char>((v & 0x7f) | 0x80));
    v >>= 7;
  }
  dst->push_back(static_cast<char>(v));
}

// Builds the densest possible valid stream: one literal byte followed by
// `copies` 3-byte copy tags (length 64, offset 1). It decodes to
// 1 + 64 * copies bytes from 2 + 3 * copies element bytes.
std::string DenseSnappyStream(uint32_t copies) {
  std::string s;
  AppendSnappyLength(&s, 1 + 64 * copies);
  s.push_back('\x00');  // Literal, length 1.
  s.push_back('d');
  for (uint32_t i = 0; i < copies; ++i) {
    s.push_back(static_cast<char>((63 << 2) | 2));  // Copy-2, length 64.
    s.push_back('\x01');                            // Offset 1, little-endian.
    s.push_back('\x00');
  }
  return s;
}
#endif  // HAVE_SNAPPY

TEST(ReadBlockIssue1349Test, AcceptsMaximallyDenseSnappyBelowFastPathLimit) {
#if HAVE_SNAPPY
  const std::string payload = DenseSnappyStream(1000);
  BlockContents out;
  Status s = ReadPayload(payload, kSnappyCompression, &out);
  ASSERT_TRUE(s.ok()) << s.ToString();
  ASSERT_EQ(1 + 64 * 1000u, out.data.size());
  EXPECT_EQ(std::string(out.data.size(), 'd'), out.data.ToString());
  if (out.heap_allocated) delete[] out.data.data();
#else
  GTEST_SKIP() << "Snappy support unavailable";
#endif
}

TEST(ReadBlockIssue1349Test, AcceptsMaximallyDenseSnappyAboveFastPathLimit) {
#if HAVE_SNAPPY
  // About 9 MiB decoded from about 432 KiB: ~21.3x, the format's maximum.
  const uint32_t copies = (9 * 1024 * 1024) / 64;
  const std::string payload = DenseSnappyStream(copies);
  BlockContents out;
  Status s = ReadPayload(payload, kSnappyCompression, &out);
  ASSERT_TRUE(s.ok()) << s.ToString();
  ASSERT_EQ(1 + 64 * static_cast<size_t>(copies), out.data.size());
  EXPECT_EQ(std::string(out.data.size(), 'd'), out.data.ToString());
  if (out.heap_allocated) delete[] out.data.data();
#else
  GTEST_SKIP() << "Snappy support unavailable";
#endif
}

TEST(ReadBlockIssue1349Test, SnappyExpansionBoundIsExact) {
#if HAVE_SNAPPY
  // An 8-byte payload whose preamble is 2 bytes for both claims below.
  // Claims up to 22 * 8 + 21 pass the O(1) bound (then fail decoding);
  // 22 * 9 is the first claim it rejects.
  for (uint32_t claim : {22u * 8 + 21, 22u * 9}) {
    std::string payload;
    AppendSnappyLength(&payload, claim);
    ASSERT_EQ(2u, payload.size());
    payload.append(6, '\x00');
    ASSERT_EQ(8u, payload.size());
    BlockContents out;
    Status s = ReadPayload(payload, kSnappyCompression, &out);
    ASSERT_TRUE(s.IsCorruption()) << claim << ": " << s.ToString();
    const bool rejected_as_length =
        s.ToString().find("block length") != std::string::npos;
    EXPECT_EQ(claim == 22u * 9, rejected_as_length)
        << claim << ": " << s.ToString();
    EXPECT_FALSE(out.heap_allocated);
  }
#else
  GTEST_SKIP() << "Snappy support unavailable";
#endif
}

TEST(ReadBlockIssue1349Test, RejectsImpossibleSnappyClaimBelowFastPathLimit) {
#if HAVE_SNAPPY
  // A 4 MiB claim backed by 8 bytes is under the validation threshold, so
  // only the expansion bound stops a 4 MiB allocation for it.
  std::string payload;
  AppendSnappyLength(&payload, 4 * 1024 * 1024);
  payload.append(4, '\x00');
  BlockContents out;
  Status s = ReadPayload(payload, kSnappyCompression, &out);
  ASSERT_TRUE(s.IsCorruption()) << s.ToString();
  EXPECT_NE(std::string::npos, s.ToString().find("block length"))
      << s.ToString();
  EXPECT_FALSE(out.heap_allocated);
#else
  GTEST_SKIP() << "Snappy support unavailable";
#endif
}

TEST(ReadBlockIssue1349Test, RejectsBlockHandleSizeOverflow) {
  // n + kBlockTrailerSize must not wrap around to a small allocation.
  BlockHandle handle;
  handle.set_offset(0);
  handle.set_size(~static_cast<uint64_t>(0) - 1);
  StringSource1349 source("");
  ReadOptions options;
  BlockContents out;
  Status s = ReadBlock(&source, options, handle, &out);
  ASSERT_TRUE(s.IsCorruption()) << s.ToString();
  EXPECT_NE(std::string::npos, s.ToString().find("bad block handle"))
      << s.ToString();
  EXPECT_FALSE(out.heap_allocated);
}

}  // namespace
}  // namespace leveldb
