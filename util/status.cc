// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include <stdio.h>
#include "port/port.h"
#include "include/status.h"

namespace leveldb {

Status::Status(Code code, const Slice& msg, const Slice& msg2) {
  assert(code != kOk);
  state_ = new State(make_pair(code, std::string(msg.data(), msg.size())));
  if (!msg2.empty()) {
    state_->second.append(": ");
    state_->second.append(msg2.data(), msg2.size());
  }
}

std::string Status::ToString() const {
  if (state_ == NULL) {
    return "OK";
  } else {
    char tmp[30];
    const char* type;
    switch (state_->first) {
      case kOk:
        type = "OK";
        break;
      case kNotFound:
        type = "NotFound";
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
        snprintf(tmp, sizeof(tmp), "Unknown code(%d): ",
                 static_cast<int>(state_->first));
        type = tmp;
        break;
    }
    std::string result(type);
    if (!state_->second.empty()) {
      result.append(state_->second);
    }
    return result;
  }
}

}
