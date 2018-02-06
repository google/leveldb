// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef STORAGE_LEVELDB_DB_SNAPSHOT_H_
#define STORAGE_LEVELDB_DB_SNAPSHOT_H_

#include "db/dbformat.h"
#include "leveldb/db.h"

namespace leveldb {

class SnapshotList;

// Snapshots are kept in a doubly-linked list in the DB.
// Each SnapshotImpl corresponds to a particular sequence number.
class SnapshotImpl : public Snapshot {
 public:
  SequenceNumber number_;  // const after creation

 private:
  friend class SnapshotList;

  // SnapshotImpl is kept in a doubly-linked circular list
  SnapshotImpl* prev_;
  SnapshotImpl* next_;

  SnapshotList* list_;                 // just for sanity checks
};

class SnapshotList {
 public:
  SnapshotList() {
    list_.prev_ = &list_;
    list_.next_ = &list_;
  }

  bool empty() const { return list_.next_ == &list_; }
  SnapshotImpl* oldest() const { assert(!empty()); return list_.next_; }
  SnapshotImpl* newest() const { assert(!empty()); return list_.prev_; }

  const SnapshotImpl* New(SequenceNumber seq) {
    SnapshotImpl* s = new SnapshotImpl;
    s->number_ = seq;
    s->list_ = this;
    s->next_ = &list_;
    s->prev_ = list_.prev_;
    s->prev_->next_ = s;
    s->next_->prev_ = s;
    return s;
  }

// inserts a new snapshot according to its sequence number into the ordered list
  const SnapshotImpl* Insert(SequenceNumber seq) {
	  if (empty()) {
		  return New (seq);
	  }

	  // iterate over last until an existing snapshot with larger sequence number is
	   SnapshotImpl *s = list_.next_;
		while (true) {
			if (s->number_ > seq) {
				// insert before
				SnapshotImpl* newSnapShot = new SnapshotImpl;

				newSnapShot->number_ = seq;
				newSnapShot->list_ = this;
				s->prev_->next_ = newSnapShot;
				newSnapShot->prev_ = s->prev_;
				newSnapShot->next_ = s;
				s->prev_ = newSnapShot;
				return newSnapShot;
			}
			if (s->next_ == (SnapshotImpl*) s->list_) {
				break;
			}
			s = s->next_;
		}
	 return New (seq);
  }

  void Delete(const SnapshotImpl* s) {
    assert(s->list_ == this);
    s->prev_->next_ = s->next_;
    s->next_->prev_ = s->prev_;
    delete s;
  }

  // return existing snapshot with sequence number
  SnapshotImpl* Get(SequenceNumber seq) {
	  if (empty()) {
		  return NULL;
	  }
	  SnapshotImpl *s = list_.next_;
	  while (true) {
		  if (s->number_ == seq) {
			  return s;
		  }
		  if (s->next_ == (SnapshotImpl*) s->list_) {
			  break;
		  }
		  s = s->next_;
	  }
	  return NULL;
  }

	std::string GetSequenceStringWithDelimiter(std::string delimiter) {
		std::string result = "";

		if (empty()) {
			return result;
		}
		
		SnapshotImpl *s = list_.next_;
		while (true) {
			result += NumberToString (s->number_) + delimiter;
			if (s->next_ == (SnapshotImpl*) s->list_) {
				break;
			}
			s = s->next_;
		}
		return result;
	}

 private:
  // Dummy head of doubly-linked list of snapshots
  SnapshotImpl list_;
};

}  // namespace leveldb

#endif  // STORAGE_LEVELDB_DB_SNAPSHOT_H_
