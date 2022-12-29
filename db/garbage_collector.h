
#ifndef STORAGE_LEVELDB_DB_GARBAGE_COLLECTOR_H_
#define STORAGE_LEVELDB_DB_GARBAGE_COLLECTOR_H_

#include "stdint.h"
#include "db/vlog_reader.h"

namespace leveldb{
class VReader;
class DBImpl;
class VersionEdit;

class GarbageCollector
{
    public:
        GarbageCollector(DBImpl* db):vlog_number_(0), garbage_pos_(0), vlog_reader_(NULL), db_(db){}
        ~GarbageCollector(){delete vlog_reader_;}
        void SetVlog(uint64_t vlog_number, uint64_t garbage_beg_pos=0);
        void BeginGarbageCollect(VersionEdit* edit, bool* save_edit);

    private:
        uint64_t vlog_number_;
        uint64_t garbage_pos_;//vlog文件起始垃圾回收的地方
        log::VReader* vlog_reader_;
        DBImpl* db_;
};

}

#endif
