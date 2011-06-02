# Copyright (c) 2011 The LevelDB Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file. See the AUTHORS file for names of contributors.

CC = g++

#-----------------------------------------------
# Uncomment exactly one of the lines labelled (A), (B), and (C) below
# to switch between compilation modes.

OPT = -O2 -DNDEBUG       # (A) Production use (optimized mode)
# OPT = -g2              # (B) Debug mode, w/ full line-level debugging symbols
# OPT = -O2 -g2 -DNDEBUG # (C) Profiling mode: opt, but w/debugging symbols
#-----------------------------------------------


UNAME := $(shell uname)

ifeq ($(UNAME), Darwin)
# To build for iOS, set PLATFORM=IOS.
ifndef PLATFORM
PLATFORM=OSX
endif # PLATFORM
PLATFORM_CFLAGS = -DLEVELDB_PLATFORM_OSX
PORT_MODULE = port_osx.o
else # UNAME
PLATFORM_CFLAGS = -DLEVELDB_PLATFORM_POSIX -std=c++0x
PORT_MODULE = port_posix.o
endif # UNAME

CFLAGS = -c -I. -I./include $(PLATFORM_CFLAGS) $(OPT)

LDFLAGS=-lpthread

LIBOBJECTS = \
	./db/builder.o \
	./db/db_impl.o \
	./db/db_iter.o \
	./db/filename.o \
	./db/dbformat.o \
	./db/log_reader.o \
	./db/log_writer.o \
	./db/memtable.o \
	./db/repair.o \
	./db/table_cache.o \
	./db/version_edit.o \
	./db/version_set.o \
	./db/write_batch.o \
	./port/$(PORT_MODULE) \
	./table/block.o \
	./table/block_builder.o \
	./table/format.o \
	./table/iterator.o \
	./table/merger.o \
	./table/table.o \
	./table/table_builder.o \
	./table/two_level_iterator.o \
	./util/arena.o \
	./util/cache.o \
	./util/coding.o \
	./util/comparator.o \
	./util/crc32c.o \
	./util/env.o \
	./util/env_posix.o \
	./util/hash.o \
	./util/histogram.o \
	./util/logging.o \
	./util/options.o \
	./util/status.o

TESTUTIL = ./util/testutil.o
TESTHARNESS = ./util/testharness.o $(TESTUTIL)

TESTS = \
	arena_test \
	cache_test \
	coding_test \
	corruption_test \
	crc32c_test \
	db_test \
	dbformat_test \
	env_test \
	filename_test \
	log_test \
	skiplist_test \
	table_test \
	version_edit_test \
	write_batch_test

PROGRAMS = db_bench $(TESTS)

LIBRARY = libleveldb.a

ifeq ($(PLATFORM), IOS)
# Only XCode can build executable applications for iOS.
all: $(LIBRARY)
else
all: $(PROGRAMS) $(LIBRARY)
endif

check: $(TESTS)
	for t in $(TESTS); do echo "***** Running $$t"; ./$$t || exit 1; done

clean:
	-rm -f $(PROGRAMS) $(LIBRARY) */*.o ios-x86/*/*.o ios-arm/*/*.o
	-rmdir -p ios-x86/* ios-arm/*

$(LIBRARY): $(LIBOBJECTS)
	rm -f $@
	$(AR) -rs $@ $(LIBOBJECTS)

db_bench: db/db_bench.o $(LIBOBJECTS) $(TESTUTIL)
	$(CC) $(LDFLAGS) db/db_bench.o $(LIBOBJECTS) $(TESTUTIL) -o $@

arena_test: util/arena_test.o $(LIBOBJECTS) $(TESTHARNESS)
	$(CC) $(LDFLAGS) util/arena_test.o $(LIBOBJECTS) $(TESTHARNESS) -o $@

cache_test: util/cache_test.o $(LIBOBJECTS) $(TESTHARNESS)
	$(CC) $(LDFLAGS) util/cache_test.o $(LIBOBJECTS) $(TESTHARNESS) -o $@

coding_test: util/coding_test.o $(LIBOBJECTS) $(TESTHARNESS)
	$(CC) $(LDFLAGS) util/coding_test.o $(LIBOBJECTS) $(TESTHARNESS) -o $@

corruption_test: db/corruption_test.o $(LIBOBJECTS) $(TESTHARNESS)
	$(CC) $(LDFLAGS) db/corruption_test.o $(LIBOBJECTS) $(TESTHARNESS) -o $@

crc32c_test: util/crc32c_test.o $(LIBOBJECTS) $(TESTHARNESS)
	$(CC) $(LDFLAGS) util/crc32c_test.o $(LIBOBJECTS) $(TESTHARNESS) -o $@

db_test: db/db_test.o $(LIBOBJECTS) $(TESTHARNESS)
	$(CC) $(LDFLAGS) db/db_test.o $(LIBOBJECTS) $(TESTHARNESS) -o $@

dbformat_test: db/dbformat_test.o $(LIBOBJECTS) $(TESTHARNESS)
	$(CC) $(LDFLAGS) db/dbformat_test.o $(LIBOBJECTS) $(TESTHARNESS) -o $@

env_test: util/env_test.o $(LIBOBJECTS) $(TESTHARNESS)
	$(CC) $(LDFLAGS) util/env_test.o $(LIBOBJECTS) $(TESTHARNESS) -o $@

filename_test: db/filename_test.o $(LIBOBJECTS) $(TESTHARNESS)
	$(CC) $(LDFLAGS) db/filename_test.o $(LIBOBJECTS) $(TESTHARNESS) -o $@

log_test: db/log_test.o $(LIBOBJECTS) $(TESTHARNESS)
	$(CC) $(LDFLAGS) db/log_test.o $(LIBOBJECTS) $(TESTHARNESS) -o $@

table_test: table/table_test.o $(LIBOBJECTS) $(TESTHARNESS)
	$(CC) $(LDFLAGS) table/table_test.o $(LIBOBJECTS) $(TESTHARNESS) -o $@

skiplist_test: db/skiplist_test.o $(LIBOBJECTS) $(TESTHARNESS)
	$(CC) $(LDFLAGS) db/skiplist_test.o $(LIBOBJECTS) $(TESTHARNESS) -o $@

version_edit_test: db/version_edit_test.o $(LIBOBJECTS) $(TESTHARNESS)
	$(CC) $(LDFLAGS) db/version_edit_test.o $(LIBOBJECTS) $(TESTHARNESS) -o $@

write_batch_test: db/write_batch_test.o $(LIBOBJECTS) $(TESTHARNESS)
	$(CC) $(LDFLAGS) db/write_batch_test.o $(LIBOBJECTS) $(TESTHARNESS) -o $@

ifeq ($(PLATFORM), IOS)
# For iOS, create universal object files to be used on both the simulator and
# a device.
.cc.o:
	mkdir -p ios-x86/$(dir $@)
	$(CC) $(CFLAGS) -isysroot /Developer/Platforms/iPhoneSimulator.platform/Developer/SDKs/iPhoneSimulator4.3.sdk -arch i686 $< -o ios-x86/$@
	mkdir -p ios-arm/$(dir $@)
	$(CC) $(CFLAGS) -isysroot /Developer/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS4.3.sdk -arch armv6 -arch armv7 $< -o ios-arm/$@
	lipo ios-x86/$@ ios-arm/$@ -create -output $@
else
.cc.o:
	$(CC) $(CFLAGS) $< -o $@
endif

# TODO(gabor): dependencies for .o files
# TODO(gabor): Build library
