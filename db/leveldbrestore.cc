#include "db/dbformat.h"
#include <inttypes.h>
#include <leveldb/db.h>
#include <leveldb/env.h>

#include "leveldb/table.h"

#include "port/port.h"
#include "table/block.h"
#include "table/format.h"

#include "filename.h"

namespace leveldb {

namespace {
leveldb::Env* g_env = nullptr;
};

// From dumpfile.cc
bool GuessType(const std::string& fname, FileType* type) {
  size_t pos = fname.rfind('/');
  std::string basename;
  if (pos == std::string::npos) {
    basename = fname;
  } else {
    basename = std::string(fname.data() + pos + 1, fname.size() - pos - 1);
  }
  uint64_t ignored;
  return ParseFileName(basename, &ignored, type);
}

}  // namespace leveldb

int main(int argc, char** argv) {
  struct Args {
    std::string db;
    std::string out;
    int use_repair = 0;
    int use_open = 2;
    bool use_table = true;
    int lost = 1;
    bool use_block = true;
    int max_block_size = 50000;
    int skip = false;
    bool allow_delete = true;
    int verbose = 1;
  } args;

  if (argc <= 1) {
    std::fprintf(stderr, R"(OPTIONS:
    --db=%s
        Try restore db with the following name.
    --out=%s
        Write restored data to new db.
    --use_repair=%d
        WILL WORK ON YOUR ORIGINAL --db AND CAN DESTROY DATA
        Try standard repair on open.
        0: disable
        1: repair
        2: repair with paranoid checks
    --use_open=%d
        0: disable
        1: Try open base first. Disable if you have already repaired but almost empy db (all data moved to lost dir)
        2: open with paranoid checks
    --use_table=%d
        Try use table reader. Fast and works on undamaged .ldb files
    --use_block=%d
        Try use block reader. Slowly find blocks by guessing start point and length, match by crc
    --max_block_size=%u
        Try find data block with maximum size. Smaller size = faster work
    --skip=%u
        1: Skip not 0,1 end bytes on seek (Blocks always ends with 0,1 bytes?)
        2: Skip not 0 start bytes (will skip all compressed blocks) - greatly improves speed
    --allow_delete=%d
        Allow delete keys
    --lost=%d
        0: Ignore lost dir
        1: Read lost dir
        2: Read ONLY lost dir
    --verbose=%d
        Print more debug data


)",
                 args.db.c_str(),
                 args.out.c_str(),
                 args.use_repair,
                 args.use_open,
                 args.use_table,
                 args.use_block,
                 args.max_block_size,
                 args.skip,
                 args.allow_delete,
                 args.lost,
                 args.verbose);
  }

  for (int i = 1; i < argc; i++) {
    int n;
    char junk;
    if (strncmp(argv[i], "--db=", 5) == 0) {
      args.db = argv[i] + 5;
    } else if (strncmp(argv[i], "--out=", 6) == 0) {
      args.out = argv[i] + 6;
    } else if (sscanf(argv[i], "--use_repair=%d%c", &n, &junk) == 1) {
      args.use_repair = n;
    } else if (sscanf(argv[i], "--use_open=%d%c", &n, &junk) == 1) {
      args.use_open = n;
    } else if (sscanf(argv[i], "--use_table=%d%c", &n, &junk) == 1 &&
               (n == 0 || n == 1)) {
      args.use_table = n;
    } else if (sscanf(argv[i], "--use_block=%d%c", &n, &junk) == 1 &&
               (n == 0 || n == 1)) {
      args.use_block = n;
    } else if (sscanf(argv[i], "--skip=%d%c", &n, &junk) == 1) {
      args.skip = n;
    } else if (sscanf(argv[i], "--max_block_size=%d%c", &n, &junk) == 1) {
      args.max_block_size = n;
    } else if (sscanf(argv[i], "--allow_delete=%d%c", &n, &junk) == 1 &&
               (n == 0 || n == 1)) {
      args.allow_delete = n;
    } else if (sscanf(argv[i], "--lost=%d%c", &n, &junk) == 1) {
      args.lost = n;
    } else if (sscanf(argv[i], "--verbose=%d%c", &n, &junk) == 1) {
      args.verbose = n;
    } else {
      std::fprintf(stderr, "Invalid flag '%s'\n", argv[i]);
      std::exit(1);
    }
  }
#ifndef NDEBUG
  if (args.use_block)
    std::fprintf(stderr, "Built in debug mode. Block reader Can be slow\n");
#endif

#if !HAVE_SNAPPY
    // TODO: why always true?
    // std::fprintf(stderr, "Built without snappy. Compressed data will not read!\n");
#endif

  leveldb::g_env = leveldb::Env::Default();

  if (args.db.empty()) {
    std::fprintf(stderr, "Required flag --db=xxx\n");
    std::exit(1);
  }

  if (args.out.empty()) {
    args.out = args.db + "_restored";
  }

  if (args.use_repair) {
    leveldb::Options options;
    if (args.use_repair >= 2)
      options.paranoid_checks = 1;
    options.create_if_missing = 0;
    std::fprintf(stderr, "Trying repair [%s]\n", args.db.c_str());
    auto status = leveldb::RepairDB(args.db, options);
    std::fprintf(stderr, "Repair [%s] is [%s]\n", args.db.c_str(), status.ToString().c_str());
  }

  if (args.use_open) {
    leveldb::DB* db = nullptr;
    leveldb::Options options;
    if (args.use_open >= 2)
      options.paranoid_checks = 1;
    auto status = leveldb::DB::Open(options, args.db, &db);

    if (status.ok()) {
      std::fprintf(stderr, "Db [%s] is [%s]\n", args.db.c_str(), status.ToString().c_str());
      std::exit(1);
    }
    std::fprintf(stderr, "Open try fail: '%s' trying restore to [%s]...\n", status.ToString().c_str(), args.out.c_str());
  }

  auto env = leveldb::g_env;
  struct filenames_store {
    std::string name;
    std::string full;
  };
  std::vector<filenames_store> filenames;
  std::vector<std::string> filenames_temp;
  if (args.lost >= 1) {
    auto result =
        leveldb::g_env->GetChildren(args.db + "/" + "lost", &filenames_temp);
    if (result.ok()) {
      for (auto& i : filenames_temp)
        filenames.push_back({i, args.db + "/lost/" + i});
    }
  }
  if (args.lost != 2) {
    auto result = leveldb::g_env->GetChildren(args.db, &filenames_temp);
    if (result.ok()) {
      for (auto& i : filenames_temp)
        filenames.push_back({i, args.db + "/" + i});
    }
  }

  std::sort(
      filenames.begin(), filenames.end(),
      [](filenames_store& a, filenames_store& b) { return a.name < b.name; });

  leveldb::DB* db = nullptr;
  leveldb::Options options;
  leveldb::WriteOptions write_options;
  options.create_if_missing = true;
  options.compression = leveldb::kNoCompression;  // improve compatibility of result base
  auto status = leveldb::DB::Open(options, args.out, &db);

  for (auto& f : filenames) {
    const auto& fname = f.full;

    leveldb::FileType ftype;
    if (!GuessType(fname, &ftype)) {
      std::fprintf(stderr, "Skip file [%s]\n", fname.c_str());
      continue;
    }

    switch (ftype) {
      case leveldb::kLogFile:
        // return DumpLog(env, fname, dst);
        // TODO!!!! ^^^^
        break;
      case leveldb::kTableFile: {
        size_t keys_read = 0, data_read = 0;

        uint64_t file_size = 0;
        auto s = env->GetFileSize(fname, &file_size);
        if (!s.ok()) {
          std::fprintf(stderr, "Filesize fail [%s]\n", s.ToString().c_str());
          continue;
        }

        std::fprintf(stderr, "file [%s] size [%" PRIu64 "]\n", fname.c_str(), file_size);
        if (!file_size) continue;

        leveldb::RandomAccessFile* file = nullptr;
        s = env->NewRandomAccessFile(fname, &file);
        if (!s.ok()) {
          std::fprintf(stderr, "new file fail [%s]\n", s.ToString().c_str());
          continue;
        }

        if (args.use_table) {
          // Based on dumpfile.cc : DumpTable
          leveldb::Table* table = nullptr;
          s = leveldb::Table::Open(leveldb::Options(), file, file_size, &table);
          if (s.ok()) {
            leveldb::ReadOptions ro;
            ro.fill_cache = false;
            leveldb::Iterator* iter = table->NewIterator(ro);
            std::string r;
            for (iter->SeekToFirst(); iter->Valid(); iter->Next()) {
              r.clear();
              leveldb::ParsedInternalKey key;
              if (!ParseInternalKey(iter->key(), &key)) {
                r = "badkey '";
                AppendEscapedStringTo(&r, iter->key());
                r += "' => '";
                AppendEscapedStringTo(&r, iter->value());
                r += "'\n";
                std::fprintf(stderr, "ParsedInternalKey error '%s'\n", r.c_str());
                continue;
              }

              r = "'";
              AppendEscapedStringTo(&r, key.user_key);
              r += "' @ ";
              leveldb::AppendNumberTo(&r, key.sequence);
              r += " : ";
              if (key.type == leveldb::kTypeDeletion) {
                r += "del";
                if (args.allow_delete) {
                  auto status = db->Delete(write_options, key.user_key);
                }
              } else if (key.type == leveldb::kTypeValue) {
                r += "val";
                auto status = db->Put(write_options, key.user_key, iter->value());
                ++keys_read;
                data_read += iter->value().size();
              } else {
                AppendNumberTo(&r, key.type);
              }
              r += " => '";
              // AppendEscapedStringTo(&r, iter->value());
              leveldb::AppendNumberTo(&r, iter->value().size());
              // std::fprintf(stderr, "READ '%s'\n", r.c_str());
            }
            delete iter;
            delete table;
            std::fprintf(stderr, "table read keys=%lu data size=%lu\n", keys_read, data_read);
            continue;

          } else {
            delete table;
          }
          std::fprintf(stderr, "open file fail [%s]\n", s.ToString().c_str());
        }

        if (args.use_block) {
          char scratch[100];
          char* fill_file_buf = new char[file_size];
          leveldb::Slice full_file_slice;
          s = file->Read(0, file_size, &full_file_slice, fill_file_buf);
          leveldb::Slice handle_value;
          leveldb::BlockHandle handle;
          size_t last_good = 0;
          for (size_t offset = 0; offset < file_size - leveldb::kBlockTrailerSize; ++offset) {
            unsigned char chr = full_file_slice[offset];
            if (args.verbose >= 3)
              std::fprintf(stderr, "offset=%lu/%" PRIu64 " [%c]\t%0X\t%d\n", offset, file_size, chr >= 0x20 ? chr : '?', chr, chr);
            // Blocks starts only from 0 byte for uncompressed? but any byte for compressed
            if (args.skip >= 2 && chr > 0)
              continue;

            handle.set_offset(offset);
            // TODO: better increment, maybe read file and find some
            // pattern or faster forward on a-z0-9...? symbols?
            for (size_t sz = leveldb::kBlockTrailerSize; offset + sz < file_size - leveldb::kBlockTrailerSize && sz < args.max_block_size; ++sz) {
              unsigned char chr_seek = full_file_slice[offset + sz];
              // Blocks ends only with 0 and 1 byte?
              if (args.skip >= 1 && chr_seek > 1)
                continue;

              if (args.verbose >= 4)
                std::fprintf(stderr, "seek=%lu = %lu/%" PRIu64 " [%c]\t%0X\t%d\n", sz, offset + sz, file_size, chr_seek >= 0x20 ? chr_seek : '?', chr_seek, chr_seek);
              handle.set_size(sz);
              // std::fprintf(stderr, "handle read size=%lu offset=%lu\n", handle.size(), handle.offset());

              leveldb::ReadOptions ropt;
              ropt.verify_checksums = 1;
              leveldb::BlockContents block_contents;
              s = leveldb::ReadBlock(file, ropt, handle, &block_contents);
              if (!s.ok()) {
                // std::fprintf(stderr, "readblock fail [%s]\n", s.ToString().c_str());
                continue;
              }
              auto skipped = offset - last_good;
              if (skipped > 0) {
                std::fprintf(stderr, "Skipped bytes %lu ", offset - last_good);
                if (skipped > args.max_block_size) {
                  std::fprintf(stderr, " Maybe use --max_block_size=%lu ", skipped);
                }
                std::fprintf(stderr, "\n");
              }
              std::fprintf(stderr, "found block at offset=%lu size=%lu first byte: [%c]\t%0X\t%d\t\tlast byte: [%c]\t%0X\t%d\n", offset, sz, chr >= 0x20 ? chr : '?', chr, chr, chr_seek >= 0x20 ? chr_seek : '?', chr_seek, chr_seek);
              leveldb::Block block(block_contents);
              if (args.verbose >= 1)
                std::fprintf(stderr, "Read block size=%lu\n", block.size());
              // std::fprintf(stderr, "read [%s]\n", block.data.ToString().c_str());
              auto iter = block.NewIterator(leveldb::BytewiseComparator());
              for (iter->SeekToFirst(); iter->Valid(); iter->Next()) {
                // std::fprintf(stderr, "iter valid=%lu status=[%s]\n", iter->Valid(), iter->status().ToString().c_str());

                leveldb::ParsedInternalKey key;
                if (!ParseInternalKey(iter->key(), &key)) {
                  std::fprintf(stderr, "ParsedInternalKey error [%s] on [%s]->[%s]\n", key.DebugString().c_str(), iter->key().ToString().c_str(), iter->value().ToString().c_str());
                  continue;
                }

                if (key.type == leveldb::kTypeDeletion) {
                  if (args.allow_delete) {
                    auto status = db->Delete(write_options, key.user_key);
                  }
                } else if (key.type == leveldb::kTypeValue) {
                  auto status = db->Put(write_options, key.user_key, iter->value());
                  if (args.verbose >= 2)
                    std::fprintf(stderr,
                                 "iter valid=%u type=%u seq=%" PRIu64
                                 " key=(%zu)[%s], "
                                 "value=(%zu)[...]\n",
                                 iter->Valid(), key.type, key.sequence,
                                 key.user_key.size(), key.user_key.ToString().c_str(), iter->value().size()
                                 /*,iter->value().ToString().c_str()*/);
                  ++keys_read;
                  data_read += iter->value().size();
                } else {
                  std::fprintf(stderr, "Unknown key type [%d] on [%s]->[%s]\n", key.type, key.user_key.ToString().c_str(), iter->value().ToString().c_str());
                }
              }
              delete iter;
              offset += sz + leveldb::kBlockTrailerSize - 1;
              last_good = offset + 1;
              break;
            }
          }
          std::fprintf(stderr, "block read keys=%lu data size=%lu\n", keys_read, data_read);
        }
      }
      default:
        break;
    }
  }
  delete db;
  db = nullptr;
  return 0;
}
