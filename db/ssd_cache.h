#ifndef STORAGE_LEVELDB_DB_SSD_CACHE_H_
#define STORAGE_LEVELDB_DB_SSD_CACHE_H_

#include <string>
#include <stdint.h>
#include <vector>
#include "table/format.h"
#include "include/leveldb/env.h"
#include "db/dbformat.h"
#include "db/log_writer.h"
#include "db/snapshot.h"
#include "leveldb/db.h"
#include "leveldb/env.h"
#include "port/port.h"
#include "port/thread_annotations.h"
#include "leveldb/cache.h"
#include "leveldb/table.h"

namespace leveldb{
class Cache_block;
class SSD_cache{
public:
	// create a indexcache which size is cachesize Bytes
	// this function will ask for enough space in SSD and init it
	SSD_cache() {//std::cout << "ssd cache test"<<std::endl;

	}
	SSD_cache(const Options& options,uint64_t cachesize);
	//release a index_cache
	virtual ~SSD_cache(){}

	// provide the same interaction with Table::open by looking up the cache
	// if fail to look,returns NULL
	Status Lookup(RandomAccessFile* file, BlockContents* result);

	//insert the index_block into the cache
	virtual Status Insert(const Options& options,
            RandomAccessFile* file,
            uint64_t file_size,
            Table  table){}


private:
	const std::string ssdpath_ = "/tmp/vssd/";

	const std::string cache_filename_ = "indexcache";

	// How many bytes in a block
	const uint64_t blocknum_ = 100;

	//How many Bytes in  the cache
	uint64_t  cachesize_;

	uint64_t leftblock_;

	//using std::vector;
	std::vector<class Cache_block> blocks_;

	uint64_t lastestnode_;
	uint64_t oldestnode_;
	uint64_t firstfree_;

};

class Cache_block{
public:
	Cache_block(){}
	uint64_t  getseek();
	uint64_t  gethash(){return this->hash_;}
	void sethash(uint64_t hash){this->hash_ =  hash;}
	uint64_t  getnextlastnode();
	uint64_t  getnextoldnode();
	uint64_t getnexthash(){return this->nexthash_;}
	void setnexthash(uint64_t nexthash){this->nexthash_ =  nexthash;}
	uint64_t getnextfree(){return this->nextfree_;}
	void setnextfree(uint64_t nextfree){this->nextfree_ =  nextfree;}
	bool Fileequal(std::string& targetfile);

private:
		std::string fname_;
	    uint64_t seek_;
	    uint64_t size_;
		uint64_t hash_;

		uint64_t nextlastnode_;
		uint64_t nextoldnode_;
		uint64_t nexthash_;
		uint64_t nextfree_;
};
}

#endif // STORAGE_LEVELDB_DB_SSD_CACHE_H_
