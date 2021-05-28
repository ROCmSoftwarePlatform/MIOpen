/*******************************************************************************
 *
 * MIT License
 *
 * Copyright (c) 2020 Advanced Micro Devices, Inc.
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
#ifndef CK_ASM_IMPLICITGEMM_HPP_
#define CK_ASM_IMPLICITGEMM_HPP_
#include <string>
#include <ostream>
#include <tuple>
#include <vector>
#include <limits>

namespace miopen {

namespace solver {

struct TunableImplicitGemmGTCDynamic_t
{
    std::string direction      = " ";
    miopenDataType_t precision = miopenFloat;
    int nxb                    = 0;
    int nxe                    = 0;

    int gemm_m_per_block = 0;
    int gemm_n_per_block = 0;
    int gemm_k_per_block = 0;

    int wave_tile_m   = 0;
    int wave_tile_n   = 0;
    int wave_tile_k   = 0;
    int wave_step_m   = 0;
    int wave_step_n   = 0;
    int wave_repeat_m = 0;
    int wave_repeat_n = 0;

    int tensor_a_thread_lengths[4]  = {0, 0, 0, 0};
    int tensor_a_cluster_lengths[4] = {0, 0, 0, 0};
    int tensor_b_thread_lengths[4]  = {0, 0, 0, 0};
    int tensor_b_cluster_lengths[4] = {0, 0, 0, 0};
    int gemm_k_global_split         = 0;

    int GetBlockSize() const
    {
        const auto WaveSize  = 64;
        const auto divisor_m = wave_tile_m * wave_step_m * wave_repeat_m;
        const auto divisor_n = wave_tile_n * wave_step_n * wave_repeat_n;
        assert(divisor_m != 0 && divisor_n != 0);
        return (gemm_m_per_block / divisor_m) * (gemm_n_per_block / divisor_n) * WaveSize;
    }

    std::string GetKernelName() const
    {
        std::ostringstream kernel_name;
        std::string kernel_precision = precision == miopenFloat ? "fp32" : "fp16";
        kernel_name << "igemm_" << direction << "_gtcx_nchw_" << kernel_precision << "_bx" << nxb
                    << "_ex" << nxe << "_bt" << gemm_m_per_block << "x" << gemm_n_per_block << "x"
                    << gemm_k_per_block << "_wt" << wave_tile_m << "x" << wave_tile_n << "x"
                    << wave_tile_k << "_ws" << wave_step_m << "x" << wave_step_n << "_wr"
                    << wave_repeat_m << "x" << wave_repeat_n << "_ta" << tensor_a_thread_lengths[0]
                    << "x" << tensor_a_thread_lengths[1] << "x" << tensor_a_thread_lengths[2] << "x"
                    << tensor_a_thread_lengths[3] << "_" << tensor_a_cluster_lengths[0] << "x"
                    << tensor_a_cluster_lengths[1] << "x" << tensor_a_cluster_lengths[2] << "x"
                    << tensor_a_cluster_lengths[3] << "_tb" << tensor_b_thread_lengths[0] << "x"
                    << tensor_b_thread_lengths[1] << "x" << tensor_b_thread_lengths[2] << "x"
                    << tensor_b_thread_lengths[3] << "_" << tensor_b_cluster_lengths[0] << "x"
                    << tensor_b_cluster_lengths[1] << "x" << tensor_b_cluster_lengths[2] << "x"
                    << tensor_b_cluster_lengths[3];
        if(this->gemm_k_global_split != 0)
            kernel_name << "_gkgs";

        return kernel_name.str();
    }
};

static inline size_t
ComputeMatrixPadSize(size_t col, size_t col_per_block, size_t row, size_t row_per_block)
{
    size_t col_padded = ((col + col_per_block - 1) / col_per_block) * col_per_block;
    size_t row_padded = ((row + row_per_block - 1) / row_per_block) * row_per_block;
    size_t col_extra  = col_padded - col;
    size_t row_extra  = row_padded - row;

    return col_extra * row + row_extra * col + col_extra * row_extra;
}

static inline std::tuple<int, int, int> // m_per_block, n_per_block, k_per_block
    HeuristicInitMacroTileNoPadGemmK(size_t gemm_m,
                                     size_t gemm_n,
                                     size_t gemm_k,
                                     const std::vector<std::tuple<int, int, int>>& tile_list)
{
    int m_per_block, n_per_block, k_per_block;
    bool found = false;

    // find exact divide
    for(const auto& tile : tile_list)
    {
        int m, n, k;
        std::tie(m, n, k) = tile;
        if(gemm_m % m == 0 && gemm_n % n == 0 && gemm_k % k == 0)
        {
            m_per_block = m;
            n_per_block = n;
            k_per_block = k;
            found       = true;
            break;
        }
    }

    if(!found)
    {
        size_t min_pad_pixel = std::numeric_limits<std::size_t>::max();
        int gemm_m_pad       = 0;
        int gemm_n_pad       = 0;
        // first try gemm_m, gemm_n padding
        for(const auto& tile : tile_list)
        {
            int m, n, k;
            std::tie(m, n, k) = tile;
            if(gemm_k % k != 0)
                continue;
            size_t cur_pad_pixel = ComputeMatrixPadSize(gemm_m, m, gemm_k, k) +
                                   ComputeMatrixPadSize(gemm_n, n, gemm_k, k) +
                                   ComputeMatrixPadSize(gemm_m, m, gemm_n, n);
            if(min_pad_pixel < cur_pad_pixel)
            {
                cur_pad_pixel = min_pad_pixel;
                gemm_m_pad    = m;
                gemm_n_pad    = n;
            }
        }

        // second, we need find the max k_per_block among the same m/n per block
        for(const auto& tile : tile_list)
        {
            int m, n, k;
            std::tie(m, n, k) = tile;
            if(m == gemm_m_pad && n == gemm_n_pad)
            {
                if(gemm_k % k == 0)
                {
                    m_per_block = m;
                    n_per_block = n;
                    k_per_block = k;
                    found       = true;
                    break;
                }
            }
        }
    }

    if(found)
        return std::make_tuple(m_per_block, n_per_block, k_per_block);
    else
        return std::make_tuple(0, 0, 0);
}

} // namespace solver
} // namespace miopen
#endif
