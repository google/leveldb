################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CC_SRCS += \
../table/block.cc \
../table/block_builder.cc \
../table/filter_block.cc \
../table/filter_block_test.cc \
../table/format.cc \
../table/iterator.cc \
../table/merger.cc \
../table/table.cc \
../table/table_builder.cc \
../table/table_test.cc \
../table/two_level_iterator.cc 

CC_DEPS += \
./table/block.d \
./table/block_builder.d \
./table/filter_block.d \
./table/filter_block_test.d \
./table/format.d \
./table/iterator.d \
./table/merger.d \
./table/table.d \
./table/table_builder.d \
./table/table_test.d \
./table/two_level_iterator.d 

OBJS += \
./table/block.o \
./table/block_builder.o \
./table/filter_block.o \
./table/filter_block_test.o \
./table/format.o \
./table/iterator.o \
./table/merger.o \
./table/table.o \
./table/table_builder.o \
./table/table_test.o \
./table/two_level_iterator.o 


# Each subdirectory must supply rules for building sources it contributes
table/%.o: ../table/%.cc
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C++ Compiler'
	g++ -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


