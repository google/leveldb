#include "leveldb/dumpfile.h"

#include <algorithm>
#include <string>

#include "gtest/gtest.h"
#include "leveldb/env.h"

namespace leveldb {
namespace {

class NoopWritableFile : public WritableFile {
 public:
  Status Append(const Slice&) override { return Status::OK(); }
  Status Close() override { return Status::OK(); }
  Status Flush() override { return Status::OK(); }
  Status Sync() override { return Status::OK(); }
};

TEST(DumpFileTest, AcceptsWindowsPathSeparators) {
#if defined(LEVELDB_PLATFORM_WINDOWS)
  Env* env = Env::Default();
  const std::string filename = testing::TempDir() + "000001.log";
  WritableFile* file;
  ASSERT_TRUE(env->NewWritableFile(filename, &file).ok());
  delete file;

  std::string windows_filename = filename;
  std::replace(windows_filename.begin(), windows_filename.end(), '/', '\\');

  NoopWritableFile sink;
  ASSERT_TRUE(DumpFile(env, windows_filename, &sink).ok());

  ASSERT_TRUE(env->RemoveFile(filename).ok());
#endif
}

}
}  // namespace leveldb