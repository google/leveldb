
#include "db/mem_version_set.h"

#include "db/memtable.h"
#include <algorithm>
#include <cstdio>

#include "table/merger.h"
#include "table/two_level_iterator.h"
#include "util/coding.h"

namespace leveldb {

// memtable max size
static size_t TargetTableSize(const Options* options) {
  return options->max_table_size;
}

static uint64_t MaxTableSizeForLevel(const Options* options) {
  return TargetTableSize(options);
}

static int64_t TotalTableSize(const std::vector<MemTableMetaData*>& tables) {
  int64_t sum = 0;
  for (size_t i = 0; i < tables.size(); i++) {
    sum += tables[i]->table_size;
  }
  return sum;
}
// max size of m1/m2
static double MaxBytesForMemLevel(int level) {
  double result = 12. *4*1024*1024.0;  // 12*4M
  if (level == 1) {
    return result;
  } else {
    return result * 10;
  }
}

static int64_t ExpandedCompactionByteSizeLimit(const Options* options) {
  return 25 * TargetTableSize(options);
}

// iterator in single table
static Iterator* GetTableIterator(char* table) {
  MemTable* mem = reinterpret_cast<MemTable*>(table);
  return mem->NewIterator();
}

// find a table that key falls into
int FindTable(const InternalKeyComparator& icmp,
              const std::vector<MemTableMetaData*>& tables, const Slice& key) {
  uint32_t left = 0;
  uint32_t right = tables.size();
  while (left < right) {
    uint32_t mid = (left + right) / 2;
    const MemTableMetaData* f = tables[mid];
    if (icmp.InternalKeyComparator::Compare(f->largest.Encode(), key) < 0) {
      left = mid + 1;
    } else {
      right = mid;
    }
  }
  return right;
}

//——————————————————————————————————————————————————
// memversion
MemVersion::~MemVersion() {
  assert(refs_ == 0);

  // Remove from linked list
  prev_->next_ = next_;
  next_->prev_ = prev_;

  for (int level = 0; level < config::kNumMemLevels; level++) {
    for (size_t i = 0; i < tables_[level].size(); i++) {
      MemTableMetaData* f = tables_[level][i];
      assert(f->refs > 0);
      f->refs--;
      if (f->refs <= 0) {
        delete f;
      }
    }
  }
}
// sorted level table iter
class MemVersion::LevelTableNumIterator : public Iterator {
 public:
  LevelTableNumIterator(const InternalKeyComparator& icmp,
                        const std::vector<MemTableMetaData*>* flist)
      : icmp_(icmp), flist_(flist), index_(flist->size()) {  // Marks as invalid
  }
  bool Valid() const override { return index_ < flist_->size(); }
  void Seek(const Slice& target) override {
    index_ = FindTable(icmp_, *flist_, target);
  }
  void SeekToFirst() override { index_ = 0; }
  void SeekToLast() override {
    index_ = flist_->empty() ? 0 : flist_->size() - 1;
  }
  void Next() override {
    assert(Valid());
    index_++;
  }
  void Prev() override {
    assert(Valid());
    if (index_ == 0) {
      index_ = flist_->size();
    } else {
      index_--;
    }
  }
  Slice key() const override {
    assert(Valid());
    return (*flist_)[index_]->largest.Encode();  // max internalkey
  }
  Slice value() const override {  // pointer
    assert(Valid());
    return Slice(reinterpret_cast<char *>((*flist_)[index_]->mem_table));
  }
  Status status() const override { return Status::OK(); }

 private:
  const InternalKeyComparator icmp_;
  const std::vector<MemTableMetaData*>* const flist_;
  uint32_t index_;

  // Backing store for value().  Holds the number and size.
  mutable char value_buf_[16];
};

// memversion get
static bool NewestFirst(MemTableMetaData* a, MemTableMetaData* b) {
  return a->number > b->number;
}

void MemVersion::ForEachOverlapping(Slice user_key, Slice internal_key,
                                    void* arg,
                                    bool (*func)(void*, MemTableMetaData*)) {
  const Comparator* ucmp = mvset_->icmp_.user_comparator();
  // Search level-0 in order from newest to oldest.
  std::vector<MemTableMetaData*> tmp;
  tmp.reserve(tables_[0].size());
  for (uint32_t i = 0; i < tables_[0].size(); i++) {
    MemTableMetaData* f = tables_[0][i];
    if (ucmp->Compare(user_key, f->smallest.user_key()) >= 0 &&
        ucmp->Compare(user_key, f->largest.user_key()) <= 0) {
      tmp.push_back(f);
    }
  }
  if (!tmp.empty()) {
    std::sort(tmp.begin(), tmp.end(), NewestFirst);

    for (uint32_t i = 0; i < tmp.size(); i++) {
      if (!(*func)(arg, tmp[i])) {//error
        return;
      }
    }
  }  
  
  // Search levels.
  for (int level = 1; level < config::kNumMemLevels; level++) {
    size_t num_tables = tables_[level].size();
    if (num_tables == 0) continue;

    // Binary search to find earliest index whose largest key >= internal_key.
    uint32_t index = FindTable(mvset_->icmp_, tables_[level], internal_key);
    if (index < num_tables) {
      MemTableMetaData* f = tables_[level][index];
      if (ucmp->Compare(user_key, f->smallest.user_key()) < 0) {
      } else {
        if (!(*func)(arg, f)) {  // match,if return false,return
          return;
        }
      }
    }
  }
}

Status MemVersion::Get(const LookupKey& k, std::string* value) {
  struct State {
    Slice lkey;
    Slice ukey;
    std::string* value;

    MemVersionSet* mvset;
    Status s;
    bool found;

    static bool Match(void* arg, MemTableMetaData* f) {
      State* state = reinterpret_cast<State*>(arg);
      // search memtable
      MemTable* m = f->mem_table;
      LookupKey lookKey(state->lkey);
      bool is_found = m->Get(lookKey,state->value, &state->s);
      if (is_found) {
        std::fprintf(stdout,"get from versions\n");
        if (state->s.ok()) {
          // found valid
          state->found = true;
          return false;
        } else if (state->s.IsNotFound()) {
          // deleted
          return false;
        }
      } else {
        return true;  // continue finding tables
      }
    }
  };
  State state;
  state.found = false;
  state.lkey=k.internal_key();
  state.ukey=k.user_key();
  state.mvset = mvset_;

  ForEachOverlapping(state.ukey, k.internal_key(), &state,
                     &State::Match);  // search

  return state.found ? state.s : Status::NotFound(Slice());
}

void MemVersion::Ref() { ++refs_; }

void MemVersion::Unref() {
  assert(this != &mvset_->dummy_versions_);
  assert(refs_ >= 1);
  --refs_;
  if (refs_ == 0) {
    delete this;
  }
}

void MemVersion::GetOverlappingInputs(int level, const InternalKey* begin,
                                      const InternalKey* end,
                                      std::vector<MemTableMetaData*>* inputs) {
  assert(level >= 0);
  assert(level < config::kNumMemLevels);
  inputs->clear();
  Slice user_begin, user_end;
  if (begin != nullptr) {
    user_begin = begin->user_key();
  }
  if (end != nullptr) {
    user_end = end->user_key();
  }
  const Comparator* user_cmp = mvset_->icmp_.user_comparator();
  for (size_t i = 0; i < tables_[level].size();) {
    MemTableMetaData* f = tables_[level][i++];
    const Slice table_start = f->smallest.user_key();
    const Slice table_limit = f->largest.user_key();
    if (begin != nullptr && user_cmp->Compare(table_limit, user_begin) < 0) {
      // "f" is completely before specified range; skip it
    } else if (end != nullptr && user_cmp->Compare(table_start, user_end) > 0) {
      // "f" is completely after specified range; skip it
    } else {
      inputs->push_back(f);
      if (level == 0) {
        // expand range and start from first table
        if (begin != nullptr &&
            user_cmp->Compare(table_start, user_begin) < 0) {
          user_begin = table_start;
          inputs->clear();
          i = 0;
        } else if (end != nullptr &&
                   user_cmp->Compare(table_limit, user_end) > 0) {
          user_end = table_limit;
          inputs->clear();
          i = 0;
        }
      }
    }
  }
}
std::string MemVersion::DebugString() const {
  std::string r;
  for (int level = 0; level < config::kNumMemLevels; level++) {
    // E.g.,
    //   --- level 1 ---
    //   17:123['a' .. 'd']
    //   20:43['e' .. 'g']
    r.append("--- level ");
    AppendNumberTo(&r, level);
    r.append(" ---\n");
    const std::vector<MemTableMetaData*>& tables = tables_[level];
    for (size_t i = 0; i < tables.size(); i++) {
      r.push_back(' ');
      AppendNumberTo(&r, tables[i]->number);
      r.push_back(':');
      AppendNumberTo(&r, tables[i]->table_size);
      r.append("[");
      r.append(tables[i]->smallest.DebugString());
      r.append(" .. ");
      r.append(tables[i]->largest.DebugString());
      r.append("]\n");
    }
  }
  return r;
}
//——————————————————————————————————————————————————
// memversionset
class MemVersionSet::Builder {
 private:
  struct BySmallestKey {
    const InternalKeyComparator* internal_comparator;

    bool operator()(MemTableMetaData* f1, MemTableMetaData* f2) const {
      int r = internal_comparator->Compare(f1->smallest, f2->smallest);
      if (r != 0) {
        return (r < 0);
      } else {
        return (f1->number < f2->number);
      }
    }
  };

  typedef std::set<MemTableMetaData*, BySmallestKey> TableSet;
  struct LevelState {
    std::set<uint64_t> deleted_tables;
    TableSet* added_tables;
  };

  MemVersionSet* vset_;
  MemVersion* base_;
  LevelState levels_[config::kNumMemLevels];

 public:
  Builder(MemVersionSet* vset, MemVersion* base) : vset_(vset), base_(base) {
    base_->Ref();
    BySmallestKey cmp;
    cmp.internal_comparator = &vset_->icmp_;
    for (int level = 0; level < config::kNumMemLevels; level++) {
      levels_[level].added_tables = new TableSet(cmp);
    }
  }

  ~Builder() {
    for (int level = 0; level < config::kNumMemLevels; level++) {
      const TableSet* added = levels_[level].added_tables;
      std::vector<MemTableMetaData*> to_unref;
      to_unref.reserve(added->size());
      for (TableSet::const_iterator it = added->begin(); it != added->end();
           ++it) {
        to_unref.push_back(*it);
      }
      delete added;
      for (uint32_t i = 0; i < to_unref.size(); i++) {
        MemTableMetaData* f = to_unref[i];
        f->refs--;
        if (f->refs <= 0) {
          delete f;
        }
      }
    }
    base_->Unref();
  }

  // Apply all of the edits in *edit to the current state.
  void Apply(MemVersionEdit* edit) { 
    // Update compaction pointers
    if(edit->compact_pointers_.size()!=0)
      vset_->compact_pointer_ = edit->compact_pointers_[0].Encode().ToString();
    // Delete tables
    for (const auto& deleted_table_set_kvp : edit->deleted_tables_) {
      const int level = deleted_table_set_kvp.first;
      const uint64_t number = deleted_table_set_kvp.second;
      levels_[level].deleted_tables.insert(number);//error
    }
    // Add new tables
    for (size_t i = 0; i < edit->new_tables_.size(); i++) {
      const int level = edit->new_tables_[i].first;
      MemTableMetaData* f = new MemTableMetaData(edit->new_tables_[i].second);
      f->refs = 1;
      levels_[level].deleted_tables.erase(f->number);
      levels_[level].added_tables->insert(f);
    }
  }

  // Save the current state in *v.
  void SaveTo(MemVersion* v) {
    BySmallestKey cmp;
    cmp.internal_comparator = &vset_->icmp_;
    for (int level = 0; level < config::kNumMemLevels; level++) {
      const std::vector<MemTableMetaData*>& base_tables = base_->tables_[level];
      std::vector<MemTableMetaData*>::const_iterator base_iter =
          base_tables.begin();
      std::vector<MemTableMetaData*>::const_iterator base_end =
          base_tables.end();
      const TableSet* added_tables = levels_[level].added_tables;//这层新加入的表
      v->tables_[level].reserve(base_tables.size() + added_tables->size());
      for (const auto& added_table : *added_tables) {
        for (std::vector<MemTableMetaData*>::const_iterator bpos =
                 std::upper_bound(base_iter, base_end, added_table, cmp);
             base_iter != bpos; ++base_iter) {
          MaybeAddTable(v, level, *base_iter);
        }
        //添加新文件
        MaybeAddTable(v, level, added_table);
      }

      for (; base_iter != base_end; ++base_iter) {
        MaybeAddTable(v, level, *base_iter);
      }

#ifndef NDEBUG
      // Make sure there is no overlap in levels > 0
      if (level > 0) {
        for (uint32_t i = 1; i < v->tables_[level].size(); i++) {
          const InternalKey& prev_end = v->tables_[level][i - 1]->largest;
          const InternalKey& this_begin = v->tables_[level][i]->smallest;
          if (vset_->icmp_.Compare(prev_end, this_begin) >= 0) {
            std::fprintf(stderr, "overlapping ranges in same level %s vs. %s\n",
                         prev_end.DebugString().c_str(),
                         this_begin.DebugString().c_str());
            std::abort();
          }
        }
      }
#endif
    }
  }

  // add table to a version
  void MaybeAddTable(MemVersion* v, int level, MemTableMetaData* f) {
    if (levels_[level].deleted_tables.count(f->number) > 0) {
    } else {
      std::vector<MemTableMetaData*>* tables = &v->tables_[level];
      if (level > 0 && !tables->empty()) {
        assert(vset_->icmp_.Compare((*tables)[tables->size() - 1]->largest,
                                    f->smallest) < 0);
      }
      f->refs++;
      tables->push_back(f);
    }
  }
};

MemVersionSet::MemVersionSet(const std::string& dbname, const Options* options,
                             const InternalKeyComparator* cmp,VersionSet* versions)
    : dbname_(dbname),
      options_(options),
      icmp_(*cmp),
      next_table_number_(0),
      dummy_versions_(this),
      current_(nullptr) ,
      versions_(versions){
  AppendVersion(new MemVersion(this));
}

MemVersionSet::~MemVersionSet() {
  current_->Unref();
  assert(dummy_versions_.next_ == &dummy_versions_);  // List must be empty
}

// Append version to linked list
void MemVersionSet::AppendVersion(MemVersion* v) {
  assert(v->refs_ == 0);
  assert(v != current_);
  if (current_ != nullptr) {
    current_->Unref();
  }
  current_ = v;
  v->Ref();

  v->prev_ = dummy_versions_.prev_;
  v->next_ = &dummy_versions_;
  v->prev_->next_ = v;
  v->next_->prev_ = v;
}

// former logandapply
void MemVersionSet::Apply(MemVersionEdit* edit) {
  edit->SetNextTable(next_table_number_);
  edit->SetLastSequence(LastSequence());
  MemVersion* v = new MemVersion(this);
  {
    Builder builder(this, current_);
    builder.Apply(edit);
    builder.SaveTo(v);//error where is mem?
  }
  Finalize(v);  // need to compaction?
  AppendVersion(v);
}

int MemVersionSet::NumLevelTables(int level) const {
  return current_->tables_[level].size();
}

int64_t MemVersionSet::NumLevelBytes(int level) const {
  assert(level >= 0);
  assert(level < config::kNumMemLevels);
  return TotalTableSize(current_->tables_[level]);
}

// select compaction input
void MemVersionSet::GetRange(const std::vector<MemTableMetaData*>& inputs,
                             InternalKey* smallest, InternalKey* largest) {
  assert(!inputs.empty());
  smallest->Clear();
  largest->Clear();
  for (size_t i = 0; i < inputs.size(); i++) {
    MemTableMetaData* f = inputs[i];
    if (i == 0) {
      *smallest = f->smallest;
      *largest = f->largest;
    } else {
      if (icmp_.Compare(f->smallest, *smallest) < 0) {
        *smallest = f->smallest;
      }
      if (icmp_.Compare(f->largest, *largest) > 0) {
        *largest = f->largest;
      }
    }
  }
}
void MemVersionSet::GetRange2(const std::vector<MemTableMetaData*>& inputs1,
                              const std::vector<MemTableMetaData*>& inputs2,
                              InternalKey* smallest, InternalKey* largest) {
  std::vector<MemTableMetaData*> all = inputs1;
  all.insert(all.end(), inputs2.begin(), inputs2.end());
  GetRange(all, smallest, largest);
}

void MemVersionSet::AddLiveTables(std::set<uint64_t>* live) {//把所有version中的表都添加到live
  for (MemVersion* v = dummy_versions_.next_; v != &dummy_versions_;
       v = v->next_) {
    for (int level = 0; level < config::kNumMemLevels; level++) {
      const std::vector<MemTableMetaData*>& tables = v->tables_[level];
      for (size_t i = 0; i < tables.size(); i++) {
        live->insert(tables[i]->number);
      }
    }
  }
}

// iterator
Iterator* MemVersionSet::MakeInputIterator(MemCompaction* c) {
  // need?
  ReadOptions options;
  options.verify_checksums = options_->paranoid_checks;
  options.fill_cache = false;

  // count of iterators
  const int space = c->inputs_[0].size() + 1;
  Iterator** list = new Iterator*[space];
  int num = 0;

  // l1
  const std::vector<MemTableMetaData*>& tables = c->inputs_[0];
  for (size_t i = 0; i < tables.size(); i++) {
    MemTable* table =tables[i]->mem_table;
    list[num++] = table->NewIterator();
  }
  // l2
  if(!c->inputs_[1].empty()){
    list[num++] = NewMemTwoLevelIterator(
      new MemVersion::LevelTableNumIterator(icmp_, &c->inputs_[1]),
      &GetTableIterator);
  }
  assert(num <= space);
  Iterator* result = NewMergingIterator(&icmp_, list, num);
  delete[] list;
  return result;
}

void MemVersionSet::Finalize(MemVersion* v) {
  const uint64_t level_bytes = TotalTableSize(v->tables_[0]);
  v->compaction_score_ =
      static_cast<double>(level_bytes) / MaxBytesForMemLevel(1);
}

// add l1-l2 compact inputs
MemCompaction* MemVersionSet::PickCompaction() {
  const bool size_compaction = (current_->compaction_score_ >= 1);

  if (size_compaction) {
    MemCompaction* c = new MemCompaction(options_);
    // Pick the first file that comes after compact_pointer_
    for (size_t i = 0; i < current_->tables_[0].size(); i++) {
      MemTableMetaData* f = current_->tables_[0][i];
      if (compact_pointer_.empty() ||
          icmp_.Compare(f->largest.Encode(), compact_pointer_) > 0) {
        c->inputs_[0].push_back(f);
        break;
      }
    }

    if (c->inputs_[0].empty()) {
      c->inputs_[0].push_back(current_->tables_[0][0]);
    }
    c->input_version_ = current_;
    c->input_version_->Ref();

    // get all l1 inputs that overlap
    InternalKey smallest, largest;
    GetRange(c->inputs_[0], &smallest, &largest);
    current_->GetOverlappingInputs(0, &smallest, &largest, &c->inputs_[0]);
    assert(!c->inputs_[0].empty());

    // add l2 inputs and adjust
    SetupOtherInputs(c);

    return c;
  } else {
    return nullptr;
  }
}

bool FindLargestKey(const InternalKeyComparator& icmp,
                    const std::vector<MemTableMetaData*>& tables,
                    InternalKey* largest_key) {
  if (tables.empty()) {
    return false;
  }
  *largest_key = tables[0]->largest;
  for (size_t i = 1; i < tables.size(); ++i) {
    MemTableMetaData* f = tables[i];
    if (icmp.Compare(f->largest, *largest_key) > 0) {
      *largest_key = f->largest;
    }
  }
  return true;
}

MemTableMetaData* FindSmallestBoundaryTable(
    const InternalKeyComparator& icmp,
    const std::vector<MemTableMetaData*>& level_tables,
    const InternalKey& largest_key) {
  const Comparator* user_cmp = icmp.user_comparator();
  MemTableMetaData* smallest_boundary_table = nullptr;
  for (size_t i = 0; i < level_tables.size(); ++i) {
    MemTableMetaData* f = level_tables[i];
    if (icmp.Compare(f->smallest, largest_key) > 0 &&
        user_cmp->Compare(f->smallest.user_key(), largest_key.user_key()) ==
            0) {
      if (smallest_boundary_table == nullptr ||
          icmp.Compare(f->smallest, smallest_boundary_table->smallest) < 0) {
        smallest_boundary_table = f;
      }
    }
  }
  return smallest_boundary_table;
}

void AddBoundaryInputs(const InternalKeyComparator& icmp,
                       const std::vector<MemTableMetaData*>& level_tables,
                       std::vector<MemTableMetaData*>* compaction_tables) {
  InternalKey largest_key;

  // Quick return if compaction_tables is empty.
  if (!FindLargestKey(icmp, *compaction_tables, &largest_key)) {
    return;
  }

  bool continue_searching = true;
  while (continue_searching) {
    MemTableMetaData* smallest_boundary_table =
        FindSmallestBoundaryTable(icmp, level_tables, largest_key);

    // If a boundary table was found advance largest_key, otherwise we're done.
    if (smallest_boundary_table != NULL) {
      compaction_tables->push_back(smallest_boundary_table);
      largest_key = smallest_boundary_table->largest;
    } else {
      continue_searching = false;
    }
  }
}

void MemVersionSet::SetupOtherInputs(MemCompaction* c) {
  const int level = 0;
  InternalKey smallest, largest;

  AddBoundaryInputs(icmp_, current_->tables_[level], &c->inputs_[0]);
  GetRange(c->inputs_[0], &smallest, &largest);

  current_->GetOverlappingInputs(level + 1, &smallest, &largest,
                                 &c->inputs_[1]);

  InternalKey all_start, all_limit;
  GetRange2(c->inputs_[0], c->inputs_[1], &all_start, &all_limit);

  if (!c->inputs_[1].empty()) {
    std::vector<MemTableMetaData*> expanded0;
    current_->GetOverlappingInputs(level, &all_start, &all_limit, &expanded0);
    AddBoundaryInputs(icmp_, current_->tables_[level], &expanded0);
    const int64_t inputs0_size = TotalTableSize(c->inputs_[0]);
    const int64_t inputs1_size = TotalTableSize(c->inputs_[1]);
    const int64_t expanded0_size = TotalTableSize(expanded0);
    if (expanded0.size() > c->inputs_[0].size() &&
        inputs1_size + expanded0_size <
            ExpandedCompactionByteSizeLimit(options_)) {
      InternalKey new_start, new_limit;
      GetRange(expanded0, &new_start, &new_limit);
      std::vector<MemTableMetaData*> expanded1;
      current_->GetOverlappingInputs(level + 1, &new_start, &new_limit,
                                     &expanded1);
      if (expanded1.size() == c->inputs_[1].size()) {
        // no expand in l2
        smallest = new_start;
        largest = new_limit;
        c->inputs_[0] = expanded0;
        c->inputs_[1] = expanded1;
        GetRange2(c->inputs_[0], c->inputs_[1], &all_start, &all_limit);
      }
    }
  }

  compact_pointer_ = largest.Encode().ToString();
  c->edit_.SetCompactPointer(largest);
}

//——————————————————————————————————————————————————
// memcompaction
MemCompaction::MemCompaction(const Options* options)
    : max_output_table_size_(MaxTableSizeForLevel(options)),
      input_version_(nullptr),
      seen_key_(false) {}
MemCompaction::~MemCompaction() {
  if (input_version_ != nullptr) {
    input_version_->Unref();
  }
}
bool MemCompaction::IsTrivialMove() const {
  return (num_input_tables(0) == 1 && num_input_tables(1) == 0);
}
void MemCompaction::AddInputDeletions(MemVersionEdit* edit) {
  for (int which = 0; which < 2; which++) {
    for (size_t i = 0; i < inputs_[which].size(); i++) {
      edit->RemoveTable(which, inputs_[which][i]->number);
      
    }
  }
}
void MemCompaction::ReleaseInputs() {
  if (input_version_ != nullptr) {
    input_version_->Unref();
    input_version_ = nullptr;
  }
}

}  // namespace leveldb
