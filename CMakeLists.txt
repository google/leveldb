# Copyright 2017 The LevelDB Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file. See the AUTHORS file for names of contributors.

cmake_minimum_required(VERSION 3.9)
# Keep the version below in sync with the one in db.h
project(leveldb VERSION 1.22.0 LANGUAGES C CXX)

# This project can use C11, but will gracefully decay down to C89.
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED OFF)
set(CMAKE_C_EXTENSIONS OFF)

# This project requires C++11.
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (WIN32)
  set(LEVELDB_PLATFORM_NAME LEVELDB_PLATFORM_WINDOWS)
  # TODO(cmumford): Make UNICODE configurable for Windows.
  add_definitions(-D_UNICODE -DUNICODE)
else (WIN32)
  set(LEVELDB_PLATFORM_NAME LEVELDB_PLATFORM_POSIX)
endif (WIN32)

option(LEVELDB_BUILD_TESTS "Build LevelDB's unit tests" ON)
option(LEVELDB_BUILD_BENCHMARKS "Build LevelDB's benchmarks" ON)
option(LEVELDB_INSTALL "Install LevelDB's header and library" ON)

include(TestBigEndian)
test_big_endian(LEVELDB_IS_BIG_ENDIAN)

include(CheckIncludeFile)
check_include_file("unistd.h" HAVE_UNISTD_H)

include(CheckLibraryExists)
check_library_exists(crc32c crc32c_value "" HAVE_CRC32C)
check_library_exists(snappy snappy_compress "" HAVE_SNAPPY)
check_library_exists(tcmalloc malloc "" HAVE_TCMALLOC)

include(CheckCXXSymbolExists)
# Using check_cxx_symbol_exists() instead of check_c_symbol_exists() because
# we're including the header from C++, and feature detection should use the same
# compiler language that the project will use later. Principles aside, some
# versions of do not expose fdatasync() in <unistd.h> in standard C mode
# (-std=c11), but do expose the function in standard C++ mode (-std=c++11).
check_cxx_symbol_exists(fdatasync "unistd.h" HAVE_FDATASYNC)
check_cxx_symbol_exists(F_FULLFSYNC "fcntl.h" HAVE_FULLFSYNC)
check_cxx_symbol_exists(O_CLOEXEC "fcntl.h" HAVE_O_CLOEXEC)

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
  # Disable C++ exceptions.
  string(REGEX REPLACE "/EH[a-z]+" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHs-c-")
  add_definitions(-D_HAS_EXCEPTIONS=0)

  # Disable RTTI.
  string(REGEX REPLACE "/GR" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /GR-")
else(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
  # Enable strict prototype warnings for C code in clang and gcc.
  if(NOT CMAKE_C_FLAGS MATCHES "-Wstrict-prototypes")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wstrict-prototypes")
  endif(NOT CMAKE_C_FLAGS MATCHES "-Wstrict-prototypes")

  # Disable C++ exceptions.
  string(REGEX REPLACE "-fexceptions" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-exceptions")

  # Disable RTTI.
  string(REGEX REPLACE "-frtti" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti")
endif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")

include(CheckCXXSourceCompiles)

# Test whether -Wthread-safety is available. See
# https://clang.llvm.org/docs/ThreadSafetyAnalysis.html
# -Werror is necessary because unknown attributes only generate warnings.
set(OLD_CMAKE_REQUIRED_FLAGS ${CMAKE_REQUIRED_FLAGS})
list(APPEND CMAKE_REQUIRED_FLAGS -Werror -Wthread-safety)
check_cxx_source_compiles("
struct __attribute__((lockable)) Lock {
  void Acquire() __attribute__((exclusive_lock_function()));
  void Release() __attribute__((unlock_function()));
};
struct ThreadSafeType {
  Lock lock_;
  int data_ __attribute__((guarded_by(lock_)));
};
int main() { return 0; }
"  HAVE_CLANG_THREAD_SAFETY)
set(CMAKE_REQUIRED_FLAGS ${OLD_CMAKE_REQUIRED_FLAGS})

# Test whether C++17 __has_include is available.
check_cxx_source_compiles("
#if defined(__has_include) &&  __has_include(<string>)
#include <string>
#endif
int main() { std::string str; return 0; }
" HAVE_CXX17_HAS_INCLUDE)

set(LEVELDB_PUBLIC_INCLUDE_DIR "include/leveldb")
set(LEVELDB_PORT_CONFIG_DIR "include/port")

configure_file(
  "${PROJECT_SOURCE_DIR}/port/port_config.h.in"
  "${PROJECT_BINARY_DIR}/${LEVELDB_PORT_CONFIG_DIR}/port_config.h"
)

include_directories(
  "${PROJECT_BINARY_DIR}/include"
  "${PROJECT_SOURCE_DIR}"
)

if(BUILD_SHARED_LIBS)
  # Only export LEVELDB_EXPORT symbols from the shared library.
  add_compile_options(-fvisibility=hidden)
endif(BUILD_SHARED_LIBS)

add_library(leveldb "")
target_sources(leveldb
  PRIVATE
    "${PROJECT_BINARY_DIR}/${LEVELDB_PORT_CONFIG_DIR}/port_config.h"
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
    "${PROJECT_SOURCE_DIR}/port/port_stdcxx.h"
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
    "${PROJECT_SOURCE_DIR}/util/filter_policy.cc"
    "${PROJECT_SOURCE_DIR}/util/hash.cc"
    "${PROJECT_SOURCE_DIR}/util/hash.h"
    "${PROJECT_SOURCE_DIR}/util/logging.cc"
    "${PROJECT_SOURCE_DIR}/util/logging.h"
    "${PROJECT_SOURCE_DIR}/util/mutexlock.h"
    "${PROJECT_SOURCE_DIR}/util/no_destructor.h"
    "${PROJECT_SOURCE_DIR}/util/options.cc"
    "${PROJECT_SOURCE_DIR}/util/random.h"
    "${PROJECT_SOURCE_DIR}/util/status.cc"

  # Only CMake 3.3+ supports PUBLIC sources in targets exported by "install".
  $<$<VERSION_GREATER:CMAKE_VERSION,3.2>:PUBLIC>
    "${LEVELDB_PUBLIC_INCLUDE_DIR}/c.h"
    "${LEVELDB_PUBLIC_INCLUDE_DIR}/cache.h"
    "${LEVELDB_PUBLIC_INCLUDE_DIR}/comparator.h"
    "${LEVELDB_PUBLIC_INCLUDE_DIR}/db.h"
    "${LEVELDB_PUBLIC_INCLUDE_DIR}/dumpfile.h"
    "${LEVELDB_PUBLIC_INCLUDE_DIR}/env.h"
    "${LEVELDB_PUBLIC_INCLUDE_DIR}/export.h"
    "${LEVELDB_PUBLIC_INCLUDE_DIR}/filter_policy.h"
    "${LEVELDB_PUBLIC_INCLUDE_DIR}/iterator.h"
    "${LEVELDB_PUBLIC_INCLUDE_DIR}/options.h"
    "${LEVELDB_PUBLIC_INCLUDE_DIR}/slice.h"
    "${LEVELDB_PUBLIC_INCLUDE_DIR}/status.h"
    "${LEVELDB_PUBLIC_INCLUDE_DIR}/table_builder.h"
    "${LEVELDB_PUBLIC_INCLUDE_DIR}/table.h"
    "${LEVELDB_PUBLIC_INCLUDE_DIR}/write_batch.h"
)

if (WIN32)
  target_sources(leveldb
    PRIVATE
      "${PROJECT_SOURCE_DIR}/util/env_windows.cc"
      "${PROJECT_SOURCE_DIR}/util/windows_logger.h"
  )
else (WIN32)
  target_sources(leveldb
    PRIVATE
      "${PROJECT_SOURCE_DIR}/util/env_posix.cc"
      "${PROJECT_SOURCE_DIR}/util/posix_logger.h"
  )
endif (WIN32)

# MemEnv is not part of the interface and could be pulled to a separate library.
target_sources(leveldb
  PRIVATE
    "${PROJECT_SOURCE_DIR}/helpers/memenv/memenv.cc"
    "${PROJECT_SOURCE_DIR}/helpers/memenv/memenv.h"
)

target_include_directories(leveldb
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

set_target_properties(leveldb
  PROPERTIES VERSION ${PROJECT_VERSION} SOVERSION ${PROJECT_VERSION_MAJOR})

target_compile_definitions(leveldb
  PRIVATE
    # Used by include/export.h when building shared libraries.
    LEVELDB_COMPILE_LIBRARY
    # Used by port/port.h.
    ${LEVELDB_PLATFORM_NAME}=1
)
if (NOT HAVE_CXX17_HAS_INCLUDE)
  target_compile_definitions(leveldb
    PRIVATE
      LEVELDB_HAS_PORT_CONFIG_H=1
  )
endif(NOT HAVE_CXX17_HAS_INCLUDE)

if(BUILD_SHARED_LIBS)
  target_compile_definitions(leveldb
    PUBLIC
      # Used by include/export.h.
      LEVELDB_SHARED_LIBRARY
  )
endif(BUILD_SHARED_LIBS)

if(HAVE_CLANG_THREAD_SAFETY)
  target_compile_options(leveldb
    PUBLIC
      -Werror -Wthread-safety)
endif(HAVE_CLANG_THREAD_SAFETY)

if(HAVE_CRC32C)
  target_link_libraries(leveldb crc32c)
endif(HAVE_CRC32C)
if(HAVE_SNAPPY)
  target_link_libraries(leveldb snappy)
endif(HAVE_SNAPPY)
if(HAVE_TCMALLOC)
  target_link_libraries(leveldb tcmalloc)
endif(HAVE_TCMALLOC)

# Needed by port_stdcxx.h
find_package(Threads REQUIRED)
target_link_libraries(leveldb Threads::Threads)

add_executable(leveldbutil
  "${PROJECT_SOURCE_DIR}/db/leveldbutil.cc"
)
target_link_libraries(leveldbutil leveldb)

if(LEVELDB_BUILD_TESTS)
  enable_testing()

  function(leveldb_test test_file)
    get_filename_component(test_target_name "${test_file}" NAME_WE)

    add_executable("${test_target_name}" "")
    target_sources("${test_target_name}"
      PRIVATE
        "${PROJECT_BINARY_DIR}/${LEVELDB_PORT_CONFIG_DIR}/port_config.h"
        "${PROJECT_SOURCE_DIR}/util/testharness.cc"
        "${PROJECT_SOURCE_DIR}/util/testharness.h"
        "${PROJECT_SOURCE_DIR}/util/testutil.cc"
        "${PROJECT_SOURCE_DIR}/util/testutil.h"

        "${test_file}"
    )
    target_link_libraries("${test_target_name}" leveldb)
    target_compile_definitions("${test_target_name}"
      PRIVATE
        ${LEVELDB_PLATFORM_NAME}=1
    )
    if (NOT HAVE_CXX17_HAS_INCLUDE)
      target_compile_definitions("${test_target_name}"
        PRIVATE
          LEVELDB_HAS_PORT_CONFIG_H=1
      )
    endif(NOT HAVE_CXX17_HAS_INCLUDE)

    add_test(NAME "${test_target_name}" COMMAND "${test_target_name}")
  endfunction(leveldb_test)

  leveldb_test("${PROJECT_SOURCE_DIR}/db/c_test.c")
  leveldb_test("${PROJECT_SOURCE_DIR}/db/fault_injection_test.cc")

  leveldb_test("${PROJECT_SOURCE_DIR}/issues/issue178_test.cc")
  leveldb_test("${PROJECT_SOURCE_DIR}/issues/issue200_test.cc")
  leveldb_test("${PROJECT_SOURCE_DIR}/issues/issue320_test.cc")

  leveldb_test("${PROJECT_SOURCE_DIR}/util/env_test.cc")
  leveldb_test("${PROJECT_SOURCE_DIR}/util/status_test.cc")
  leveldb_test("${PROJECT_SOURCE_DIR}/util/no_destructor_test.cc")

  if(NOT BUILD_SHARED_LIBS)
    leveldb_test("${PROJECT_SOURCE_DIR}/db/autocompact_test.cc")
    leveldb_test("${PROJECT_SOURCE_DIR}/db/corruption_test.cc")
    leveldb_test("${PROJECT_SOURCE_DIR}/db/db_test.cc")
    leveldb_test("${PROJECT_SOURCE_DIR}/db/dbformat_test.cc")
    leveldb_test("${PROJECT_SOURCE_DIR}/db/filename_test.cc")
    leveldb_test("${PROJECT_SOURCE_DIR}/db/log_test.cc")
    leveldb_test("${PROJECT_SOURCE_DIR}/db/recovery_test.cc")
    leveldb_test("${PROJECT_SOURCE_DIR}/db/skiplist_test.cc")
    leveldb_test("${PROJECT_SOURCE_DIR}/db/version_edit_test.cc")
    leveldb_test("${PROJECT_SOURCE_DIR}/db/version_set_test.cc")
    leveldb_test("${PROJECT_SOURCE_DIR}/db/write_batch_test.cc")

    leveldb_test("${PROJECT_SOURCE_DIR}/helpers/memenv/memenv_test.cc")

    leveldb_test("${PROJECT_SOURCE_DIR}/table/filter_block_test.cc")
    leveldb_test("${PROJECT_SOURCE_DIR}/table/table_test.cc")

    leveldb_test("${PROJECT_SOURCE_DIR}/util/arena_test.cc")
    leveldb_test("${PROJECT_SOURCE_DIR}/util/bloom_test.cc")
    leveldb_test("${PROJECT_SOURCE_DIR}/util/cache_test.cc")
    leveldb_test("${PROJECT_SOURCE_DIR}/util/coding_test.cc")
    leveldb_test("${PROJECT_SOURCE_DIR}/util/crc32c_test.cc")
    leveldb_test("${PROJECT_SOURCE_DIR}/util/hash_test.cc")
    leveldb_test("${PROJECT_SOURCE_DIR}/util/logging_test.cc")

    # TODO(costan): This test also uses
    #               "${PROJECT_SOURCE_DIR}/util/env_{posix|windows}_test_helper.h"
    if (WIN32)
      leveldb_test("${PROJECT_SOURCE_DIR}/util/env_windows_test.cc")
    else (WIN32)
      leveldb_test("${PROJECT_SOURCE_DIR}/util/env_posix_test.cc")
    endif (WIN32)
  endif(NOT BUILD_SHARED_LIBS)
endif(LEVELDB_BUILD_TESTS)

if(LEVELDB_BUILD_BENCHMARKS)
  function(leveldb_benchmark bench_file)
    get_filename_component(bench_target_name "${bench_file}" NAME_WE)

    add_executable("${bench_target_name}" "")
    target_sources("${bench_target_name}"
      PRIVATE
        "${PROJECT_BINARY_DIR}/${LEVELDB_PORT_CONFIG_DIR}/port_config.h"
        "${PROJECT_SOURCE_DIR}/util/histogram.cc"
        "${PROJECT_SOURCE_DIR}/util/histogram.h"
        "${PROJECT_SOURCE_DIR}/util/testharness.cc"
        "${PROJECT_SOURCE_DIR}/util/testharness.h"
        "${PROJECT_SOURCE_DIR}/util/testutil.cc"
        "${PROJECT_SOURCE_DIR}/util/testutil.h"

        "${bench_file}"
    )
    target_link_libraries("${bench_target_name}" leveldb)
    target_compile_definitions("${bench_target_name}"
      PRIVATE
        ${LEVELDB_PLATFORM_NAME}=1
    )
    if (NOT HAVE_CXX17_HAS_INCLUDE)
      target_compile_definitions("${bench_target_name}"
        PRIVATE
          LEVELDB_HAS_PORT_CONFIG_H=1
      )
    endif(NOT HAVE_CXX17_HAS_INCLUDE)
  endfunction(leveldb_benchmark)

  if(NOT BUILD_SHARED_LIBS)
    leveldb_benchmark("${PROJECT_SOURCE_DIR}/benchmarks/db_bench.cc")
  endif(NOT BUILD_SHARED_LIBS)

  check_library_exists(sqlite3 sqlite3_open "" HAVE_SQLITE3)
  if(HAVE_SQLITE3)
    leveldb_benchmark("${PROJECT_SOURCE_DIR}/benchmarks/db_bench_sqlite3.cc")
    target_link_libraries(db_bench_sqlite3 sqlite3)
  endif(HAVE_SQLITE3)

  # check_library_exists is insufficient here because the library names have
  # different manglings when compiled with clang or gcc, at least when installed
  # with Homebrew on Mac.
  set(OLD_CMAKE_REQURED_LIBRARIES ${CMAKE_REQUIRED_LIBRARIES})
  list(APPEND CMAKE_REQUIRED_LIBRARIES kyotocabinet)
  check_cxx_source_compiles("
#include <kcpolydb.h>

int main() {
  kyotocabinet::TreeDB* db = new kyotocabinet::TreeDB();
  delete db;
  return 0;
}
  "  HAVE_KYOTOCABINET)
  set(CMAKE_REQUIRED_LIBRARIES ${OLD_CMAKE_REQURED_LIBRARIES})
  if(HAVE_KYOTOCABINET)
    leveldb_benchmark("${PROJECT_SOURCE_DIR}/benchmarks/db_bench_tree_db.cc")
    target_link_libraries(db_bench_tree_db kyotocabinet)
  endif(HAVE_KYOTOCABINET)
endif(LEVELDB_BUILD_BENCHMARKS)

if(LEVELDB_INSTALL)
  include(GNUInstallDirs)
  install(TARGETS leveldb
    EXPORT leveldbTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )
  install(
    FILES
      "${PROJECT_SOURCE_DIR}/${LEVELDB_PUBLIC_INCLUDE_DIR}/c.h"
      "${PROJECT_SOURCE_DIR}/${LEVELDB_PUBLIC_INCLUDE_DIR}/cache.h"
      "${PROJECT_SOURCE_DIR}/${LEVELDB_PUBLIC_INCLUDE_DIR}/comparator.h"
      "${PROJECT_SOURCE_DIR}/${LEVELDB_PUBLIC_INCLUDE_DIR}/db.h"
      "${PROJECT_SOURCE_DIR}/${LEVELDB_PUBLIC_INCLUDE_DIR}/dumpfile.h"
      "${PROJECT_SOURCE_DIR}/${LEVELDB_PUBLIC_INCLUDE_DIR}/env.h"
      "${PROJECT_SOURCE_DIR}/${LEVELDB_PUBLIC_INCLUDE_DIR}/export.h"
      "${PROJECT_SOURCE_DIR}/${LEVELDB_PUBLIC_INCLUDE_DIR}/filter_policy.h"
      "${PROJECT_SOURCE_DIR}/${LEVELDB_PUBLIC_INCLUDE_DIR}/iterator.h"
      "${PROJECT_SOURCE_DIR}/${LEVELDB_PUBLIC_INCLUDE_DIR}/options.h"
      "${PROJECT_SOURCE_DIR}/${LEVELDB_PUBLIC_INCLUDE_DIR}/slice.h"
      "${PROJECT_SOURCE_DIR}/${LEVELDB_PUBLIC_INCLUDE_DIR}/status.h"
      "${PROJECT_SOURCE_DIR}/${LEVELDB_PUBLIC_INCLUDE_DIR}/table_builder.h"
      "${PROJECT_SOURCE_DIR}/${LEVELDB_PUBLIC_INCLUDE_DIR}/table.h"
      "${PROJECT_SOURCE_DIR}/${LEVELDB_PUBLIC_INCLUDE_DIR}/write_batch.h"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/leveldb
  )

  include(CMakePackageConfigHelpers)
  write_basic_package_version_file(
      "${PROJECT_BINARY_DIR}/leveldbConfigVersion.cmake"
      COMPATIBILITY SameMajorVersion
  )
  install(
    EXPORT leveldbTargets
    NAMESPACE leveldb::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/leveldb"
  )
  install(
    FILES
      "${PROJECT_SOURCE_DIR}/cmake/leveldbConfig.cmake"
      "${PROJECT_BINARY_DIR}/leveldbConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/leveldb"
  )
endif(LEVELDB_INSTALL)
