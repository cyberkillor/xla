load("//xla:xla.bzl", "xla_cc_binary")
load("@tsl//tsl:tsl.default.bzl", "get_compatible_with_cloud")
load("@bazel_skylib//rules:build_test.bzl", "build_test")

package_group(
    name = "friends",
    packages = [
        # copybara:uncomment_begin(google-only)
        # "//platforms/xla/service/cpu/...",
        # "//learning/brain/experimental/tfrt/autofusion/...",
        # "//third_party/mlir_edge/tpgen/...",
        # # TODO(ezhulenev): Clean up dependencies that are leforvers from Autofusion project.
        # "@tf_runtime//...",
        # "//third_party/tf_runtime_google/...",
        # "//tensorflow/compiler/mlir/tfrt/...",
        # copybara:uncomment_end(google-only)
        "//xla/mlir/...",
        "//xla/runtime/...",
        # TODO(ezhulenev): All targets depending on mlir must be under xla/mlir folder
        "//xla/service/cpu/...",
        "//xla/service/gpu/...",
    ],
)

package(
    # copybara:uncomment default_applicable_licenses = ["//tensorflow:license"],
    default_visibility = [":friends"],
    licenses = ["notice"],
)

build_test(
    name = "xla-runtime-opt_build_test",
    targets = [
        ":xla-runtime-opt",
    ],
)

xla_cc_binary(
    name = "xla-runtime-opt",
    srcs = ["xla-runtime-opt.cc"],
    compatible_with = get_compatible_with_cloud(),
    deps = [
        "//xla/mlir/math/transforms:passes",
        "//xla/mlir/memref/transforms:passes",
        "//xla/mlir/runtime/ir/tests:testlib",
        "//xla/mlir/runtime/transforms:compilation_pipeline_cpu",
        "//xla/mlir/runtime/transforms:compilation_pipeline_gpu",
        "//xla/mlir/runtime/transforms:passes",
        "@llvm-project//mlir:AsyncDialect",
        "@llvm-project//mlir:FuncDialect",
        "@llvm-project//mlir:MathDialect",
        "@llvm-project//mlir:MemRefDialect",
        "@llvm-project//mlir:MlirOptLib",
    ],
)
