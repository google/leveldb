# Copyright (c) 2011 The LevelDB Authors. All rights reserved.                 # Use of this source code is governed by a BSD-style license that can be       # found in the LICENSE file. See the AUTHORS file for names of contributors.   

# To build for Android, add the Android NDK to your path and type 'ndk-build'.

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

include common.mk

LOCAL_MODULE := leveldb
LOCAL_C_INCLUDES := $(C_INCLUDES)
LOCAL_CPP_EXTENSION := .cc
LOCAL_CFLAGS := -DLEVELDB_PLATFORM_ANDROID -std=gnu++0x
LOCAL_SRC_FILES := $(SOURCES:%.cc=../%.cc) ../port/port_android.cc

include $(BUILD_SHARED_LIBRARY)
