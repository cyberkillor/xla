#!/usr/bin/env python
# Copyright 2023 The TensorFlow Authors. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
# ==============================================================================

"""Finds best tuning for a single matmul."""
import sys

from absl import app
from absl import flags
from matmul_lib import benchmark_cublas
from matmul_lib import benchmark_matmul
from matmul_lib import generate_tiling_configs
from matmul_lib import MatmulSize
from matmul_lib import MatmulTiming
from matmul_lib import parse_int_list
from matmul_lib import print_roofline_performance
import torch
import tqdm

_M = flags.DEFINE_integer('m', 64, 'Size of first matrix')
_K = flags.DEFINE_integer('k', 64, 'Size of contracting dimension')
_N = flags.DEFINE_integer('n', 64, 'Size of second matrix')
_QUANTIZED_LHS = flags.DEFINE_integer(
    'quantized_lhs', 0, 'Whether LHS is in int8'
)

_TILINGS_M = flags.DEFINE_string(
    'tilings_m', '32, 64, 128, 256', 'Tilings to try for M'
)
_TILINGS_N = flags.DEFINE_string(
    'tilings_n', '32, 64, 128, 256', 'Tilings to try for N'
)
_TILINGS_K = flags.DEFINE_string(
    'tilings_k', '32, 64, 128, 256, 512', 'Tilings to try for K'
)
_NUM_STAGES = flags.DEFINE_string(
    'num_stages', '1,2,3', 'Number of stages to try'
)
_NUM_WARPS = flags.DEFINE_string('num_warps', '4,8', 'Number of warps to try')
_SPLIT_KS = flags.DEFINE_string(
    'split_ks', '1,2,3,4,5', 'Number of split_k values to try'
)
_DEBUG = flags.DEFINE_bool('debug', False, 'Print debug information')


def main() -> None:
  dims = MatmulSize(
      M=_M.value, N=_N.value, K=_K.value, quantized_lhs=_QUANTIZED_LHS.value
  )
  s = torch.cuda.Stream()
  tilings = generate_tiling_configs(
      parse_int_list(_TILINGS_M.value),
      parse_int_list(_TILINGS_N.value),
      parse_int_list(_TILINGS_K.value),
      parse_int_list(_SPLIT_KS.value),
      parse_int_list(_NUM_STAGES.value),
      parse_int_list(_NUM_WARPS.value),
  )
  pbar = tqdm.tqdm(total=len(tilings))
  timings = sorted(
      benchmark_matmul(
          dims, pbar, s, tilings, repetitions_ms=300, debug=_DEBUG.value
      ),
      key=lambda t: t.min_time_ms,
  )
  fastest: MatmulTiming = timings[0]
  print(f'Fastest configuration: {fastest}')

  features = {
      'BLOCK_M',
      'BLOCK_N',
      'BLOCK_K',
      'SPLIT_K',
      'num_stages',
      'num_warps',
  }
  for f in features:
    other_features = features - {f}

    def other_features_equal_to_best(t):
      return all(
          getattr(fastest.tiling, of) == getattr(t.tiling, of)
          for of in other_features  # pylint: disable=cell-var-from-loop
      )

    # Keep everyting but the currently evaluated feature fixed to the best
    # value.
    others_fixed = [t for t in timings if other_features_equal_to_best(t)]

    # TODO(cheshire): Visualize.
    print(
        f'Varying feature {f}:',
        ', '.join(
            f'{t.min_time_ms:0.4f} @ {f}={getattr(t.tiling, f)}'
            for t in others_fixed
        ),
    )

  print_roofline_performance(dims, fastest.min_time_ms)
  cublas_time = benchmark_cublas(dims)
  print(f'Reference cuBLAS time (bf16xbf16->bf16): {cublas_time:0.4f}ms')


if __name__ == '__main__':
  app.parse_flags_with_usage(sys.argv)
  main()
