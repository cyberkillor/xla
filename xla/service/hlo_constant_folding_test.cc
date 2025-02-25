/* Copyright 2017 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "xla/service/hlo_constant_folding.h"

#include <memory>
#include <utility>
#include <vector>

#include "xla/hlo/ir/hlo_computation.h"
#include "xla/hlo/ir/hlo_instruction.h"
#include "xla/hlo/ir/hlo_opcode.h"
#include "xla/layout_util.h"
#include "xla/literal.h"
#include "xla/permutation_util.h"
#include "xla/service/hlo_parser.h"
#include "xla/service/hlo_pass_fix.h"
#include "xla/service/pattern_matcher.h"
#include "xla/service/pattern_matcher_gmock.h"
#include "xla/shape_util.h"
#include "xla/test.h"
#include "xla/tests/hlo_test_base.h"
#include "xla/tests/literal_test_util.h"
#include "xla/types.h"

namespace xla {
namespace {

namespace m = xla::match;

using HloConstantFoldingTest = HloTestBase;

TEST_F(HloConstantFoldingTest, ConvertF32ToS64) {
  HloComputation::Builder builder(TestName());
  HloInstruction* input = builder.AddInstruction(
      HloInstruction::CreateConstant(LiteralUtil::CreateR0<float>(42.0f)));
  builder.AddInstruction(
      HloInstruction::CreateConvert(ShapeUtil::MakeShape(S64, {}), input));

  auto module = CreateNewVerifiedModule();
  auto computation = module->AddEntryComputation(builder.Build());

  EXPECT_THAT(computation->root_instruction(),
              GmockMatch(m::Convert().WithOperand(0, m::Op().Is(input))));

  HloConstantFolding const_folder;
  TF_ASSERT_OK_AND_ASSIGN(bool result, const_folder.Run(module.get()));
  EXPECT_TRUE(result);

  EXPECT_THAT(computation->root_instruction(), GmockMatch(m::Constant()));
  EXPECT_EQ(
      computation->root_instruction()->literal().GetFirstElement<int64_t>(),
      42);
}

TEST_F(HloConstantFoldingTest, ConvertS64ToF32) {
  HloComputation::Builder builder(TestName());
  HloInstruction* input = builder.AddInstruction(
      HloInstruction::CreateConstant(LiteralUtil::CreateR0<int64_t>(42)));
  builder.AddInstruction(
      HloInstruction::CreateConvert(ShapeUtil::MakeShape(F32, {}), input));

  auto module = CreateNewVerifiedModule();
  auto computation = module->AddEntryComputation(builder.Build());

  EXPECT_THAT(computation->root_instruction(),
              GmockMatch(m::Convert().WithOperand(0, m::Op().Is(input))));

  HloConstantFolding const_folder;
  TF_ASSERT_OK_AND_ASSIGN(bool result, const_folder.Run(module.get()));
  EXPECT_TRUE(result);

  EXPECT_THAT(computation->root_instruction(), GmockMatch(m::Constant()));
  EXPECT_EQ(computation->root_instruction()->literal().GetFirstElement<float>(),
            42.0f);
}

TEST_F(HloConstantFoldingTest, ConvertF32ArrayToS64Array) {
  HloComputation::Builder builder(TestName());
  HloInstruction* input = builder.AddInstruction(HloInstruction::CreateConstant(
      LiteralUtil::CreateR1<float>({42.0f, 19.0f})));
  builder.AddInstruction(
      HloInstruction::CreateConvert(ShapeUtil::MakeShape(S64, {2}), input));

  auto module = CreateNewVerifiedModule();
  auto computation = module->AddEntryComputation(builder.Build());

  EXPECT_THAT(computation->root_instruction(),
              GmockMatch(m::Convert().WithOperand(0, m::Op().Is(input))));

  HloConstantFolding const_folder;
  TF_ASSERT_OK_AND_ASSIGN(bool result, const_folder.Run(module.get()));
  EXPECT_TRUE(result);

  EXPECT_THAT(computation->root_instruction(), GmockMatch(m::Constant()));
  EXPECT_EQ(computation->root_instruction()->literal().Get<int64_t>({0}), 42);
  EXPECT_EQ(computation->root_instruction()->literal().Get<int64_t>({1}), 19);
}

TEST_F(HloConstantFoldingTest, Concatenate) {
  const struct TestConfig {
    int concat_dimension;
    std::vector<int64_t> dimensions;
    std::vector<int64_t> concat_sizes;
  } test_configs[] = {
      {1, {11, 0, 7, 5, 9}, {2, 5, 7, 11}},
      {3, {1, 4, 17, 0, 8}, {1, 3, 9, 12}},
  };

  for (auto& test_config : test_configs) {
    HloComputation::Builder builder(TestName());
    std::vector<int64_t> dimensions(test_config.dimensions.begin(),
                                    test_config.dimensions.end());
    int64_t concat_size = 0;
    std::vector<HloInstruction*> operands;
    for (auto csize : test_config.concat_sizes) {
      dimensions[test_config.concat_dimension] = csize;
      concat_size += csize;
      auto literal = LiteralUtil::CreateFromDimensions(F32, dimensions);
      HloInstruction* insn = builder.AddInstruction(
          HloInstruction::CreateConstant(std::move(literal)));
      operands.push_back(insn);
    }
    dimensions[test_config.concat_dimension] = concat_size;
    Shape shape = ShapeUtil::MakeShape(F32, dimensions);
    builder.AddInstruction(HloInstruction::CreateConcatenate(
        shape, operands, test_config.concat_dimension));
    auto module = CreateNewVerifiedModule();
    auto computation = module->AddEntryComputation(builder.Build());

    HloConstantFolding const_folder;
    TF_ASSERT_OK_AND_ASSIGN(bool result, const_folder.Run(module.get()));
    EXPECT_TRUE(result);

    HloInstruction* root = computation->root_instruction();
    EXPECT_THAT(root, GmockMatch(m::Constant()));
    EXPECT_TRUE(ShapeUtil::Equal(root->shape(), shape));
  }
}

TEST_F(HloConstantFoldingTest, Slice) {
  HloComputation::Builder builder(TestName());
  const int64_t dimensions[] = {11, 8, 7, 5, 9};
  const int64_t slice_start[] = {4, 2, 3, 1, 5};
  const int64_t slice_limits[] = {10, 8, 6, 5, 9};
  const int64_t slice_strides[] = {1, 1, 1, 1, 1};
  TF_ASSERT_OK_AND_ASSIGN(auto literal,
                          LiteralUtil::CreateRandomLiteral<F32>(
                              ShapeUtil::MakeShape(F32, dimensions), 0.0, 1.0));
  HloInstruction* literal_instruction = builder.AddInstruction(
      HloInstruction::CreateConstant(std::move(literal)));
  Shape shape = ShapeUtil::MakeShape(F32, {6, 6, 3, 4, 4});
  builder.AddInstruction(HloInstruction::CreateSlice(
      shape, literal_instruction, slice_start, slice_limits, slice_strides));
  auto module = CreateNewVerifiedModule();
  auto computation = module->AddEntryComputation(builder.Build());

  HloConstantFolding const_folder;
  TF_ASSERT_OK_AND_ASSIGN(bool result, const_folder.Run(module.get()));
  EXPECT_TRUE(result);

  HloInstruction* root = computation->root_instruction();
  EXPECT_THAT(root, GmockMatch(m::Constant()));
  EXPECT_TRUE(ShapeUtil::Equal(root->shape(), shape));
}

TEST_F(HloConstantFoldingTest, TransposeConstantFold) {
  HloComputation::Builder builder(TestName());
  const int64_t dimensions[] = {11, 8, 7, 5, 9};
  TF_ASSERT_OK_AND_ASSIGN(auto literal,
                          LiteralUtil::CreateRandomLiteral<F32>(
                              ShapeUtil::MakeShape(F32, dimensions), 0.0, 1.0));
  auto literal_clone = literal.Clone();
  HloInstruction* literal_instruction = builder.AddInstruction(
      HloInstruction::CreateConstant(std::move(literal)));
  Shape shape = ShapeUtil::MakeShape(F32, {8, 7, 11, 9, 5});
  const int64_t permutation[] = {1, 2, 0, 4, 3};
  builder.AddInstruction(
      HloInstruction::CreateTranspose(shape, literal_instruction, permutation));
  auto module = CreateNewVerifiedModule();
  auto computation = module->AddEntryComputation(builder.Build());

  HloConstantFolding const_folder;
  TF_ASSERT_OK_AND_ASSIGN(bool result, const_folder.Run(module.get()));
  EXPECT_TRUE(result);

  HloInstruction* root = computation->root_instruction();
  EXPECT_THAT(root, GmockMatch(m::Constant()));
  EXPECT_TRUE(ShapeUtil::Compatible(root->shape(), shape));

  using NativeT = typename primitive_util::PrimitiveTypeToNative<F32>::type;
  bool matched = true;
  root->literal().EachCell<NativeT>(
      [&](absl::Span<const int64_t> indices, NativeT value) {
        std::vector<int64_t> rindexes = PermuteInverse(indices, permutation);
        matched = matched && (value == literal_clone.Get<NativeT>(rindexes));
      });
  EXPECT_TRUE(matched);
}

const char* const kConstantFoldReduce = R"(
  HloModule ConstantFoldReduce

  add {
    a = s32[] parameter(0)
    b = s32[] parameter(1)
    ROOT add = s32[] add(a, b)
  }

  ENTRY r {
    x = s32[3] constant({1, 2, 3})
    init = s32[] constant(0)
    ROOT reduce = s32[] reduce(x, init), dimensions={0}, to_apply=add
  })";

TEST_F(HloConstantFoldingTest, ConstantFoldReduce) {
  TF_ASSERT_OK_AND_ASSIGN(auto m,
                          ParseAndReturnVerifiedModule(kConstantFoldReduce));
  HloConstantFolding const_folder;
  TF_ASSERT_OK_AND_ASSIGN(bool result, const_folder.Run(m.get()));
  EXPECT_TRUE(result);

  EXPECT_EQ(6, m->entry_computation()
                   ->root_instruction()
                   ->literal()
                   .GetFirstElement<int32_t>());
}

TEST_F(HloConstantFoldingTest, ConstantFoldReduceNoLayout) {
  TF_ASSERT_OK_AND_ASSIGN(auto m,
                          ParseAndReturnVerifiedModule(kConstantFoldReduce));
  HloInstruction* add = (*m->computations().begin())->root_instruction();
  LayoutUtil::ClearLayout(add->mutable_shape());
  HloConstantFolding const_folder;
  TF_ASSERT_OK_AND_ASSIGN(bool result, const_folder.Run(m.get()));
  EXPECT_FALSE(result);

  EXPECT_THAT(m->entry_computation()->root_instruction(),
              GmockMatch(m::Reduce()));
}

const char* const kConstantFoldLargePad = R"(
  HloModule ConstantFoldLargePad

  ENTRY r {
    a = f32[1,1,1] constant({{{7}}})
    b = f32[] constant(42)
    ROOT pad = f32[2048,2048,128] pad(a, b), padding=1024_1023x1024_1023x64_63
  })";

TEST_F(HloConstantFoldingTest, DoesNotFoldLargePad) {
  TF_ASSERT_OK_AND_ASSIGN(auto module,
                          ParseAndReturnVerifiedModule(kConstantFoldLargePad));
  HloConstantFolding const_folder;
  TF_ASSERT_OK_AND_ASSIGN(bool result, const_folder.Run(module.get()));
  EXPECT_FALSE(result);

  EXPECT_THAT(module->entry_computation()->root_instruction(),
              GmockMatch(m::Pad(m::Constant(), m::Constant())));
}

TEST_F(HloConstantFoldingTest, DontFoldSubcomputationContainingAfterAll) {
  const char* const kModuleStr = R"(
  HloModule test

  Fn {
    tok = token[] after-all()
    ROOT root = f32[10] iota(), iota_dimension=0
  }

  ENTRY entry {
    ROOT call = f32[10] call(), to_apply=Fn
  })";
  TF_ASSERT_OK_AND_ASSIGN(auto module,
                          ParseAndReturnVerifiedModule(kModuleStr));
  HloConstantFolding constant_folding;
  TF_ASSERT_OK_AND_ASSIGN(bool result,
                          RunHloPass(&constant_folding, module.get()));
  EXPECT_FALSE(result);
}

TEST_F(HloConstantFoldingTest,
       DontFoldSubcomputationTransitivelyContainingRng) {
  const char* const kModuleStr = R"(
  HloModule test

  InnerFn {
    c0 = f32[] constant(0)
    c1 = f32[] constant(1)
    ROOT rng = f32[10] rng(c0, c1), distribution=rng_uniform
  }

  Fn {
    ROOT fusion = f32[10] fusion(), kind=kLoop, calls=InnerFn
  }

  ENTRY entry {
    ROOT call = f32[10] call(), to_apply=Fn
  })";
  TF_ASSERT_OK_AND_ASSIGN(auto module,
                          ParseAndReturnVerifiedModule(kModuleStr));
  HloConstantFolding constant_folding;
  TF_ASSERT_OK_AND_ASSIGN(bool result,
                          RunHloPass(&constant_folding, module.get()));
  EXPECT_FALSE(result);
}

TEST_F(HloConstantFoldingTest, FoldOpsWhereOneOperandIsBroadcast) {
  const char* const kModuleStr = R"(
  HloModule test

  ENTRY entry {
    not_folded1 = f32[4] broadcast(f32[] constant(1))
    not_folded2 = add(f32[4] broadcast(f32[] constant(2)),
                      f32[4] broadcast(f32[] constant(3)))
    folded1 = add(f32[4] broadcast(f32[] constant(5)),
                  f32[4] constant({0,1,2,3}))
    folded2 = add(f32[4] constant({0,1,2,3}),
                  f32[4] broadcast(f32[] constant(5)))
    ROOT root = tuple(not_folded1, not_folded2, folded1, folded2)
  })";
  TF_ASSERT_OK_AND_ASSIGN(auto module,
                          ParseAndReturnVerifiedModule(kModuleStr));
  HloConstantFolding constant_folding;
  TF_ASSERT_OK_AND_ASSIGN(bool result,
                          RunHloPass(&constant_folding, module.get()));
  EXPECT_TRUE(result);
  EXPECT_THAT(module->entry_computation()->root_instruction(),
              GmockMatch(m::Tuple(m::Broadcast(m::Constant()),
                                  m::Add(m::Broadcast(m::Constant()),
                                         m::Broadcast(m::Constant())),
                                  m::Constant(),
                                  m::Constant()  //
                                  )));
}

TEST_F(HloConstantFoldingTest, BigReduceWindow) {
  constexpr absl::string_view kModuleStr = R"(
    HloModule test

    add_bf16 {
      lhs = bf16[] parameter(0)
      rhs = bf16[] parameter(1)
      ROOT add = bf16[] add(lhs, rhs)
    }

    ENTRY accumulated_all_reduce {
      x = bf16[160,10,10,512]{3,2,1,0} broadcast(bf16[] constant(1.0))
      init = bf16[] constant(0)
      ROOT reduce-window = reduce-window(x, init), window={size=1x2x2x1
      stride=1x2x2x1}, to_apply=add_bf16
    }
  )";
  TF_ASSERT_OK_AND_ASSIGN(auto module,
                          ParseAndReturnVerifiedModule(kModuleStr));
  HloConstantFolding constant_folding;
  TF_ASSERT_OK_AND_ASSIGN(bool result,
                          RunHloPass(&constant_folding, module.get()));
  EXPECT_TRUE(result);
}

TEST_F(HloConstantFoldingTest, TimingConsumingTest) {
  constexpr absl::string_view mod_str = R"(
    HloModule jit_f, entry_computation_layout={()->f32[]}
    region_0.4 {
      Arg_0.5 = f32[] parameter(0)
      Arg_1.6 = f32[] parameter(1)
      ROOT add.7 = f32[] add(Arg_0.5, Arg_1.6)
    }

    ENTRY main.9 {
      constant.1 = f32[] constant(1)
      broadcast.2 = f32[32,999,40,512]{3,2,1,0} broadcast(constant.1), dimensions={}
      constant.3 = f32[] constant(0)
      ROOT reduce.8 = f32[] reduce(broadcast.2, constant.3), dimensions={0,1,2,3}, to_apply=region_0.4
    }
   )";
  TF_ASSERT_OK_AND_ASSIGN(auto module, ParseAndReturnVerifiedModule(mod_str));
  HloConstantFolding const_fold;
  TF_ASSERT_OK_AND_ASSIGN(bool result, RunHloPass(&const_fold, module.get()));
  EXPECT_FALSE(result);
}

TEST_F(HloConstantFoldingTest, FoldFusionWithoutParameters) {
  const char* const kModuleStr = R"(
  HloModule test

  fused_computation.1 {
    iota.1 = f32[2496]{0} iota(), iota_dimension=0
    constant_1 = f32[] constant(-1)
    broadcast.1 = f32[2496]{0} broadcast(constant_1), dimensions={}
    add.1 = f32[2496]{0} add(iota.1, broadcast.1)

    iota.2 = f32[2496]{0} iota(), iota_dimension=0
    constant_2 = f32[] constant(0.5)
    broadcast.2 = f32[2496]{0} broadcast(constant_2), dimensions={}
    add.2 = f32[2496]{0} add(iota.2, broadcast.2)

    subtract.1 = f32[2496]{0} subtract(add.1, add.2)
    broadcast.3 = f32[2496,3]{1,0} broadcast(subtract.1), dimensions={0}
    ROOT bitcast.1 = f32[7488]{0} bitcast(broadcast.3 )
  }

  fused_computation.2 {
    constant_3 = f32[] constant(127)
    broadcast.4 = f32[7488]{0} broadcast(constant_3), dimensions={}
    param_1.1 = f32[7488]{0} parameter(0)
    add.3 = f32[7488]{0} add(param_1.1, broadcast.4)
    ROOT bitcast.2 = f32[3,2496]{1,0} bitcast(add.3)
  }

  ENTRY entry {
    fusion.1 = f32[7488]{0} fusion(), kind=kLoop, calls=fused_computation.1
    ROOT fusion.2 = f32[3,2496]{1,0} fusion(fusion.1), kind=kInput, calls=fused_computation.2
  })";
  TF_ASSERT_OK_AND_ASSIGN(auto module,
                          ParseAndReturnVerifiedModule(kModuleStr));
  HloConstantFolding constant_folding;
  TF_ASSERT_OK_AND_ASSIGN(bool result,
                          RunHloPass(&constant_folding, module.get()));
  EXPECT_TRUE(result);
  EXPECT_THAT(module->entry_computation()->root_instruction(),
              GmockMatch(m::Constant()));
}

TEST_F(HloConstantFoldingTest, DoNotFoldFusionsWithIllegalInstructions) {
  const char* const kModuleStr = R"(
  HloModule test

  fused_computation.1 {
    iota.1 = f32[2496]{0} iota(), iota_dimension=0
    constant_1 = f32[] constant(-1)
    broadcast.1 = f32[2496]{0} broadcast(constant_1), dimensions={}
    add.1 = f32[2496]{0} add(iota.1, broadcast.1)

    constant_2 = f32[] constant(0)
    constant_3 = f32[] constant(1)
    rng.1 = f32[2496] rng(constant_2, constant_3), distribution=rng_uniform
    constant_4 = f32[] constant(0.5)
    broadcast.2 = f32[2496]{0} broadcast(constant_4), dimensions={}
    add.2 = f32[2496]{0} add(rng.1, broadcast.2)

    subtract.1 = f32[2496]{0} subtract(add.1, add.2)
    broadcast.3 = f32[2496,3]{1,0} broadcast(subtract.1), dimensions={0}
    ROOT bitcast.1 = f32[7488]{0} bitcast(broadcast.3 )
  }

  fused_computation.2 {
    constant_3 = f32[] constant(127)
    broadcast.4 = f32[7488]{0} broadcast(constant_3), dimensions={}
    param_1.1 = f32[7488]{0} parameter(0)
    add.3 = f32[7488]{0} add(param_1.1, broadcast.4)
    ROOT bitcast.2 = f32[3,2496]{1,0} bitcast(add.3)
  }

  ENTRY entry {
    fusion.1 = f32[7488]{0} fusion(), kind=kLoop, calls=fused_computation.1
    ROOT fusion.2 = f32[3,2496]{1,0} fusion(fusion.1), kind=kInput, calls=fused_computation.2
  })";
  TF_ASSERT_OK_AND_ASSIGN(auto module,
                          ParseAndReturnVerifiedModule(kModuleStr));
  HloConstantFolding constant_folding;
  TF_ASSERT_OK_AND_ASSIGN(bool result,
                          RunHloPass(&constant_folding, module.get()));
  EXPECT_FALSE(result);
}

}  // namespace
}  // namespace xla
