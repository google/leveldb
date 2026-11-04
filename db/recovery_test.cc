// Copyright (c) 2014 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "gtest/gtest.h"
#include "db/db_impl.h"
#include "db/filename.h"
#include "db/version_set.h"
#include "db/write_batch_internal.h"
#include "leveldb/db.h"
#include "leveldb/env.h"
#include "leveldb/write_batch.h"
#include "util/logging.h"
#include "util/testutil.h"

namespace leveldb {

class RecoveryTest : public testing::Test {
 public:
  RecoveryTest() : env_(Env::Default()), db_(nullptr) {
    dbname_ = testing::TempDir() + "recovery_test";
    DestroyDB(dbname_, Options());
    Open();
  }

  ~RecoveryTest() {
    Close();
    DestroyDB(dbname_, Options());
  }

  DBImpl* dbfull() const { return reinterpret_cast<DBImpl*>(db_); }
  Env* env() const { return env_; }

  bool CanAppend() {
    WritableFile* tmp;
    Status s = env_->NewAppendableFile(CurrentFileName(dbname_), &tmp);
    delete tmp;
    if (s.IsNotSupportedError()) {
      return false;
    } else {
      return true;
    }
  }

  void Close() {
    delete db_;
    db_ = nullptr;
  }

  Status OpenWithStatus(Options* options = nullptr) {
    Close();
    Options opts;
    if (options != nullptr) {
      opts = *options;
    } else {
      opts.reuse_logs = true;  // TODO(sanjay): test both ways
      opts.create_if_missing = true;
    }
    if (opts.env == nullptr) {
      opts.env = env_;
    }
    return DB::Open(opts, dbname_, &db_);
  }

  void Open(Options* options = nullptr) {
    ASSERT_LEVELDB_OK(OpenWithStatus(options));
    ASSERT_EQ(1, NumLogs());
  }

  Status Put(const std::string& k, const std::string& v) {
    return db_->Put(WriteOptions(), k, v);
  }

  std::string Get(const std::string& k, const Snapshot* snapshot = nullptr) {
    std::string result;
    Status s = db_->Get(ReadOptions(), k, &result);
    if (s.IsNotFound()) {
      result = "NOT_FOUND";
    } else if (!s.ok()) {
      result = s.ToString();
    }
    return result;
  }

  std::string ManifestFileName() {
    std::string current;
    EXPECT_LEVELDB_OK(
        ReadFileToString(env_, CurrentFileName(dbname_), &current));
    size_t len = current.size();
    if (len > 0 && current[len - 1] == '\n') {
      current.resize(len - 1);
    }
    return dbname_ + "/" + current;
  }

  std::string CurrentFile() { return CurrentFileName(dbname_); }

  std::string LogName(uint64_t number) { return LogFileName(dbname_, number); }

  size_t RemoveLogFiles() {
    // Linux allows unlinking open files, but Windows does not.
    // Closing the db allows for file deletion.
    Close();
    std::vector<uint64_t> logs = GetFiles(kLogFile);
    for (size_t i = 0; i < logs.size(); i++) {
      EXPECT_LEVELDB_OK(env_->RemoveFile(LogName(logs[i]))) << LogName(logs[i]);
    }
    return logs.size();
  }

  void RemoveManifestFile() {
    ASSERT_LEVELDB_OK(env_->RemoveFile(ManifestFileName()));
  }

  uint64_t FirstLogFile() { return GetFiles(kLogFile)[0]; }

  std::vector<uint64_t> GetFiles(FileType t) {
    std::vector<std::string> filenames;
    EXPECT_LEVELDB_OK(env_->GetChildren(dbname_, &filenames));
    std::vector<uint64_t> result;
    for (size_t i = 0; i < filenames.size(); i++) {
      uint64_t number;
      FileType type;
      if (ParseFileName(filenames[i], &number, &type) && type == t) {
        result.push_back(number);
      }
    }
    return result;
  }

  int NumLogs() { return GetFiles(kLogFile).size(); }

  int NumTables() { return GetFiles(kTableFile).size(); }

  uint64_t FileSize(const std::string& fname) {
    uint64_t result;
    EXPECT_LEVELDB_OK(env_->GetFileSize(fname, &result)) << fname;
    return result;
  }

  void CompactMemTable() { dbfull()->TEST_CompactMemTable(); }

  // Directly construct a log file that sets key to val.
  void MakeLogFile(uint64_t lognum, SequenceNumber seq, Slice key, Slice val) {
    std::string fname = LogFileName(dbname_, lognum);
    WritableFile* file;
    ASSERT_LEVELDB_OK(env_->NewWritableFile(fname, &file));
    log::Writer writer(file);
    WriteBatch batch;
    batch.Put(key, val);
    WriteBatchInternal::SetSequence(&batch, seq);
    ASSERT_LEVELDB_OK(writer.AddRecord(WriteBatchInternal::Contents(&batch)));
    ASSERT_LEVELDB_OK(file->Flush());
    delete file;
  }

 private:
  std::string dbname_;
  Env* env_;
  DB* db_;
};

TEST_F(RecoveryTest, ManifestReused) {
  if (!CanAppend()) {
    std::fprintf(stderr,
                 "skipping test because env does not support appending\n");
    return;
  }
  ASSERT_LEVELDB_OK(Put("foo", "bar"));
  Close();
  std::string old_manifest = ManifestFileName();
  Open();
  ASSERT_EQ(old_manifest, ManifestFileName());
  ASSERT_EQ("bar", Get("foo"));
  Open();
  ASSERT_EQ(old_manifest, ManifestFileName());
  ASSERT_EQ("bar", Get("foo"));
}

TEST_F(RecoveryTest, LargeManifestCompacted) {
  if (!CanAppend()) {
    std::fprintf(stderr,
                 "skipping test because env does not support appending\n");
    return;
  }
  ASSERT_LEVELDB_OK(Put("foo", "bar"));
  Close();
  std::string old_manifest = ManifestFileName();

  // Pad with zeroes to make manifest file very big.
  {
    uint64_t len = FileSize(old_manifest);
    WritableFile* file;
    ASSERT_LEVELDB_OK(env()->NewAppendableFile(old_manifest, &file));
    std::string zeroes(3 * 1048576 - static_cast<size_t>(len), 0);
    ASSERT_LEVELDB_OK(file->Append(zeroes));
    ASSERT_LEVELDB_OK(file->Flush());
    delete file;
  }

  Open();
  std::string new_manifest = ManifestFileName();
  ASSERT_NE(old_manifest, new_manifest);
  ASSERT_GT(10000, FileSize(new_manifest));
  ASSERT_EQ("bar", Get("foo"));

  Open();
  ASSERT_EQ(new_manifest, ManifestFileName());
  ASSERT_EQ("bar", Get("foo"));
}

TEST_F(RecoveryTest, NoLogFiles) {
  ASSERT_LEVELDB_OK(Put("foo", "bar"));
  ASSERT_EQ(1, RemoveLogFiles());
  Open();
  ASSERT_EQ("NOT_FOUND", Get("foo"));
  Open();
  ASSERT_EQ("NOT_FOUND", Get("foo"));
}

TEST_F(RecoveryTest, LogFileReuse) {
  if (!CanAppend()) {
    std::fprintf(stderr,
                 "skipping test because env does not support appending\n");
    return;
  }
  for (int i = 0; i < 2; i++) {
    ASSERT_LEVELDB_OK(Put("foo", "bar"));
    if (i == 0) {
      // Compact to ensure current log is empty
      CompactMemTable();
    }
    Close();
    ASSERT_EQ(1, NumLogs());
    uint64_t number = FirstLogFile();
    if (i == 0) {
      ASSERT_EQ(0, FileSize(LogName(number)));
    } else {
      ASSERT_LT(0, FileSize(LogName(number)));
    }
    Open();
    ASSERT_EQ(1, NumLogs());
    ASSERT_EQ(number, FirstLogFile()) << "did not reuse log file";
    ASSERT_EQ("bar", Get("foo"));
    Open();
    ASSERT_EQ(1, NumLogs());
    ASSERT_EQ(number, FirstLogFile()) << "did not reuse log file";
    ASSERT_EQ("bar", Get("foo"));
  }
}

TEST_F(RecoveryTest, MultipleMemTables) {
  // Make a large log.
  const int kNum = 1000;
  for (int i = 0; i < kNum; i++) {
    char buf[100];
    std::snprintf(buf, sizeof(buf), "%050d", i);
    ASSERT_LEVELDB_OK(Put(buf, buf));
  }
  ASSERT_EQ(0, NumTables());
  Close();
  ASSERT_EQ(0, NumTables());
  ASSERT_EQ(1, NumLogs());
  uint64_t old_log_file = FirstLogFile();

  // Force creation of multiple memtables by reducing the write buffer size.
  Options opt;
  opt.reuse_logs = true;
  opt.write_buffer_size = (kNum * 100) / 2;
  Open(&opt);
  ASSERT_LE(2, NumTables());
  ASSERT_EQ(1, NumLogs());
  ASSERT_NE(old_log_file, FirstLogFile()) << "must not reuse log";
  for (int i = 0; i < kNum; i++) {
    char buf[100];
    std::snprintf(buf, sizeof(buf), "%050d", i);
    ASSERT_EQ(buf, Get(buf));
  }
}

TEST_F(RecoveryTest, MultipleLogFiles) {
  ASSERT_LEVELDB_OK(Put("foo", "bar"));
  Close();
  ASSERT_EQ(1, NumLogs());

  // Make a bunch of uncompacted log files.
  uint64_t old_log = FirstLogFile();
  MakeLogFile(old_log + 1, 1000, "hello", "world");
  MakeLogFile(old_log + 2, 1001, "hi", "there");
  MakeLogFile(old_log + 3, 1002, "foo", "bar2");

  // Recover and check that all log files were processed.
  Open();
  ASSERT_LE(1, NumTables());
  ASSERT_EQ(1, NumLogs());
  uint64_t new_log = FirstLogFile();
  ASSERT_LE(old_log + 3, new_log);
  ASSERT_EQ("bar2", Get("foo"));
  ASSERT_EQ("world", Get("hello"));
  ASSERT_EQ("there", Get("hi"));

  // Test that previous recovery produced recoverable state.
  Open();
  ASSERT_LE(1, NumTables());
  ASSERT_EQ(1, NumLogs());
  if (CanAppend()) {
    ASSERT_EQ(new_log, FirstLogFile());
  }
  ASSERT_EQ("bar2", Get("foo"));
  ASSERT_EQ("world", Get("hello"));
  ASSERT_EQ("there", Get("hi"));

  // Check that introducing an older log file does not cause it to be re-read.
  Close();
  MakeLogFile(old_log + 1, 2000, "hello", "stale write");
  Open();
  ASSERT_LE(1, NumTables());
  ASSERT_EQ(1, NumLogs());
  if (CanAppend()) {
    ASSERT_EQ(new_log, FirstLogFile());
  }
  ASSERT_EQ("bar2", Get("foo"));
  ASSERT_EQ("world", Get("hello"));
  ASSERT_EQ("there", Get("hi"));
}

TEST_F(RecoveryTest, ManifestMissing) {
  ASSERT_LEVELDB_OK(Put("foo", "bar"));
  Close();
  RemoveManifestFile();

  Status status = OpenWithStatus();
#if defined(LEVELDB_PLATFORM_CHROMIUM)
  // TODO(crbug.com/760362): See comment in MakeIOError() from env_chromium.cc.
  ASSERT_TRUE(status.IsIOError());
#else
  ASSERT_TRUE(status.IsCorruption());
#endif  // defined(LEVELDB_PLATFORM_CHROMIUM)
}

TEST_F(RecoveryTest, CurrentFileWithCrlf) {
  ASSERT_LEVELDB_OK(Put("foo", "bar"));
  Close();

  // Simulate a Windows text tool rewriting CURRENT with CRLF line endings:
  // without CRLF tolerance the trailing '\r' ends up inside the manifest file
  // name, so every later open fails with corruption.
  std::string current;
  ASSERT_LEVELDB_OK(ReadFileToString(env(), CurrentFile(), &current));
  ASSERT_EQ('\n', current[current.size() - 1]);
  current.resize(current.size() - 1);
  ASSERT_LEVELDB_OK(
      WriteStringToFile(env(), current + "\r\n", CurrentFile()));

  Open();
  ASSERT_EQ("bar", Get("foo"));
  Close();

  // A torn CURRENT write without any line terminator must still be rejected.
  ASSERT_LEVELDB_OK(WriteStringToFile(env(), current, CurrentFile()));
  ASSERT_TRUE(OpenWithStatus().IsCorruption());
}

// ---------------------------------------------------------------------------
// CURRENT file line terminators.
//
// A CURRENT file holds exactly one line: the name of the MANIFEST file plus a
// line terminator. Windows text tools and sync clients rewrite CURRENT with
// CRLF endings, and some old Mac-style writers use a lone CR; the terminator
// must be stripped before the name is used to open the MANIFEST, otherwise
// every later DB::Open() fails with
//     Corruption: CURRENT points to a non-existent file
// A CURRENT without any terminator is a torn write and must stay rejected.
//
// History: the same defect was fixed independently three times downstream --
// Mojang's original leveldb-mcpe tree (commit 52edeaab, 2017),
// Amulet-Team/leveldb-mcpe#6 (2021) and mojang/leveldb#8 (2024, merged into
// Minecraft Bedrock in 2024-12). Upstream google/leveldb still has the
// defect, so the tests below are the red side of the fix.
// ---------------------------------------------------------------------------

// One shape of the CURRENT file. "terminator" are the bytes that follow the
// manifest file name; "empty_file" describes a CURRENT that holds no bytes
// at all; "expect_open" is the outcome the database must produce.
struct CurrentFileTerminatorCase {
  const char* label;       // printable case name, used in the test output
  const char* terminator;  // bytes appended after the manifest file name
  bool empty_file;         // true: CURRENT is zero-length instead
  bool expect_open;        // true: DB::Open() must succeed
};

std::ostream& operator<<(std::ostream& os, const CurrentFileTerminatorCase& c) {
  return os << c.label;
}

class CurrentFileTerminator
    : public ::testing::TestWithParam<CurrentFileTerminatorCase> {
 public:
  CurrentFileTerminator() : env_(Env::Default()), db_(nullptr) {
    dbname_ = testing::TempDir() + "current_file_terminator_test";
    DestroyDB(dbname_, Options());
    // A failure here means the environment is broken, not the code under
    // test. The assertion lives in a helper because an ASSERT_* macro may
    // not return from a constructor.
    OpenExpectSuccess();
  }

  ~CurrentFileTerminator() {
    Close();
    DestroyDB(dbname_, Options());
  }

  Env* env() const { return env_; }
  const std::string& dbname() const { return dbname_; }
  std::string CurrentFile() const { return CurrentFileName(dbname_); }

  Status OpenWithStatus() {
    Close();
    Options opts;
    opts.reuse_logs = true;
    opts.create_if_missing = true;
    opts.env = env_;
    return DB::Open(opts, dbname_, &db_);
  }

  // Opens the database and aborts the current test if that fails. Only for
  // the places where opening is expected to work.
  void OpenExpectSuccess() { ASSERT_LEVELDB_OK(OpenWithStatus()); }

  void Close() {
    delete db_;
    db_ = nullptr;
  }

  Status Put(const std::string& k, const std::string& v) {
    return db_->Put(WriteOptions(), k, v);
  }

  std::string Get(const std::string& k) {
    std::string result;
    Status s = db_->Get(ReadOptions(), k, &result);
    if (s.IsNotFound()) {
      result = "NOT_FOUND";
    } else if (!s.ok()) {
      result = s.ToString();
    }
    return result;
  }

  // The manifest file name CURRENT points at, with its line terminator
  // removed. The database accepts "\n", "\r\n" and a lone "\r" as terminators
  // (see VersionSet::Recover), so all three are stripped here.
  std::string ManifestName() {
    std::string current;
    EXPECT_LEVELDB_OK(
        ReadFileToString(env_, CurrentFileName(dbname_), &current));
    if (current.size() >= 2 && current[current.size() - 2] == '\r' &&
        current[current.size() - 1] == '\n') {
      current.resize(current.size() - 2);
    } else if (!current.empty() && (current[current.size() - 1] == '\n' ||
                                   current[current.size() - 1] == '\r')) {
      current.resize(current.size() - 1);
    }
    return current;
  }

 private:
  std::string dbname_;
  Env* env_;
  DB* db_;
};

// The value domain: the three terminators the database must accept, plus the
// two shapes it must keep rejecting.
INSTANTIATE_TEST_SUITE_P(CurrentFileTerminators, CurrentFileTerminator,
                         ::testing::Values(
                             CurrentFileTerminatorCase{"LF", "\n", false, true},
                             CurrentFileTerminatorCase{"CRLF", "\r\n", false,
                                                      true},
                             CurrentFileTerminatorCase{"CR", "\r", false,
                                                      true},
                             CurrentFileTerminatorCase{"NoTerminator", "",
                                                      false, false},
                             CurrentFileTerminatorCase{"Empty", "", true,
                                                      false}));

TEST_P(CurrentFileTerminator, TerminatorIsStrippedBeforeTheManifestIsOpened) {
  const CurrentFileTerminatorCase& c = GetParam();

  ASSERT_LEVELDB_OK(Put("foo", "bar"));
  Close();

  // The manifest name leveldb itself wrote, without its LF terminator.
  const std::string manifest = ManifestName();
  ASSERT_FALSE(manifest.empty());

  // Content consistency: the MANIFEST exists under the name without the
  // terminator, and no file exists under the name with the terminator
  // appended. The database can therefore only open if the terminator was
  // stripped before the name was used. (With no terminator at all the two
  // names are the same string, so there is nothing to compare.)
  ASSERT_TRUE(env()->FileExists(dbname() + "/" + manifest));
  if (!c.empty_file && c.terminator[0] != '\0') {
    ASSERT_FALSE(env()->FileExists(dbname() + "/" + manifest + c.terminator));
  }

  // Rewrite CURRENT with the terminator under test.
  const std::string rewritten =
      c.empty_file ? std::string() : manifest + c.terminator;
  ASSERT_LEVELDB_OK(WriteStringToFile(env(), rewritten, CurrentFile()));

  if (c.expect_open) {
    // The terminator was stripped, so CURRENT still names the real MANIFEST.
    Status s = OpenWithStatus();
    ASSERT_TRUE(s.ok()) << "CURRENT terminator case " << c.label
                         << " must open the database, got: " << s.ToString();
    ASSERT_EQ("bar", Get("foo"));
    Close();

    // The tolerance has to hold for every open, not only for the first one:
    // the failure this guards against hits *every* DB::Open(). CURRENT is
    // rewritten here because the database may have replaced it in the
    // meantime.
    const std::string reopened = ManifestName();
    ASSERT_FALSE(reopened.empty());
    ASSERT_TRUE(env()->FileExists(dbname() + "/" + reopened));
    ASSERT_LEVELDB_OK(
        WriteStringToFile(env(), reopened + c.terminator, CurrentFile()));
    s = OpenWithStatus();
    ASSERT_TRUE(s.ok()) << "CURRENT terminator case " << c.label
                         << " must open the database again, got: "
                         << s.ToString();
    ASSERT_EQ("bar", Get("foo"));
    Close();
  } else {
    // A CURRENT that is not "<manifest><terminator>" is a torn write: it must
    // be refused instead of silently pointing at some other file.
    const Status s = OpenWithStatus();
    ASSERT_TRUE(s.IsCorruption()) << s.ToString();
    Close();

    // Refusing must not cost data: a well-formed CURRENT recovers the very
    // same database with the very same contents.
    ASSERT_LEVELDB_OK(WriteStringToFile(env(), manifest + "\n", CurrentFile()));
    Status restored = OpenWithStatus();
    ASSERT_TRUE(restored.ok())
        << "CURRENT terminator case " << c.label
        << " must not cost data: the database did not come back: "
        << restored.ToString();
    ASSERT_EQ("bar", Get("foo"));
    Close();
  }
}

}  // namespace leveldb
