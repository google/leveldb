
- mio
Our experimental studies reveal that the performance bottleneck of LSM-tree based KV stores using NVMs mainly stems from (1) costly data serialization/deserialization across memory and storage

- novelsm
Serialization of in-memory data to SSTable storage blocks

Deserialization of block data to in-memory data during read

In-memory structures must be serialized to block format

----
serialize: 
- builder.cc
>    for (; iter->Valid(); iter->Next()) 
>      
>       s = builder->Finish();

----
deserialize:
- table.cc

> Table::InternalGet
>
> (*handle_result)(arg, block_iter->key(), block_iter->value());


uftrace record 

uftrace replay -C ____함수 이름__

uftrace report 

uftrace tui


