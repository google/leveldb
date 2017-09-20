# Copyright 2017 The LEVELDB Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file. See the AUTHORS file for names of contributors.

cmake_minimum_required(VERSION 3.1)
project(Leveldb VERSION 0.1.0 LANGUAGES C CXX)

# This project can take advantage of C++11.
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED OFF)
# TODO(pwnall): See if setting this to ON gives us the *_unlocked functions.
set(CMAKE_CXX_EXTENSIONS OFF)

option(LEVELDB_BUILD_TESTS "Build LevelDB's unit tests" ON)
option(LEVELDB_BUILD_BENCHMARKS "Build LevelDB's benchmarks" ON)
option(LEVELDB_INSTALL "Install LevelDB's header and library" ON)

include(TestBigEndian)
test_big_endian(LEVELDB_IS_BIG_ENDIAN)

include(CheckIncludeFile)
check_include_file("unistd.h" HAVE_UNISTD_H)

include(CheckIncludeFileCXX)
check_include_file_cxx("atomic" LEVELDB_ATOMIC_PRESENT)

include(CheckLibraryExists)
check_library_exists(crc32c crc32c_value "" HAVE_CRC32C)
check_library_exists(snappy snappy_compress "" HAVE_SNAPPY)

include(CheckSymbolExists)
check_symbol_exists(fdatasync "unistd.h" HAVE_FDATASYNC)

configure_file(
  "${PROJECT_SOURCE_DIR}/port/port_config.h.in"
  "${PROJECT_BINARY_DIR}/include/port/port_config.h"
)

include_directories(
  "${PROJECT_BINARY_DIR}/include"
  "${PROJECT_SOURCE_DIR}"
)

<<<<<<< HEAD
=======
# SSE4.2 code is built separately, so we don't accidentally compile unsupported
# instructions into code that gets run without SSE4.2 support.
add_library(leveldb_port_sse42 OBJECT "")
target_sources(leveldb_port_sse42
  PRIVATE
    "${PROJECT_BINARY_DIR}/include/port/port_config.h"
    "${PROJECT_SOURCE_DIR}/port/port_posix_sse.cc"
    "${PROJECT_SOURCE_DIR}/port/port_posix.h"
    "${PROJECT_SOURCE_DIR}/port/port.h"
)
target_compile_definitions(leveldb_port_sse42
  PRIVATE
    LEVELDB_PLATFORM_POSIX
)
if(LEVELDB_PLATFORM_POSIX_SSE)
  if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(leveldb_port_sse42 PRIVATE "/arch:AVX")
  else(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(leveldb_port_sse42 PRIVATE "-msse4.2")
  endif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
endif(LEVELDB_PLATFORM_POSIX_SSE)

>>>>>>> 02d98fe... Add CMake build support.
# POSIX code is specified separately so we can leave it out in the future.
add_library(leveldb_port_posix OBJECT "")
target_sources(leveldb_port_posix
  PRIVATE
    "${PROJECT_SOURCE_DIR}/port/port_posix.cc"

  PUBLIC
    # The headers below are dependencies for leveldb, but aren't needed by users
    # that link to the installed version of leveldb and rely on its public API.
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include/port/port_config.h>
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/port/atomic_pointer.h>
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/port/port_posix.h>
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/port/port.h>
)

add_library(leveldb ""
  # TODO(pwnall): Move the TARGET_OBJECTS generator expressions to the PRIVATE
  # section of target_sources when cmake_minimum_required becomes 3.9 or above.
  $<TARGET_OBJECTS:leveldb_port_posix>
<<<<<<< HEAD
=======
  $<TARGET_OBJECTS:leveldb_port_sse42>
>>>>>>> 02d98fe... Add CMake build support.
)
target_sources(leveldb
  PRIVATE
    "${PROJECT_SOURCE_DIR}/db/builder.cc"
    "${PROJECT_SOURCE_DIR}/db/builder.h"
    "${PROJECT_SOURCE_DIR}/db/c.cc"
    "${PROJECT_SOURCE_DIR}/db/db_impl.cc"
    "${PROJECT_SOURCE_DIR}/db/db_impl.h"
    "${PROJECT_SOURCE_DIR}/db/db_iter.cc"
    "${PROJECT_SOURCE_DIR}/db/db_iter.h"
    "${PROJECT_SOURCE_DIR}/db/dbformat.cc"
    "${PROJECT_SOURCE_DIR}/db/dbformat.h"
    "${PROJECT_SOURCE_DIR}/db/dumpfile.cc"
    "${PROJECT_SOURCE_DIR}/db/filename.cc"
    "${PROJECT_SOURCE_DIR}/db/filename.h"
    "${PROJECT_SOURCE_DIR}/db/log_format.h"
    "${PROJECT_SOURCE_DIR}/db/log_reader.cc"
    "${PROJECT_SOURCE_DIR}/db/log_reader.h"
    "${PROJECT_SOURCE_DIR}/db/log_writer.cc"
    "${PROJECT_SOURCE_DIR}/db/log_writer.h"
    "${PROJECT_SOURCE_DIR}/db/memtable.cc"
    "${PROJECT_SOURCE_DIR}/db/memtable.h"
    "${PROJECT_SOURCE_DIR}/db/repair.cc"
    "${PROJECT_SOURCE_DIR}/db/skiplist.h"
    "${PROJECT_SOURCE_DIR}/db/snapshot.h"
    "${PROJECT_SOURCE_DIR}/db/table_cache.cc"
    "${PROJECT_SOURCE_DIR}/db/table_cache.h"
    "${PROJECT_SOURCE_DIR}/db/version_edit.cc"
    "${PROJECT_SOURCE_DIR}/db/version_edit.h"
    "${PROJECT_SOURCE_DIR}/db/version_set.cc"
    "${PROJECT_SOURCE_DIR}/db/version_set.h"
    "${PROJECT_SOURCE_DIR}/db/write_batch_internal.h"
    "${PROJECT_SOURCE_DIR}/db/write_batch.cc"
    "${PROJECT_SOURCE_DIR}/port/port.h"
    "${PROJECT_SOURCE_DIR}/port/thread_annotations.h"
    "${PROJECT_SOURCE_DIR}/table/block_builder.cc"
    "${PROJECT_SOURCE_DIR}/table/block_builder.h"
    "${PROJECT_SOURCE_DIR}/table/block.cc"
    "${PROJECT_SOURCE_DIR}/table/block.h"
    "${PROJECT_SOURCE_DIR}/table/filter_block.cc"
    "${PROJECT_SOURCE_DIR}/table/filter_block.h"
    "${PROJECT_SOURCE_DIR}/table/format.cc"
    "${PROJECT_SOURCE_DIR}/table/format.h"
    "${PROJECT_SOURCE_DIR}/table/iterator_wrapper.h"
    "${PROJECT_SOURCE_DIR}/table/iterator.cc"
    "${PROJECT_SOURCE_DIR}/table/merger.cc"
    "${PROJECT_SOURCE_DIR}/table/merger.h"
    "${PROJECT_SOURCE_DIR}/table/table_builder.cc"
    "${PROJECT_SOURCE_DIR}/table/table.cc"
    "${PROJECT_SOURCE_DIR}/table/two_level_iterator.cc"
    "${PROJECT_SOURCE_DIR}/table/two_level_iterator.h"
    "${PROJECT_SOURCE_DIR}/util/arena.cc"
    "${PROJECT_SOURCE_DIR}/util/arena.h"
    "${PROJECT_SOURCE_DIR}/util/bloom.cc"
    "${PROJECT_SOURCE_DIR}/util/cache.cc"
    "${PROJECT_SOURCE_DIR}/util/coding.cc"
    "${PROJECT_SOURCE_DIR}/util/coding.h"
    "${PROJECT_SOURCE_DIR}/util/comparator.cc"
    "${PROJECT_SOURCE_DIR}/util/crc32c.cc"
    "${PROJECT_SOURCE_DIR}/util/crc32c.h"
    "${PROJECT_SOURCE_DIR}/util/env_posix.cc"
    "${PROJECT_SOURCE_DIR}/util/env.cc"
    "${PROJECT_SOURCE_DIR}/util/filter_policy.cc"
    "${PROJECT_SOURCE_DIR}/util/hash.cc"
    "${PROJECT_SOURCE_DIR}/util/hash.h"
    "${PROJECT_SOURCE_DIR}/util/logging.cc"
    "${PROJECT_SOURCE_DIR}/util/logging.h"
    "${PROJECT_SOURCE_DIR}/util/mutexlock.h"
    "${PROJECT_SOURCE_DIR}/util/options.cc"
    "${PROJECT_SOURCE_DIR}/util/posix_logger.h"
    "${PROJECT_SOURCE_DIR}/util/random.h"
    "${PROJECT_SOURCE_DIR}/util/status.cc"

  # Only CMake 3.3+ supports PUBLIC sources in targets exported by "install".
  $<$<VERSION_GREATER:CMAKE_VERSION,3.2>:PUBLIC>
    "include/leveldb/c.h"
    "include/leveldb/cache.h"
    "include/leveldb/comparator.h"
    "include/leveldb/db.h"
    "include/leveldb/dumpfile.h"
    "include/leveldb/env.h"
    "include/leveldb/filter_policy.h"
    "include/leveldb/iterator.h"
    "include/leveldb/options.h"
    "include/leveldb/slice.h"
    "include/leveldb/status.h"
    "include/leveldb/table_builder.h"
    "include/leveldb/table.h"
    "include/leveldb/write_batch.h"
)
target_include_directories(leveldb
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_compile_definitions(leveldb
  PRIVATE
    LEVELDB_PLATFORM_POSIX
)

# TODO(pwnall): This is only needed for port_posix.
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(leveldb Threads::Threads)

<<<<<<< HEAD
if (HAVE_CRC32C)
  target_link_libraries(leveldb crc32c)
endif (HAVE_CRC32C)
=======
>>>>>>> 02d98fe... Add CMake build support.
if (HAVE_SNAPPY)
  target_link_libraries(leveldb snappy)
endif (HAVE_SNAPPY)

add_library(leveldb_memenv "")
target_sources(leveldb_memenv
  PRIVATE
    "${PROJECT_SOURCE_DIR}/helpers/memenv/memenv.cc"

  # Only CMake 3.3+ supports PUBLIC sources in targets exported by "install".
  $<$<VERSION_GREATER:CMAKE_VERSION,3.2>:PUBLIC>
    "${PROJECT_SOURCE_DIR}/helpers/memenv/memenv.h"
)
target_compile_definitions(leveldb_memenv
  PRIVATE
    LEVELDB_PLATFORM_POSIX
)
target_link_libraries(leveldb_memenv leveldb)

add_executable(leveldbutil
  "${PROJECT_SOURCE_DIR}/db/leveldbutil.cc"
)
target_link_libraries(leveldbutil leveldb)

if(LEVELDB_BUILD_TESTS)
  enable_testing()

  add_library(leveldb_test_base "")
  target_sources(leveldb_test_base
    PRIVATE
      "${PROJECT_SOURCE_DIR}/util/testharness.cc"
      "${PROJECT_SOURCE_DIR}/util/testutil.cc"
    PUBLIC
      "${PROJECT_BINARY_DIR}/include/port/port_config.h"
      "${PROJECT_SOURCE_DIR}/util/testharness.h"
      "${PROJECT_SOURCE_DIR}/util/testutil.h"
  )
  target_compile_definitions(leveldb_test_base
    PUBLIC
      LEVELDB_PLATFORM_POSIX
  )
  target_link_libraries(leveldb_test_base leveldb)

  add_executable(autocompact_test
    "${PROJECT_SOURCE_DIR}/db/autocompact_test.cc"
  )
  target_link_libraries(autocompact_test leveldb_test_base)
  add_test(NAME autocompact_test COMMAND autocompact_test)

<<<<<<< HEAD
  add_executable(c_test
    "${PROJECT_SOURCE_DIR}/db/c_test.c"
  )
  target_link_libraries(c_test leveldb_test_base)
  add_test(NAME c_test COMMAND c_test)

=======
>>>>>>> 02d98fe... Add CMake build support.
  add_executable(corruption_test
    "${PROJECT_SOURCE_DIR}/db/corruption_test.cc"
  )
  target_link_libraries(corruption_test leveldb_test_base)
  add_test(NAME corruption_test COMMAND corruption_test)

  add_executable(db_test
    "${PROJECT_SOURCE_DIR}/db/db_test.cc"
  )
  target_link_libraries(db_test leveldb_test_base)
  add_test(NAME db_test COMMAND db_test)

  add_executable(dbformat_test
    "${PROJECT_SOURCE_DIR}/db/dbformat_test.cc"
  )
  target_link_libraries(dbformat_test leveldb_test_base)
  add_test(NAME dbformat_test COMMAND dbformat_test)

  add_executable(fault_injection_test
    "${PROJECT_SOURCE_DIR}/db/fault_injection_test.cc"
  )
  target_link_libraries(fault_injection_test leveldb_test_base)
  add_test(NAME fault_injection_test COMMAND fault_injection_test)

  add_executable(filename_test
    "${PROJECT_SOURCE_DIR}/db/filename_test.cc"
  )
  target_link_libraries(filename_test leveldb_test_base)
  add_test(NAME filename_test COMMAND filename_test)

  add_executable(log_test
    "${PROJECT_SOURCE_DIR}/db/log_test.cc"
  )
  target_link_libraries(log_test leveldb_test_base)
  add_test(NAME log_test COMMAND log_test)

  add_executable(recovery_test
    "${PROJECT_SOURCE_DIR}/db/recovery_test.cc"
  )
  target_link_libraries(recovery_test leveldb_test_base)
  add_test(NAME recovery_test COMMAND recovery_test)

  add_executable(skiplist_test
    "${PROJECT_SOURCE_DIR}/db/skiplist_test.cc"
  )
  target_link_libraries(skiplist_test leveldb_test_base)
  add_test(NAME skiplist_test COMMAND skiplist_test)

  add_executable(version_edit_test
    "${PROJECT_SOURCE_DIR}/db/version_edit_test.cc"
  )
  target_link_libraries(version_edit_test leveldb_test_base)
  add_test(NAME version_edit_test COMMAND version_edit_test)

  add_executable(version_set_test
    "${PROJECT_SOURCE_DIR}/db/version_set_test.cc"
  )
  target_link_libraries(version_set_test leveldb_test_base)
  add_test(NAME version_set_test COMMAND version_set_test)

  add_executable(write_batch_test
    "${PROJECT_SOURCE_DIR}/db/write_batch_test.cc"
  )
  target_link_libraries(write_batch_test leveldb_test_base)
  add_test(NAME write_batch_test COMMAND write_batch_test)

  add_executable(memenv_test
    "${PROJECT_SOURCE_DIR}/helpers/memenv/memenv_test.cc"
  )
  target_link_libraries(memenv_test leveldb_test_base leveldb_memenv)
  add_test(NAME memenv_test COMMAND memenv_test)

  add_executable(issue178_test
    "${PROJECT_SOURCE_DIR}/issues/issue178_test.cc"
  )
  target_link_libraries(issue178_test leveldb_test_base)
  add_test(NAME issue178_test COMMAND issue178_test)

  add_executable(issue200_test
    "${PROJECT_SOURCE_DIR}/issues/issue200_test.cc"
  )
  target_link_libraries(issue200_test leveldb_test_base)
  add_test(NAME issue200_test COMMAND issue200_test)

  add_executable(filter_block_test
    "${PROJECT_SOURCE_DIR}/table/filter_block_test.cc"
  )
  target_link_libraries(filter_block_test leveldb_test_base)
  add_test(NAME filter_block_test COMMAND filter_block_test)

  add_executable(table_test
    "${PROJECT_SOURCE_DIR}/table/table_test.cc"
  )
  target_link_libraries(table_test leveldb_test_base)
  add_test(NAME table_test COMMAND table_test)

  add_executable(arena_test
    "${PROJECT_SOURCE_DIR}/util/arena_test.cc"
  )
  target_link_libraries(arena_test leveldb_test_base)
  add_test(NAME arena_test COMMAND arena_test)

  add_executable(bloom_test
    "${PROJECT_SOURCE_DIR}/util/bloom_test.cc"
  )
  target_link_libraries(bloom_test leveldb_test_base)
  add_test(NAME bloom_test COMMAND bloom_test)

  add_executable(cache_test
    "${PROJECT_SOURCE_DIR}/util/cache_test.cc"
  )
  target_link_libraries(cache_test leveldb_test_base)
  add_test(NAME cache_test COMMAND cache_test)

  add_executable(coding_test
    "${PROJECT_SOURCE_DIR}/util/coding_test.cc"
  )
  target_link_libraries(coding_test leveldb_test_base)
  add_test(NAME coding_test COMMAND coding_test)

  add_executable(crc32c_test
    "${PROJECT_SOURCE_DIR}/util/crc32c_test.cc"
  )
  target_link_libraries(crc32c_test leveldb_test_base)
  add_test(NAME crc32c_test COMMAND crc32c_test)

  add_executable(env_posix_test
    "${PROJECT_SOURCE_DIR}/util/env_posix_test_helper.h"
    "${PROJECT_SOURCE_DIR}/util/env_posix_test.cc"
  )
  target_link_libraries(env_posix_test leveldb_test_base)
  add_test(NAME env_posix_test COMMAND env_posix_test)

  add_executable(env_test
    "${PROJECT_SOURCE_DIR}/util/env_test.cc"
  )
  target_link_libraries(env_test leveldb_test_base)
  add_test(NAME env_test COMMAND env_test)

  add_executable(hash_test
    "${PROJECT_SOURCE_DIR}/util/hash_test.cc"
  )
  target_link_libraries(hash_test leveldb_test_base)
  add_test(NAME hash_test COMMAND hash_test)
endif(LEVELDB_BUILD_TESTS)

if(LEVELDB_BUILD_BENCHMARKS)
  add_library(leveldb_bench_base "")
  target_sources(leveldb_bench_base
    PRIVATE
      "${PROJECT_SOURCE_DIR}/util/histogram.cc"
      "${PROJECT_SOURCE_DIR}/util/testutil.cc"
    PUBLIC
      "${PROJECT_BINARY_DIR}/include/port/port_config.h"
      "${PROJECT_SOURCE_DIR}/util/histogram.h"
      "${PROJECT_SOURCE_DIR}/util/testutil.h"
  )
  target_compile_definitions(leveldb_bench_base
    PUBLIC
      LEVELDB_PLATFORM_POSIX
  )
  target_link_libraries(leveldb_bench_base leveldb)

  add_executable(db_bench
    "${PROJECT_SOURCE_DIR}/db/db_bench.cc"
  )
  target_link_libraries(db_bench leveldb_bench_base)

  check_library_exists(sqlite3 sqlite3_open "" HAVE_SQLITE3)
  if (HAVE_SQLITE3)
    add_executable(db_bench_sqlite3
      "${PROJECT_SOURCE_DIR}/doc/bench/db_bench_sqlite3.cc"
    )
    target_link_libraries(db_bench_sqlite3 leveldb_bench_base sqlite3)
  endif (HAVE_SQLITE3)

  # check_library_exists is insufficient here because the library names have
  # different manglings when compiled with clang or gcc, at least when installed
  # with Homebrew on Mac.
  set(OLD_CMAKE_REQURED_LIBRARIES ${CMAKE_REQUIRED_LIBRARIES})
  list(APPEND CMAKE_REQUIRED_LIBRARIES kyotocabinet)
<<<<<<< HEAD
  include(CheckCXXSourceCompiles)
=======
>>>>>>> 02d98fe... Add CMake build support.
  check_cxx_source_compiles("
#include <kcpolydb.h>

int main() {
  kyotocabinet::TreeDB* db = new kyotocabinet::TreeDB();
  delete db;
  return 0;
}
  "  HAVE_KYOTOCABINET)
  set(CMAKE_REQUIRED_LIBRARIES ${OLD_CMAKE_REQURED_LIBRARIES})
  if (HAVE_KYOTOCABINET)
    add_executable(db_bench_tree_db
      "${PROJECT_SOURCE_DIR}/doc/bench/db_bench_tree_db.cc"
    )
    target_link_libraries(db_bench_tree_db leveldb_bench_base kyotocabinet)
  endif (HAVE_KYOTOCABINET)
endif(LEVELDB_BUILD_BENCHMARKS)

if(LEVELDB_INSTALL)
  include(GNUInstallDirs)
  install(TARGETS leveldb
    EXPORT LeveldbTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )
  install(
    FILES
      "${PROJECT_SOURCE_DIR}/include/leveldb/c.h"
      "${PROJECT_SOURCE_DIR}/include/leveldb/cache.h"
      "${PROJECT_SOURCE_DIR}/include/leveldb/comparator.h"
      "${PROJECT_SOURCE_DIR}/include/leveldb/db.h"
      "${PROJECT_SOURCE_DIR}/include/leveldb/dumpfile.h"
      "${PROJECT_SOURCE_DIR}/include/leveldb/env.h"
      "${PROJECT_SOURCE_DIR}/include/leveldb/filter_policy.h"
      "${PROJECT_SOURCE_DIR}/include/leveldb/iterator.h"
      "${PROJECT_SOURCE_DIR}/include/leveldb/options.h"
      "${PROJECT_SOURCE_DIR}/include/leveldb/slice.h"
      "${PROJECT_SOURCE_DIR}/include/leveldb/status.h"
      "${PROJECT_SOURCE_DIR}/include/leveldb/table_builder.h"
      "${PROJECT_SOURCE_DIR}/include/leveldb/table.h"
      "${PROJECT_SOURCE_DIR}/include/leveldb/write_batch.h"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/leveldb
  )

  include(CMakePackageConfigHelpers)
  write_basic_package_version_file(
      "${PROJECT_BINARY_DIR}/LeveldbConfigVersion.cmake"
      COMPATIBILITY SameMajorVersion
  )
  install(
    EXPORT LeveldbTargets
    NAMESPACE Leveldb::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Leveldb"
  )
  install(
    FILES
      "${PROJECT_SOURCE_DIR}/LeveldbConfig.cmake"
      "${PROJECT_BINARY_DIR}/LeveldbConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Leveldb"
  )
endif(LEVELDB_INSTALL)
