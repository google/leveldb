// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "db/vlog_reader.h"
#include "db/vlog_writer.h"
#include "leveldb/env.h"
#include "util/coding.h"
#include "util/crc32c.h"
#include "util/random.h"
#include "util/testharness.h"

namespace leveldb {
namespace log {

// Construct a string of the specified length made out of the supplied
// partial string.
static std::string BigString(const std::string& partial_string, size_t n) {
  std::string result;
  while (result.size() < n) {
    result.append(partial_string);
  }
  result.resize(n);
  return result;
}

// Construct a string from a number
static std::string NumberString(int n) {
  char buf[50];
  snprintf(buf, sizeof(buf), "%d.", n);
  return std::string(buf);
}

// Return a skewed potentially long string
static std::string RandomSkewedString(int i, Random* rnd) {
  return BigString(NumberString(i), rnd->Skewed(17));
}

class VlogTest {
 private:
  class StringDest : public WritableFile {
   public:
    std::string contents_;

    virtual Status Close() { return Status::OK(); }
    virtual Status Flush() { return Status::OK(); }
    virtual Status Sync() { return Status::OK(); }
    virtual Status Append(const Slice& slice) {
      contents_.append(slice.data(), slice.size());
      return Status::OK();
    }
  };

  class StringSource : public SequentialFile {
   public:
    Slice contents_;
    std::string* data;
    bool force_error_;
    bool returned_partial_;
    StringSource() : force_error_(false), returned_partial_(false) { }

    virtual Status Read(size_t n, Slice* result, char* scratch) {
      ASSERT_TRUE(!returned_partial_) << "must not Read() after eof/error";

      if (force_error_) {
        force_error_ = false;
        returned_partial_ = true;
        return Status::Corruption("read error");
      }

      if (contents_.size() < n) {
        n = contents_.size();
        returned_partial_ = true;
      }
      memcpy(scratch, contents_.data(), n);
      *result = Slice(scratch, n);
      contents_.remove_prefix(n);
      return Status::OK();
    }

    virtual Status Skip(uint64_t n) {
      if (n > contents_.size()) {
        contents_.clear();
        return Status::NotFound("in-memory file skipped past end");
      }

      contents_.remove_prefix(n);

      return Status::OK();
    }

    virtual Status SkipFromHead(uint64_t n)
    {
      if (n > data->size()) {
        contents_.clear();
        return Status::NotFound("in-memory file SkipFromHead past end");
      }

        contents_ = Slice((char*)data->data() + n, data->size() - n);
        return Status::OK();
    }

  };

  class ReportCollector : public VReader::Reporter {
   public:
    size_t dropped_bytes_;
    std::string message_;

    ReportCollector() : dropped_bytes_(0) { }
    virtual void Corruption(size_t bytes, const Status& status) {
      dropped_bytes_ += bytes;
      message_.append(status.ToString());
    }
  };

  StringDest* pd_;
  StringSource* ps_;
  StringDest& dest_;
  StringSource& source_;
  ReportCollector report_;
  bool reading_;
  VWriter* writer_;
  VReader* reader_;

  // Record metadata for testing initial offset functionality
  static size_t initial_offset_record_sizes_[];
  static uint64_t initial_offset_last_record_offsets_[];
  static uint64_t initial_offset_record_offsets_[];//每条记录头部后的位置
  static int num_initial_offset_records_;
 public:
  VlogTest() :pd_(new StringDest),
              ps_(new StringSource),
              dest_(*pd_),
              source_(*ps_),
              reading_(false),
              writer_(new VWriter(pd_)),
              reader_(new VReader(ps_, &report_, true/*checksum*/,
                      0/*initial_offset*/)) {
              /*writer_(new VWriter(&dest_)),*/
              //reader_(new VReader(&source_, &report_, true[>checksum<],
                      /*0[>initial_offset<])) {*/
  }

  ~VlogTest() {
    delete writer_;
    delete reader_;
  }

  void Write(const std::string& msg) {
    ASSERT_TRUE(!reading_) << "Write() after starting to read";
    int header_size;
    writer_->AddRecord(Slice(msg), header_size);
  }

  void Write(const std::string& msg, int& header_size) {
    ASSERT_TRUE(!reading_) << "Write() after starting to read";
    writer_->AddRecord(Slice(msg), header_size);
  }

  size_t WrittenBytes() const {
    return dest_.contents_.size();
  }

  std::string Read() {
    if (!reading_) {
      reading_ = true;
      source_.contents_ = Slice(dest_.contents_);
      source_.data = &dest_.contents_;
    }
    std::string scratch;
    Slice record;
    int header_size;
    if (reader_->ReadRecord(&record, &scratch, header_size)) {
      return record.ToString();
    } else {
      return "EOF";
    }
  }

  void IncrementByte(int offset, int delta) {
    dest_.contents_[offset] += delta;
  }

  void SetByte(int offset, char new_byte) {
    dest_.contents_[offset] = new_byte;
  }

  void ShrinkSize(int bytes) {
    dest_.contents_.resize(dest_.contents_.size() - bytes);
  }

  void ForceError() {
    source_.force_error_ = true;
  }

  size_t DroppedBytes() const {
    return report_.dropped_bytes_;
  }

  std::string ReportMessage() const {
    return report_.message_;
  }

  // Returns OK iff recorded error message contains "msg"
  std::string MatchError(const std::string& msg) const {
    if (report_.message_.find(msg) == std::string::npos) {
      return report_.message_;
    } else {
      return "OK";
    }
  }
//按照初始化好的长度写入num_initial_offset_records_条记录
  void WriteInitialOffsetLog() {
      uint64_t last_record_offset = 0;
    for (int i = 0; i < num_initial_offset_records_; i++) {
      initial_offset_last_record_offsets_[i] = last_record_offset;
      std::string record(initial_offset_record_sizes_[i],
                         static_cast<char>('a' + i));
      int header_size;
      Write(record, header_size);
      initial_offset_record_offsets_[i] = last_record_offset + header_size;
      last_record_offset += (header_size + record.size());
    }
  }

//从initial_offset偏移处创建vlog_reader,读取num_initial_offset_records_ - expected_record_offset条记录
//验证读取的每条记录就是初始化时写入的记录，expected_record_offset代表的是从第几条初始化记录开始验证
  void CheckInitialOffsetRecord(uint64_t initial_offset,
                                int expected_record_offset) {
    WriteInitialOffsetLog();
    reading_ = true;
    source_.contents_ = Slice(dest_.contents_);
      source_.data = &dest_.contents_;
    reader_->SkipToPos(initial_offset);
    // Read all records from expected_record_offset through the last one.
    ASSERT_LT(expected_record_offset, num_initial_offset_records_);
    for (; expected_record_offset < num_initial_offset_records_;
         ++expected_record_offset) {
      Slice record;
      std::string scratch;
      int header_size;
      //ASSERT_TRUE(offset_reader->ReadRecord(&record, &scratch, header_size));
      ASSERT_TRUE(reader_->ReadRecord(&record, &scratch, header_size));
      ASSERT_EQ(initial_offset_record_sizes_[expected_record_offset],
                record.size());
      ASSERT_EQ((char)('a' + expected_record_offset), record.data()[0]);
    }
  }
//这个是测试vlog_read的read接口
  void CheckReadRecord() {
    WriteInitialOffsetLog();
    reading_ = true;
    source_.contents_ = Slice(dest_.contents_);
      source_.data = &dest_.contents_;

    // Read all records from expected_record_offset through the last one.
    int expected_record_offset = 0;
    size_t pos = 0;
    char buf[3*kBlockSize];
    for (; expected_record_offset < num_initial_offset_records_;
         ++expected_record_offset) {
      std::string scratch;
      pos = initial_offset_record_offsets_[expected_record_offset];
      ASSERT_TRUE(reader_->Read(buf, initial_offset_record_sizes_[expected_record_offset], pos));
      ASSERT_EQ((char)('a' + expected_record_offset), buf[0]);
      ASSERT_EQ((char)('a' + expected_record_offset), buf[initial_offset_record_sizes_[expected_record_offset]-1]);
    }

      ASSERT_TRUE(reader_->Read(buf, initial_offset_record_sizes_[1], initial_offset_record_offsets_[1]));
      ASSERT_EQ((char)('b'), buf[0]);
      ASSERT_EQ((char)('b'), buf[initial_offset_record_sizes_[1]-1]);

  }
};

size_t VlogTest::initial_offset_record_sizes_[] =
    {log::kBlockSize - 4 - 3,//刚好占满一个block,kBlockSize可以用3个字节的变量表示其长度
    log::kBlockSize - 4 - 1,//用于一个block的剩余空间不足kheadsize
     100,//slice会回退2，读取kBlockSize - 2个字节到读缓冲区中
     100,//record完全在刚读的读缓冲区中
     log::kBlockSize,//读完读缓冲区内容后， 该条记录剩余待读部分小于kblocksize/2
     2 * log::kBlockSize - 1000,  //读完读缓冲区内容后， 剩余部分大于kblocksize/2
     1
    };
uint64_t VlogTest::initial_offset_last_record_offsets_[] ={0,0,0,0,0,0,0};
uint64_t VlogTest::initial_offset_record_offsets_[] ={0,0,0,0,0,0,0};

// LogTest::initial_offset_last_record_offsets_ must be defined before this.
int VlogTest::num_initial_offset_records_ =
    sizeof(VlogTest::initial_offset_record_sizes_)/sizeof(size_t);

TEST(VlogTest, Empty) {
  ASSERT_EQ("EOF", Read());
}

TEST(VlogTest, ReadWrite) {
  Write("foo");
  Write("bar");
  Write("");
  Write("xxxx");
  ASSERT_EQ("foo", Read());
  ASSERT_EQ("bar", Read());
  ASSERT_EQ("", Read());
  ASSERT_EQ("xxxx", Read());
  ASSERT_EQ("EOF", Read());
  ASSERT_EQ("EOF", Read());  // Make sure reads at eof work
}

TEST(VlogTest, ManyBlocks) {
  for (int i = 0; i < 100; i++) {
    Write(NumberString(i));
  }
  for (int i = 0; i < 100; i++) {
    ASSERT_EQ(NumberString(i), Read());
  }
  ASSERT_EQ("EOF", Read());
}

TEST(VlogTest, RandomRead) {
  const int N = 500;
  Random write_rnd(301);
  for (int i = 0; i < N; i++) {
    Write(RandomSkewedString(i, &write_rnd));
  }
  Random read_rnd(301);
  for (int i = 0; i < N; i++) {
    ASSERT_EQ(RandomSkewedString(i, &read_rnd), Read());
  }
  ASSERT_EQ("EOF", Read());
}

// Tests of all the error paths in log_reader.cc follow:

TEST(VlogTest, ReadError) {
  Write("foo");
  ForceError();
  ASSERT_EQ("EOF", Read());
  ASSERT_EQ(kBlockSize, DroppedBytes());
  ASSERT_EQ("OK", MatchError("read error"));
}


TEST(VlogTest, TruncatedTrailingRecordIsIgnored) {
  Write("foo");
  ShrinkSize(4);   // Drop all payload as well as a header byte
  ASSERT_EQ("EOF", Read());
  // Truncated last record is ignored, not treated as an error.
  ASSERT_EQ(0, DroppedBytes());
  ASSERT_EQ("", ReportMessage());
}


TEST(VlogTest, BadLengthAtEndIsIgnored) {
  Write("foo");
  ShrinkSize(1);
  ASSERT_EQ("EOF", Read());
  ASSERT_EQ(0, DroppedBytes());
  ASSERT_EQ("", ReportMessage());
}

TEST(VlogTest, ChecksumMismatch) {
  Write("foo");
  IncrementByte(0, 1);
  ASSERT_EQ("EOF", Read());
  ASSERT_EQ(4 + 1 + 3, DroppedBytes());
  ASSERT_EQ("OK", MatchError("checksum mismatch"));
}

TEST(VlogTest, PartialLastIsIgnored1) {
  Write(BigString("bar", kBlockSize));//没有超过blocksize/2
  // Cause a bad record length in the LAST block.
  ShrinkSize(1);
  ASSERT_EQ("EOF", Read());
  ASSERT_EQ("OK", MatchError("last record not full"));
}

TEST(VlogTest, PartialLastIsIgnored2) {
  Write(BigString("bar", 2*kBlockSize));//超过blocksize/2
  // Cause a bad record length in the LAST block.
  ShrinkSize(1);
  ASSERT_EQ("EOF", Read());
  ASSERT_EQ("", ReportMessage());
  ASSERT_EQ(0, DroppedBytes());
}

TEST(VlogTest, ReadStart) {
  CheckInitialOffsetRecord(0, 0);
}

TEST(VlogTest, ReadSecondOneOff) {
  CheckInitialOffsetRecord(kBlockSize, 1);
}

TEST(VlogTest, Read) {
    CheckReadRecord();
}

}  // namespace log
}  // namespace leveldb

int main(int argc, char** argv) {
  return leveldb::test::RunAllTests();
}
