// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "port/port_chromium.h"

#include "util/logging.h"

#if defined(USE_SNAPPY)
#  include "third_party/snappy/src/snappy.h"
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

bool Snappy_Compress(const char* input, size_t input_length,
                     std::string* output) {
#if defined(USE_SNAPPY)
  output->resize(snappy::MaxCompressedLength(input_length));
  size_t outlen;
  snappy::RawCompress(input, input_length, &(*output)[0], &outlen);
  output->resize(outlen);
  return true;
#else
  return false;
#endif
}

bool Snappy_GetUncompressedLength(const char* input, size_t length,
                                  size_t* result) {
#if defined(USE_SNAPPY)
  return snappy::GetUncompressedLength(input_data, input_length, result);
#else
  return false;
#endif
}

bool Snappy_Uncompress(const char* input_data, size_t input_length,
                       char* output) {
#if defined(USE_SNAPPY)
  return snappy::RawUncompress(input_data, input_length, output);
#else
  return false;
#endif
}

}
}
