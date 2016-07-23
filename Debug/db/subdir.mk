################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CC_SRCS += \
../db/autocompact_test.cc \
../db/builder.cc \
../db/c.cc \
../db/corruption_test.cc \
../db/db_bench.cc \
../db/db_impl.cc \
../db/db_iter.cc \
../db/db_test.cc \
../db/dbformat.cc \
../db/dbformat_test.cc \
../db/dumpfile.cc \
../db/fault_injection_test.cc \
../db/filename.cc \
../db/filename_test.cc \
../db/leveldbutil.cc \
../db/log_reader.cc \
../db/log_test.cc \
../db/log_writer.cc \
../db/memtable.cc \
../db/recovery_test.cc \
../db/repair.cc \
../db/skiplist_test.cc \
../db/ssd_cache.cc \
../db/table_cache.cc \
../db/version_edit.cc \
../db/version_edit_test.cc \
../db/version_set.cc \
../db/version_set_test.cc \
../db/write_batch.cc \
../db/write_batch_test.cc 

C_SRCS += \
../db/c_test.c 

CC_DEPS += \
./db/autocompact_test.d \
./db/builder.d \
./db/c.d \
./db/corruption_test.d \
./db/db_bench.d \
./db/db_impl.d \
./db/db_iter.d \
./db/db_test.d \
./db/dbformat.d \
./db/dbformat_test.d \
./db/dumpfile.d \
./db/fault_injection_test.d \
./db/filename.d \
./db/filename_test.d \
./db/leveldbutil.d \
./db/log_reader.d \
./db/log_test.d \
./db/log_writer.d \
./db/memtable.d \
./db/recovery_test.d \
./db/repair.d \
./db/skiplist_test.d \
./db/ssd_cache.d \
./db/table_cache.d \
./db/version_edit.d \
./db/version_edit_test.d \
./db/version_set.d \
./db/version_set_test.d \
./db/write_batch.d \
./db/write_batch_test.d 

OBJS += \
./db/autocompact_test.o \
./db/builder.o \
./db/c.o \
./db/c_test.o \
./db/corruption_test.o \
./db/db_bench.o \
./db/db_impl.o \
./db/db_iter.o \
./db/db_test.o \
./db/dbformat.o \
./db/dbformat_test.o \
./db/dumpfile.o \
./db/fault_injection_test.o \
./db/filename.o \
./db/filename_test.o \
./db/leveldbutil.o \
./db/log_reader.o \
./db/log_test.o \
./db/log_writer.o \
./db/memtable.o \
./db/recovery_test.o \
./db/repair.o \
./db/skiplist_test.o \
./db/ssd_cache.o \
./db/table_cache.o \
./db/version_edit.o \
./db/version_edit_test.o \
./db/version_set.o \
./db/version_set_test.o \
./db/write_batch.o \
./db/write_batch_test.o 

C_DEPS += \
./db/c_test.d 


# Each subdirectory must supply rules for building sources it contributes
db/%.o: ../db/%.cc
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C++ Compiler'
	g++ -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

db/%.o: ../db/%.c
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C Compiler'
	gcc -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


