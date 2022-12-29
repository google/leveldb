// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "db/vlog_reader.h"
#include <stdio.h>
#include "leveldb/env.h"
#include "util/coding.h"
#include "util/crc32c.h"
#include "util/mutexlock.h"

namespace leveldb {
namespace log {

VReader::Reporter::~Reporter() {
}

VReader::VReader(SequentialFile* file,  bool checksum,
               uint64_t initial_offset)
    : file_(file),
      reporter_ (NULL),
      checksum_(checksum),
      backing_store_(new char[kBlockSize]),//一次从磁盘读kblocksize，多余的做缓存以便下次读
      buffer_(),
      eof_(false){
    if(initial_offset > 0)
        SkipToPos(initial_offset);
}

VReader::VReader(SequentialFile* file, Reporter* reporter, bool checksum,
               uint64_t initial_offset)
    : file_(file),
      reporter_ (reporter),
      checksum_(checksum),
      backing_store_(new char[kBlockSize]),//一次从磁盘读kblocksize，多余的做缓存以便下次读
      buffer_(),
      eof_(false){
    if(initial_offset > 0)
        SkipToPos(initial_offset);
}

VReader::~VReader() {
    delete[] backing_store_;
    delete file_;
}

bool VReader::SkipToPos(size_t pos) {
  if (pos > 0) {//跳到距file文件头偏移pos的地方
      Status skip_status = file_->SkipFromHead(pos);
    if (!skip_status.ok()) {
      ReportDrop(pos, skip_status);
      return false;
    }
  }
  return true;
}

bool VReader::ReadRecord(Slice* record, std::string* scratch, int& head_size)
{//日志回放的时候是单线程
    scratch->clear();
    record->clear();

    if(buffer_.size() < kVHeaderMaxSize)
    {//遇到buffer_剩的空间不够解析头部时
        if(!eof_)
        {
            size_t left_head_size = buffer_.size();
            if(left_head_size > 0)//如果读缓冲还剩内容，拷贝到读缓冲区头
                memcpy(backing_store_, buffer_.data(), left_head_size);
            buffer_.clear();
            Status status = file_->Read(kBlockSize - left_head_size, &buffer_, backing_store_ + left_head_size);

            if(left_head_size > 0)
                buffer_.go_back(left_head_size);

            if (!status.ok())
            {
                buffer_.clear();
                ReportDrop(kBlockSize, status);
                eof_ = true;
                return false;
            }
            else if(buffer_.size() < kBlockSize)//因为前面回退了，所以这里是kblocksize
            {
                eof_ = true;
                if(buffer_.size() < 4 + 1 + 1)//最少的一条记录也需要6个字节，一个字节的数据
                   return false;
            }
        }
        else
        {
            if(buffer_.size() < 4 + 1 + 1)
            {
                buffer_.clear();
                return false;
            }
        }
    }
    //解析头部
    uint64_t length = 0;
    uint32_t expected_crc = crc32c::Unmask(DecodeFixed32(buffer_.data()));//早一点解析出crc,因为后面可能buffer_.data内容会变
    buffer_.remove_prefix(4);
    const char *varint64_begin = buffer_.data();
    bool b = GetVarint64(&buffer_, &length);
    assert(b);
    head_size = 4 + (buffer_.data() - varint64_begin);
    if(length <= buffer_.size())
    {
        //逻辑记录完整的在buffer中(在block中)
        if (checksum_) {
            uint32_t actual_crc = crc32c::Value(buffer_.data(), length);
            if (actual_crc != expected_crc) {
                ReportCorruption(head_size + length, "checksum mismatch");
                return false;
            }
        }
        *record = Slice(buffer_.data(), length);
        buffer_.remove_prefix(length);
        return true;
    }
    else
    {
        if(eof_ == true)
        {
            return false;//日志最后一条记录不完整的情况，直接忽略
        }
        //逻辑记录不能在buffer中全部容纳，需要将读取结果写入到scratch
        scratch->reserve(length);
        size_t buffer_size = buffer_.size();
        scratch->assign(buffer_.data(), buffer_size);
        buffer_.clear();
        const uint64_t left_length = length - buffer_size;
        if(left_length > kBlockSize/2)//如果剩余待读的记录超过block块的一半大小，则直接读到scratch中
        {
            Slice buffer;
            scratch->resize(length);
            Status status = file_->Read(left_length, &buffer, const_cast<char*>(scratch->data()) + buffer_size);

            if(!status.ok())
            {
                ReportDrop(left_length, status);
                return false;
            }
            if(buffer.size() < left_length)
            {
                eof_ = true;
                scratch->clear();

                return false;
            }
        }
        else
        {//否则读一整块到buffer中
            Status status = file_->Read(kBlockSize, &buffer_, backing_store_);

            if(!status.ok())
            {
                ReportDrop(kBlockSize, status);
                return false;
            }
            else if(buffer_.size() < kBlockSize)
            {
                if(buffer_.size() < left_length)
                {
                    eof_ = true;
                    scratch->clear();
                    ReportCorruption(left_length, "last record not full");
                    return false;////////////////////////////////
                }
                eof_ = true; //这个判断不要也可以，加的话算是优化，提早知道到头了，省的read一次才知道
            }
            scratch->append(buffer_.data(), left_length);
            buffer_.remove_prefix(left_length);
        }
        if (checksum_) {
            uint32_t actual_crc = crc32c::Value(scratch->data(), length);
            if (actual_crc != expected_crc) {
               ReportCorruption(head_size + length, "checksum mismatch");
                return false;
            }
        }
        *record = Slice(*scratch);
        return true;
    }
}

//get查询中根据索引从vlog文件中读value值
bool VReader::Read(char* val, size_t size, size_t pos)
{//要考虑多线程情况
    MutexLock l(&mutex_);
    if (!SkipToPos(pos)) {//因为read读的位置随机，因此file的skip接口不行，因为file的skip是相对于当前位置的
      return false;
    }
    Slice buffer;
    Status status = file_->Read(size, &buffer, val);
    if (!status.ok() || buffer.size() != size)
    {
        ReportDrop(size, status);
        return false;
    }
    return true;
}

void VReader::ReportCorruption(uint64_t bytes, const char* reason) {
  ReportDrop(bytes, Status::Corruption(reason));
}

void VReader::ReportDrop(uint64_t bytes, const Status& reason) {
  if (reporter_ != NULL)
  {
    reporter_->Corruption(static_cast<size_t>(bytes), reason);
  }
}

bool VReader::DeallocateDiskSpace(uint64_t offset, size_t len)
{
    return file_->DeallocateDiskSpace(offset, len).ok();
}

}  // namespace log
}  // namespace leveldb
