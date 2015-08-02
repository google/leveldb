cmake_minimum_required (VERSION 2.8)
project (leveldb)

file(GLOB SOURCES1 "db/*.cc")
file(GLOB TESTS db "db/*test*.cc")
file(GLOB BENCH db "db/*_bench.cc")
list(REMOVE_ITEM SOURCES1 ${TESTS} ";" ${BENCH})

file(GLOB SOURCES2 "util/*.cc")
file(GLOB TESTS db "util/*test*.cc")
file(GLOB BENCH db "util/*_bench.cc")
file(GLOB POSIX db "util/*_posix.cc")
list(REMOVE_ITEM SOURCES2 ${TESTS} ";" ${BENCH})

file(GLOB SOURCES3 "table/*.cc")
file(GLOB TESTS db "table/*test*.cc")
file(GLOB BENCH db "table/*_bench.cc")
list(REMOVE_ITEM SOURCES3 ${TESTS} ";" ${BENCH})

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
	# using Visual Studio C++
	list(APPEND SOURCES3 "port/port_win.cc")
	list(REMOVE_ITEM SOURCES2 ${POSIX})
else()
	list(APPEND SOURCES3 "port/port_posix.cc")
endif()

include_directories(.)
include_directories(include)

ADD_DEFINITIONS(-DOS_LINUX -DCYGWIN -DLEVELDB_ATOMIC_PRESENT)

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
	# using Visual Studio C++
	SET(LIBRARYNAME "libleveldb")
	add_library(${LIBRARYNAME} STATIC ${SOURCES1} ${SOURCES2} ${SOURCES3})
	ADD_DEFINITIONS(-DNOMINMAX -D_CRT_SECURE_NO_WARNINGS -DBOOST_ALL_NO_LIB -DLEVELDB_PLATFORM_WINDOWS)
	set(Boost_USE_STATIC_LIBS ON)
	set(Boost_USE_MULTITHREADED ON)
	find_package(Boost 1.50.0 REQUIRED COMPONENTS system thread atomic filesystem)
	if(Boost_FOUND)
		include_directories(${Boost_INCLUDE_DIRS})
		target_link_libraries(${LIBRARYNAME} ${Boost_LIBRARIES})
	endif()
else()
	SET(LIBRARYNAME "leveldb")
	add_library(${LIBRARYNAME} STATIC ${SOURCES1} ${SOURCES2} ${SOURCES3})
	ADD_DEFINITIONS(-DLEVELDB_PLATFORM_POSIX -std=c++0x)
	add_dependencies(${LIBRARYNAME} pthread)
	target_link_libraries(${LIBRARYNAME} pthread)
endif()

##	Utilities
add_executable(db_bench db/db_bench.cc util/testutil.cc util/testharness.cc)
add_dependencies(db_bench ${LIBRARYNAME})
target_link_libraries(db_bench ${LIBRARYNAME})

##	Benchmarks
add_executable(leveldbutil db/leveldb_main.cc)
add_dependencies(leveldbutil ${LIBRARYNAME})
target_link_libraries(leveldbutil ${LIBRARYNAME})

add_executable(db_bench_sqlite3 doc/bench/db_bench_sqlite3.cc util/testutil.cc util/testharness.cc)
add_dependencies(db_bench_sqlite3 ${LIBRARYNAME})
target_link_libraries(db_bench_sqlite3 ${LIBRARYNAME})

add_executable(db_bench_tree_db doc/bench/db_bench_tree_db.cc util/testutil.cc util/testharness.cc)
add_dependencies(db_bench_tree_db ${LIBRARYNAME})
target_link_libraries(db_bench_tree_db ${LIBRARYNAME})

##	Tests
add_executable(autocompact_test db/autocompact_test.cc util/testutil.cc util/testharness.cc)
add_dependencies(autocompact_test ${LIBRARYNAME})
target_link_libraries(autocompact_test ${LIBRARYNAME})

add_executable(arena_test util/arena_test.cc util/testutil.cc util/testharness.cc)
add_dependencies(arena_test ${LIBRARYNAME})
target_link_libraries(arena_test ${LIBRARYNAME})
