// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#if defined(LEVELDB_PLATFORM_POSIX) || defined(LEVELDB_PLATFORM_ANDROID)
#include <unordered_set>
#elif defined(LEVELDB_PLATFORM_CHROMIUM)
#include "base/hash_tables.h"
#else
#include <hash_set> // TODO(sanjay): Switch to unordered_set when possible.
#endif

#include <assert.h>

#include "include/cache.h"
#include "port/port.h"
#include "util/hash.h"
#include "util/mutexlock.h"

namespace leveldb {

Cache::~Cache() {
}

namespace {

// LRU cache implementation

// An entry is a variable length heap-allocated structure.  Entries
// are kept in a circular doubly linked list ordered by access time.
struct LRUHandle {
  void* value;
  void (*deleter)(const Slice&, void* value);
  LRUHandle* next;
  LRUHandle* prev;
  size_t charge;      // TODO(opt): Only allow uint32_t?
  size_t key_length;
  size_t refs;        // TODO(opt): Pack with "key_length"?
  char key_data[1];   // Beginning of key

  Slice key() const {
    // For cheaper lookups, we allow a temporary Handle object
    // to store a pointer to a key in "value".
    if (next == this) {
      return *(reinterpret_cast<Slice*>(value));
    } else {
      return Slice(key_data, key_length);
    }
  }
};

// Pick a platform specific hash_set instantiation
#if defined(LEVELDB_PLATFORM_CHROMIUM) && defined(OS_WIN)
  // Microsoft's hash_set deviates from the standard. See
  // http://msdn.microsoft.com/en-us/library/1t4xas78(v=vs.80).aspx
  // for details. Basically the 2 param () operator is a less than and
  // the 1 param () operator is a hash function.
  struct HandleHashCompare : public stdext::hash_compare<LRUHandle*> {
    size_t operator() (LRUHandle* h) const {
      Slice k = h->key();
      return Hash(k.data(), k.size(), 0);
    }
    bool operator() (LRUHandle* a, LRUHandle* b) const {
      return a->key().compare(b->key()) < 0;
    }
  };
  typedef base::hash_set<LRUHandle*, HandleHashCompare> HandleTable;
#else
  struct HandleHash {
    inline size_t operator()(LRUHandle* h) const {
      Slice k = h->key();
      return Hash(k.data(), k.size(), 0);
    }
  };

  struct HandleEq {
    inline bool operator()(LRUHandle* a, LRUHandle* b) const {
      return a->key() == b->key();
    }
  };
#  if defined(LEVELDB_PLATFORM_CHROMIUM)
    typedef base::hash_set<LRUHandle*, HandleHash, HandleEq> HandleTable;
#  elif defined(LEVELDB_PLATFORM_POSIX) || defined(LEVELDB_PLATFORM_ANDROID)
    typedef std::unordered_set<LRUHandle*, HandleHash, HandleEq> HandleTable;
#  else
    typedef __gnu_cxx::hash_set<LRUHandle*, HandleHash, HandleEq> HandleTable;
#  endif
#endif

class LRUCache : public Cache {
 public:
  explicit LRUCache(size_t capacity);
  virtual ~LRUCache();

  virtual Handle* Insert(const Slice& key, void* value, size_t charge,
                         void (*deleter)(const Slice& key, void* value));
  virtual Handle* Lookup(const Slice& key);
  virtual void Release(Handle* handle);
  virtual void* Value(Handle* handle);
  virtual void Erase(const Slice& key);
  virtual uint64_t NewId();

 private:
  void LRU_Remove(LRUHandle* e);
  void LRU_Append(LRUHandle* e);
  void Unref(LRUHandle* e);

  // Constructor parameters
  const size_t capacity_;

  // mutex_ protects the following state.
  port::Mutex mutex_;
  size_t usage_;
  uint64_t last_id_;

  // Dummy head of LRU list.
  // lru.prev is newest entry, lru.next is oldest entry.
  LRUHandle lru_;

  HandleTable table_;
};

LRUCache::LRUCache(size_t capacity)
    : capacity_(capacity),
      usage_(0),
      last_id_(0) {
  // Make empty circular linked list
  lru_.next = &lru_;
  lru_.prev = &lru_;
}

LRUCache::~LRUCache() {
  table_.clear();
  for (LRUHandle* e = lru_.next; e != &lru_; ) {
    LRUHandle* next = e->next;
    assert(e->refs == 1);  // Error if caller has an unreleased handle
    Unref(e);
    e = next;
  }
}

void LRUCache::Unref(LRUHandle* e) {
  assert(e->refs > 0);
  e->refs--;
  if (e->refs <= 0) {
    usage_ -= e->charge;
    (*e->deleter)(e->key(), e->value);
    free(e);
  }
}

void LRUCache::LRU_Remove(LRUHandle* e) {
  e->next->prev = e->prev;
  e->prev->next = e->next;
}

void LRUCache::LRU_Append(LRUHandle* e) {
  // Make "e" newest entry by inserting just before lru_
  e->next = &lru_;
  e->prev = lru_.prev;
  e->prev->next = e;
  e->next->prev = e;
}

Cache::Handle* LRUCache::Lookup(const Slice& key) {
  MutexLock l(&mutex_);

  LRUHandle dummy;
  dummy.next = &dummy;
  dummy.value = const_cast<Slice*>(&key);
  HandleTable::iterator iter = table_.find(&dummy);
  if (iter == table_.end()) {
    return NULL;
  } else {
    LRUHandle* e = const_cast<LRUHandle*>(*iter);
    e->refs++;
    LRU_Remove(e);
    LRU_Append(e);
    return reinterpret_cast<Handle*>(e);
  }
}

void* LRUCache::Value(Handle* handle) {
  return reinterpret_cast<LRUHandle*>(handle)->value;
}

void LRUCache::Release(Handle* handle) {
  MutexLock l(&mutex_);
  Unref(reinterpret_cast<LRUHandle*>(handle));
}

Cache::Handle* LRUCache::Insert(const Slice& key, void* value, size_t charge,
                             void (*deleter)(const Slice& key, void* value)) {
  MutexLock l(&mutex_);

  LRUHandle* e = reinterpret_cast<LRUHandle*>(
      malloc(sizeof(LRUHandle)-1 + key.size()));
  e->value = value;
  e->deleter = deleter;
  e->charge = charge;
  e->key_length = key.size();
  e->refs = 2;  // One from LRUCache, one for the returned handle
  memcpy(e->key_data, key.data(), key.size());
  LRU_Append(e);
  usage_ += charge;

  std::pair<HandleTable::iterator,bool> p = table_.insert(e);
  if (!p.second) {
    // Kill existing entry
    LRUHandle* old = const_cast<LRUHandle*>(*(p.first));
    LRU_Remove(old);
    table_.erase(p.first);
    table_.insert(e);
    Unref(old);
  }

  while (usage_ > capacity_ && lru_.next != &lru_) {
    LRUHandle* old = lru_.next;
    LRU_Remove(old);
    table_.erase(old);
    Unref(old);
  }

  return reinterpret_cast<Handle*>(e);
}

void LRUCache::Erase(const Slice& key) {
  MutexLock l(&mutex_);

  LRUHandle dummy;
  dummy.next = &dummy;
  dummy.value = const_cast<Slice*>(&key);
  HandleTable::iterator iter = table_.find(&dummy);
  if (iter != table_.end()) {
    LRUHandle* e = const_cast<LRUHandle*>(*iter);
    LRU_Remove(e);
    table_.erase(iter);
    Unref(e);
  }
}

uint64_t LRUCache::NewId() {
  MutexLock l(&mutex_);
  return ++(last_id_);
}

}  // end anonymous namespace

Cache* NewLRUCache(size_t capacity) {
  return new LRUCache(capacity);
}

}
