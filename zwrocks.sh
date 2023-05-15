#!/bin/bash
: << "END" 
--use_existing_keys=1
--use_existing_db=1
key 范围设置
value 大小设置 
--threads="$thread"\
--max_background_jobs="$thread" \

- blob db 配置

  blob_file_starting_level=0
  blob_compaction_readahead_size=0
  blob_garbage_collection_force_threshold=1.000000
  enable_blob_garbage_collection=false
  min_blob_size=0
  enable_blob_files=false
  prepopulate_blob_cache=kDisable
  blob_file_size=268435456
  blob_compression_type=kNoCompression
  blob_garbage_collection_age_cutoff=0.250000


                        --enable_blob_files=true\
             --min_blob_size=100\
             --enable_blob_garbage_collection=true\
END



BENCH="fillrandom,stats,compact,levelstats,memstats,sstables"
BENCH_R="readrandom,stats,compact,levelstats,memstats,sstables"
ZKEY=("16")
# ZVALUE=("496")
ZVALUE=("496" "1008" "4080" "8192" "16368")
# ZVALUE=("112" "240" "496" "1008" "2032" "4080" "8192" "16368"  "65520")
# ZVALUE=("112" "240" "496" "1008")
thread=("1")

DB="/home/eros/workspace/tmp/"
DBc="/home/eros/workspace/tmp/cpu/"
DBi="/home/eros/workspace/tmp/IO/"
DBm="/home/eros/workspace/tmp/mem/"

# for write
restext="Wresult.txt"
sarcpu="Wsarcpu.txt"
sario="Wsario.txt"
sarmem="Wsarmem.txt"
# sarblock="sarablock.txt"

# for read
restext_R="Rresult.txt"
sarcpu_R="Rsarcpu.txt"
sario_R="Rsario.txt"
sarmem_R="Rsarmem.txt"


sartxt="Wsar"
sartxt_R="Rsar"

# cpuUsage=`top -b -n1 | fgrep "Cpu" | awk '{print 100-$8,"%"}'`
# mem_used_persent=`free -m | fgrep "Mem" | awk '{printf "%d", ($3)/$2*100}'`
# diskUsage=`df -h | fgrep "/dev/sdb2" | awk '{print $5}'`

 
mkdir -p "$DB"
mkdir -p "$DBc"
mkdir -p "$DBi" 
mkdir -p "$DBm"   


for key in "${ZKEY[@]}"
do 
    for value in "${ZVALUE[@]}"
    do

    nums=$((10737418240/($key+$value)))

    # nums=$((85899345920/($key+$value)))
# : << "END"    
    CMD="./db_bench \
            --use_existing_db=0 \
            --benchmarks="$BENCH" \
            --num="$nums" \
            --histogram=true \
                        --enable_blob_files=true\
             --min_blob_size=100\
             --enable_blob_garbage_collection=true\

            --compaction_style=0 \
            --key_size="$key" \
            --value_size="$value" \
            --db="$DB$key$value"\
            "

            # --disable_auto_compactions=1 \
            
    resultfile="$DB$key$value$restext"

    echo "$CMD" | tee -a "$resultfile"
                # top -d 1 -b -n 10 > test.csv  &
                # echo "CPU使用率 : ${cpuUsage}  内存使用率:${mem_used_persent} 磁盘使用率:${diskUsage}"  | tee -a "$resultfile"
                
                sar -d -p -b -r -o "$DB$value" 1 &
                
                RESULT=$($CMD)
                pkill "sar";
                echo "$RESULT" | tee -a "$resultfile"
                echo | tee -a "$resultfile"
                

                sar -u ALL -f "$DB$value" > "$DBc$key$value$sarcpu"
                sar -b -f "$DB$value" > "$DBi$key$value$sario"
                sar -r -f "$DB$value" > "$DBm$key$value$sarmem"
                #sar -d -p -f "$DB" >"$DB$key$value$sarblock"
                python3 /home/eros/workspace/rocksdb8.1.0/zcpu.py "$DBc$key$value$sartxt"
                python3 /home/eros/workspace/rocksdb8.1.0/zio.py "$DBi$key$value$sartxt"
                python3 /home/eros/workspace/rocksdb8.1.0/zmem.py "$DBm$key$value$sartxt"
                

                # echo 1 > /proc/sys/vm/drop_caches

                # 删除memcached缓存
                # echo "flush_all" | nc

                # 查看内存
                # du -sh tmp
# END
# for read 
: << 'END'
 


    CMD="./db_bench \
            --use_existing_db=1 \
            --benchmarks="$BENCH_R" \
            --num="$nums" \
            --histogram=true \
            --compaction_style=0 \
            --key_size="$key" \
            --value_size="$value" \
            --db="$DB$key$value"\
            "
            #  --enable_blob_files=true\
            #  --min_blob_size=100\
            # --enable_blob_garbage_collection=true\

    resultfile="$DB$key$value$restext_R"
    echo "$CMD" | tee -a "$resultfile"
                # top -d 1 -b -n 10 > test.csv  &
                # echo "CPU使用率 : ${cpuUsage}  内存使用率:${mem_used_persent} 磁盘使用率:${diskUsage}"  | tee -a "$resultfile"
                # sar -d -p -b -r -o "$DB$value"_R 1 &
                RESULT=$($CMD)
                # pkill "sar";
                echo "$RESULT" | tee -a "$resultfile"
                echo | tee -a "$resultfile"
: << 'END'
                sar -u ALL -f "$DB$value"_R > "$DBc$key$value$sarcpu_R"
                sar -b -f "$DB$value"_R > "$DBi$key$value$sario_R"
                sar -r -f "$DB$value"_R > "$DBm$key$value$sarmem_R"
                #sar -d -p -f "$DB" >"$DB$key$value$sarblock"
                python3 /home/eros/workspace/rocksdb/zcpu.py "$DBc$key$value$sartxt_R"
                python3 /home/eros/workspace/rocksdb/zio.py "$DBi$key$value$sartxt_R"
                python3 /home/eros/workspace/rocksdb/zmem.py "$DBm$key$value$sartxt_R"

    echo 3 > /proc/sys/vm/drop_caches


END


    done
done

# tune2fs -l /dev/sdb2 | grep -i 'block size'