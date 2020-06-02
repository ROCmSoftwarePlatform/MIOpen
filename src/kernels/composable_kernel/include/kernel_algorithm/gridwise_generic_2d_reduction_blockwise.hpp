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
#ifndef _CK_GRIDWISE_GENERIC_2D_REDUCTION_BLOCKWISE_HPP_
#define _CK_GRIDWISE_GENERIC_2D_REDUCTION_BLOCKWISE_HPP_

#include "float_type.hpp"
#include "reduction_operator.hpp"
#include "reduction_functions.hpp"
#include "reduction_common.hpp"

#include "blockwise_generic_tensor_slice_copy.hpp"
#include "ConstantMatrixDescriptor.hpp"

namespace ck {

template <index_t BlockSize,
          typename srcDataType,
          typename dstDataType,
          typename src2dDesc,
          typename dst1dDesc,
          typename compType,
          ckReduceTensorOp_t op,
          ckNanPropagation_t nanPropaOpt,
          ckReduceTensorIndices_t reduceIndicesOpt,
          int callId,
          index_t GredAccessesPerThreadInBlock>
struct Gridwise_generic_reduction_xy_to_x_blockwise
{
    static constexpr bool indexable = reduce_binary_operator<compType, op>::indexable;
    static constexpr bool need_indices =
        indexable && (reduceIndicesOpt != CK_REDUCE_TENSOR_NO_INDICES);
    static constexpr bool firstCall   = (callId == 0) ? true : false;
    static constexpr compType zeroVal = reduce_binary_operator<compType, op>::zeroVal;

    static constexpr int BlockBufferSize = BlockSize * GredAccessesPerThreadInBlock;

    using opReduce = typename reduce_binary_operator<compType, op>::opType;

    __device__ void Run(srcDataType alpha,
                        const srcDataType* const __restrict__ p_src_global,
                        srcDataType beta,
                        dstDataType* const __restrict__ p_dst_global,
                        int* const __restrict__ ws_indices_global,
                        int* const __restrict__ indices_global)
    {
        static_if<need_indices>{}([&](auto) {
            static_if<firstCall>{}(
                [&](auto) { RunImpl2(alpha, p_src_global, beta, p_dst_global, indices_global); })
                .Else([&](auto) {
                    RunImpl3(
                        alpha, p_src_global, beta, p_dst_global, ws_indices_global, indices_global);
                });
        })
            .Else([&](auto) { RunImpl1(alpha, p_src_global, beta, p_dst_global); });
    };

    __device__ static void RunImpl1(srcDataType alpha,
                                    const srcDataType* const __restrict__ p_src_global,
                                    srcDataType beta,
                                    dstDataType* const __restrict__ p_dst_global)
    {
        // LDS
        __shared__ compType p_in_block_buffer[BlockBufferSize];

        // VGPR, only useful for thread 0
        compType accuValue = zeroVal;

        const index_t thread_local_id    = get_thread_local_1d_id();
        const index_t block_global_1d_id = get_block_1d_id();

        constexpr auto in_block_desc =
            make_native_tensor_descriptor_packed(Sequence<1, BlockBufferSize>{});

        using ThreadSliceLengths   = Sequence<1, GredAccessesPerThreadInBlock>;
        using ThreadClusterLengths = Sequence<1, BlockSize>;

        auto blockwise_src_load =
            BlockwiseGenericTensorSliceCopy_v4<BlockSize,
                                               src2dDesc,
                                               decltype(in_block_desc),
                                               decltype(in_block_desc.GetLengths()),
                                               ThreadSliceLengths,
                                               ThreadClusterLengths,
                                               Sequence<0, 1>,
                                               Sequence<0, 1>,
                                               Sequence<0, 1>,
                                               1,
                                               1,
                                               1,
                                               1,
                                               AddressSpace::Global,
                                               AddressSpace::Vgpr,
                                               AddressSpace::Lds,
                                               InMemoryDataOperation::Set>({block_global_1d_id, 0},
                                                                           {0, 0});

        constexpr auto block_buff_mtx_desc = make_ConstantMatrixDescriptor_packed(
            Number<GredAccessesPerThreadInBlock>{}, Number<BlockSize>{});

        using blockwise_reduce = BlockwiseReduction_2d_block_buffer<decltype(block_buff_mtx_desc),
                                                                    compType,
                                                                    true,
                                                                    opReduce,
                                                                    nanPropaOpt>;

        const index_t toReduceBlocks = (src2dDesc::GetLengths()[1] + BlockSize - 1) / BlockSize;

        for(index_t reducedBlocks = 0; reducedBlocks < toReduceBlocks;
            reducedBlocks += GredAccessesPerThreadInBlock)
        {
            blockwise_reduce::set_buffer_value(p_in_block_buffer, zeroVal);

            // load block data from global to LDS, no use of double buffers (to be improved)
            // blockwise_src_load.Run(p_src_global, p_in_block_buffer,
            // type_convert<srcDataType>{}(zeroVal));

            compType p_thread_buffer[blockwise_src_load.GetThreadBufferSize()];

            blockwise_src_load.RunLoadThreadBuffer(
                p_src_global, p_thread_buffer, type_convert<srcDataType>{}(zeroVal));
            blockwise_src_load.RunStoreThreadBuffer(p_thread_buffer, p_in_block_buffer, zeroVal);

            __syncthreads();

            index_t BlocksInOneOp = (reducedBlocks < toReduceBlocks - GredAccessesPerThreadInBlock)
                                        ? GredAccessesPerThreadInBlock
                                        : toReduceBlocks - reducedBlocks;
            blockwise_reduce::reduce(p_in_block_buffer, BlocksInOneOp, accuValue);

            constexpr auto True = integral_constant<bool, true>{};
            blockwise_src_load.MoveSrcSliceWindow(Sequence<0, BlockBufferSize>{}, True);
        };

        using ReducedDataLengths       = Sequence<1>;
        constexpr auto ReducedDataDesc = make_native_tensor_descriptor_packed(ReducedDataLengths{});

        // The first thread in the block stores the reduced result to the global location
        // representing the block
        if(thread_local_id == 0)
        {
            if(alpha != type_convert<srcDataType>{}(1.0f))
                accuValue *= type_convert<compType>{}(alpha);

            if(beta != type_convert<srcDataType>{}(0.0f))
            {
                auto threadwise_dst_load =
                    ThreadwiseGenericTensorSliceCopy_v4r2<dst1dDesc,
                                                          decltype(ReducedDataDesc),
                                                          ReducedDataLengths,
                                                          Sequence<0>,
                                                          0,
                                                          1,
                                                          1,
                                                          AddressSpace::Global,
                                                          AddressSpace::Vgpr,
                                                          InMemoryDataOperation::Set>(
                        {block_global_1d_id}, {0});
                dstDataType priorDstValue;

                threadwise_dst_load.Run(
                    p_dst_global, &priorDstValue, type_convert<dstDataType>{}(zeroVal));

                accuValue +=
                    type_convert<compType>{}(priorDstValue * type_convert<dstDataType>{}(beta));
            };

            auto threadwise_dst_store =
                ThreadwiseGenericTensorSliceCopy_v4r2<decltype(ReducedDataDesc),
                                                      dst1dDesc,
                                                      ReducedDataLengths,
                                                      Sequence<0>,
                                                      0,
                                                      1,
                                                      1,
                                                      AddressSpace::Vgpr,
                                                      AddressSpace::Global,
                                                      InMemoryDataOperation::Set>(
                    {0}, {block_global_1d_id});
            threadwise_dst_store.Run(&accuValue, p_dst_global, zeroVal);
        };
    };

    __device__ static void RunImpl2(srcDataType alpha,
                                    const srcDataType* const __restrict__ p_src_global,
                                    srcDataType beta,
                                    dstDataType* const __restrict__ p_dst_global,
                                    int* const __restrict__ indices_global)
    {
        // LDS
        __shared__ compType p_in_block_buffer[BlockBufferSize];
        __shared__ int block_indices_buffer[BlockBufferSize];

        // VGPR, only useful for thread 0
        compType accuValue = zeroVal;
        int accuIndex      = 0;

        const index_t thread_local_id    = get_thread_local_1d_id();
        const index_t block_global_1d_id = get_block_1d_id();

        constexpr auto in_block_desc =
            make_native_tensor_descriptor_packed(Sequence<1, BlockBufferSize>{});

        using ThreadSliceLengths   = Sequence<1, GredAccessesPerThreadInBlock>;
        using ThreadClusterLengths = Sequence<1, BlockSize>;

        auto blockwise_src_load =
            BlockwiseGenericTensorSliceCopy_v4<BlockSize,
                                               src2dDesc,
                                               decltype(in_block_desc),
                                               decltype(in_block_desc.GetLengths()),
                                               ThreadSliceLengths,
                                               ThreadClusterLengths,
                                               Sequence<0, 1>,
                                               Sequence<0, 1>,
                                               Sequence<0, 1>,
                                               1,
                                               1,
                                               1,
                                               1,
                                               AddressSpace::Global,
                                               AddressSpace::Vgpr,
                                               AddressSpace::Lds,
                                               InMemoryDataOperation::Set>({block_global_1d_id, 0},
                                                                           {0, 0});

        constexpr auto block_buff_mtx_desc = make_ConstantMatrixDescriptor_packed(
            Number<GredAccessesPerThreadInBlock>{}, Number<BlockSize>{});

        using blockwise_reduce = BlockwiseReduction_2d_block_buffer<decltype(block_buff_mtx_desc),
                                                                    compType,
                                                                    true,
                                                                    opReduce,
                                                                    nanPropaOpt>;

        const index_t toReduceBlocks = (src2dDesc::GetLengths()[1] + BlockSize - 1) / BlockSize;

        int indexOffset = 0;

        for(index_t reducedBlocks = 0; reducedBlocks < toReduceBlocks;
            reducedBlocks += GredAccessesPerThreadInBlock)
        {
            blockwise_reduce::set_buffer_value(p_in_block_buffer, zeroVal);

            // load block data from global to LDS, no use of double buffers (to be improved)
            blockwise_src_load.Run(
                p_src_global, p_in_block_buffer, type_convert<srcDataType>{}(zeroVal));

            __syncthreads();

            // construct the indices for the current toReduce blocks
            blockwise_reduce::init_buffer_indices(block_indices_buffer, indexOffset);

            index_t BlocksInOneOp = (reducedBlocks < toReduceBlocks - GredAccessesPerThreadInBlock)
                                        ? GredAccessesPerThreadInBlock
                                        : toReduceBlocks - reducedBlocks;

            blockwise_reduce::reduce2(
                p_in_block_buffer, block_indices_buffer, BlocksInOneOp, accuValue, accuIndex);

            indexOffset += BlockBufferSize;

            constexpr auto True = integral_constant<bool, true>{};
            blockwise_src_load.MoveSrcSliceWindow(Sequence<0, BlockBufferSize>{}, True);
        };

        using ReducedDataLengths       = Sequence<1>;
        constexpr auto ReducedDataDesc = make_native_tensor_descriptor_packed(ReducedDataLengths{});

        // The first thread in the block stores the reduced result to the global location
        // representing the block
        if(thread_local_id == 0)
        {
            if(alpha != type_convert<srcDataType>{}(1.0f))
                accuValue *= type_convert<compType>{}(alpha);

            if(beta != type_convert<srcDataType>{}(0.0f))
            {
                auto threadwise_dst_load =
                    ThreadwiseGenericTensorSliceCopy_v4r2<dst1dDesc,
                                                          decltype(ReducedDataDesc),
                                                          ReducedDataLengths,
                                                          Sequence<0>,
                                                          0,
                                                          1,
                                                          1,
                                                          AddressSpace::Global,
                                                          AddressSpace::Vgpr,
                                                          InMemoryDataOperation::Set>(
                        {block_global_1d_id}, {0});
                dstDataType priorDstValue;

                threadwise_dst_load.Run(
                    p_dst_global, &priorDstValue, type_convert<dstDataType>{}(zeroVal));

                accuValue +=
                    type_convert<compType>{}(priorDstValue * type_convert<dstDataType>{}(beta));
            };

            auto threadwise_dst_store =
                ThreadwiseGenericTensorSliceCopy_v4r2<decltype(ReducedDataDesc),
                                                      dst1dDesc,
                                                      ReducedDataLengths,
                                                      Sequence<0>,
                                                      0,
                                                      1,
                                                      1,
                                                      AddressSpace::Vgpr,
                                                      AddressSpace::Global,
                                                      InMemoryDataOperation::Set>(
                    {0}, {block_global_1d_id});
            threadwise_dst_store.Run(&accuValue, p_dst_global, zeroVal);
            threadwise_dst_store.Run(&accuIndex, indices_global, 0);
        };
    };

    __device__ static void RunImpl3(srcDataType alpha,
                                    const srcDataType* const __restrict__ p_src_global,
                                    srcDataType beta,
                                    dstDataType* const __restrict__ p_dst_global,
                                    int* const __restrict__ ws_indices_global,
                                    int* const __restrict__ indices_global)
    {
        // LDS
        __shared__ compType p_in_block_buffer[BlockBufferSize];
        __shared__ int block_indices_buffer[BlockBufferSize];

        // VGPR, only useful for thread 0
        compType accuValue = zeroVal;
        int accuIndex      = 0;

        const index_t thread_local_id    = get_thread_local_1d_id();
        const index_t block_global_1d_id = get_block_1d_id();

        constexpr auto in_block_desc =
            make_native_tensor_descriptor_packed(Sequence<1, BlockBufferSize>{});

        using ThreadSliceLengths   = Sequence<1, GredAccessesPerThreadInBlock>;
        using ThreadClusterLengths = Sequence<1, BlockSize>;

        auto blockwise_src_load =
            BlockwiseGenericTensorSliceCopy_v4<BlockSize,
                                               src2dDesc,
                                               decltype(in_block_desc),
                                               decltype(in_block_desc.GetLengths()),
                                               ThreadSliceLengths,
                                               ThreadClusterLengths,
                                               Sequence<0, 1>,
                                               Sequence<0, 1>,
                                               Sequence<0, 1>,
                                               1,
                                               1,
                                               1,
                                               1,
                                               AddressSpace::Global,
                                               AddressSpace::Vgpr,
                                               AddressSpace::Lds,
                                               InMemoryDataOperation::Set>({block_global_1d_id, 0},
                                                                           {0, 0});

        constexpr auto block_buff_mtx_desc = make_ConstantMatrixDescriptor_packed(
            Number<GredAccessesPerThreadInBlock>{}, Number<BlockSize>{});

        using blockwise_reduce = BlockwiseReduction_2d_block_buffer<decltype(block_buff_mtx_desc),
                                                                    compType,
                                                                    true,
                                                                    opReduce,
                                                                    nanPropaOpt>;

        const index_t toReduceBlocks = (src2dDesc::GetLengths()[1] + BlockSize - 1) / BlockSize;

        for(index_t reducedBlocks = 0; reducedBlocks < toReduceBlocks;
            reducedBlocks += GredAccessesPerThreadInBlock)
        {
            blockwise_reduce::set_buffer_value(p_in_block_buffer, zeroVal);

            // load block data from global to LDS, no use of double buffers (to be improved)
            blockwise_src_load.Run(
                p_src_global, p_in_block_buffer, type_convert<srcDataType>{}(zeroVal));
            blockwise_src_load.Run(ws_indices_global, block_indices_buffer, static_cast<int>(0));

            __syncthreads();

            index_t BlocksInOneOp = (reducedBlocks < toReduceBlocks - GredAccessesPerThreadInBlock)
                                        ? GredAccessesPerThreadInBlock
                                        : toReduceBlocks - reducedBlocks;

            blockwise_reduce::reduce3(
                p_in_block_buffer, block_indices_buffer, BlocksInOneOp, accuValue, accuIndex);

            constexpr auto True = integral_constant<bool, true>{};
            blockwise_src_load.MoveSrcSliceWindow(
                Sequence<0, BlockSize * GredAccessesPerThreadInBlock>{}, True);
        };

        using ReducedDataLengths       = Sequence<1>;
        constexpr auto ReducedDataDesc = make_native_tensor_descriptor_packed(ReducedDataLengths{});

        // The first thread in the block stores the reduced result to the global location
        // representing the block
        if(thread_local_id == 0)
        {
            if(alpha != type_convert<srcDataType>{}(1.0f))
                accuValue *= type_convert<compType>{}(alpha);

            if(beta != type_convert<srcDataType>{}(0.0f))
            {
                auto threadwise_dst_load =
                    ThreadwiseGenericTensorSliceCopy_v4r2<dst1dDesc,
                                                          decltype(ReducedDataDesc),
                                                          ReducedDataLengths,
                                                          Sequence<0>,
                                                          0,
                                                          1,
                                                          1,
                                                          AddressSpace::Global,
                                                          AddressSpace::Vgpr,
                                                          InMemoryDataOperation::Set>(
                        {block_global_1d_id}, {0});
                dstDataType priorDstValue;

                threadwise_dst_load.Run(
                    p_dst_global, &priorDstValue, type_convert<dstDataType>{}(zeroVal));

                accuValue +=
                    type_convert<compType>{}(priorDstValue * type_convert<dstDataType>{}(beta));
            };

            auto threadwise_dst_store =
                ThreadwiseGenericTensorSliceCopy_v4r2<decltype(ReducedDataDesc),
                                                      dst1dDesc,
                                                      ReducedDataLengths,
                                                      Sequence<0>,
                                                      0,
                                                      1,
                                                      1,
                                                      AddressSpace::Vgpr,
                                                      AddressSpace::Global,
                                                      InMemoryDataOperation::Set>(
                    {0}, {block_global_1d_id});
            threadwise_dst_store.Run(&accuValue, p_dst_global, zeroVal);
            threadwise_dst_store.Run(&accuIndex, indices_global, 0);
        };
    };
};

} // namespace ck
#endif
