/*******************************************************************************
 *
 * MIT License
 *
 * Copyright (c) 2017 Advanced Micro Devices, Inc.
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

#include <miopen/rnn.hpp>
#include <miopen/errors.hpp>
#include <miopen/logger.hpp>
#include <miopen/tensor_ops.hpp>
#include <vector>
// TODO: Make miopenConvAlgoPerf_t loggable
// inline std::ostream& operator<<(std::ostream& os, miopenConvAlgoPerf_t) { return os; }

extern "C" miopenStatus_t miopenCreateRNNDescriptor(miopenRNNDescriptor_t* rnnDesc)
{
    //    MIOPEN_LOG_FUNCTION(rnnDesc);
    return miopen::try_([&] { miopen::deref(rnnDesc) = new miopen::RNNDescriptor(); });
}

extern "C" miopenStatus_t miopenInitRNNDescriptor(
    miopenRNNDescriptor_t rnnDesc, miopenRNNMode_t mode, int seqLength, int layer, int bidir, int bias)
{

    //    MIOPEN_LOG_FUNCTION(rnnDesc, mode, seqLength, layer, bidir, bias);
    return miopen::try_(
        [&] { miopen::deref(rnnDesc) = miopen::RNNDescriptor(mode, seqLength, layer, bidir, bias); });
}

extern "C" miopenStatus_t miopenGetRNNDescriptor(
    miopenRNNDescriptor_t rnnDesc, miopenRNNMode_t* mode, int* seqLength, int* layer, int* bidir, int *bias)
{

    //	MIOPEN_LOG_FUNCTION(rnnDesc, mode, seqLength, layer, bidir, bias);
    return miopen::try_([&] {
        miopen::deref(mode)      = miopen::deref(rnnDesc).mode;
        miopen::deref(seqLength) = miopen::deref(rnnDesc).seqLength;
        miopen::deref(layer)     = miopen::deref(rnnDesc).layer;
        miopen::deref(bidir)     = miopen::deref(rnnDesc).bidir;
		miopen::deref(bias) = miopen::deref(rnnDesc).bias;
    });
}

extern "C" miopenStatus_t miopenDestroyRNNDescriptor(miopenRNNDescriptor_t rnnDesc)
{
    //    MIOPEN_LOG_FUNCTION(rnnDesc);
    return miopen::try_([&] { delete rnnDesc; });
}

extern "C" miopenStatus_t miopenRNNForwardTraining(miopenHandle_t handle,
	const miopenRNNDescriptor_t rnnDesc,
	const int seqLen,
	const miopenTensorDescriptor_t xDesc,
	const void* x,
	const miopenTensorDescriptor_t hxDesc,
	const void* hx,
	const miopenTensorDescriptor_t cxDesc,
	const void* cx,
	const miopenTensorDescriptor_t wDesc,
	const void* w,
	const miopenTensorDescriptor_t yDesc,
	void* y,
	const miopenTensorDescriptor_t hyDesc,
	void* hy,
	const miopenTensorDescriptor_t cyDesc,
	void* cy,
	void* workSpace,
	size_t workSpaceSize,
	void* reserveSpace,
	size_t reserveSpaceSize,
	const std::vector<int> &in_n,
	const int in_h,
	const int hy_d,
	const int hy_n,
	const int hy_h,
	const int out_h)
{

//    MIOPEN_LOG_FUNCTION(
//		rnnDesc, seqLen, xDesc, x, hxDesc, hx, cxDesc, cx, wDesc, w, yDesc, y, hyDesc, hy, cyDesc, cy, workSpace, workSpaceSize, reserveSpace, reserveSpaceSize);
    return miopen::try_([&] {
        miopen::deref(rnnDesc).RNNForwardTraining(miopen::deref(handle),
													seqLen,
 //                                                  miopen::deref(xDesc),
                                                   DataCast(x),
//													miopen::deref(hxDesc),
													DataCast(hx),
//													miopen::deref(cxDesc),
													DataCast(cx),
//                                                   miopen::deref(wDesc),
                                                   DataCast(w),
//                                                   miopen::deref(yDesc),
                                                   DataCast(y),
//													miopen::deref(hyDesc),
													DataCast(hy),
//													miopen::deref(cyDesc),
													DataCast(cy),
                                                   DataCast(workSpace),
                                                   workSpaceSize,
													DataCast(reserveSpace),
													reserveSpaceSize,
			in_n,
			in_h,
			hy_d,
			hy_n,
			hy_h,
			out_h);
    });
}

extern "C" miopenStatus_t
miopenRNNBackwardData(miopenHandle_t handle,
	const miopenRNNDescriptor_t rnnDesc,
	const int seqLen,
	const miopenTensorDescriptor_t yDesc,
	const void* y,
	const miopenTensorDescriptor_t dyDesc,
	const void* dy,
	const miopenTensorDescriptor_t dhyDesc,
	const void* dhy,
	const miopenTensorDescriptor_t dcyDesc,
	const void* dcy,
	const miopenTensorDescriptor_t wDesc,
	const void* w,
	const miopenTensorDescriptor_t hxDesc,
	const void* hx,
	const miopenTensorDescriptor_t cxDesc,
	const void* cx,
	const miopenTensorDescriptor_t dxDesc,
	void* dx,
	const miopenTensorDescriptor_t dhxDesc,
	void* dhx,
	const miopenTensorDescriptor_t dcxDesc,
	void* dcx,
	void* workSpace,
	size_t workSpaceSize,
	const void* reserveSpace,
	size_t reserveSpaceSize,
	const std::vector<int> &in_n,
	const int in_h,
	const int hy_d,
	const int hy_n,
	const int hy_h,
	const int out_h)
{

//    MIOPEN_LOG_FUNCTION(
//	rnnDesc, seqLen, yDesc, y, dyDesc, dy, dhyDesc, dhy, dcyDesc, dcy, wDesc, w, hxDesc, hx, cxDesc, cx, dxDesc, dx, dhxDesc, dhx, dcxDesc, dcx, workSpace, workSpaceSize, reserveSpace, reserveSpaceSize);
    return miopen::try_([&] {
        miopen::deref(rnnDesc).RNNBackwardData(miopen::deref(handle),
		seqLen,
//			miopen::deref(yDesc),
			DataCast(y),
//			miopen::deref(dyDesc),
			DataCast(dy),
//			miopen::deref(dhyDesc),
			DataCast(dhy),
//			miopen::deref(dcyDesc),
			DataCast(dcy),
//			miopen::deref(wDesc),
			DataCast(w),
//			miopen::deref(hxDesc),
			DataCast(hx),
//			miopen::deref(cxDesc),
			DataCast(cx),
//			miopen::deref(dxDesc),
			DataCast(dx),
//			miopen::deref(dhxDesc),
			DataCast(dhx),
//			miopen::deref(dcxDesc),
			DataCast(dcx),
			DataCast(workSpace),
			workSpaceSize,
			DataCast(reserveSpace),
			reserveSpaceSize,
			in_n,
			in_h,
			hy_d,
			hy_n,
			hy_h,
			out_h);
    });
}

extern "C" miopenStatus_t
miopenRNNBackwardWeights(miopenHandle_t handle,
	const miopenRNNDescriptor_t rnnDesc,
	const int seqLen,
	const miopenTensorDescriptor_t xDesc,
	const void* x,
	const miopenTensorDescriptor_t hxDesc,
	const void* hx,
	const miopenTensorDescriptor_t dyDesc,
	const void* dy,
	const void* workSpace,
	size_t workSpaceSize,
	const miopenTensorDescriptor_t dwDesc,
	void* dw,
	const void* reserveSpace,
	size_t reserveSpaceSize,
	const std::vector<int> &in_n,
	const int in_h,
	const int hy_d,
	const int hy_n,
	const int hy_h,
	const int out_h)
{

//    MIOPEN_LOG_FUNCTION(
//	rnnDesc, seqLen, xDesc, x, hxDesc, hx, dyDesc, dy, workSpace, workSpaceSize, dwDesc, dw, reserveSpace, reserveSpaceSize);
    return miopen::try_([&] {
        miopen::deref(rnnDesc).RNNBackwardWeights(miopen::deref(handle),
			seqLen,
//			miopen::deref(xDesc),
			DataCast(x),
//			miopen::deref(hxDesc),
			DataCast(hx),
//			miopen::deref(dyDesc),
			DataCast(dy),
			DataCast(workSpace),
			workSpaceSize,
//			miopen::deref(dwDesc),
			DataCast(dw),
			DataCast(reserveSpace),
			reserveSpaceSize,
			in_n,
			in_h,
			hy_d,
			hy_n,
			hy_h,
			out_h);
    });
}
