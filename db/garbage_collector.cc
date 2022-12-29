#include "db/garbage_collector.h"
#include "leveldb/slice.h"
#include "db/write_batch_internal.h"
#include "db/db_impl.h"
#include "fcntl.h"
#include "db/filename.h"
#include "db/version_edit.h"
namespace leveldb{

void GarbageCollector::SetVlog(uint64_t vlog_number, uint64_t garbage_beg_pos)
{
    SequentialFile* vlr_file;
    db_->options_.env->NewSequentialFile(VLogFileName(db_->dbname_, vlog_number), &vlr_file);
    vlog_reader_ = new log::VReader(vlr_file, true,0);
    vlog_number_ = vlog_number;
    garbage_pos_ = garbage_beg_pos;
}

void GarbageCollector::BeginGarbageCollect(VersionEdit* edit, bool* save_edit)
{
    *save_edit = false;
    uint64_t garbage_pos = garbage_pos_;
    Slice record;
    std::string str;
    WriteOptions write_options;
    if(garbage_pos_ > 0)
    {
        if(!vlog_reader_->SkipToPos(garbage_pos_))//从指定位置开始回收
        {
            Log(db_->options_.info_log,"clean vlog %lu false because of SkipToPos %lu false",vlog_number_,garbage_pos_);
        }
    }
    Log(db_->options_.info_log,"begin clean from %lu in vlog%lu\n", garbage_pos_, vlog_number_);

    Slice key,value;
    WriteBatch batch, clean_valid_batch;
    std::string val;
    bool isEndOfFile = false;
    while(!db_->IsShutDown())//db关了
    {
        int head_size = 0;
        if(!vlog_reader_->ReadRecord(&record, &str, head_size))//读日志记录读取失败了
        {
            isEndOfFile = true;
            break;
        }

        garbage_pos_ += head_size;
        WriteBatchInternal::SetContents(&batch, record);//会把record的内容拷贝到batch中去
        ReadOptions read_options;
        uint64_t size = record.size();//size是整个batch的长度，包括batch头
        uint64_t pos = 0;//是相对batch起始位置的偏移
        uint64_t old_garbage_pos = garbage_pos_;
        while(pos < size)//遍历batch看哪些kv有效
        {
            bool isDel = false;
            Status s =WriteBatchInternal::ParseRecord(&batch, pos, key, value, isDel);//解析完一条kv后pos是下一条kv的pos
            assert(s.ok());
            garbage_pos_ = old_garbage_pos + pos;

            //log文件里的delete记录可以直接丢掉，因为sst文件会记录
            if(!isDel && db_->GetPtr(read_options, key, &val).ok())
            {
                Slice val_ptr(val);
                uint32_t file_numb;
                uint64_t item_pos, item_size;
                GetVarint64(&val_ptr, &item_size);
                GetVarint32(&val_ptr, &file_numb);
                GetVarint64(&val_ptr, &item_pos);
                if(item_pos + item_size == garbage_pos_ && file_numb == vlog_number_ )
                {
                    clean_valid_batch.Put(key, value);
                }
            }
        }
        assert(pos == size);
        if(WriteBatchInternal::ByteSize(&clean_valid_batch) > db_->options_.clean_write_buffer_size)
        {//clean_write_buffer_size必须要大于12才行，12是batch的头部长，创建batch或者clear batch后的初始大小就是12
            Status s = db_->Write(write_options, &clean_valid_batch);
            assert(s.ok());
            clean_valid_batch.Clear();
        }
    }

#ifndef NDEBUG
    Log(db_->options_.info_log,"tail is %lu, last key is %s, ;last value is %s\n", garbage_pos_,key.data(),value.data());
    if(db_->IsShutDown())
        Log(db_->options_.info_log," clean stop by shutdown\n");
    else if(isEndOfFile)
        Log(db_->options_.info_log," clean stop by read end\n");
    else
        Log(db_->options_.info_log," clean stop by unknown reason\n");
#endif
    if(WriteBatchInternal::Count(&clean_valid_batch) > 0)
    {
        Status s = db_->Write(write_options, &clean_valid_batch);
        assert(s.ok());
        clean_valid_batch.Clear();
    }

    if(garbage_pos_ - garbage_pos > 0)
    {
        if(isEndOfFile)
        {
            std::string file_name = VLogFileName(db_->dbname_, vlog_number_);
            db_->env_->DeleteFile(file_name);
            Log(db_->options_.info_log,"clean vlog %lu ok and delete it\n", vlog_number_);
            *save_edit = false;
        }
        else
        {
            vlog_reader_->DeallocateDiskSpace(garbage_pos, garbage_pos_ - garbage_pos);
            edit->SetTailInfo(vlog_number_, garbage_pos_);
            *save_edit = true;
        }
    }
}
}
