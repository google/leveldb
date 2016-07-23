################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CC_SRCS += \
../issues/issue178_test.cc \
../issues/issue200_test.cc 

CC_DEPS += \
./issues/issue178_test.d \
./issues/issue200_test.d 

OBJS += \
./issues/issue178_test.o \
./issues/issue200_test.o 


# Each subdirectory must supply rules for building sources it contributes
issues/%.o: ../issues/%.cc
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C++ Compiler'
	g++ -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


