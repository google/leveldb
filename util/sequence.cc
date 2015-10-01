// Copyright (c) 2012 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "leveldb/sequence_policy.h"

#include "leveldb/env.h"

namespace leveldb {
namespace {

class TimeSequencePolicy : public SequencePolicy {
 private:
  Env* const env_;
  time_t max_lifetime_;

 public:
  TimeSequencePolicy(Env* env, time_t max_lifetime)
    : env_(env), max_lifetime_(max_lifetime) {}

  virtual SequenceNumber Next(
      const SequenceNumber& sequence,
      size_t count) const {
    uint64_t now = env_->NowMicros();
    return (now > sequence ? now : sequence) + count;
  }

  virtual bool IsExpired(const SequenceNumber& sequence) const {
    if (!sequence || !max_lifetime_) {
      return false;
    }
    uint64_t now = env_->NowMicros();
    return now > sequence && max_lifetime_ < static_cast<time_t>(now - sequence);
  }
};

}

const SequencePolicy* NewTimeSequencePolicy(Env* env, time_t max_lifetime) {
  return new TimeSequencePolicy(env, max_lifetime);
}

}  // namespace leveldb
