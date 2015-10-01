// Copyright (c) 2012 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
//
// A database can be configured with a custom SequencePolicy object.
// This object is responsible for generating monotonic sequences
// for stamping writes persisted in database and controlling its lifetime.
//
#ifndef STORAGE_LEVELDB_INCLUDE_SEQUENCE_POLICY_H_
#define STORAGE_LEVELDB_INCLUDE_SEQUENCE_POLICY_H_

#include <ctime>
#include <stdint.h>

namespace leveldb {

typedef uint64_t SequenceNumber;    // from dbformat.h

class Env;

class SequencePolicy {
 public:
  virtual ~SequencePolicy() {}

  virtual SequenceNumber Next(
    const SequenceNumber& sequence,
    size_t count) const = 0;

  virtual bool IsExpired(const SequenceNumber& sequence) const = 0;
};

inline SequenceNumber NextSequence(
  const SequencePolicy* user_policy,
  const SequenceNumber& sequence,
  size_t count)
{
  return user_policy ? user_policy->Next(sequence, count) : sequence + count;
}

inline bool IsSequenceExpired(
    const SequencePolicy* user_policy,
    const SequenceNumber& sequence)
{
  return user_policy ? user_policy->IsExpired(sequence) : false;
}

// Returns a new sequence policy that uses system timer to track records lifetime.
extern const SequencePolicy* NewTimeSequencePolicy(Env* env, time_t max_lifetime);

}

#endif  // STORAGE_LEVELDB_INCLUDE_SEQUENCE_POLICY_H_
