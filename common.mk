# Copyright (c) 2011 The LevelDB Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file. See the AUTHORS file for names of contributors.

# Definitions of build variable common between Android and non-Android builds.

# Also add /include directory for extra leveldb includes.
C_INCLUDES = . ./include

SOURCES = \
	./db/builder.cc \
	./db/c.cc \
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
	./util/status.cc
