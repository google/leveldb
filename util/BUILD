package(default_visibility = ["//visibility:public"])

cc_library(
    name = "util",
    srcs = glob(
        ["*.cc"],
        exclude = glob(["*_test.cc"]),
    ),
    hdrs = glob(["*.h"]),
    linkopts = select({
        "//port:osx_x86_64": [],
        "//port:linux_x86_64": [
            "-pthread",
        ],
        "//conditions:default": [],
    }),
    deps = [
        "//include/leveldb",
        "//port",
    ],
)

cc_test(
    name = "arena_test",
    srcs = ["arena_test.cc"],
    deps = [":util"],
)

cc_test(
    name = "bloom_test",
    srcs = ["bloom_test.cc"],
    deps = [":util"],
)

cc_test(
    name = "cache_test",
    srcs = ["cache_test.cc"],
    deps = [":util"],
)

cc_test(
    name = "coding_test",
    srcs = ["coding_test.cc"],
    deps = [":util"],
)

cc_test(
    name = "crc32c_test",
    srcs = ["crc32c_test.cc"],
    deps = [":util"],
)

cc_test(
    name = "env_posix_test",
    srcs = ["env_posix_test.cc"],
    deps = [":util"],
)

cc_test(
    name = "env_test",
    srcs = ["env_test.cc"],
    deps = [":util"],
)

cc_test(
    name = "hash_test",
    srcs = ["hash_test.cc"],
    deps = [":util"],
)

cc_test(
    name = "logging_test",
    srcs = ["logging_test.cc"],
    deps = [":util"],
)

cc_test(
    name = "status_test",
    srcs = ["status_test.cc"],
    deps = [
        ":util",
        "//include/leveldb",
    ],
)
