################################################################################
# Automatically-generated file. Do not edit!
################################################################################

-include ../makefile.init

RM := rm -rf

# All of the sources participating in the build are defined here
-include sources.mk
-include util/subdir.mk
-include table/subdir.mk
-include port/subdir.mk
-include out-static/util/subdir.mk
-include out-static/table/subdir.mk
-include out-static/port/subdir.mk
-include out-static/helpers/memenv/subdir.mk
-include out-static/db/subdir.mk
-include out-shared/util/subdir.mk
-include out-shared/table/subdir.mk
-include out-shared/port/subdir.mk
-include out-shared/db/subdir.mk
-include issues/subdir.mk
-include helpers/memenv/subdir.mk
-include doc/bench/subdir.mk
-include db/subdir.mk
-include subdir.mk
-include objects.mk

ifneq ($(MAKECMDGOALS),clean)
ifneq ($(strip $(CC_DEPS)),)
-include $(CC_DEPS)
endif
ifneq ($(strip $(C++_DEPS)),)
-include $(C++_DEPS)
endif
ifneq ($(strip $(C_UPPER_DEPS)),)
-include $(C_UPPER_DEPS)
endif
ifneq ($(strip $(CXX_DEPS)),)
-include $(CXX_DEPS)
endif
ifneq ($(strip $(CPP_DEPS)),)
-include $(CPP_DEPS)
endif
ifneq ($(strip $(C_DEPS)),)
-include $(C_DEPS)
endif
endif

-include ../makefile.defs

# Add inputs and outputs from these tool invocations to the build variables 

# All Target
all: leveldb5

# Tool invocations
leveldb5: $(OBJS) $(USER_OBJS)
	@echo 'Building target: $@'
	@echo 'Invoking: GCC C++ Linker'
	g++  -o "leveldb5" $(OBJS) $(USER_OBJS) $(LIBS)
	@echo 'Finished building target: $@'
	@echo ' '

# Other Targets
clean:
	-$(RM) $(CC_DEPS)$(C++_DEPS)$(EXECUTABLES)$(C_UPPER_DEPS)$(CXX_DEPS)$(OBJS)$(CPP_DEPS)$(C_DEPS) leveldb5
	-@echo ' '

.PHONY: all clean dependents
.SECONDARY:

-include ../makefile.targets
