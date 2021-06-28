/*******************************************************************************
 *
 * MIT License
 *
 * Copyright (c) 2021 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 *******************************************************************************/

#include <cstddef>
#include <miopen/solver.hpp>
#include <miopen/handle.hpp>
#include <miopen/generic_search.hpp>
#include <miopen/conv/wrw_invoke_params.hpp>
#include <miopen/solver/implicitgemm_util.hpp>
#include <miopen/gcn_asm_utils.hpp>
#include <miopen/tensor_ops.hpp>
#include <miopen/conv/asm_implicit_gemm.hpp>
#include <stdio.h>

MIOPEN_DECLARE_ENV_VAR(MIOPEN_DEBUG_CONV_IMPLICIT_GEMM_ASM_WRW_GTC_XDLOPS_NHWC)

#define WRW_MAX_GEMM_K_SPLITS 10
//#define DEBUG_IGEMM_ASM_WRW_NHWC_CHECK_VALID_TILE_LIST

namespace miopen {
namespace solver {

static const inline std::vector<PerformanceConfigAsmImplicitGemmGTCWrwXdlopsNHWC>&
GetWrwXdlopsNHWCConfigList()
{
    // clang-format off
    static const  std::vector<PerformanceConfigAsmImplicitGemmGTCWrwXdlopsNHWC> kernel_param_list {
        {"wrw", "nhwc", miopenFloat,  0, 0, 256, 128,  16, 32, 32,  2, 2, 1, 2, 2, 0, 0, 0, 0, 0, { 1, 1, 1,16}, {  1, 16,  1, 16}, { 1, 1, 1, 8}, {  1, 16,  1, 16}},
        {"wrw", "nhwc", miopenFloat,  0, 0, 256, 128,  16, 32, 32,  2, 2, 1, 2, 2, 0, 0, 1, 0, 0, { 1, 1, 1,16}, {  1, 16,  1, 16}, { 1, 1, 1, 8}, {  1, 16,  1, 16}},
        {"wrw", "nhwc", miopenFloat,  0, 1, 256, 128,  16, 32, 32,  2, 2, 1, 2, 2, 0, 0, 0, 0, 0, { 1, 1, 1,16}, {  1, 16,  1, 16}, { 1, 1, 1, 8}, {  1, 16,  1, 16}},
        {"wrw", "nhwc", miopenFloat,  0, 1, 256, 128,  16, 32, 32,  2, 2, 1, 2, 2, 0, 0, 1, 0, 0, { 1, 1, 1,16}, {  1, 16,  1, 16}, { 1, 1, 1, 8}, {  1, 16,  1, 16}},
        {"wrw", "nhwc", miopenFloat,  0, 0, 128, 256,  16, 32, 32,  2, 1, 2, 2, 2, 0, 0, 0, 0, 0, { 1, 1, 1, 8}, {  1, 16,  1, 16}, { 1, 1, 1,16}, {  1, 16,  1, 16}},
        {"wrw", "nhwc", miopenFloat,  0, 0, 128, 256,  16, 32, 32,  2, 1, 2, 2, 2, 0, 0, 1, 0, 0, { 1, 1, 1, 8}, {  1, 16,  1, 16}, { 1, 1, 1,16}, {  1, 16,  1, 16}},
        {"wrw", "nhwc", miopenFloat,  0, 1, 128, 256,  16, 32, 32,  2, 1, 2, 2, 2, 0, 0, 0, 0, 0, { 1, 1, 1, 8}, {  1, 16,  1, 16}, { 1, 1, 1,16}, {  1, 16,  1, 16}},
        {"wrw", "nhwc", miopenFloat,  0, 1, 128, 256,  16, 32, 32,  2, 1, 2, 2, 2, 0, 0, 1, 0, 0, { 1, 1, 1, 8}, {  1, 16,  1, 16}, { 1, 1, 1,16}, {  1, 16,  1, 16}},
        {"wrw", "nhwc", miopenFloat,  0, 0, 128, 128,  16, 32, 32,  2, 1, 2, 1, 2, 0, 0, 0, 0, 0, { 1, 1, 1, 8}, {  1, 16,  1, 16}, { 1, 1, 1, 8}, {  1, 16,  1, 16}},
        {"wrw", "nhwc", miopenFloat,  0, 0, 128, 128,  16, 32, 32,  2, 1, 2, 1, 2, 0, 0, 1, 0, 0, { 1, 1, 1, 8}, {  1, 16,  1, 16}, { 1, 1, 1, 8}, {  1, 16,  1, 16}},
        {"wrw", "nhwc", miopenFloat,  0, 1, 128, 128,  16, 32, 32,  2, 1, 2, 1, 2, 0, 0, 0, 0, 0, { 1, 1, 1, 8}, {  1, 16,  1, 16}, { 1, 1, 1, 8}, {  1, 16,  1, 16}},
        {"wrw", "nhwc", miopenFloat,  0, 1, 128, 128,  16, 32, 32,  2, 1, 2, 1, 2, 0, 0, 1, 0, 0, { 1, 1, 1, 8}, {  1, 16,  1, 16}, { 1, 1, 1, 8}, {  1, 16,  1, 16}},
        {"wrw", "nhwc", miopenFloat,  0, 0, 256,  64,  16, 32, 32,  2, 1, 1, 2, 2, 0, 0, 0, 0, 0, { 1, 1, 1,16}, {  1, 16,  1, 16}, { 1, 1, 1, 4}, {  1, 16,  1, 16}},
        {"wrw", "nhwc", miopenFloat,  0, 0, 256,  64,  16, 32, 32,  2, 1, 1, 2, 2, 0, 0, 1, 0, 0, { 1, 1, 1,16}, {  1, 16,  1, 16}, { 1, 1, 1, 4}, {  1, 16,  1, 16}},
        {"wrw", "nhwc", miopenFloat,  0, 1, 256,  64,  16, 32, 32,  2, 1, 1, 2, 2, 0, 0, 0, 0, 0, { 1, 1, 1,16}, {  1, 16,  1, 16}, { 1, 1, 1, 4}, {  1, 16,  1, 16}},
        {"wrw", "nhwc", miopenFloat,  0, 1, 256,  64,  16, 32, 32,  2, 1, 1, 2, 2, 0, 0, 1, 0, 0, { 1, 1, 1,16}, {  1, 16,  1, 16}, { 1, 1, 1, 4}, {  1, 16,  1, 16}},
        {"wrw", "nhwc", miopenFloat,  0, 0,  64, 256,  16, 32, 32,  2, 1, 1, 2, 2, 0, 0, 0, 0, 0, { 1, 1, 1, 4}, {  1, 16,  1, 16}, { 1, 1, 1,16}, {  1, 16,  1, 16}},
        {"wrw", "nhwc", miopenFloat,  0, 0,  64, 256,  16, 32, 32,  2, 1, 1, 2, 2, 0, 0, 1, 0, 0, { 1, 1, 1, 4}, {  1, 16,  1, 16}, { 1, 1, 1,16}, {  1, 16,  1, 16}},
        {"wrw", "nhwc", miopenFloat,  0, 1,  64, 256,  16, 32, 32,  2, 1, 1, 2, 2, 0, 0, 0, 0, 0, { 1, 1, 1, 4}, {  1, 16,  1, 16}, { 1, 1, 1,16}, {  1, 16,  1, 16}},
        {"wrw", "nhwc", miopenFloat,  0, 1,  64, 256,  16, 32, 32,  2, 1, 1, 2, 2, 0, 0, 1, 0, 0, { 1, 1, 1, 4}, {  1, 16,  1, 16}, { 1, 1, 1,16}, {  1, 16,  1, 16}},
        {"wrw", "nhwc", miopenFloat,  0, 0, 128,  64,  16, 32, 32,  2, 1, 1, 2, 1, 0, 0, 0, 0, 0, { 1, 1, 1, 8}, {  1, 16,  1, 16}, { 1, 1, 1, 4}, {  1, 16,  1, 16}},
        {"wrw", "nhwc", miopenFloat,  0, 0, 128,  64,  16, 32, 32,  2, 1, 1, 2, 1, 0, 0, 1, 0, 0, { 1, 1, 1, 8}, {  1, 16,  1, 16}, { 1, 1, 1, 4}, {  1, 16,  1, 16}},
        {"wrw", "nhwc", miopenFloat,  0, 1, 128,  64,  16, 32, 32,  2, 1, 1, 2, 1, 0, 0, 0, 0, 0, { 1, 1, 1, 8}, {  1, 16,  1, 16}, { 1, 1, 1, 4}, {  1, 16,  1, 16}},
        {"wrw", "nhwc", miopenFloat,  0, 1, 128,  64,  16, 32, 32,  2, 1, 1, 2, 1, 0, 0, 1, 0, 0, { 1, 1, 1, 8}, {  1, 16,  1, 16}, { 1, 1, 1, 4}, {  1, 16,  1, 16}},
        {"wrw", "nhwc", miopenFloat,  0, 0,  64, 128,  16, 32, 32,  2, 1, 1, 1, 2, 0, 0, 0, 0, 0, { 1, 1, 1, 4}, {  1, 16,  1, 16}, { 1, 1, 1, 8}, {  1, 16,  1, 16}},
        {"wrw", "nhwc", miopenFloat,  0, 0,  64, 128,  16, 32, 32,  2, 1, 1, 1, 2, 0, 0, 1, 0, 0, { 1, 1, 1, 4}, {  1, 16,  1, 16}, { 1, 1, 1, 8}, {  1, 16,  1, 16}},
        {"wrw", "nhwc", miopenFloat,  0, 1,  64, 128,  16, 32, 32,  2, 1, 1, 1, 2, 0, 0, 0, 0, 0, { 1, 1, 1, 4}, {  1, 16,  1, 16}, { 1, 1, 1, 8}, {  1, 16,  1, 16}},
        {"wrw", "nhwc", miopenFloat,  0, 1,  64, 128,  16, 32, 32,  2, 1, 1, 1, 2, 0, 0, 1, 0, 0, { 1, 1, 1, 4}, {  1, 16,  1, 16}, { 1, 1, 1, 8}, {  1, 16,  1, 16}},
        {"wrw", "nhwc", miopenFloat,  0, 0, 256,  32,  16, 32, 32,  2, 1, 1, 2, 1, 0, 0, 0, 0, 0, { 1, 1, 1,16}, {  1, 16,  1, 16}, { 1, 1, 1, 2}, {  1, 16,  1, 16}},
        {"wrw", "nhwc", miopenFloat,  0, 0, 256,  32,  16, 32, 32,  2, 1, 1, 2, 1, 0, 0, 1, 0, 0, { 1, 1, 1,16}, {  1, 16,  1, 16}, { 1, 1, 1, 2}, {  1, 16,  1, 16}},
        {"wrw", "nhwc", miopenFloat,  0, 1, 256,  32,  16, 32, 32,  2, 1, 1, 2, 1, 0, 0, 0, 0, 0, { 1, 1, 1,16}, {  1, 16,  1, 16}, { 1, 1, 1, 2}, {  1, 16,  1, 16}},
        {"wrw", "nhwc", miopenFloat,  0, 1, 256,  32,  16, 32, 32,  2, 1, 1, 2, 1, 0, 0, 1, 0, 0, { 1, 1, 1,16}, {  1, 16,  1, 16}, { 1, 1, 1, 2}, {  1, 16,  1, 16}},
        {"wrw", "nhwc", miopenFloat,  0, 0,  32, 256,  16, 32, 32,  2, 1, 1, 1, 2, 0, 0, 0, 0, 0, { 1, 1, 1, 2}, {  1, 16,  1, 16}, { 1, 1, 1,16}, {  1, 16,  1, 16}},
        {"wrw", "nhwc", miopenFloat,  0, 0,  32, 256,  16, 32, 32,  2, 1, 1, 1, 2, 0, 0, 1, 0, 0, { 1, 1, 1, 2}, {  1, 16,  1, 16}, { 1, 1, 1,16}, {  1, 16,  1, 16}},
        {"wrw", "nhwc", miopenFloat,  0, 1,  32, 256,  16, 32, 32,  2, 1, 1, 1, 2, 0, 0, 0, 0, 0, { 1, 1, 1, 2}, {  1, 16,  1, 16}, { 1, 1, 1,16}, {  1, 16,  1, 16}},
        {"wrw", "nhwc", miopenFloat,  0, 1,  32, 256,  16, 32, 32,  2, 1, 1, 1, 2, 0, 0, 1, 0, 0, { 1, 1, 1, 2}, {  1, 16,  1, 16}, { 1, 1, 1,16}, {  1, 16,  1, 16}},
        {"wrw", "nhwc", miopenFloat,  0, 0,  64,  64,  16, 32, 32,  2, 1, 1, 1, 1, 0, 0, 0, 0, 0, { 1, 1, 1, 4}, {  1, 16,  1, 16}, { 1, 1, 1, 4}, {  1, 16,  1, 16}},
        {"wrw", "nhwc", miopenFloat,  0, 0,  64,  64,  16, 32, 32,  2, 1, 1, 1, 1, 0, 0, 1, 0, 0, { 1, 1, 1, 4}, {  1, 16,  1, 16}, { 1, 1, 1, 4}, {  1, 16,  1, 16}},
        {"wrw", "nhwc", miopenFloat,  0, 1,  64,  64,  16, 32, 32,  2, 1, 1, 1, 1, 0, 0, 0, 0, 0, { 1, 1, 1, 4}, {  1, 16,  1, 16}, { 1, 1, 1, 4}, {  1, 16,  1, 16}},
        {"wrw", "nhwc", miopenFloat,  0, 1,  64,  64,  16, 32, 32,  2, 1, 1, 1, 1, 0, 0, 1, 0, 0, { 1, 1, 1, 4}, {  1, 16,  1, 16}, { 1, 1, 1, 4}, {  1, 16,  1, 16}},
        {"wrw", "nhwc", miopenFloat,  0, 0, 128,  32,  16, 32, 32,  2, 1, 1, 1, 1, 0, 0, 0, 0, 0, { 1, 1, 1, 8}, {  1, 16,  1, 16}, { 1, 1, 1, 2}, {  1, 16,  1, 16}},
        {"wrw", "nhwc", miopenFloat,  0, 0, 128,  32,  16, 32, 32,  2, 1, 1, 1, 1, 0, 0, 1, 0, 0, { 1, 1, 1, 8}, {  1, 16,  1, 16}, { 1, 1, 1, 2}, {  1, 16,  1, 16}},
        {"wrw", "nhwc", miopenFloat,  0, 1, 128,  32,  16, 32, 32,  2, 1, 1, 1, 1, 0, 0, 0, 0, 0, { 1, 1, 1, 8}, {  1, 16,  1, 16}, { 1, 1, 1, 2}, {  1, 16,  1, 16}},
        {"wrw", "nhwc", miopenFloat,  0, 1, 128,  32,  16, 32, 32,  2, 1, 1, 1, 1, 0, 0, 1, 0, 0, { 1, 1, 1, 8}, {  1, 16,  1, 16}, { 1, 1, 1, 2}, {  1, 16,  1, 16}},
        {"wrw", "nhwc", miopenFloat,  0, 0,  32, 128,  16, 32, 32,  2, 1, 1, 1, 1, 0, 0, 0, 0, 0, { 1, 1, 1, 2}, {  1, 16,  1, 16}, { 1, 1, 1, 8}, {  1, 16,  1, 16}},
        {"wrw", "nhwc", miopenFloat,  0, 0,  32, 128,  16, 32, 32,  2, 1, 1, 1, 1, 0, 0, 1, 0, 0, { 1, 1, 1, 2}, {  1, 16,  1, 16}, { 1, 1, 1, 8}, {  1, 16,  1, 16}},
        {"wrw", "nhwc", miopenFloat,  0, 1,  32, 128,  16, 32, 32,  2, 1, 1, 1, 1, 0, 0, 0, 0, 0, { 1, 1, 1, 2}, {  1, 16,  1, 16}, { 1, 1, 1, 8}, {  1, 16,  1, 16}},
        {"wrw", "nhwc", miopenFloat,  0, 1,  32, 128,  16, 32, 32,  2, 1, 1, 1, 1, 0, 0, 1, 0, 0, { 1, 1, 1, 2}, {  1, 16,  1, 16}, { 1, 1, 1, 8}, {  1, 16,  1, 16}},
        {"wrw", "nhwc", miopenFloat,  0, 0,  64,  32,  16, 16, 16,  4, 1, 1, 2, 1, 0, 0, 0, 0, 0, { 1, 1, 1, 4}, {  1, 16,  1, 16}, { 1, 1, 1, 2}, {  1, 16,  1, 16}},
        {"wrw", "nhwc", miopenFloat,  0, 0,  64,  32,  16, 16, 16,  4, 1, 1, 2, 1, 0, 0, 1, 0, 0, { 1, 1, 1, 4}, {  1, 16,  1, 16}, { 1, 1, 1, 2}, {  1, 16,  1, 16}},
        {"wrw", "nhwc", miopenFloat,  0, 1,  64,  32,  16, 16, 16,  4, 1, 1, 2, 1, 0, 0, 0, 0, 0, { 1, 1, 1, 4}, {  1, 16,  1, 16}, { 1, 1, 1, 2}, {  1, 16,  1, 16}},
        {"wrw", "nhwc", miopenFloat,  0, 1,  64,  32,  16, 16, 16,  4, 1, 1, 2, 1, 0, 0, 1, 0, 0, { 1, 1, 1, 4}, {  1, 16,  1, 16}, { 1, 1, 1, 2}, {  1, 16,  1, 16}},
        {"wrw", "nhwc", miopenFloat,  0, 0,  32,  64,  16, 16, 16,  4, 1, 1, 1, 2, 0, 0, 0, 0, 0, { 1, 1, 1, 2}, {  1, 16,  1, 16}, { 1, 1, 1, 4}, {  1, 16,  1, 16}},
        {"wrw", "nhwc", miopenFloat,  0, 0,  32,  64,  16, 16, 16,  4, 1, 1, 1, 2, 0, 0, 1, 0, 0, { 1, 1, 1, 2}, {  1, 16,  1, 16}, { 1, 1, 1, 4}, {  1, 16,  1, 16}},
        {"wrw", "nhwc", miopenFloat,  0, 1,  32,  64,  16, 16, 16,  4, 1, 1, 1, 2, 0, 0, 0, 0, 0, { 1, 1, 1, 2}, {  1, 16,  1, 16}, { 1, 1, 1, 4}, {  1, 16,  1, 16}},
        {"wrw", "nhwc", miopenFloat,  0, 1,  32,  64,  16, 16, 16,  4, 1, 1, 1, 2, 0, 0, 1, 0, 0, { 1, 1, 1, 2}, {  1, 16,  1, 16}, { 1, 1, 1, 4}, {  1, 16,  1, 16}},
        {"wrw", "nhwc", miopenFloat,  0, 0,  32,  32,  32, 16, 16,  4, 1, 1, 1, 1, 0, 0, 0, 0, 0, { 1, 1, 1, 4}, {  1, 32,  1,  8}, { 1, 1, 1, 4}, {  1, 32,  1,  8}},
        {"wrw", "nhwc", miopenFloat,  0, 0,  32,  32,  32, 16, 16,  4, 1, 1, 1, 1, 0, 0, 1, 0, 0, { 1, 1, 1, 4}, {  1, 32,  1,  8}, { 1, 1, 1, 4}, {  1, 32,  1,  8}},
        {"wrw", "nhwc", miopenFloat,  0, 1,  32,  32,  32, 16, 16,  4, 1, 1, 1, 1, 0, 0, 0, 0, 0, { 1, 1, 1, 4}, {  1, 32,  1,  8}, { 1, 1, 1, 4}, {  1, 32,  1,  8}},
        {"wrw", "nhwc", miopenFloat,  0, 1,  32,  32,  32, 16, 16,  4, 1, 1, 1, 1, 0, 0, 1, 0, 0, { 1, 1, 1, 4}, {  1, 32,  1,  8}, { 1, 1, 1, 4}, {  1, 32,  1,  8}},

        {"wrw", "nhwc", miopenHalf,  0, 1, 256, 128,  32, 32, 32,  8, 2, 1, 2, 2, 0, 0, 0, 0, 0, { 1, 4, 1, 8}, {  1,  8,  1, 32}, { 1, 4, 1, 4}, {  1,  8,  1, 32}},
        {"wrw", "nhwc", miopenHalf,  0, 1, 256, 128,  32, 32, 32,  8, 2, 1, 2, 2, 0, 0, 1, 0, 0, { 1, 4, 1, 8}, {  1,  8,  1, 32}, { 1, 4, 1, 4}, {  1,  8,  1, 32}},
        {"wrw", "nhwc", miopenHalf,  0, 1, 256, 128,  16, 32, 32,  8, 2, 1, 2, 2, 0, 0, 1, 0, 0, { 1, 4, 1, 4}, {  1,  4,  1, 64}, { 1, 4, 1, 2}, {  1,  4,  1, 64}},
        {"wrw", "nhwc", miopenHalf,  0, 0, 256, 128,  32, 32, 32,  8, 2, 1, 2, 2, 0, 0, 0, 0, 0, { 1, 4, 1, 8}, {  1,  8,  1, 32}, { 1, 4, 1, 4}, {  1,  8,  1, 32}},
        {"wrw", "nhwc", miopenHalf,  0, 0, 256, 128,  32, 32, 32,  8, 2, 1, 2, 2, 0, 0, 1, 0, 0, { 1, 4, 1, 8}, {  1,  8,  1, 32}, { 1, 4, 1, 4}, {  1,  8,  1, 32}},
        {"wrw", "nhwc", miopenHalf,  0, 0, 256, 128,  16, 32, 32,  8, 2, 1, 2, 2, 0, 0, 1, 0, 0, { 1, 4, 1, 4}, {  1,  4,  1, 64}, { 1, 4, 1, 2}, {  1,  4,  1, 64}},
        {"wrw", "nhwc", miopenHalf,  0, 1, 128, 256,  32, 32, 32,  8, 1, 2, 2, 2, 0, 0, 0, 0, 0, { 1, 4, 1, 4}, {  1,  8,  1, 32}, { 1, 4, 1, 8}, {  1,  8,  1, 32}},
        {"wrw", "nhwc", miopenHalf,  0, 1, 128, 256,  32, 32, 32,  8, 1, 2, 2, 2, 0, 0, 1, 0, 0, { 1, 4, 1, 4}, {  1,  8,  1, 32}, { 1, 4, 1, 8}, {  1,  8,  1, 32}},
        {"wrw", "nhwc", miopenHalf,  0, 1, 128, 256,  16, 32, 32,  8, 1, 2, 2, 2, 0, 0, 1, 0, 0, { 1, 4, 1, 2}, {  1,  4,  1, 64}, { 1, 4, 1, 4}, {  1,  4,  1, 64}},
        {"wrw", "nhwc", miopenHalf,  0, 0, 128, 256,  32, 32, 32,  8, 1, 2, 2, 2, 0, 0, 0, 0, 0, { 1, 4, 1, 4}, {  1,  8,  1, 32}, { 1, 4, 1, 8}, {  1,  8,  1, 32}},
        {"wrw", "nhwc", miopenHalf,  0, 0, 128, 256,  32, 32, 32,  8, 1, 2, 2, 2, 0, 0, 1, 0, 0, { 1, 4, 1, 4}, {  1,  8,  1, 32}, { 1, 4, 1, 8}, {  1,  8,  1, 32}},
        {"wrw", "nhwc", miopenHalf,  0, 0, 128, 256,  16, 32, 32,  8, 1, 2, 2, 2, 0, 0, 1, 0, 0, { 1, 4, 1, 2}, {  1,  4,  1, 64}, { 1, 4, 1, 4}, {  1,  4,  1, 64}},
        {"wrw", "nhwc", miopenHalf,  0, 1, 128, 128,  32, 32, 32,  8, 1, 1, 2, 2, 0, 0, 0, 0, 0, { 1, 4, 1, 4}, {  1,  8,  1, 32}, { 1, 4, 1, 4}, {  1,  8,  1, 32}},
        {"wrw", "nhwc", miopenHalf,  0, 1, 128, 128,  32, 32, 32,  8, 1, 1, 2, 2, 0, 0, 1, 0, 0, { 1, 4, 1, 4}, {  1,  8,  1, 32}, { 1, 4, 1, 4}, {  1,  8,  1, 32}},
        {"wrw", "nhwc", miopenHalf,  0, 1, 128, 128,  16, 32, 32,  8, 1, 1, 2, 2, 0, 0, 1, 0, 0, { 1, 4, 1, 2}, {  1,  4,  1, 64}, { 1, 4, 1, 2}, {  1,  4,  1, 64}},
        {"wrw", "nhwc", miopenHalf,  0, 0, 128, 128,  32, 32, 32,  8, 1, 1, 2, 2, 0, 0, 0, 0, 0, { 1, 4, 1, 4}, {  1,  8,  1, 32}, { 1, 4, 1, 4}, {  1,  8,  1, 32}},
        {"wrw", "nhwc", miopenHalf,  0, 0, 128, 128,  32, 32, 32,  8, 1, 1, 2, 2, 0, 0, 1, 0, 0, { 1, 4, 1, 4}, {  1,  8,  1, 32}, { 1, 4, 1, 4}, {  1,  8,  1, 32}},
        {"wrw", "nhwc", miopenHalf,  0, 0, 128, 128,  16, 32, 32,  8, 1, 1, 2, 2, 0, 0, 1, 0, 0, { 1, 4, 1, 2}, {  1,  4,  1, 64}, { 1, 4, 1, 2}, {  1,  4,  1, 64}},
        {"wrw", "nhwc", miopenHalf,  0, 1, 256,  64,  32, 32, 32,  8, 1, 1, 2, 2, 0, 0, 0, 0, 0, { 1, 4, 1, 8}, {  1,  8,  1, 32}, { 1, 4, 1, 2}, {  1,  8,  1, 32}},
        {"wrw", "nhwc", miopenHalf,  0, 1, 256,  64,  32, 32, 32,  8, 1, 1, 2, 2, 0, 0, 1, 0, 0, { 1, 4, 1, 8}, {  1,  8,  1, 32}, { 1, 4, 1, 2}, {  1,  8,  1, 32}},
        {"wrw", "nhwc", miopenHalf,  0, 1, 256,  64,  16, 32, 32,  8, 1, 1, 2, 2, 0, 0, 1, 0, 0, { 1, 4, 1, 4}, {  1,  4,  1, 64}, { 1, 4, 1, 1}, {  1,  4,  1, 64}},
        {"wrw", "nhwc", miopenHalf,  0, 0, 256,  64,  32, 32, 32,  8, 1, 1, 2, 2, 0, 0, 0, 0, 0, { 1, 4, 1, 8}, {  1,  8,  1, 32}, { 1, 4, 1, 2}, {  1,  8,  1, 32}},
        {"wrw", "nhwc", miopenHalf,  0, 0, 256,  64,  32, 32, 32,  8, 1, 1, 2, 2, 0, 0, 1, 0, 0, { 1, 4, 1, 8}, {  1,  8,  1, 32}, { 1, 4, 1, 2}, {  1,  8,  1, 32}},
        {"wrw", "nhwc", miopenHalf,  0, 0, 256,  64,  16, 32, 32,  8, 1, 1, 2, 2, 0, 0, 1, 0, 0, { 1, 4, 1, 4}, {  1,  4,  1, 64}, { 1, 4, 1, 1}, {  1,  4,  1, 64}},
        {"wrw", "nhwc", miopenHalf,  0, 1,  64, 256,  32, 32, 32,  8, 1, 1, 2, 2, 0, 0, 0, 0, 0, { 1, 4, 1, 2}, {  1,  8,  1, 32}, { 1, 4, 1, 8}, {  1,  8,  1, 32}},
        {"wrw", "nhwc", miopenHalf,  0, 1,  64, 256,  32, 32, 32,  8, 1, 1, 2, 2, 0, 0, 1, 0, 0, { 1, 4, 1, 2}, {  1,  8,  1, 32}, { 1, 4, 1, 8}, {  1,  8,  1, 32}},
        {"wrw", "nhwc", miopenHalf,  0, 1,  64, 256,  16, 32, 32,  8, 1, 1, 2, 2, 0, 0, 1, 0, 0, { 1, 4, 1, 1}, {  1,  4,  1, 64}, { 1, 4, 1, 4}, {  1,  4,  1, 64}},
        {"wrw", "nhwc", miopenHalf,  0, 0,  64, 256,  32, 32, 32,  8, 1, 1, 2, 2, 0, 0, 0, 0, 0, { 1, 4, 1, 2}, {  1,  8,  1, 32}, { 1, 4, 1, 8}, {  1,  8,  1, 32}},
        {"wrw", "nhwc", miopenHalf,  0, 0,  64, 256,  32, 32, 32,  8, 1, 1, 2, 2, 0, 0, 1, 0, 0, { 1, 4, 1, 2}, {  1,  8,  1, 32}, { 1, 4, 1, 8}, {  1,  8,  1, 32}},
        {"wrw", "nhwc", miopenHalf,  0, 0,  64, 256,  16, 32, 32,  8, 1, 1, 2, 2, 0, 0, 1, 0, 0, { 1, 4, 1, 1}, {  1,  4,  1, 64}, { 1, 4, 1, 4}, {  1,  4,  1, 64}},
        {"wrw", "nhwc", miopenHalf,  0, 1, 128,  64,  32, 32, 32,  8, 1, 1, 1, 2, 0, 0, 0, 0, 0, { 1, 4, 1, 4}, {  1,  8,  1, 32}, { 1, 4, 1, 2}, {  1,  8,  1, 32}},
        {"wrw", "nhwc", miopenHalf,  0, 1, 128,  64,  32, 32, 32,  8, 1, 1, 1, 2, 0, 0, 1, 0, 0, { 1, 4, 1, 4}, {  1,  8,  1, 32}, { 1, 4, 1, 2}, {  1,  8,  1, 32}},
        {"wrw", "nhwc", miopenHalf,  0, 0, 128,  64,  32, 32, 32,  8, 1, 1, 1, 2, 0, 0, 0, 0, 0, { 1, 4, 1, 4}, {  1,  8,  1, 32}, { 1, 4, 1, 2}, {  1,  8,  1, 32}},
        {"wrw", "nhwc", miopenHalf,  0, 0, 128,  64,  32, 32, 32,  8, 1, 1, 1, 2, 0, 0, 1, 0, 0, { 1, 4, 1, 4}, {  1,  8,  1, 32}, { 1, 4, 1, 2}, {  1,  8,  1, 32}},
        {"wrw", "nhwc", miopenHalf,  0, 1,  64, 128,  32, 32, 32,  8, 1, 1, 2, 1, 0, 0, 0, 0, 0, { 1, 4, 1, 2}, {  1,  8,  1, 32}, { 1, 4, 1, 4}, {  1,  8,  1, 32}},
        {"wrw", "nhwc", miopenHalf,  0, 1,  64, 128,  32, 32, 32,  8, 1, 1, 2, 1, 0, 0, 1, 0, 0, { 1, 4, 1, 2}, {  1,  8,  1, 32}, { 1, 4, 1, 4}, {  1,  8,  1, 32}},
        {"wrw", "nhwc", miopenHalf,  0, 1,  64, 128,  16, 32, 32,  8, 1, 1, 2, 1, 0, 0, 1, 0, 0, { 1, 4, 1, 1}, {  1,  4,  1, 64}, { 1, 4, 1, 2}, {  1,  4,  1, 64}},
        {"wrw", "nhwc", miopenHalf,  0, 0,  64, 128,  32, 32, 32,  8, 1, 1, 2, 1, 0, 0, 0, 0, 0, { 1, 4, 1, 2}, {  1,  8,  1, 32}, { 1, 4, 1, 4}, {  1,  8,  1, 32}},
        {"wrw", "nhwc", miopenHalf,  0, 0,  64, 128,  32, 32, 32,  8, 1, 1, 2, 1, 0, 0, 1, 0, 0, { 1, 4, 1, 2}, {  1,  8,  1, 32}, { 1, 4, 1, 4}, {  1,  8,  1, 32}},
        {"wrw", "nhwc", miopenHalf,  0, 0,  64, 128,  16, 32, 32,  8, 1, 1, 2, 1, 0, 0, 1, 0, 0, { 1, 4, 1, 1}, {  1,  4,  1, 64}, { 1, 4, 1, 2}, {  1,  4,  1, 64}},
        {"wrw", "nhwc", miopenHalf,  0, 1, 256,  32,  32, 64, 16,  4, 1, 1, 2, 1, 0, 0, 0, 0, 0, { 1, 4, 1, 8}, {  1,  8,  1, 32}, { 1, 4, 1, 1}, {  1,  8,  1, 32}},
        {"wrw", "nhwc", miopenHalf,  0, 1, 256,  32,  32, 64, 16,  4, 1, 1, 2, 1, 0, 0, 1, 0, 0, { 1, 4, 1, 8}, {  1,  8,  1, 32}, { 1, 4, 1, 1}, {  1,  8,  1, 32}},
        {"wrw", "nhwc", miopenHalf,  0, 0, 256,  32,  32, 64, 16,  4, 1, 1, 2, 1, 0, 0, 0, 0, 0, { 1, 4, 1, 8}, {  1,  8,  1, 32}, { 1, 4, 1, 1}, {  1,  8,  1, 32}},
        {"wrw", "nhwc", miopenHalf,  0, 0, 256,  32,  32, 64, 16,  4, 1, 1, 2, 1, 0, 0, 1, 0, 0, { 1, 4, 1, 8}, {  1,  8,  1, 32}, { 1, 4, 1, 1}, {  1,  8,  1, 32}},
        {"wrw", "nhwc", miopenHalf,  0, 1,  32, 256,  32, 16, 64,  4, 1, 1, 1, 2, 0, 0, 0, 0, 0, { 1, 4, 1, 1}, {  1,  8,  1, 32}, { 1, 4, 1, 8}, {  1,  8,  1, 32}},
        {"wrw", "nhwc", miopenHalf,  0, 1,  32, 256,  32, 16, 64,  4, 1, 1, 1, 2, 0, 0, 1, 0, 0, { 1, 4, 1, 1}, {  1,  8,  1, 32}, { 1, 4, 1, 8}, {  1,  8,  1, 32}},
        {"wrw", "nhwc", miopenHalf,  0, 0,  32, 256,  32, 16, 64,  4, 1, 1, 1, 2, 0, 0, 0, 0, 0, { 1, 4, 1, 1}, {  1,  8,  1, 32}, { 1, 4, 1, 8}, {  1,  8,  1, 32}},
        {"wrw", "nhwc", miopenHalf,  0, 0,  32, 256,  32, 16, 64,  4, 1, 1, 1, 2, 0, 0, 1, 0, 0, { 1, 4, 1, 1}, {  1,  8,  1, 32}, { 1, 4, 1, 8}, {  1,  8,  1, 32}},
        {"wrw", "nhwc", miopenHalf,  0, 1,  64,  64,  32, 32, 32,  8, 1, 1, 1, 1, 0, 0, 1, 0, 0, { 1, 8, 1, 1}, {  1,  4,  1, 64}, { 1, 8, 1, 1}, {  1,  4,  1, 64}},
        {"wrw", "nhwc", miopenHalf,  0, 1,  64,  64,  16, 32, 32,  8, 1, 1, 1, 1, 0, 0, 1, 0, 0, { 1, 4, 1, 1}, {  1,  4,  1, 64}, { 1, 4, 1, 1}, {  1,  4,  1, 64}},
        {"wrw", "nhwc", miopenHalf,  0, 1,  64,  64,  64, 32, 32,  8, 1, 1, 1, 1, 0, 0, 0, 0, 0, { 1, 4, 1, 4}, {  1, 16,  1, 16}, { 1, 4, 1, 4}, {  1, 16,  1, 16}},
        {"wrw", "nhwc", miopenHalf,  0, 1,  64,  64,  64, 32, 32,  8, 1, 1, 1, 1, 0, 0, 1, 0, 0, { 1, 4, 1, 4}, {  1, 16,  1, 16}, { 1, 4, 1, 4}, {  1, 16,  1, 16}},
        {"wrw", "nhwc", miopenHalf,  0, 0,  64,  64,  64, 32, 32,  8, 1, 1, 1, 1, 0, 0, 0, 0, 0, { 1, 4, 1, 4}, {  1, 16,  1, 16}, { 1, 4, 1, 4}, {  1, 16,  1, 16}},
        {"wrw", "nhwc", miopenHalf,  0, 0,  64,  64,  64, 32, 32,  8, 1, 1, 1, 1, 0, 0, 1, 0, 0, { 1, 4, 1, 4}, {  1, 16,  1, 16}, { 1, 4, 1, 4}, {  1, 16,  1, 16}},
        {"wrw", "nhwc", miopenHalf,  0, 1,  64,  32,  32, 16, 16, 16, 1, 1, 2, 1, 0, 0, 0, 0, 0, { 1, 4, 1, 2}, {  1,  8,  1, 32}, { 1, 4, 1, 1}, {  1,  8,  1, 32}},
        {"wrw", "nhwc", miopenHalf,  0, 1,  64,  32,  32, 16, 16, 16, 1, 1, 2, 1, 0, 0, 1, 0, 0, { 1, 4, 1, 2}, {  1,  8,  1, 32}, { 1, 4, 1, 1}, {  1,  8,  1, 32}},
        {"wrw", "nhwc", miopenHalf,  0, 0,  64,  32,  32, 16, 16, 16, 1, 1, 2, 1, 0, 0, 0, 0, 0, { 1, 4, 1, 2}, {  1,  8,  1, 32}, { 1, 4, 1, 1}, {  1,  8,  1, 32}},
        {"wrw", "nhwc", miopenHalf,  0, 0,  64,  32,  32, 16, 16, 16, 1, 1, 2, 1, 0, 0, 1, 0, 0, { 1, 4, 1, 2}, {  1,  8,  1, 32}, { 1, 4, 1, 1}, {  1,  8,  1, 32}},
    };
    // clang-format on
    return kernel_param_list;
}

static std::tuple<std::string, // kernel_name
                  size_t,      // block_size
                  size_t,      // grid_size
                  size_t>      // occupancy
    GetImplicitGemmGtcDynamicWrwXdlopsNHWCKernel(
        const ConvolutionContext& ctx,
        const PerformanceConfigAsmImplicitGemmGTCWrwXdlopsNHWC& config)
{
    // const auto& n     = ctx.batch_sz;
    const auto& k = ctx.n_inputs;
    const auto& c = ctx.n_outputs;
    // const auto& ho    = ctx.in_height;
    // const auto& wo    = ctx.in_width;
    const auto& y     = ctx.kernel_size_h;
    const auto& x     = ctx.kernel_size_w;
    const auto& group = ctx.group_counts;

    // c need to be carefully padded
    const auto c_vec_min = config.tensor_b_thread_lengths[3];
    const auto c_padded  = ((c / group) + c_vec_min - 1) / c_vec_min * c_vec_min;
    const auto gemm_n = (c_padded * y * x + config.gemm_n_per_block - 1) / config.gemm_n_per_block *
                        config.gemm_n_per_block;

    const auto gemm_m = k / group;
    size_t block_size = config.BlockSize();
    size_t grid_size  = group * integer_divide_ceil(gemm_m, config.gemm_m_per_block) *
                       integer_divide_ceil(gemm_n, config.gemm_n_per_block);
    std::string kernel_name = config.ToKernelName();
    size_t occupancy        = config.ComputeKernelOccupancy();
    return std::make_tuple(kernel_name, block_size, grid_size, occupancy);
}

size_t PerformanceConfigAsmImplicitGemmGTCWrwXdlopsNHWC::ComputeKernelOccupancy() const
{
    size_t acc_usage = gemm_m_per_block * gemm_n_per_block / BlockSize();
    size_t vgpr_usage;
    size_t aux_vgpr_usage;
    size_t a_elements_per_vgpr = 1;
    size_t b_elements_per_vgpr = 1;
    size_t lds_a               = gemm_m_per_block * gemm_k_per_block * GetTypeSize(precision);
    size_t lds_b               = gemm_n_per_block * gemm_k_per_block * GetTypeSize(precision);

    size_t lds_single = lds_a >= lds_b ? lds_a * 2 : lds_b * 2;
    size_t lds_usage;
    size_t occupancy;

    if(nxe == 0)
    {
        aux_vgpr_usage = 36;
    }
    else
    {
        aux_vgpr_usage = 42;
    }

    if(GetTypeSize(precision) == 2 && tensor_a_thread_lengths[3] > 1)
    {
        a_elements_per_vgpr = 2;
    }
    if(GetTypeSize(precision) == 2 && tensor_b_thread_lengths[3] > 1)
    {
        b_elements_per_vgpr = 2;
    }

    vgpr_usage =
        tensor_a_thread_lengths[1] * tensor_a_thread_lengths[3] / a_elements_per_vgpr +
        tensor_b_thread_lengths[1] * tensor_b_thread_lengths[3] / a_elements_per_vgpr +
        tensor_a_thread_lengths[1] * tensor_a_thread_lengths[3] / (4 / GetTypeSize(precision)) +
        tensor_b_thread_lengths[1] * tensor_b_thread_lengths[3] / (4 / GetTypeSize(precision)) +
        aux_vgpr_usage;
    if(GetTypeSize(precision) == 2)
    {
        if(lds_single >= 32 * 1024 || (lds_single <= 16 * 1024 && lds_single > 8 * 1024 &&
                                       acc_usage < 128 && vgpr_usage <= 84))
        {
            lds_usage = lds_single;
        }
        else
        {
            // use lds double buffer
            lds_usage = lds_single * 2;
        }
    }
    else
    {
        lds_usage = lds_single;
    }

    // std::cout << "lds_usage=" << lds_usage << ", acc_usage=" << acc_usage << ", vgpr_usage=" <<
    // vgpr_usage << std::endl;

    occupancy = std::min(64 * 1024 / lds_usage, std::min(256 / acc_usage, 256 / vgpr_usage));
    return occupancy;
}

void PerformanceConfigAsmImplicitGemmGTCWrwXdlopsNHWC::HeuristicInit(const ConvolutionContext& ctx)
{
    static const std::vector<std::tuple<int, int, int>> tile_list_fp32 = {
        std::make_tuple(128, 128, 16),
        std::make_tuple(128, 64, 16),
        std::make_tuple(64, 128, 16),
        std::make_tuple(128, 32, 16),
        std::make_tuple(256, 64, 16),
        std::make_tuple(64, 256, 16),
        std::make_tuple(64, 64, 16),
        std::make_tuple(64, 32, 16),
        std::make_tuple(32, 64, 16),
        std::make_tuple(32, 32, 32),
    };

    static const std::vector<std::tuple<int, int, int>> tile_list_fp16 = {

        std::make_tuple(256, 128, 16), std::make_tuple(256, 128, 32), std::make_tuple(128, 256, 16),
        std::make_tuple(128, 256, 32), std::make_tuple(128, 128, 16), std::make_tuple(128, 128, 32),
        std::make_tuple(256, 64, 16),  std::make_tuple(256, 64, 32),  std::make_tuple(64, 256, 16),
        std::make_tuple(64, 256, 32),  std::make_tuple(128, 64, 32),  std::make_tuple(64, 128, 16),
        std::make_tuple(64, 128, 32),  std::make_tuple(64, 64, 64),   std::make_tuple(64, 64, 32),
        std::make_tuple(256, 32, 32),  std::make_tuple(32, 256, 32),  std::make_tuple(64, 32, 32),
        std::make_tuple(64, 64, 16),
    };

#ifdef DEBUG_IGEMM_ASM_WRW_NHWC_CHECK_VALID_TILE_LIST
    const auto& c_list = GetWrwXdlopsNHWCConfigList();
    for(const auto& tile : tile_list_fp16)
    {
        int mp, np, kp;
        std::tie(mp, np, kp) = tile;
        bool found = false;
        for(const auto& config : c_list)
        {
            if(config.precision == miopenFloat)
                continue;
            if(config.gemm_m_per_block == mp && config.gemm_n_per_block == np &&
               config.gemm_k_per_block == kp &&)
            {
                found = true;
                break;
            }
        }
        if(!found)
        {
            MIOPEN_LOG_E("fp16 list can't find " << mp << "x" << np << "x" << kp);
            MIOPEN_THROW(miopenStatusInternalError);
        }
    }
    for(const auto& tile : tile_list_fp32)
    {
        int mp, np, kp;
        std::tie(mp, np, kp) = tile;
        bool found = false;
        for(const auto& config : c_list)
        {
            if(config.precision == miopenHalf)
                continue;
            if(config.gemm_m_per_block == mp && config.gemm_n_per_block == np &&
               config.gemm_k_per_block == kp)
            {
                found = true;
                break;
            }
        }
        if(!found)
        {
            MIOPEN_LOG_E("fp32 list can't find " << mp << "x" << np << "x" << kp);
            MIOPEN_THROW(miopenStatusInternalError);
        }
    }
#endif

    // const auto& n = ctx.batch_sz;
    const auto& k = ctx.n_inputs;
    const auto& c = ctx.n_outputs;
    // const auto& ho        = ctx.in_height;
    // const auto& wo        = ctx.in_width;
    const auto& y         = ctx.kernel_size_h;
    const auto& x         = ctx.kernel_size_w;
    const auto stride_h   = ConvolutionContextInterpreter::GetAdjustedConvolutionStrideH(ctx);
    const auto stride_w   = ConvolutionContextInterpreter::GetAdjustedConvolutionStrideW(ctx);
    const auto dilation_h = ctx.kernel_size_h > 1 ? ctx.kernel_dilation_h : 1;
    const auto dilation_w = ctx.kernel_size_w > 1 ? ctx.kernel_dilation_w : 1;
    const auto& pad_h     = ctx.pad_h;
    const auto& pad_w     = ctx.pad_w;
    // const auto& precision = ctx.IsFp16() ? miopenHalf : miopenFloat;
    const auto& group = ctx.group_counts;

    auto gemm_n        = (c / group) * y * x;
    const auto& gemm_m = k / group;

    bool unit_conv = (x == 1) && (y == 1) && (stride_h == 1) && (stride_w == 1) &&
                     (dilation_h == 1) && (dilation_w == 1) && (pad_h == 0) && (pad_w == 0);
    bool not_support_vector_store = ctx.IsFp16() && ((c / group) % 2 != 0);
    int m_per_block, n_per_block, k_per_block;

    std::tie(m_per_block, n_per_block, k_per_block) = HeuristicInitMacroTileNoPadGemmK(
        gemm_m, gemm_n, 0, ctx.IsFp32() ? tile_list_fp32 : tile_list_fp16);

    if((m_per_block == 0 && n_per_block == 0 && k_per_block == 0) || not_support_vector_store)
    {
        // not found, let's try  gemm_k pad now.
        const auto& config_list = GetWrwXdlopsNHWCConfigList();
        size_t min_pad_pixel    = std::numeric_limits<std::size_t>::max();
        size_t selected_index   = 0;
        for(size_t i = 0; i < config_list.size(); i++)
        {
            const auto& config = config_list[i];
            if(!((ctx.IsFp16() && config.precision == miopenHalf) ||
                 (ctx.IsFp32() && config.precision == miopenFloat)))
                continue;

            if(ctx.IsFp16())
            {
                if((c / group) % config.tensor_b_thread_lengths[3] != 0)
                {
                    continue;
                }
                if((k / group) % config.tensor_a_thread_lengths[3] != 0)
                {
                    continue;
                }
            }

            if(ctx.IsFp32())
            {
                // c need to be carefully padded
                const auto c_vec_min = config.tensor_b_thread_lengths[3];
                const auto c_padded  = ((c / group) + c_vec_min - 1) / c_vec_min * c_vec_min;
                gemm_n               = (c_padded * y * x + config.gemm_n_per_block - 1) /
                         config.gemm_n_per_block * config.gemm_n_per_block;
            }

            size_t cur_pad_pixel =
                ComputeMatrixPadSize(gemm_m, config.gemm_m_per_block, 0, config.gemm_k_per_block) +
                ComputeMatrixPadSize(gemm_n, config.gemm_n_per_block, 0, config.gemm_k_per_block) +
                ComputeMatrixPadSize(
                    gemm_m, config.gemm_m_per_block, gemm_n, config.gemm_n_per_block);
            if(cur_pad_pixel < min_pad_pixel)
            {
                min_pad_pixel  = cur_pad_pixel;
                selected_index = i;
            }
        }

        size_t current_grid_size;
        size_t occupancy;
        std::tie(std::ignore, std::ignore, current_grid_size, occupancy) =
            GetImplicitGemmGtcDynamicWrwXdlopsNHWCKernel(ctx, config_list[selected_index]);
        bool need_k_split = current_grid_size > 600 ? false : true;
        size_t gks        = ComputeGemmKGlobalSplitsWith2DMerge(current_grid_size, occupancy);
        need_k_split |= gks != 0;

        CopyParameters(config_list[selected_index]);
        if(need_k_split)
            gemm_k_global_split = occupancy;
    }
    else
    {
        // found a suitable m/n/k, now let's prepare other parmater and initialize one
        const auto& config_list = GetWrwXdlopsNHWCConfigList();
        for(const auto& config : config_list)
        {
            if(!((ctx.IsFp16() && config.precision == miopenHalf) ||
                 (ctx.IsFp32() && config.precision == miopenFloat)))
                continue;

            if(m_per_block == config.gemm_m_per_block && n_per_block == config.gemm_n_per_block &&
               k_per_block == config.gemm_k_per_block)
            {
                size_t current_grid_size;
                size_t occupancy;
                std::tie(std::ignore, std::ignore, current_grid_size, occupancy) =
                    GetImplicitGemmGtcDynamicWrwXdlopsNHWCKernel(ctx, config);
                bool need_k_split = current_grid_size > 600 ? false : true;
                size_t gks = ComputeGemmKGlobalSplitsWith2DMerge(current_grid_size, occupancy);
                need_k_split |= gks != 0;

                // std::cout << "need_k_split:" << need_k_split << std::endl;
                // std::cout << "gks:" << gks << std::endl;

                if((unit_conv && config.nxe == 0) || (!unit_conv && config.nxe != 0))
                {
                    CopyParameters(config);
                    if(need_k_split)
                        gemm_k_global_split = occupancy;
                    return;
                }
                else
                    continue;
            }
        }
        MIOPEN_LOG_E("can't find a suitable heuristic config");
        MIOPEN_THROW(miopenStatusInternalError);
    }
}

bool PerformanceConfigAsmImplicitGemmGTCWrwXdlopsNHWC::SetNextValue()
{
    if(use_spare_set)
    {
        const auto& config_list = GetWrwXdlopsNHWCConfigList();
        if(IsDefaultConstructed())
        {
            CopyParameters(config_list[index]);
        }
        else
        {
            // std::cout << __FUNCTION__ << std::endl;
            // std::cout << "gemm_k_global_split:" << gemm_k_global_split << std::endl;
            if(gemm_k_global_split != 0)
            {
                if(NextLinear<1, WRW_MAX_GEMM_K_SPLITS>(gemm_k_global_split))
                    index++;
                else
                    return true;
            }
            else
            {
                index++;
            }
            if(index >= config_list.size())
                return false;
            CopyParameters(config_list[index]);
        }
        return true;
    }
    else
    {
        // always break generic search of main set (no spare), make sure we can use spare set
        return false;
    }
}
bool PerformanceConfigAsmImplicitGemmGTCWrwXdlopsNHWC::IsValidValue() const
{
    if(IsDefaultConstructed())
        return true;
    const auto& config_list = GetWrwXdlopsNHWCConfigList();
    if(index >= config_list.size())
        return false;
    return *this == config_list[index];
}
bool PerformanceConfigAsmImplicitGemmGTCWrwXdlopsNHWC::IsValid(const ConvolutionContext& ctx) const
{
    if(IsDefaultConstructed())
        return false;

    if(!((ctx.IsFp16() && precision == miopenHalf) || (ctx.IsFp32() && precision == miopenFloat)))
        return false;

    // const auto& n = ctx.batch_sz;
    const auto& k = ctx.n_inputs;
    const auto& c = ctx.n_outputs;
    // const auto& ho        = ctx.in_height;
    // const auto& wo        = ctx.in_width;
    const auto& y         = ctx.kernel_size_h;
    const auto& x         = ctx.kernel_size_w;
    const auto stride_h   = ConvolutionContextInterpreter::GetAdjustedConvolutionStrideH(ctx);
    const auto stride_w   = ConvolutionContextInterpreter::GetAdjustedConvolutionStrideW(ctx);
    const auto dilation_h = ctx.kernel_size_h > 1 ? ctx.kernel_dilation_h : 1;
    const auto dilation_w = ctx.kernel_size_w > 1 ? ctx.kernel_dilation_w : 1;
    const auto& pad_h     = ctx.pad_h;
    const auto& pad_w     = ctx.pad_w;
    const auto& precision = ctx.IsFp16() ? miopenHalf : miopenFloat;
    // const auto& group     = ctx.group_counts;

    bool unit_conv = (x == 1) && (y == 1) && (stride_h == 1) && (stride_w == 1) &&
                     (dilation_h == 1) && (dilation_w == 1) && (pad_h == 0) && (pad_w == 0);

    if((nxe == 0) && !unit_conv)
    {
        return false;
    }

    // std::cout << __FUNCTION__ << std::endl;
    // std::cout << "gemm_k_global_split:" << gemm_k_global_split << std::endl;

    if(precision == miopenHalf)
    {
        if(c % tensor_b_thread_lengths[3] != 0)
        {
            return false;
        }
        if(k % tensor_a_thread_lengths[3] != 0)
        {
            return false;
        }
    }

    // add more restriction for spare
    if(use_spare_set)
    {
        // non 1x1 kernel(except padding gemm_k) can't run 1x1 case
        if(unit_conv && nxe != 0)
            return false;
    }

    return true;
}

PerformanceConfigAsmImplicitGemmGTCWrwXdlopsNHWC
ConvAsmImplicitGemmGTCDynamicWrwXdlopsNHWC::GetPerformanceConfig(
    const ConvolutionContext& params) const
{
    PerformanceConfigAsmImplicitGemmGTCWrwXdlopsNHWC pp;
    pp.HeuristicInit(params);
    MIOPEN_LOG_I(pp.ToString());
    return pp;
}
bool ConvAsmImplicitGemmGTCDynamicWrwXdlopsNHWC::IsValidPerformanceConfig(
    const ConvolutionContext& problem,
    const PerformanceConfigAsmImplicitGemmGTCWrwXdlopsNHWC& config) const
{
    return config.IsValidValue() && config.IsValid(problem);
}
PerformanceConfigAsmImplicitGemmGTCWrwXdlopsNHWC
ConvAsmImplicitGemmGTCDynamicWrwXdlopsNHWC::Search(const ConvolutionContext& ctx,
                                                   const AnyInvokeParams& invoke_ctx) const
{
    return GenericSearch(*this, ctx, invoke_ctx);
}

bool ConvAsmImplicitGemmGTCDynamicWrwXdlopsNHWC::IsApplicable(const ConvolutionContext& ctx) const
{
    if(miopen::IsDisabled(MIOPEN_DEBUG_CONV_IMPLICIT_GEMM_ASM_WRW_GTC_XDLOPS_NHWC{}))
        return false;

    const auto device_name = ctx.GetStream().GetDeviceName();
    if(device_name != "gfx908")
        return false;

    if(!ctx.use_asm_kernels)
        return false;

    if(!ctx.direction.IsBackwardWrW())
        return false;

    if(!ctx.Is2d())
        return false;

    if(!ctx.IsFp32() && !ctx.IsFp16())
        return false;

    if(!ctx.rmv.IsV3())
        return false;

    if(!ctx.IsLayoutNHWC())
        return false;
    return true;
}

inline std::vector<OpKernelArg>
ComputeDynamicIGemmWrwKernelArgsNHWC(const conv::ProblemDescription& conv_problem,
                                     const int gemm_k_global_splits,
                                     const int gemm_k_per_wg)
{
    int hi         = conv_problem.GetOutHeight();
    int wi         = conv_problem.GetOutWidth();
    int n          = conv_problem.GetInBatchSize();
    int k          = conv_problem.GetInChannels();
    int c          = conv_problem.GetOutChannels();
    int ho         = conv_problem.GetInHeight();
    int wo         = conv_problem.GetInWidth();
    int stride_h   = conv_problem.GetInHeight() > 1 ? conv_problem.GetKernelStrideH() : 1;
    int stride_w   = conv_problem.GetInWidth() > 1 ? conv_problem.GetKernelStrideW() : 1;
    int dilation_h = conv_problem.GetWeightsHeight() > 1 ? conv_problem.GetDilationH() : 1;
    int dilation_w = conv_problem.GetWeightsWidth() > 1 ? conv_problem.GetDilationW() : 1;
    int pad_h      = conv_problem.GetPadH();
    int pad_w      = conv_problem.GetPadW();
    int y          = conv_problem.GetWeightsHeight();
    int x          = conv_problem.GetWeightsWidth();
    int group      = conv_problem.GetGroupCount();

    std::vector<OpKernelArg> opArgs;
    opArgs.emplace_back(hi);
    opArgs.emplace_back(wi);
    opArgs.emplace_back(n);
    opArgs.emplace_back(k);
    opArgs.emplace_back(c);
    opArgs.emplace_back(ho);
    opArgs.emplace_back(wo);
    opArgs.emplace_back(stride_h);
    opArgs.emplace_back(stride_w);
    opArgs.emplace_back(dilation_h);
    opArgs.emplace_back(dilation_w);
    opArgs.emplace_back(pad_h);
    opArgs.emplace_back(pad_w);
    opArgs.emplace_back(y);
    opArgs.emplace_back(x);
    opArgs.emplace_back(gemm_k_global_splits);
    opArgs.emplace_back(group);
    opArgs.emplace_back(gemm_k_per_wg);

    return opArgs;
}

size_t
ConvAsmImplicitGemmGTCDynamicWrwXdlopsNHWC::GetWorkspaceSize(const ConvolutionContext& ctx) const
{
    if(ctx.IsFp32())
        return 0;
    else
    {
        const auto k       = ctx.n_inputs;
        const auto c       = ctx.n_outputs;
        const auto y       = ctx.kernel_size_h;
        const auto x       = ctx.kernel_size_w;
        const auto ngroups = ctx.group_counts;

        return static_cast<size_t>(ngroups) * (k / ngroups) * (c / ngroups) * y * x *
               miopen::GetTypeSize(miopenFloat);
    }
}

ConvSolution ConvAsmImplicitGemmGTCDynamicWrwXdlopsNHWC::GetSolution(
    const ConvolutionContext& ctx,
    const PerformanceConfigAsmImplicitGemmGTCWrwXdlopsNHWC& config,
    bool disableConfigOverrideFromEnv) const
{
    ConvSolution result;
    KernelInfo kernel;
    std::ostringstream options;
    (void)disableConfigOverrideFromEnv;

    std::string kernel_name;
    size_t block_size;
    size_t grid_size;

    std::tie(kernel_name, block_size, grid_size, std::ignore) =
        GetImplicitGemmGtcDynamicWrwXdlopsNHWCKernel(ctx, config);

    int gemm_k_global_splits =
        config.gemm_k_global_split >= 1
            ? ComputeGemmKGlobalSplitsWith2DMerge(grid_size, config.gemm_k_global_split)
            : 1;
    int min_n_per_block = config.nxe == 1 ? config.tensor_a_thread_lengths[1] : 1;
    int nb_per_block =
        config.nxe == 1 ? config.tensor_a_cluster_lengths[1] : config.gemm_k_per_block;

    if(gemm_k_global_splits == 0)
        gemm_k_global_splits = 1;

    // compute workload for 1 workgroup and update gemmk splits (remove the ones compute 0 data)
    int gemmk = integer_divide_ceil(ctx.batch_sz, min_n_per_block) * ctx.in_height * ctx.in_width;
    int gemmk_per_wg = integer_divide_ceil(gemmk, gemm_k_global_splits);

    gemmk_per_wg         = (gemmk_per_wg + nb_per_block - 1) / nb_per_block * nb_per_block;
    gemm_k_global_splits = integer_divide_ceil(gemmk, gemmk_per_wg);

    const auto required_workspace_size = GetWorkspaceSize(ctx);
    result.workspce_sz                 = required_workspace_size;

    kernel.kernel_file = kernel_name + ".s";
    kernel.kernel_name = kernel_name;
    kernel.g_wk.clear();
    kernel.g_wk.push_back(grid_size * block_size);
    kernel.g_wk.push_back(1);
    kernel.g_wk.push_back(gemm_k_global_splits);
    kernel.l_wk.clear();
    kernel.l_wk.push_back(block_size);
    kernel.l_wk.push_back(1);
    kernel.l_wk.push_back(1);

    GenerateClangDefsym(options, "ROCM_METADATA_VERSION", ctx.rmv.UseV3() ? 5 : 4);

    kernel.comp_options = options.str();

    MIOPEN_LOG_I2("ConvAsmImplicitGemmGTCDynamicWrwXdlopsNHWC: " + config.ToString());

    result.construction_params.push_back(kernel);

    const auto& conv_problem = ctx.conv_problem;
    const auto& lowp_quant   = ctx.conv_problem.GetConv().lowp_quant;

    auto opShapeArgs =
        ComputeDynamicIGemmWrwKernelArgsNHWC(conv_problem, gemm_k_global_splits, gemmk_per_wg);

    if(conv_problem.IsFp16() && gemm_k_global_splits >= 1 && config.tensor_b_thread_lengths[3] == 1)
    {
        TensorDescriptor workspaceDesc(miopenFloat,
                                       conv_problem.GetWeights().GetLengths(),
                                       conv_problem.GetWeights().GetStrides());
        result.invoker_factory = [=](const std::vector<Kernel>& kernels) {
            return [=](const Handle& handle, const AnyInvokeParams& primitive_parameters) {
                decltype(auto) wrw_invoke_params =
                    primitive_parameters.CastTo<conv::WrWInvokeParams>();
                const auto& tensors       = wrw_invoke_params.tensors;
                const auto k              = handle.Run(kernels[0]);
                const auto& workSpace     = wrw_invoke_params.workSpace;
                const auto& workSpaceSize = wrw_invoke_params.workSpaceSize;
                float elapsed             = 0;
                float zero                = 0.f;

                if(workSpace == nullptr || workSpaceSize < required_workspace_size)
                    MIOPEN_THROW("Not enough workspace has been provided for "
                                 "ConvAsmImplicitGemmGTCDynamicWrwXdlops with fp16 and atomic "
                                 "add.");

                SetTensor(handle, workspaceDesc, workSpace, &zero);
                if(handle.IsProfilingEnabled())
                    elapsed += handle.GetKernelTime();

                std::vector<OpKernelArg> opArgs;
                opArgs.reserve(3 + opShapeArgs.size()); // Avoids vector resize.
                opArgs.emplace_back(tensors.x);
                opArgs.emplace_back(workSpace);
                opArgs.emplace_back(tensors.dy);

                std::transform(opShapeArgs.begin(),
                               opShapeArgs.end(),
                               std::back_inserter(opArgs),
                               [](const OpKernelArg& arg) { return arg; });

                k(opArgs);
                if(handle.IsProfilingEnabled())
                    elapsed += handle.GetKernelTime();

                CastTensor(handle,
                           &lowp_quant,
                           workspaceDesc,
                           workSpace,
                           tensors.dwDesc,
                           tensors.dw,
                           0,
                           0);

                if(handle.IsProfilingEnabled())
                    elapsed += handle.GetKernelTime();

                if(handle.IsProfilingEnabled())
                {
                    handle.ResetKernelTime();
                    handle.AccumKernelTime(elapsed);
                }
            };
        };
    }
    else
    {
        result.invoker_factory = [=](const std::vector<Kernel>& kernels) {
            return [=](const Handle& handle, const AnyInvokeParams& primitive_parameters) {
                decltype(auto) wrw_invoke_params =
                    primitive_parameters.CastTo<conv::WrWInvokeParams>();
                const auto& tensors = wrw_invoke_params.tensors;
                const auto k        = handle.Run(kernels[0]);
                float elapsed       = 0;
                float zero          = 0.f;

                std::vector<OpKernelArg> opArgs;
                opArgs.reserve(3 + opShapeArgs.size()); // Avoids vector resize.
                opArgs.emplace_back(tensors.x);
                opArgs.emplace_back(tensors.dw);
                opArgs.emplace_back(tensors.dy);

                std::transform(opShapeArgs.begin(),
                               opShapeArgs.end(),
                               std::back_inserter(opArgs),
                               [](const OpKernelArg& arg) { return arg; });

                SetTensor(handle, tensors.dwDesc, tensors.dw, &zero);
                if(handle.IsProfilingEnabled())
                    elapsed += handle.GetKernelTime();

                k(opArgs);
                if(handle.IsProfilingEnabled())
                    elapsed += handle.GetKernelTime();

                if(handle.IsProfilingEnabled())
                {
                    handle.ResetKernelTime();
                    handle.AccumKernelTime(elapsed);
                }
            };
        };
    }

    return result;
}

} // namespace solver
} // namespace miopen
