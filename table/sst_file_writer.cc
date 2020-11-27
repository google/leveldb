//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).

// Copyright (c) 2020 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "leveldb/sst_file_writer.h"

#include "db/write_batch_internal.h"
#include "leveldb/table_builder.h"

namespace leveldb {

// Note: Originally copied from RocksDB(https://github.com/facebook/rocksdb)
struct SstFileWriter::Rep {
  Rep(const Options& options, const Comparator* user_comparator)
      : options(options), internal_comparator(user_comparator) {
        this->options.comparator = &internal_comparator;
      }

  std::unique_ptr<WritableFile> file;
  std::unique_ptr<TableBuilder> builder;
  Options options;
  InternalKeyComparator internal_comparator;
  ExternalSstFileInfo file_info;
  InternalKey ikey;

  Status Add(const Slice& user_key, const Slice& value,
             const ValueType value_type) {
    if (!builder) {
      return Status::InvalidArgument("File is not opened");
    }
    switch (value_type) {
      case ValueType::kTypeValue:
        ikey.Set(user_key, 0 /* Sequence Number */,
                 ValueType::kTypeValue /* Put */);
        break;
      case ValueType::kTypeDeletion:
        ikey.Set(user_key, 0 /* Sequence Number */,
                 ValueType::kTypeDeletion /* Delete */);
        break;
      default:
        return Status::InvalidArgument("Value type is not supported");
    }
    if (file_info.num_entries == 0) {
      file_info.smallest_internal_key.assign(ikey.Encode().data(),
                                             ikey.Encode().size());
    } else {
      if (internal_comparator.Compare(ikey.Encode(),
                                      file_info.largest_internal_key) <= 0) {
        // Make sure that keys are added in order
        return Status::InvalidArgument(
            "Keys must be added in strict ascending order.");
      }
    }

    builder->Add(ikey.Encode(), value);

    // update file info
    file_info.num_entries++;
    file_info.largest_internal_key.assign(ikey.Encode().data(),
                                          ikey.Encode().size());
    file_info.file_size = builder->FileSize();

    return Status::OK();
  }
};

SstFileWriter::SstFileWriter(const Options& options)
    : rep_(new Rep(options, options.comparator)) {
  rep_->file_info.file_size = 0;
}

SstFileWriter::~SstFileWriter() {
  if (rep_->builder) {
    // User did not call Finish() or Finish() failed, we need to
    // abandon the builder.
    rep_->builder->Abandon();
  }
}

Status SstFileWriter::Open(const std::string& file_path) {
  Rep* r = rep_.get();
  Status s;
  WritableFile* sst_file;
  s = r->options.env->NewWritableFile(file_path, &sst_file);
  if (!s.ok()) {
    return s;
  }
  r->file.reset(sst_file);
  r->builder.reset(new TableBuilder(r->options, sst_file));
  r->file_info = ExternalSstFileInfo();
  r->file_info.file_path = file_path;
  return s;
}

Status SstFileWriter::Put(const Slice& user_key, const Slice& value) {
  return rep_->Add(user_key, value, ValueType::kTypeValue);
}

Status SstFileWriter::Delete(const Slice& user_key) {
  return rep_->Add(user_key, Slice(), ValueType::kTypeDeletion);
}

Status SstFileWriter::Finish(ExternalSstFileInfo* file_info) {
  Rep* r = rep_.get();
  if (!r->builder) {
    return Status::InvalidArgument("File is not opened");
  }
  if (r->file_info.num_entries == 0) {
    return Status::InvalidArgument("Cannot create sst file with no entries");
  }

  Status s = r->builder->Finish();
  r->file_info.file_size = r->builder->FileSize();

  if (s.ok()) {
    s = r->file->Sync();
    if (s.ok()) {
      s = r->file->Close();
    }
  }
  if (!s.ok()) {
    r->options.env->DeleteFile(r->file_info.file_path);
  }

  if (file_info != nullptr) {
    *file_info = r->file_info;
  }

  r->builder.reset();
  return s;
}

uint64_t SstFileWriter::FileSize() const { return rep_->file_info.file_size; }

}  // namespace leveldb
