// Copyright (c) 2026 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "table/block.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <string>

#include "gtest/gtest.h"
#include "leveldb/comparator.h"
#include "leveldb/iterator.h"
#include "table/format.h"
#include "util/coding.h"

namespace leveldb {

// Regression test for an integer overflow in DecodeEntry()'s bounds check
// (table/block.cc). DecodeEntry() used to validate an entry's key/value
// lengths with:
//
//   static_cast<uint32_t>(limit - p) < (*non_shared + *value_length)
//
// Since *non_shared and *value_length are both uint32_t, the addition
// wraps around in 32-bit arithmetic. An entry header encoding
// non_shared == UINT32_MAX and value_length == 1 sums to 0, so the
// (correctly small) "remaining bytes" count was never seen as
// insufficient and the corrupt entry was accepted. Block::Iter would
// then try to copy ~4GB of key data from just past the end of the block,
// reading (and potentially crashing on) memory well outside the block's
// buffer.
//
// The fix widens the comparison to uint64_t so the sum can no longer
// wrap. This test builds a single-entry block whose header decodes to
// shared=0, non_shared=UINT32_MAX, value_length=1, with zero bytes of
// key/value data actually present, and verifies the block iterator
// rejects the entry as corrupt instead of accepting it.
TEST(BlockTest, RejectsEntryWithOverflowingLengthSum) {
  std::string contents;

  // Entry header: shared, non_shared, value_length, each Varint32-encoded.
  // shared and value_length are individually < 128, but non_shared is not,
  // so DecodeEntry() takes the Varint32 decoding path (not the single-byte
  // fast path) for all three fields.
  PutVarint32(&contents, 0);                                     // shared
  PutVarint32(&contents, std::numeric_limits<uint32_t>::max());  // non_shared
  PutVarint32(&contents, 1);                                     // value_length

  // Deliberately omit the key/value bytes: the block ends immediately
  // after the header, so DecodeEntry() sees zero bytes remaining before
  // the restart array. A correct implementation must reject the entry
  // because non_shared + value_length (>= 4GB) vastly exceeds the 0
  // bytes actually available.

  // Restart array: a single restart point at offset 0, plus the restart
  // count trailer that Block::Block() requires to parse the block.
  PutFixed32(&contents, 0);  // restarts_[0]
  PutFixed32(&contents, 1);  // num_restarts

  BlockContents block_contents;
  block_contents.data = Slice(contents);
  block_contents.cachable = false;
  block_contents.heap_allocated = false;

  Block block(block_contents);
  std::unique_ptr<Iterator> iter(block.NewIterator(BytewiseComparator()));
  iter->SeekToFirst();

  // The wrapped-length entry must be rejected: the iterator should end up
  // invalid, with a corruption status, rather than exposing an
  // out-of-bounds key/value slice.
  EXPECT_FALSE(iter->Valid());
  EXPECT_TRUE(iter->status().IsCorruption());
}

}  // namespace leveldb

