// swift-tools-version:5.0
// The swift-tools-version declares the minimum version of Swift required to build this package.

import PackageDescription

let package = Package(
  name: "leveldb",
    platforms: [
        .macOS(.v10_10),
        .iOS(.v8),
        .tvOS(.v9)
    ],

  products: [
    .library(
      name: "leveldb",
      targets: ["leveldb"]
    )
  ],

  targets: [
    .target(
      name: "leveldb",
      path: ".",
      exclude: [
        "db/leveldbutil.cc",
        "util/env_windows.cc",
        "util/testutil.cc",
        "db/autocompact_test.cc",
        "db/corruption_test.cc",
        "db/db_test.cc",
        "db/dbformat_test.cc",
        "db/fault_injection_test.cc",
        "db/filename_test.cc",
        "db/log_test.cc",
        "db/recovery_test.cc",
        "db/skiplist_test.cc",
        "db/version_edit_test.cc",
        "db/version_set_test.cc",
        "db/write_batch_test.cc",
        "issues/issue178_test.cc",
        "issues/issue200_test.cc",
        "issues/issue320_test.cc",
        "table/filter_block_test.cc",
        "table/table_test.cc",
        "util/arena_test.cc",
        "util/bloom_test.cc",
        "util/cache_test.cc",
        "util/coding_test.cc",
        "util/crc32c_test.cc",
        "util/env_posix_test.cc",
        "util/env_test.cc",
        "util/env_windows_test.cc",
        "util/hash_test.cc",
        "util/logging_test.cc",
        "util/no_destructor_test.cc",
        "util/status_test.cc",
        "db/c_test.c",
      ],
      sources: [
        "db/",
        "port/",
        "table/",
        "util/",
        "include/",
      ],
      publicHeadersPath: "include",
      cSettings: [
        .define("LEVELDB_IS_BIG_ENDIAN", to: "0"),
        .define("LEVELDB_PLATFORM_POSIX", to: "1"),
        .define("HAVE_FULLFSYNC", to: "1"),
        .headerSearchPath("./"),
        .headerSearchPath("include/"),
      ]
    ),
  ],

  cxxLanguageStandard: CXXLanguageStandard.gnucxx14
)

