################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CC_SRCS += \
../doc/bench/db_bench_sqlite3.cc \
../doc/bench/db_bench_tree_db.cc 

CC_DEPS += \
./doc/bench/db_bench_sqlite3.d \
./doc/bench/db_bench_tree_db.d 

OBJS += \
./doc/bench/db_bench_sqlite3.o \
./doc/bench/db_bench_tree_db.o 


# Each subdirectory must supply rules for building sources it contributes
doc/bench/%.o: ../doc/bench/%.cc
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C++ Compiler'
	g++ -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


