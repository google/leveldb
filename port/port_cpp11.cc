// LevelDB Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
//
// See port_example.h for documentation for the following types/functions.

// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//  * Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//  * Neither the name of the University of California, Berkeley nor the
//    names of its contributors may be used to endorse or promote products
//    derived from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE REGENTS AND CONTRIBUTORS BE LIABLE FOR ANY
// DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//

#include "port/port_cpp11.h"
#include <cassert>

namespace leveldb {
namespace port {

Mutex::Mutex() {
}

Mutex::~Mutex() {
}

void Mutex::Lock() {
  mu_.lock();
}

void Mutex::Unlock() {
  mu_.unlock();
}

CondVar::CondVar(Mutex* mu) :
    mu_(mu) {
  assert(mu_);
}

CondVar::~CondVar() {
}

void CondVar::Wait() {
  std::unique_lock<std::mutex> lock(mu_->mu_, std::defer_lock);
  cv_.wait(lock);
}

void CondVar::Signal() {
  cv_.notify_one();
}

void CondVar::SignalAll() {
  cv_.notify_all();
}

AtomicPointer::AtomicPointer(void* v) :
  rep_(ATOMIC_VAR_INIT(v)) {
}

void* AtomicPointer::Acquire_Load() const {
  return rep_.load();
}

void AtomicPointer::Release_Store(void* v) {
  rep_.store(v);
}

void* AtomicPointer::NoBarrier_Load() const {
  return rep_.load(std::memory_order_relaxed);
}

void AtomicPointer::NoBarrier_Store(void* v) {
  rep_.store(v, std::memory_order_relaxed);
}

void InitOnce(OnceType* once, void (*initializer)()) {
  std::call_once(*once, initializer);
}

}
}
