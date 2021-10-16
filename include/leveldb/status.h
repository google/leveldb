// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
//
// A Status encapsulates the result of an operation.  It may indicate success,
// or it may indicate an error with an associated error message.
//
// Multiple threads can invoke const methods on a Status without
// external synchronization, but if any of the threads may call a
// non-const method, all threads accessing the same Status must use
// external synchronization.

#ifndef STORAGE_LEVELDB_INCLUDE_STATUS_H_
#define STORAGE_LEVELDB_INCLUDE_STATUS_H_

#include <algorithm>
#include <string>

#include "leveldb/export.h"
#include "leveldb/slice.h"

namespace leveldb {

class LEVELDB_EXPORT Status {
 public:
  // Create a success status.
  Status() noexcept : state_(nullptr) {} // noexcept表示让编译器知道，该函数不会抛出异常。
  ~Status() { delete[] state_; } // delete[]表示，删除用new[]分配的内存，调用析构函数

  Status(const Status& rhs); // rhs表示right hand side, 表示赋值号右边的值。
  Status& operator=(const Status& rhs); // 重载=, 一般输入都是一个const xxx&, 返回值类型xxx&表示，允许operator chaining, 即允许a = b = c = 5;
                                        // 例如c = 5返回的是c的引用，
                                        // 这种函数一般都要return *this，如果直接返回一个对象会重新触发复制构造，比较低效。
                                        // +，-这种，不能返回Status&, 因为这是构造新值的算子，要直接返回值。
                                        // 参考资料：http://courses.cms.caltech.edu/cs11/material/cpp/donnie/cpp-ops.html
                                        // 以及，这里就不要加inline修饰符了，在实现的时候加上才是对的。

  Status(Status&& rhs) noexcept : state_(rhs.state_) { rhs.state_ = nullptr; }
  // 移动构造函数。该函数是为了移动操作的函数，移动操作不应抛出任何异常。state_(rhs.state_)表示，接管资源。
  // rhs.state_ = nullptr是为了保证，移动操作结束之后，销毁它是无害的（保证源对象rhs的state_,不再指向移走的对象）。
  // &&表示，rhs是 rvalue reference，右值引用, 一般只用来形容函数的参数。只接收r-value expression, r-value就是没有memory address的。
  // 右值引用是用来支持移动操作的。左值表达式表达对象的身份，右值表达式表达对象的值。
  // 右值引用也是引用，是对象的另一个名字。
  // 例如void foo(int&& a), 就可以foo(6), foo(6+b)。不能：int&& b = c. 可以int&& c = 5
  
  Status& operator=(Status&& rhs) noexcept;
  // 移动赋值运算符，不应抛出任何异常。
  // 什么函数用到了移动赋值呢？

  // Return a success status.
  static Status OK() { return Status(); } // 默认构造的就是ok状态

  // Return error status of an appropriate type.
  static Status NotFound(const Slice& msg, const Slice& msg2 = Slice()) {
    // Slice？
    return Status(kNotFound, msg, msg2);
    // 调用（code, msg1, msg2）
  }
  static Status Corruption(const Slice& msg, const Slice& msg2 = Slice()) {
    return Status(kCorruption, msg, msg2);
  }
  static Status NotSupported(const Slice& msg, const Slice& msg2 = Slice()) {
    return Status(kNotSupported, msg, msg2);
  }
  static Status InvalidArgument(const Slice& msg, const Slice& msg2 = Slice()) {
    return Status(kInvalidArgument, msg, msg2);
  }
  static Status IOError(const Slice& msg, const Slice& msg2 = Slice()) {
    return Status(kIOError, msg, msg2);
  }

  // Returns true iff the status indicates success.
  bool ok() const { return (state_ == nullptr); }

  // Returns true iff the status indicates a NotFound error.
  bool IsNotFound() const { return code() == kNotFound; }

  // Returns true iff the status indicates a Corruption error.
  bool IsCorruption() const { return code() == kCorruption; }

  // Returns true iff the status indicates an IOError.
  bool IsIOError() const { return code() == kIOError; }

  // Returns true iff the status indicates a NotSupportedError.
  bool IsNotSupportedError() const { return code() == kNotSupported; }

  // Returns true iff the status indicates an InvalidArgument.
  bool IsInvalidArgument() const { return code() == kInvalidArgument; }

  // Return a string representation of this status suitable for printing.
  // Returns the string "OK" for success.
  std::string ToString() const;

 private:
  enum Code { // 是一个成员变量
    kOk = 0,
    kNotFound = 1,
    kCorruption = 2,
    kNotSupported = 3,
    kInvalidArgument = 4,
    kIOError = 5
  };

  Code code() const {
    return (state_ == nullptr) ? kOk : static_cast<Code>(state_[4]);
    // 不是ok的情况下，把state_[4]这个表示code的这一位，强制case成Code类型
  }

  Status(Code code, const Slice& msg, const Slice& msg2);
  // 这个构造函数是私有的，只有内部人员可以使用。

  static const char* CopyState(const char* s); // static表示，是所有对象共用的，这个函数所有对象只存一份。
                                              // 返回的是const char*, 定义在.cc文件中

  // OK status has a null state_.  Otherwise, state_ is a new[] array
  // of the following form:
  //    state_[0..3] == length of message
  //    state_[4]    == code
  //    state_[5..]  == message
  const char* state_; // state_为什么是const? 怎么初始化？如果构造函数里没有做初始化怎么办？
                      // 命名?
};

inline Status::Status(const Status& rhs) {
  state_ = (rhs.state_ == nullptr) ? nullptr : CopyState(rhs.state_);
}
inline Status& Status::operator=(const Status& rhs) { // 类内的函数，隐式地都是inline的。减少function call, 在编译的时候，会把inline的函数插入调用的地方
                                                      // inline只是对编译器的请求，不代表编译器一定这么做。
                                                      // 在类内的时候，不要加inline修饰符，在实现的时候加上即可。
  // The following condition catches both aliasing (when this == &rhs),
  // and the common case where both rhs and *this are ok.
  // 这里check了2种情况，第一种，self_assignment的情况: a = a(这时候，this == &rhs)。第二种，当前对象*this, 跟传进来的rhs, 是identical的（*this == rhs）。
  if (state_ != rhs.state_) { // a_state = b_state的时候，相当于a_state.operator(b_state)?，rhs.state_ = b_state.state_, 而state_
                              // 引用判等,相当于引用指向的对象判等，因此对象的值一样就是相等。虽然可能并不是同一个对象。
                              // 注意identical和same的区别，前者是，2个对象一模一样，后者是，一个对象。一个对象，和它的引用，是same的。int x = y, 这时候x和y是identical
    delete[] state_; // 不等，释放掉当前对象的state_, 并copy一份rhs.state_
    state_ = (rhs.state_ == nullptr) ? nullptr : CopyState(rhs.state_); // 赋值
  }
  return *this; // 返回值是，调用operator=的对象的引用，有利于连接操作。例如a.move_cursor().set_char(), 等价于a.move_cursor();a.set_char();
                // 如果返回值不是Status&, 将会创造一个临时副本，这样就不能连续操作了。
                // 参考c++ primer 246页
}
inline Status& Status::operator=(Status&& rhs) noexcept { // 移动赋值
  // 需要检测自赋值吗：
  std::swap(state_, rhs.state_); // rhs以后就不用了吧？
  return *this; // 注意返回值
}

}  // namespace leveldb

#endif  // STORAGE_LEVELDB_INCLUDE_STATUS_H_
