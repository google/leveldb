// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "port/port_android.h"

#include <cstdlib>

extern "C" {
size_t fread_unlocked(void *a, size_t b, size_t c, FILE *d) {
  return fread(a, b, c, d);
}

size_t fwrite_unlocked(const void *a, size_t b, size_t c, FILE *d) {
  return fwrite(a, b, c, d);
}

int fflush_unlocked(FILE *f) {
  return fflush(f);
}

int fdatasync(int fd) {
  return fsync(fd);
}
}

namespace leveldb {
namespace port {

static void PthreadCall(const char* label, int result) {
  if (result != 0) {
    fprintf(stderr, "pthread %s: %s\n", label, strerror(result));
    abort();
  }
}

Mutex::Mutex() { PthreadCall("init mutex", pthread_mutex_init(&mu_, NULL)); }
Mutex::~Mutex() { PthreadCall("destroy mutex", pthread_mutex_destroy(&mu_)); }
void Mutex::Lock() { PthreadCall("lock", pthread_mutex_lock(&mu_)); }
void Mutex::Unlock() { PthreadCall("unlock", pthread_mutex_unlock(&mu_)); }

CondVar::CondVar(Mutex* mu)
    : mu_(mu) {
  PthreadCall("init cv", pthread_cond_init(&cv_, NULL));
}

CondVar::~CondVar() { 
  PthreadCall("destroy cv", pthread_cond_destroy(&cv_));
}

void CondVar::Wait() {
  PthreadCall("wait", pthread_cond_wait(&cv_, &mu_->mu_));
}

void CondVar::Signal(){
  PthreadCall("signal", pthread_cond_signal(&cv_));
}

void CondVar::SignalAll() {
  PthreadCall("broadcast", pthread_cond_broadcast(&cv_));
}

}
}
