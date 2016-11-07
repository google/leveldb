
#include "db/ssd_cache.h"
#include "util/hash.h"
#include <iostream>
#include<stdlib.h>
#include<stdio.h>
#include <string>
#include <stdint.h>
#include <vector>
#include "db/dbformat.h"
#include "db/log_writer.h"
#include "db/snapshot.h"
#include "leveldb/db.h"
#include "leveldb/env.h"
//#include "port/port.h"
#include "port/thread_annotations.h"
#include "leveldb/cache.h"
#include "leveldb/table.h"


//#include "leveldb/status.h"

namespace leveldb{

  SSD_cache::SSD_cache (const Options& options,uint64_t cachesize)
  :leftblock_(blocknum_), firstfree_(0)
  {
	//1 创建文件
	//2 构建每个Block的初始状态
	std::string  filename;
	FILE* file;
	filename = ssdpath_ +  cache_filename_;
	std::cout<<filename<<std::endl;
	FILE*	 fp = fopen(filename.c_str(),"wb+");
	fseek(fp,cachesize,SEEK_SET);
	fputc(EOF,fp);
	fclose(fp);

	//blocks_ = new std::vector<class Cache_block> (blocknum_ );
	blocks_.resize(blocknum_);
	//leftblock_ = blocknum_;

	//auto it = blocks_.begin();
	auto tail = blocks_.end() -1;
	for (auto it = blocks_.begin();it!=blocks_.end();it++){
		Cache_block& tmp = *it;
		tmp.sethash( it - blocks_.begin());
		tmp.setnexthash(  tmp.gethash());
		if (it!=tail)
			tmp.setnextfree( tmp.gethash()+1);
	}

return;
}

  Status SSD_cache::Lookup(RandomAccessFile* file, BlockContents* result){
	 std::string filename;
	 uint32_t hash;
	 //Cache_block& block;
	 Status s;
	 filename = file->GetFilename();
	 size_t a=5;
	hash = leveldb::Hash(filename.c_str(),filename.size(),100);
	 //block = blocks_.at(hash);
	 return s;
 }

  bool Cache_block::Fileequal(std::string& targetfile){
	  if (fname_ == targetfile)
		  return true;
	  else
		  return false;
  }

}
