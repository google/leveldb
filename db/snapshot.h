// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef STORAGE_LEVELDB_DB_SNAPSHOT_H_
#define STORAGE_LEVELDB_DB_SNAPSHOT_H_

#include "include/db.h"

namespace leveldb {

class SnapshotList;

// Snapshots are kept in a doubly-linked list in the DB.
// Each Snapshot corresponds to a particular sequence number.
class Snapshot {
 public:
  SequenceNumber number_;  // const after creation

 private:
  friend class SnapshotList;

  // Snapshot is kept in a doubly-linked circular list
  Snapshot* prev_;
  Snapshot* next_;

  SnapshotList* list_;                 // just for sanity checks
};

class SnapshotList {
 public:
  SnapshotList() {
    list_.prev_ = &list_;
    list_.next_ = &list_;
  }

  bool empty() const { return list_.next_ == &list_; }
  Snapshot* oldest() const { assert(!empty()); return list_.next_; }
  Snapshot* newest() const { assert(!empty()); return list_.prev_; }

  const Snapshot* New(SequenceNumber seq) {
    Snapshot* s = new Snapshot;
    s->number_ = seq;
    s->list_ = this;
    s->next_ = &list_;
    s->prev_ = list_.prev_;
    s->prev_->next_ = s;
    s->next_->prev_ = s;
    return s;
  }

  void Delete(const Snapshot* s) {
    assert(s->list_ == this);
    s->prev_->next_ = s->next_;
    s->next_->prev_ = s->prev_;
    delete s;
  }

 private:
  // Dummy head of doubly-linked list of snapshots
  Snapshot list_;
};

}

#endif  // STORAGE_LEVELDB_DB_SNAPSHOT_H_
