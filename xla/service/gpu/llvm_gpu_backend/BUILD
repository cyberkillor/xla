load("//xla:xla.bzl", "xla_cc_test")
load(
    "@local_config_rocm//rocm:build_defs.bzl",
    "if_rocm_is_configured",
)

package(
    # copybara:uncomment default_applicable_licenses = ["//tensorflow:license"],
    default_visibility = [":friends"],
    licenses = ["notice"],
)

package_group(
    name = "friends",
    includes = [
        "//xla:friends",
    ],
)

cc_library(
    name = "llvm_gpu_backend",
    srcs = [
        "gpu_backend_lib.cc",
        "utils.cc",
    ],
    hdrs = [
        "gpu_backend_lib.h",
        "utils.h",
    ],
    deps = [
        "//xla:status_macros",
        "//xla:statusor",
        "//xla:types",
        "//xla:util",
        "//xla/service:hlo_module_config",
        "//xla/service/gpu:gpu_types",
        "//xla/service/gpu:metrics",
        "//xla/service/llvm_ir:llvm_command_line_options",
        "//xla/service/llvm_ir:llvm_type_conversion_util",
        "@com_google_absl//absl/base",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@llvm-project//llvm:Analysis",
        "@llvm-project//llvm:BitReader",
        "@llvm-project//llvm:BitWriter",
        "@llvm-project//llvm:CodeGen",
        "@llvm-project//llvm:Core",
        "@llvm-project//llvm:IPO",
        "@llvm-project//llvm:IRReader",
        "@llvm-project//llvm:Linker",
        "@llvm-project//llvm:MC",
        "@llvm-project//llvm:NVPTXCodeGen",  # buildcleaner: keep
        "@llvm-project//llvm:ObjCARC",  # buildcleaner: keep
        "@llvm-project//llvm:Passes",
        "@llvm-project//llvm:Scalar",
        "@llvm-project//llvm:Support",
        "@llvm-project//llvm:Target",
        "@tsl//tsl/platform:cuda_libdevice_path",
        "@tsl//tsl/platform:env",
        "@tsl//tsl/platform:logging",
        "@tsl//tsl/platform:path",
        "@tsl//tsl/platform:random",
        "@tsl//tsl/profiler/lib:traceme",
        "@tsl//tsl/util:env_var",
    ] + if_rocm_is_configured([
        "@local_config_rocm//rocm:rocm_headers",
        "@llvm-project//llvm:AMDGPUCodeGen",
    ]),
)

xla_cc_test(
    name = "utils_test",
    size = "small",
    srcs = ["utils_test.cc"],
    data = [
        "tests_data/saxpy.ll",
    ],
    deps = [
        ":llvm_gpu_backend",
        "//xla/tests:xla_internal_test_main",
        "@llvm-project//llvm:Core",
        "@llvm-project//llvm:Support",
        "@tsl//tsl/platform:logging",
        "@tsl//tsl/platform:path",
        "@tsl//tsl/platform:resource_loader",
        "@tsl//tsl/platform:test",
    ],
)
