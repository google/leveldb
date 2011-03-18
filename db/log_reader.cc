// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "db/log_reader.h"

#include <stdint.h>
#include "include/env.h"
#include "util/coding.h"
#include "util/crc32c.h"

namespace leveldb {
namespace log {

Reader::Reporter::~Reporter() {
}

Reader::Reader(SequentialFile* file, Reporter* reporter, bool checksum)
    : file_(file),
      reporter_(reporter),
      checksum_(checksum),
      backing_store_(new char[kBlockSize]),
      buffer_(),
      eof_(false) {
}

Reader::~Reader() {
  delete[] backing_store_;
}

bool Reader::ReadRecord(Slice* record, std::string* scratch) {
  scratch->clear();
  record->clear();
  bool in_fragmented_record = false;

  Slice fragment;
  while (true) {
    switch (ReadPhysicalRecord(&fragment)) {
      case kFullType:
        if (in_fragmented_record) {
          ReportDrop(scratch->size(), "partial record without end");
        }
        scratch->clear();
        *record = fragment;
        return true;

      case kFirstType:
        if (in_fragmented_record) {
          ReportDrop(scratch->size(), "partial record without end");
        }
        scratch->assign(fragment.data(), fragment.size());
        in_fragmented_record = true;
        break;

      case kMiddleType:
        if (!in_fragmented_record) {
          ReportDrop(fragment.size(), "missing start of fragmented record");
        } else {
          scratch->append(fragment.data(), fragment.size());
        }
        break;

      case kLastType:
        if (!in_fragmented_record) {
          ReportDrop(fragment.size(), "missing start of fragmented record");
        } else {
          scratch->append(fragment.data(), fragment.size());
          *record = Slice(*scratch);
          return true;
        }
        break;

      case kEof:
        if (in_fragmented_record) {
          ReportDrop(scratch->size(), "partial record without end");
          scratch->clear();
        }
        return false;

      case kBadRecord:
        if (in_fragmented_record) {
          ReportDrop(scratch->size(), "error in middle of record");
          in_fragmented_record = false;
          scratch->clear();
        }
        break;

      default:
        ReportDrop(
            (fragment.size() + (in_fragmented_record ? scratch->size() : 0)),
            "unknown record type");
        in_fragmented_record = false;
        scratch->clear();
        break;
    }
  }
  return false;
}

void Reader::ReportDrop(size_t bytes, const char* reason) {
  if (reporter_ != NULL) {
    reporter_->Corruption(bytes, Status::Corruption(reason));
  }
}

unsigned int Reader::ReadPhysicalRecord(Slice* result) {
  while (true) {
    if (buffer_.size() <= kHeaderSize) {
      if (!eof_) {
        // Last read was a full read, so this is a trailer to skip
        buffer_.clear();
        Status status = file_->Read(kBlockSize, &buffer_, backing_store_);
        if (!status.ok()) {
          if (reporter_ != NULL) {
            reporter_->Corruption(kBlockSize, status);
          }
          buffer_.clear();
          eof_ = true;
          return kEof;
        } else if (buffer_.size() < kBlockSize) {
          eof_ = true;
        }
        continue;
      } else if (buffer_.size() == 0) {
        // End of file
        return kEof;
      } else if (buffer_.size() < kHeaderSize) {
        ReportDrop(buffer_.size(), "truncated record at end of file");
        buffer_.clear();
        return kEof;
      } else {
        // We have a trailing zero-length record.  Fall through and check it.
      }
    }

    // Parse the header
    const char* header = buffer_.data();
    const uint32_t a = static_cast<uint32_t>(header[4]) & 0xff;
    const uint32_t b = static_cast<uint32_t>(header[5]) & 0xff;
    const unsigned int type = header[6];
    const uint32_t length = a | (b << 8);
    if (kHeaderSize + length > buffer_.size()) {
      ReportDrop(buffer_.size(), "bad record length");
      buffer_.clear();
      return kBadRecord;
    }

    // Check crc
    if (checksum_) {
      if (type == kZeroType && length == 0) {
        // Skip zero length record
        buffer_.remove_prefix(kHeaderSize + length);
        return kBadRecord;
      }

      uint32_t expected_crc = crc32c::Unmask(DecodeFixed32(header));
      uint32_t actual_crc = crc32c::Value(header + 6, 1 + length);
      if (actual_crc != expected_crc) {
        ReportDrop(length, "checksum mismatch");
        buffer_.remove_prefix(kHeaderSize + length);
        return kBadRecord;
      }
    }

    buffer_.remove_prefix(kHeaderSize + length);
    *result = Slice(header + kHeaderSize, length);
    return type;
  }
}

}
}
