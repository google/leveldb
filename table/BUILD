package(default_visibility = ["//visibility:public"])

cc_library(
    name = "table",
    srcs = glob(
        ["*.cc"],
        exclude = glob(["*_test.cc"]),
    ),
    hdrs = glob(["*.h"]),
    deps = [
        "//include/leveldb",
        "//util",
    ],
)

cc_test(
    name = "table_test",
    srcs = ["table_test.cc"],
    deps = [
        ":table",
        "//db",
    ],
)

cc_test(
    name = "filter_block_test",
    srcs = ["filter_block_test.cc"],
    deps = [
        ":table",
        "//db",
    ],
)
