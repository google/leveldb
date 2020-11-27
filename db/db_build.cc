// Copyright (c) 2020 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "db/filename.h"
#include "db/log_writer.h"
#include "db/version_edit.h"
#include "leveldb/db.h"

namespace leveldb {

struct IngestedFileInfo {
  // External file path
  std::string external_file_path;
  // Smallest internal key in external file
  InternalKey smallest_internal_key;
  // Largest internal key in external file
  InternalKey largest_internal_key;
  // External file size
  uint64_t file_size;
  // File number inside the DB
  uint64_t file_number = 0;
  // total number of keys in external file
  uint64_t num_entries;
  // file path that we picked for file inside the DB
  std::string internal_file_path;

  IngestedFileInfo() = delete;

  explicit IngestedFileInfo(const ExternalSstFileInfo& file_info)
      : external_file_path(file_info.file_path),
        file_size(file_info.file_size),
        num_entries(file_info.num_entries) {
    smallest_internal_key.DecodeFrom(file_info.smallest_internal_key);
    largest_internal_key.DecodeFrom(file_info.largest_internal_key);
  }
};

static Status CheckFileOverlapping(
    const Comparator* cmp,
    const std::vector<IngestedFileInfo>& files_to_ingest) {
  size_t num_files = files_to_ingest.size();
  std::vector<const IngestedFileInfo*> sorted_files;
  for (size_t i = 0; i < num_files; i++) {
    sorted_files.push_back(&files_to_ingest[i]);
  }
  std::sort(
      sorted_files.begin(), sorted_files.end(),
      [&cmp](const IngestedFileInfo* info1, const IngestedFileInfo* info2) {
        return cmp->Compare(info1->smallest_internal_key.user_key(),
                            info2->smallest_internal_key.user_key()) < 0;
      });
  for (size_t i = 0; i + 1 < num_files; i++) {
    if (cmp->Compare(sorted_files[i]->largest_internal_key.user_key(),
                     sorted_files[i + 1]->smallest_internal_key.user_key()) >=
        0) {
      return Status::NotSupported("Files have overlapping ranges");
    }
  }
  return Status::OK();
}

static Status LogToManifest(const std::string& dbname, const Options& options,
                            const VersionEdit& edit) {
  std::string manifest = DescriptorFileName(dbname, 1);
  WritableFile* file;
  Status status = options.env->NewWritableFile(manifest, &file);
  if (!status.ok()) {
    return status;
  }
  log::Writer log(file);
  std::string record;
  edit.EncodeTo(&record);
  status = log.AddRecord(record);
  if (status.ok()) {
    status = file->Close();
  }
  delete file;
  file = nullptr;
  if (!status.ok()) {
    options.env->RemoveFile(manifest);
  } else {
    status = SetCurrentFile(options.env, dbname, 1);
  }

  return status;
}

Status BuildDBFromSstFiles(
    const Options& options, const std::string& dbname,
    const std::vector<ExternalSstFileInfo>& external_files) {
  Status status;

  // Ignore error in case dir already exist
  options.env->CreateDir(dbname);

  {
    std::vector<std::string> children;
    status = options.env->GetChildren(dbname, &children);
    if (!status.ok()) {
      return status;
    }
    uint64_t number;
    FileType type;
    for (const auto& s : children) {
      if (ParseFileName(s, &number, &type)) {
        return Status::InvalidArgument("DB directory is not empty");
      }
    }
  }

  if (external_files.size() == 0) {
    return Status::InvalidArgument("The list of files is empty");
  }
  for (const auto& f : external_files) {
    if (f.num_entries == 0) {
      return Status::InvalidArgument("File contain no entries");
    }
  }

  std::vector<IngestedFileInfo> files_to_ingest;
  for (const auto& external_file_info : external_files) {
    files_to_ingest.emplace_back(external_file_info);
  }
  // We don't allow overlapping ranges in these files
  status = CheckFileOverlapping(options.comparator, files_to_ingest);
  if (!status.ok()) {
    return status;
  }

  uint64_t next_file_number = 1;
  uint64_t files_total_size = 0;
  // Assign file numbers and move them into database directory
  // (only doable if they are in the same FS)
  for (IngestedFileInfo& f : files_to_ingest) {
    f.file_number = next_file_number++;
    f.internal_file_path = TableFileName(dbname, f.file_number);
    files_total_size += f.file_size;
    status = options.env->LinkFile(f.external_file_path, f.internal_file_path);
    if (!status.ok()) {
      break;
    }
  }

  if (status.ok()) {
    status = options.env->SyncDir(dbname);
  }

  if (status.ok()) {
    int picked_level = 2;
    uint64_t max_bytes_in_picked_level = 10 * (10 << 20);
    while (picked_level < (config::kNumLevels - 1) &&
           files_total_size > max_bytes_in_picked_level) {
      picked_level++;
      max_bytes_in_picked_level *= 10;
    }

    VersionEdit version_edit;
    version_edit.SetComparatorName(options.comparator->Name());
    version_edit.SetLogNumber(0);
    version_edit.SetNextFile(next_file_number);
    version_edit.SetLastSequence(0);

    for (IngestedFileInfo& f : files_to_ingest) {
      version_edit.AddFile(picked_level, f.file_number, f.file_size,
                           f.smallest_internal_key, f.largest_internal_key);
    }
    status = LogToManifest(dbname, options, version_edit);
  }

  if (!status.ok()) {
    // We failed, remove all files that we copied into the db
    for (const auto& f : files_to_ingest) {
      if (!f.internal_file_path.empty()) {
        options.env->RemoveFile(f.internal_file_path);
      }
    }
  } else {
    // The files were moved and added successfully, remove original file links
    for (const auto& f : files_to_ingest) {
      options.env->RemoveFile(f.external_file_path);
    }
  }
  return status;
}

}  // namespace leveldb
