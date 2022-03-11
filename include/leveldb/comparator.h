/*
 * @Author: yangxuan
 * @Date: 2022-03-08 09:01:34
 * @LastEditTime: 2022-03-08 22:47:50
 * @LastEditors: yangxuan
 * @Description: LevelDB是按key排序后进行存储，因此必然少不了对key的比较，
 * 在源码中，key的比较主要是基于Comparator这个接口实现的
 * Comparator是一个纯虚类
 * @FilePath: /leveldb/include/leveldb/comparator.h
 */
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef STORAGE_LEVELDB_INCLUDE_COMPARATOR_H_
#define STORAGE_LEVELDB_INCLUDE_COMPARATOR_H_

#include <string>

#include "leveldb/export.h"

namespace leveldb {

class Slice;

// A Comparator object provides a total order across slices that are
// used as keys in an sstable or a database.  A Comparator implementation
// must be thread-safe since leveldb may invoke its methods concurrently
// from multiple threads.
// 实现 Comparator 需要线程安全
// 在LevelDB中，有两个实现Comparator的类：一个是BytewiseComparatorImpl，另一个是InternalKeyComparator
class LEVELDB_EXPORT Comparator {
 public:
  virtual ~Comparator();

  // Three-way comparison.  Returns value:
  //   < 0 iff "a" < "b",
  //   == 0 iff "a" == "b",
  //   > 0 iff "a" > "b"
  virtual int Compare(const Slice& a, const Slice& b) const = 0;

  // The name of the comparator.  Used to check for comparator
  // mismatches (i.e., a DB created with one comparator is
  // accessed using a different comparator.
  // 返回Compare的名字，主要用于比较一致性
  //
  // The client of this package should switch to a new name whenever
  // the comparator implementation changes in a way that will cause
  // the relative ordering of any two keys to change.
  //
  // Names starting with "leveldb." are reserved and should not be used
  // by any clients of this package.
  virtual const char* Name() const = 0;

  // Advanced functions: these are used to reduce the space requirements
  // for internal data structures like index blocks.
  // 下面两个函数主要用来节省内部数据结构的存储空间

  // If *start < limit, changes *start to a short string in [start,limit).
  // 如果 *start < limit ，那么就把 *start 改成一个短字符串，在 [start,limit) 范围内
  // Simple comparator implementations may return with *start unchanged,
  // i.e., an implementation of this method that does nothing is correct.
  // 传入的参数是*start和limit这两个字符串。参数*start对应原字符串，
  // 而经过一系列逻辑处理后，相应的短字符串也将保存在*start中以返回
  // 可以参考 BytewiseComparatorImpl 中的实现
  // TODO:了解清楚具体是怎么实现的
  virtual void FindShortestSeparator(std::string* start,
                                     const Slice& limit) const = 0;

  // Changes *key to a short string >= *key.
  // Simple comparator implementations may return with *key unchanged,
  // i.e., an implementation of this method that does nothing is correct.
  // FindShortestSeparator不同的是，该类没有limit参数，在实际的代码中，
  // 首先找出key字符串中第一个不为0xff的字符，并将该字符加1，然后丢弃后面的所有字符
  virtual void FindShortSuccessor(std::string* key) const = 0;
};

// Return a builtin comparator that uses lexicographic byte-wise
// ordering.  The result remains the property of this module and
// must not be deleted.
LEVELDB_EXPORT const Comparator* BytewiseComparator();

}  // namespace leveldb

#endif  // STORAGE_LEVELDB_INCLUDE_COMPARATOR_H_
