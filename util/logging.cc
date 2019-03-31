// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "util/logging.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits>
#include "leveldb/env.h"
#include "leveldb/slice.h"

namespace leveldb {

void AppendNumberTo(std::string* str, uint64_t num) {
  char buf[30];
  // 将"num"的前30字节，写入"buf"
  snprintf(buf, sizeof(buf), "%llu", (unsigned long long) num);
  // 把"buf"追加在"str"后; 
  // append use : https://blog.csdn.net/wxn704414736/article/details/78551886
  str->append(buf);
}

void AppendEscapedStringTo(std::string* str, const Slice& value) {
  for (size_t i = 0; i < value.size(); i++) {
    char c = value[i];  // TODO value[i]?
    if (c >= ' ' && c <= '~') {  // ASCII 字符表范围
      str->push_back(c); // 将"c"加到"str"后；https://blog.csdn.net/u013630349/article/details/46853297
    } else {
      char buf[10];
      snprintf(buf, sizeof(buf), "\\x%02x",
               static_cast<unsigned int>(c) & 0xff); // https://www.cnblogs.com/chenyangchun/p/6795923.html
      str->append(buf);
    }
  }
}

std::string NumberToString(uint64_t num) {
  std::string r;
  AppendNumberTo(&r, num);
  return r;
}

std::string EscapeString(const Slice& value) {
  std::string r;
  AppendEscapedStringTo(&r, value);
  return r;
}

// 如果 (*in).data是十进制字符，则将其转换为数值
bool ConsumeDecimalNumber(Slice* in, uint64_t* val) {
  // Constants that will be optimized away.  constexpr所修饰的变量一定是编译期可求值的
  // kMaxUint64 = uint64_t 类型的最大值
  // https://en.cppreference.com/w/cpp/types/numeric_limits/max
  constexpr const uint64_t kMaxUint64 = std::numeric_limits<uint64_t>::max();
  constexpr const char kLastDigitOfMaxUint64 =
      '0' + static_cast<char>(kMaxUint64 % 10);

  uint64_t value = 0;

  // reinterpret_cast-ing from char* to unsigned char* to avoid signedness.
  const unsigned char* start =
      reinterpret_cast<const unsigned char*>(in->data());

  const unsigned char* end = start + in->size();
  const unsigned char* current = start;  // 为何不在for中赋值？因为代码长度？
  for (; current != end; ++current) {
    const unsigned char ch = *current;
    if (ch < '0' || ch > '9')
      break;

    // Overflow check.
    // kMaxUint64 / 10 is also constant and will be optimized away.
    if (value > kMaxUint64 / 10 ||
        (value == kMaxUint64 / 10 && ch > kLastDigitOfMaxUint64)) {
      return false;
    }

    value = (value * 10) + (ch - '0');
  }

  *val = value;
  const size_t digits_consumed = current - start;
  in->remove_prefix(digits_consumed);  // 跳过已转换为数字的部分？
  return digits_consumed != 0;
}

}  // namespace leveldb
