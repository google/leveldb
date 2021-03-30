  
  #include "mem_version_edit.h"
  namespace leveldb {


  void MemVersionEdit::Clear(){
    comparator_.clear();

    last_sequence_ = 0;
    next_table_number_ = 0;
    has_comparator_ = false;
    has_next_table_number_ = false;
    has_last_sequence_ = false;
    deleted_tables_.clear();
    new_tables_.clear();
  };
  
}