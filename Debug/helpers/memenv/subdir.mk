################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CC_SRCS += \
../helpers/memenv/memenv.cc \
../helpers/memenv/memenv_test.cc 

CC_DEPS += \
./helpers/memenv/memenv.d \
./helpers/memenv/memenv_test.d 

OBJS += \
./helpers/memenv/memenv.o \
./helpers/memenv/memenv_test.o 


# Each subdirectory must supply rules for building sources it contributes
helpers/memenv/%.o: ../helpers/memenv/%.cc
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C++ Compiler'
	g++ -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


