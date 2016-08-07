################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CC_SRCS += \
../util/arena.cc \
../util/arena_test.cc \
../util/bloom.cc \
../util/bloom_test.cc \
../util/cache.cc \
../util/cache_test.cc \
../util/coding.cc \
../util/coding_test.cc \
../util/comparator.cc \
../util/crc32c.cc \
../util/crc32c_test.cc \
../util/env.cc \
../util/env_posix.cc \
../util/env_test.cc \
../util/filter_policy.cc \
../util/hash.cc \
../util/hash_test.cc \
../util/histogram.cc \
../util/logging.cc \
../util/options.cc \
../util/status.cc \
../util/testharness.cc \
../util/testutil.cc 

CC_DEPS += \
./util/arena.d \
./util/arena_test.d \
./util/bloom.d \
./util/bloom_test.d \
./util/cache.d \
./util/cache_test.d \
./util/coding.d \
./util/coding_test.d \
./util/comparator.d \
./util/crc32c.d \
./util/crc32c_test.d \
./util/env.d \
./util/env_posix.d \
./util/env_test.d \
./util/filter_policy.d \
./util/hash.d \
./util/hash_test.d \
./util/histogram.d \
./util/logging.d \
./util/options.d \
./util/status.d \
./util/testharness.d \
./util/testutil.d 

OBJS += \
./util/arena.o \
./util/arena_test.o \
./util/bloom.o \
./util/bloom_test.o \
./util/cache.o \
./util/cache_test.o \
./util/coding.o \
./util/coding_test.o \
./util/comparator.o \
./util/crc32c.o \
./util/crc32c_test.o \
./util/env.o \
./util/env_posix.o \
./util/env_test.o \
./util/filter_policy.o \
./util/hash.o \
./util/hash_test.o \
./util/histogram.o \
./util/logging.o \
./util/options.o \
./util/status.o \
./util/testharness.o \
./util/testutil.o 


# Each subdirectory must supply rules for building sources it contributes
util/%.o: ../util/%.cc
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C++ Compiler'
	g++ -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


