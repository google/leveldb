// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "leveldb/status.h"

#include <cstdio>

#include "port/port.h"

namespace leveldb {

const char* Status::CopyState(const char* state) {
  uint32_t size; // uint32_t是在<cstdint>标准库中定义的。<cstdio>包含了这个标准库吗？
  std::memcpy(&size, state, sizeof(size)); // void memcpy(void *dest, const void *src, size_t n)
                                            // 从src复制到dest, 拷贝4个字节。sizeof(uint32_t)=4, 前4个字节表示的是message的长度
  char* result = new char[size + 5]; // 一个char是一个byte, message长度+5byte
  std::memcpy(result, state, size + 5); // size是message的长度，5是前5个字节=前4个字节是length of messsage, 第5个字节是code。
                                        // 所以就是把state_拷贝到result上了
  return result;
}

Status::Status(Code code, const Slice& msg, const Slice& msg2) {
  assert(code != kOk);
  const uint32_t len1 = static_cast<uint32_t>(msg.size());
  const uint32_t len2 = static_cast<uint32_t>(msg2.size());
  const uint32_t size = len1 + (len2 ? (2 + len2) : 0);
  char* result = new char[size + 5];
  std::memcpy(result, &size, sizeof(size)); // 从result开始的位置，是把size这个变量的地址，开始的地方共sizeof(size)个字节的内容复制
                                            // 其实也就是，从result开始，是以char存的size这个变量。用4个字节就存下了。
  result[4] = static_cast<char>(code);
  std::memcpy(result + 5, msg.data(), len1); // 从result + 5的地方，存放msg,总共len1个字节，mst的size()就是它的字节数
  if (len2) {
    result[5 + len1] = ':';
    result[6 + len1] = ' ';
    std::memcpy(result + 7 + len1, msg2.data(), len2); // 存第二条message
  }
  state_ = result;
}

std::string Status::ToString() const {
  // 把code转变成string?
  if (state_ == nullptr) {
    return "OK";
  } else {
    char tmp[30];
    const char* type; // 默认是nullptr
    switch (code()) { // 返回Code类型
      case kOk: // 值是kOk的情况下
        type = "OK";
        break;
      case kNotFound:
        type = "NotFound: "; // 注意这里
        break;
      case kCorruption:
        type = "Corruption: ";
        break;
      case kNotSupported:
        type = "Not implemented: ";
        break;
      case kInvalidArgument:
        type = "Invalid argument: ";
        break;
      case kIOError:
        type = "IO error: ";
        break;
      default:
        std::snprintf(tmp, sizeof(tmp),
                      "Unknown code(%d): ", static_cast<int>(code()));
        // 定义在<cstdio>里，Write formatted output to sized buffer,把内容写到tmp指向的内存里面，指针是tmp
        // snprintf(char * s, size_t n, const char * format, ...);
        type = tmp;
        break;
    }
    std::string result(type); // 用const char*构建一个string
    uint32_t length;
    std::memcpy(&length, state_, sizeof(length)); // state_是const char*, 复制前4个字节到length变量上，length变量应该表示message的length
    result.append(state_ + 5, length); // 把message给append上，包括msg,msg2. 最终会类似result = "IO error: some_msg1: some_msg2"
    return result;
  }
}

}  // namespace leveldb
