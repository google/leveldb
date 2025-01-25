// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "table/merger.h"

#include <vector>

#include "leveldb/comparator.h"
#include "leveldb/iterator.h"
#include "table/iterator_wrapper.h"

namespace leveldb {

namespace {
class MergingIterator : public Iterator {
 public:
  MergingIterator(const Comparator* comparator, Iterator** children, int n)
      : comparator_(comparator),
        children_(n, nullptr),
        n_(n),
        current_(nullptr),
        direction_(kForward) {
    for (int i = 0; i < n_; i++) {
      children_[i] = new IteratorWrapper(children[i]);
    }
  }

  ~MergingIterator() override {
    for (int i = 0; i < n_; i++) {
      delete children_[i];
    }
  }

  bool Valid() const override { return (current_ != nullptr); }

  void SeekToFirst() override {
    for (int i = 0; i < n_; i++) {
      children_[i]->SeekToFirst();
    }

    SortChildren();
    FindSmallest();
    direction_ = kForward;
  }

  void SeekToLast() override {
    for (int i = 0; i < n_; i++) {
      children_[i]->SeekToLast();
    }

    SortChildren();
    FindLargest();
    direction_ = kReverse;
  }

  void Seek(const Slice& target) override {
    for (int i = 0; i < n_; i++) {
      children_[i]->Seek(target);
    }

    SortChildren();
    FindSmallest();
    direction_ = kForward;
  }

  void Next() override {
    assert(Valid());

    bool need_sort = false;

    // Ensure that all children are positioned after key().
    // If we are moving in the forward direction, it is already
    // true for all of the non-current_ children since current_ is
    // the smallest child and key() == current_->key().  Otherwise,
    // we explicitly position the non-current_ children.
    if (direction_ != kForward) {
      need_sort = true;
      for (int i = 0; i < n_; i++) {
        IteratorWrapper* child = children_[i];
        if (child != current_) {
          child->Seek(key());
          if (child->Valid() &&
              comparator_->Compare(key(), child->key()) == 0) {
            child->Next();
          }
        }
      }
      direction_ = kForward;
    }

    current_->Next();

    if (need_sort) {
      SortChildren();
    } else {
      AdjustCurrentByNext();
    }

    FindSmallest();
  }

  void Prev() override {
    assert(Valid());

    bool need_sort = false;

    // Ensure that all children are positioned before key().
    // If we are moving in the reverse direction, it is already
    // true for all of the non-current_ children since current_ is
    // the largest child and key() == current_->key().  Otherwise,
    // we explicitly position the non-current_ children.
    if (direction_ != kReverse) {
      need_sort = true;
      for (int i = 0; i < n_; i++) {
        IteratorWrapper* child = children_[i];
        if (child != current_) {
          child->Seek(key());
          if (child->Valid()) {
            // Child is at first entry >= key().  Step back one to be < key()
            child->Prev();
          } else {
            // Child has no entries >= key().  Position at last entry.
            child->SeekToLast();
          }
        }
      }
      direction_ = kReverse;
    }

    current_->Prev();

    if (need_sort) {
      SortChildren();
    } else {
      AdjustCurrentByPrev();
    }

    FindLargest();
  }

  Slice key() const override {
    assert(Valid());
    return current_->key();
  }

  Slice value() const override {
    assert(Valid());
    return current_->value();
  }

  Status status() const override {
    Status status;
    for (int i = 0; i < n_; i++) {
      status = children_[i]->status();
      if (!status.ok()) {
        break;
      }
    }
    return status;
  }

 private:
  // Which direction is the iterator moving?
  enum Direction { kForward, kReverse };

  void FindSmallest();
  void FindLargest();

  void SortChildren();
  void AdjustCurrentByNext();
  void AdjustCurrentByPrev();

  // We might want to use a heap in case there are lots of children.
  // For now we use a simple array since we expect a very small number
  // of children in leveldb.
  const Comparator* comparator_;
  std::vector<IteratorWrapper*> children_;
  int n_;
  IteratorWrapper* current_;
  int current_idx_;
  Direction direction_;
};

void MergingIterator::FindSmallest() {
  current_ = nullptr;

  for (int i = 0; i < n_; i++) {
    IteratorWrapper* child = children_[i];
    if (child->Valid()) {
      current_ = child;
      current_idx_ = i;
      return;
    }
  }
}

void MergingIterator::FindLargest() {
  current_ = nullptr;

  for (int i = n_ - 1; i >= 0; i--) {
    IteratorWrapper* child = children_[i];
    if (child->Valid()) {
      current_ = child;
      current_idx_ = i;
      return;
    }
  }
}

void MergingIterator::SortChildren() {
  std::sort(children_.begin(), children_.end(),
            [this](const IteratorWrapper* a, const IteratorWrapper* b) {
              // Order of invalid children are not important. They are just
              // skipped.
              if (!a->Valid()) return false;
              if (!b->Valid()) return true;
              return comparator_->Compare(a->key(), b->key()) < 0;
            });
}

void MergingIterator::AdjustCurrentByNext() {
  if (!current_->Valid()) return;

  for (int next_idx = current_idx_ + 1; next_idx < n_; next_idx++) {
    IteratorWrapper* next_child = children_[next_idx];

    if (!next_child->Valid()) continue;

    if (comparator_->Compare(current_->key(), next_child->key()) > 0) {
      children_[current_idx_] = next_child;
      current_idx_ = next_idx;
      children_[next_idx] = current_;
    } else {
      break;
    }
  }
}

void MergingIterator::AdjustCurrentByPrev() {
  if (!current_->Valid()) return;

  for (int next_idx = current_idx_ - 1; next_idx >= 0 ; next_idx--) {
    IteratorWrapper* next_child = children_[next_idx];

    if (!next_child->Valid()) continue;

    if (comparator_->Compare(current_->key(), next_child->key()) < 0) {
      children_[current_idx_] = next_child;
      current_idx_ = next_idx;
      children_[next_idx] = current_;
    } else {
      break;
    }
  }
}
}  // namespace

Iterator* NewMergingIterator(const Comparator* comparator, Iterator** children,
                             int n) {
  assert(n >= 0);
  if (n == 0) {
    return NewEmptyIterator();
  } else if (n == 1) {
    return children[0];
  } else {
    return new MergingIterator(comparator, children, n);
  }
}

}  // namespace leveldb
