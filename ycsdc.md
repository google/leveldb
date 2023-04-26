### leveldb
```bash
git clone https://github.com/ls4154/YCSB-cpp.git
cd YCSB-cpp
git submodule update --init
make
make BIND_LEVELDB=1
```
            Modify config section in Makefile
            #---------------------build config-------------------------
            DEBUG_BUILD ?= 0
            # put your leveldb directory
            EXTRA_CXXFLAGS ?= -I/example/leveldb/include
            EXTRA_LDFLAGS ?= -L/example/leveldb/build -lsnappy

            BIND_LEVELDB ?= 1
            BIND_ROCKSDB ?= 0 
            BIND_LMDB ?= 0


### `Command`
```bash
./ycsb -load -db leveldb -P workloads/workloada -P leveldb/leveldb.properties -s
./ycsb -run -db leveldb -P workloads/workloada -P leveldb/leveldb.properties -s
./ycsb -run -db leveldb -P workloads/workloadb -P leveldb/leveldb.properties -s
./ycsb -run -db leveldb -P workloads/workloadd -P leveldb/leveldb.properties -s

```
----
