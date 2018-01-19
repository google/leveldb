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

if(NOT BUILD_SHARED_LIBS)
  if(MINGW OR MSYS)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static")
  elseif(UNIX)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
  endif()
endif()

# Check for includes and system endianness
include(TestBigEndian)
test_big_endian(LEVELDB_IS_BIG_ENDIAN)
if (LEVELDB_IS_BIG_ENDIAN)
    list(APPEND LEVELDB_COMPILE_DEFINITIONS LEVELDB_IS_BIG_ENDIAN=1)
endif()

include(CheckIncludeFile)
check_include_file("unistd.h" HAVE_UNISTD_H)
if (HAVE_UNISTD_H)
    list(APPEND LEVELDB_COMPILE_DEFINITIONS HAVE_UNISTD_H=1)
endif()

include(CheckIncludeFileCXX)
check_include_file_cxx("atomic" LEVELDB_ATOMIC_PRESENT)
if (LEVELDB_ATOMIC_PRESENT)
    list(APPEND LEVELDB_COMPILE_DEFINITIONS LEVELDB_ATOMIC_PRESENT=1)
endif()

include(CheckLibraryExists)
check_library_exists(crc32c crc32c_value "" HAVE_CRC32C)
check_library_exists(snappy snappy_compress "" HAVE_SNAPPY)
if (HAVE_SNAPPY)
    list(APPEND LEVELDB_COMPILE_DEFINITIONS HAVE_SNAPPY=1)
endif()

include(CheckSymbolExists)
check_symbol_exists(fdatasync "unistd.h" HAVE_FDATASYNC)
if (HAVE_FDATASYNC)
    list(APPEND LEVELDB_COMPILE_DEFINITIONS HAVE_FDATASYNC=1)
endif()

if(WIN32)
  list(APPEND LEVELDB_COMPILE_DEFINITIONS LEVELDB_PLATFORM_WINDOWS=1)
else()
  list(APPEND LEVELDB_COMPILE_DEFINITIONS LEVELDB_PLATFORM_POSIX=1)
endif()

# Check for the OS

#
#   Check if preprocessor symbol(s) exist(s).
#
#   \param target           Name of output variable
#   \param symbols          Pass remaining symbols to check at end of macro args.
#
macro(check_preprocessor_symbol_exists target)
    set(symbols ${ARGN})
    set(${target} 0)
    foreach(symbol ${symbols})
      if(NOT target)
        check_symbol_exists(${symbol} "" symbol_exists_${symbol})
        message("symbol_exists_${symbol}")
        if(symbol_exists_${symbol})
          set(${target} 1)
          list(APPEND LEVELDB_COMPILE_DEFINITIONS ${target}=1)
        endif()
      endif()
    endforeach(symbol)
endmacro(check_preprocessor_symbol_exists)

check_preprocessor_symbol_exists(OS_LINUX __linux__ linux __linux)
check_preprocessor_symbol_exists(OS_APPLE __APPLE__)
check_preprocessor_symbol_exists(OS_SOLARIS __sun__)
check_preprocessor_symbol_exists(OS_FREEBSD __FreeBSD__)
check_preprocessor_symbol_exists(OS_NETBSD __NetBSD__)
check_preprocessor_symbol_exists(OS_OPENBSD __OpenBSD__)
check_preprocessor_symbol_exists(OS_DRAGONFLYBSD __DragonFly__)
check_preprocessor_symbol_exists(OS_ANDROID __ANDROID__ ANDROID)
check_preprocessor_symbol_exists(OS_HPUX __hpux hpux _hpux)
check_preprocessor_symbol_exists(OS_WIN _WIN32)

# MSVC defines the `min` and `max` macros, unless NOMINMAX
# is defined, preventing use of the STL functions/methods.
if(MSVC)
  list(APPEND LEVELDB_COMPILE_DEFINITIONS NOMINMAX)
endif()

configure_file(
  "${PROJECT_SOURCE_DIR}/port/port_config.h.in"
  "${PROJECT_BINARY_DIR}/include/port/port_config.h"
)

include_directories(
  "${PROJECT_BINARY_DIR}/include"
  "${PROJECT_SOURCE_DIR}"
)

# TODO: Port is legacy for C++11 configurations.
add_library(leveldb_port_cpp11 OBJECT "")
target_sources(leveldb_port_cpp11
  PRIVATE
    "${PROJECT_SOURCE_DIR}/port/port_cpp11.cc"

  PUBLIC
    ## The headers below are dependencies for leveldb, but aren't needed by users
    # that link to the installed version of leveldb and rely on its public API.
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include/port/port_config.h>
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/port/atomic_pointer.h>
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/port/port_cpp11.h>
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/port/port.h>
)
target_compile_definitions(leveldb_port_cpp11
  PRIVATE
    ${LEVELDB_COMPILE_DEFINITIONS}
)

# TODO(pwnall): Move the TARGET_OBJECTS generator expressions to the PRIVATE
# section of target_sources when cmake_minimum_required becomes 3.9 or above.
set(LEVELDB_OBJECTS $<TARGET_OBJECTS:leveldb_port_cpp11>)

add_library(leveldb ""
  ${LEVELDB_OBJECTS}
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
    "${PROJECT_SOURCE_DIR}/util/env.cc"
    "${PROJECT_SOURCE_DIR}/util/file_logger.h"
    "${PROJECT_SOURCE_DIR}/util/filter_policy.cc"
    "${PROJECT_SOURCE_DIR}/util/hash.cc"
    "${PROJECT_SOURCE_DIR}/util/hash.h"
    "${PROJECT_SOURCE_DIR}/util/logging.cc"
    "${PROJECT_SOURCE_DIR}/util/logging.h"
    "${PROJECT_SOURCE_DIR}/util/mutexlock.h"
    "${PROJECT_SOURCE_DIR}/util/options.cc"
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
if(WIN32)
  target_sources(leveldb
    PRIVATE
    "${PROJECT_SOURCE_DIR}/util/env_windows.cc"
  )
else()
  target_sources(leveldb
    PRIVATE
    "${PROJECT_SOURCE_DIR}/util/env_posix.cc"
  )
endif()

target_include_directories(leveldb
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_compile_definitions(leveldb
  PRIVATE
    ${LEVELDB_COMPILE_DEFINITIONS}
)

# TODO(pwnall): This is only needed for port_posix.
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(leveldb Threads::Threads)

if (HAVE_CRC32C)
  target_link_libraries(leveldb crc32c)
endif (HAVE_CRC32C)
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
    ${LEVELDB_COMPILE_DEFINITIONS}
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
      ${LEVELDB_COMPILE_DEFINITIONS}
  )
  target_link_libraries(leveldb_test_base leveldb)

  set(LEVELDB_TEST_SOURCES
    "${PROJECT_SOURCE_DIR}/db/autocompact_test.cc"
    "${PROJECT_SOURCE_DIR}/db/corruption_test.cc"
    "${PROJECT_SOURCE_DIR}/db/db_test.cc"
    "${PROJECT_SOURCE_DIR}/db/dbformat_test.cc"
    "${PROJECT_SOURCE_DIR}/db/fault_injection_test.cc"
    "${PROJECT_SOURCE_DIR}/db/filename_test.cc"
    "${PROJECT_SOURCE_DIR}/db/log_test.cc"
    "${PROJECT_SOURCE_DIR}/db/recovery_test.cc"
    "${PROJECT_SOURCE_DIR}/db/skiplist_test.cc"
    "${PROJECT_SOURCE_DIR}/db/version_edit_test.cc"
    "${PROJECT_SOURCE_DIR}/db/version_set_test.cc"
    "${PROJECT_SOURCE_DIR}/db/write_batch_test.cc"
    "${PROJECT_SOURCE_DIR}/issues/issue178_test.cc"
    "${PROJECT_SOURCE_DIR}/issues/issue200_test.cc"
    "${PROJECT_SOURCE_DIR}/table/filter_block_test.cc"
    "${PROJECT_SOURCE_DIR}/table/table_test.cc"
    "${PROJECT_SOURCE_DIR}/util/arena_test.cc"
    "${PROJECT_SOURCE_DIR}/util/bloom_test.cc"
    "${PROJECT_SOURCE_DIR}/util/cache_test.cc"
    "${PROJECT_SOURCE_DIR}/util/coding_test.cc"
    "${PROJECT_SOURCE_DIR}/util/crc32c_test.cc"
    "${PROJECT_SOURCE_DIR}/util/env_test.cc"
    "${PROJECT_SOURCE_DIR}/util/hash_test.cc"
  )

  if (WIN32)
    list(APPEND LEVELDB_TEST_SOURCES
      "${PROJECT_SOURCE_DIR}/util/env_windows_test.cc"
    )
  else()
    list(APPEND LEVELDB_TEST_SOURCES
      # Skip c_test on WIN32, since `tmpnam()` does not work reliably
      # on MinGW, and MSVC does not fully support C99/C11 features.
      "${PROJECT_SOURCE_DIR}/db/c_test.c"
      "${PROJECT_SOURCE_DIR}/util/env_posix_test.cc"
    )
  endif()

  foreach(test ${LEVELDB_TEST_SOURCES})
      get_filename_component(basename ${test} NAME_WE)
      add_executable("${basename}" ${test})
      target_link_libraries("${basename}" leveldb_test_base)  # msvcr100
      add_test(NAME "${basename}" COMMAND "${basename}")
  endforeach(test)

  add_executable(memenv_test
    "${PROJECT_SOURCE_DIR}/helpers/memenv/memenv_test.cc"
  )
  target_link_libraries(memenv_test leveldb_test_base leveldb_memenv)
  add_test(NAME memenv_test COMMAND memenv_test)
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
      ${LEVELDB_COMPILE_DEFINITIONS}
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
  include(CheckCXXSourceCompiles)
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
