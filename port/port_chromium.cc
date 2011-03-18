// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "port/port_chromium.h"

#include "util/logging.h"

#if defined(USE_SNAPPY)
#  include "third_party/snappy/src/snappy.h"
#  include "third_party/snappy/src/snappy-stubs.h"
#endif

namespace leveldb {
namespace port {

Mutex::Mutex() {
}

Mutex::~Mutex() {
}

void Mutex::Lock() {
  mu_.Acquire();
}

void Mutex::Unlock() {
  mu_.Release();
}

void Mutex::AssertHeld() {
  mu_.AssertAcquired();
}

CondVar::CondVar(Mutex* mu)
    : cv_(&mu->mu_) {
}

CondVar::~CondVar() { }

void CondVar::Wait() {
  cv_.Wait();
}

void CondVar::Signal(){
  cv_.Signal();
}

void CondVar::SignalAll() {
  cv_.Broadcast();
}

void Lightweight_Compress(const char* input, size_t input_length,
                          std::string* output) {
#if defined(USE_SNAPPY)
  output->resize(snappy::MaxCompressedLength(input_length));
  size_t outlen;
  snappy::RawCompress(snappy::StringPiece(input, input_length),
                      &(*output)[0], &outlen);
  output->resize(outlen);
#else
  output->assign(input, input_length);
#endif
}

bool Lightweight_Uncompress(const char* input_data, size_t input_length,
                            std::string* output) {
#if defined(USE_SNAPPY)
  snappy::StringPiece input(input_data, input_length);
  size_t ulength;
  if (!snappy::GetUncompressedLength(input, &ulength)) {
    return false;
  }
  output->resize(ulength);
  return snappy::RawUncompress(input, &(*output)[0]);
#else
  output->assign(input_data, input_length);
  return true;
#endif
}

}
}
