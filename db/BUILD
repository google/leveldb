package(default_visibility = ["//visibility:public"])

cc_library(
    name = "db",
    srcs = glob(
        ["*.cc"],
        exclude = glob([
            "*_test.cc",
            "leveldbutil.cc",
            "db_bench.cc",
        ]),
    ),
    hdrs = glob([
        "*.h",
    ]),
    deps = [
        "//include/leveldb",
        "//table",
        "//util",
    ],
)

cc_binary(
    name = "leveldbutil",
    srcs = ["leveldbutil.cc"],
    deps = [
        ":db",
        "//include/leveldb",
        "//util",
    ],
)

cc_test(
    name = "autocompact_test",
    srcs = ["autocompact_test.cc"],
    deps = [":db"],
)

cc_test(
    name = "corruption_test",
    srcs = ["corruption_test.cc"],
    deps = [":db"],
)

cc_test(
    name = "db_test",
    srcs = ["db_test.cc"],
    deps = [":db"],
)

cc_test(
    name = "dbformat_test",
    srcs = ["dbformat_test.cc"],
    deps = [":db"],
)

cc_test(
    name = "fault_injection_test",
    srcs = ["fault_injection_test.cc"],
    deps = [":db"],
)

cc_test(
    name = "filename_test",
    srcs = ["filename_test.cc"],
    deps = [":db"],
)

cc_test(
    name = "log_test",
    srcs = ["log_test.cc"],
    deps = [":db"],
)

cc_test(
    name = "recovery_test",
    srcs = ["recovery_test.cc"],
    deps = [":db"],
)

cc_test(
    name = "skiplist_test",
    srcs = ["skiplist_test.cc"],
    deps = [":db"],
)

cc_test(
    name = "version_edit_test",
    srcs = ["version_edit_test.cc"],
    deps = [":db"],
)

cc_test(
    name = "write_batch_test",
    srcs = ["write_batch_test.cc"],
    deps = [":db"],
)

cc_test(
    name = "version_set_test",
    srcs = ["version_set_test.cc"],
    deps = [":db"],
)
