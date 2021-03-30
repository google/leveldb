#ifndef STORAGE_LEVELDB_DB_MEM_VERSION_EDIT_H_
#define STORAGE_LEVELDB_DB_MEM_VERSION_EDIT_H_

#include <set>
#include <utility>
#include <vector>
#include "db/memtable.h"
#include "db/dbformat.h"

namespace leveldb {

class MemVersionSet;

//memtable meta
struct MemTableMetaData {
  MemTableMetaData() : refs(0), table_size(0) {}

  int refs;
  uint64_t number;
  uint64_t table_size;
  MemTable* mem_table;
  InternalKey smallest;
  InternalKey largest;
};
class MemVersionEdit {
 public:
  MemVersionEdit() { Clear(); }
  ~MemVersionEdit() = default;

  void Clear();

  void SetComparatorName(const Slice& name) {
    has_comparator_ = true;
    comparator_ = name.ToString();
  }

  void SetNextTable(uint64_t num) {
    has_next_table_number_ = true;
    next_table_number_ = num;
  }
  void SetLastSequence(SequenceNumber seq) {
    has_last_sequence_ = true;
    last_sequence_ = seq;
  }
  void SetCompactPointer(const InternalKey& key) {
    compact_pointers_.push_back(key);
  }


  //add table to new_tables_
  void AddTable(int level, uint64_t num, uint64_t size,MemTable* table,
               const InternalKey& smallest, const InternalKey& largest) {
    MemTableMetaData m;
    m.number = num;
    m.table_size = size;
    m.mem_table = table;
    m.smallest = smallest;
    m.largest = largest;
    new_tables_.push_back(std::make_pair(level, m));
  }

  //deleted tables
  void RemoveTable(int level, uint64_t num) {
    deleted_tables_.insert(std::make_pair(level, num));
  }
 private:
  friend class MemVersionSet;

  typedef std::set<std::pair<int, uint64_t> > DeletedTableSet;

  //dont need to log
  std::string comparator_;
  uint64_t next_table_number_;
  SequenceNumber last_sequence_;
  bool has_comparator_;
  bool has_next_table_number_;
  bool has_last_sequence_;

  std::vector<InternalKey> compact_pointers_;//only m1 level
  DeletedTableSet deleted_tables_;
  std::vector<std::pair<int, MemTableMetaData> > new_tables_;
};

}  // namespace leveldb

#endif  // STORAGE_LEVELDB_DB_VERSION_EDIT_H_
