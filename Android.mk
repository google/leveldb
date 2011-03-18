# Copyright (c) 2011 The LevelDB Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file. See the AUTHORS file for names of contributors.

# INSTRUCTIONS
# After you've downloaded and installed the Android NDK from:
# http://developer.android.com/sdk/ndk/index.html
# 1. In the same directory as this file, Android.mk, type:
#    $ ln -s leveldb ../jni
#    (The Android NDK will only build native projects in 
#     subdirectories named "jni".)
# 2. $ cd ..
# 3. Execute ndk-build:
#    $ $(ANDROID_NDK_DIR)/ndk-build

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := leveldb
# Build flags:
# - LEVELDB_PLATFORM_ANDROID to use the correct port header: port_android.h
LOCAL_CFLAGS := -DLEVELDB_PLATFORM_ANDROID -std=gnu++0x
LOCAL_C_INCLUDES := $(LOCAL_PATH)/../../
LOCAL_CPP_EXTENSION := .cc

LOCAL_SRC_FILES := ./db/builder.cc \
./db/db_bench.cc \
./db/db_impl.cc \
./db/db_iter.cc \
./db/filename.cc \
./db/dbformat.cc \
./db/log_reader.cc \
./db/log_writer.cc \
./db/memtable.cc \
./db/repair.cc \
./db/table_cache.cc \
./db/version_edit.cc \
./db/version_set.cc \
./db/write_batch.cc \
./port/port_android.cc \
./table/block.cc \
./table/block_builder.cc \
./table/format.cc \
./table/iterator.cc \
./table/merger.cc \
./table/table.cc \
./table/table_builder.cc \
./table/two_level_iterator.cc \
./util/arena.cc \
./util/cache.cc \
./util/coding.cc \
./util/comparator.cc \
./util/crc32c.cc \
./util/env.cc \
./util/env_posix.cc \
./util/hash.cc \
./util/histogram.cc \
./util/logging.cc \
./util/options.cc \
./util/status.cc \
./util/testharness.cc \
./util/testutil.cc

include $(BUILD_SHARED_LIBRARY)
