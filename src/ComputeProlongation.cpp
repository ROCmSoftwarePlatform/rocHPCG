/* ************************************************************************
 * Copyright (c) 2019 Advanced Micro Devices, Inc.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * ************************************************************************ */

/*!
 @file ComputeProlongation.cpp

 HPCG routine
 */

#include "ComputeProlongation.hpp"

#include <hip/hip_runtime.h>

__global__ void kernel_prolongation(local_int_t size,
                                    const local_int_t* f2cOperator,
                                    const double* coarse,
                                    double* fine,
                                    const local_int_t* perm_fine,
                                    const local_int_t* perm_coarse)
{
    local_int_t idx_coarse = hipBlockIdx_x * hipBlockDim_x + hipThreadIdx_x;

    if(idx_coarse >= size)
    {
        return;
    }

    local_int_t idx_fine = f2cOperator[idx_coarse];

    fine[perm_fine[idx_fine]] += coarse[perm_coarse[idx_coarse]];
}

/*!
  Routine to compute the coarse residual vector.

  @param[in]  Af - Fine grid sparse matrix object containing pointers to current coarse grid correction and the f2c operator.
  @param[inout] xf - Fine grid solution vector, update with coarse grid correction.

  Note that the fine grid residual is never explicitly constructed.
  We only compute it for the fine grid points that will be injected into corresponding coarse grid points.

  @return Returns zero on success and a non-zero value otherwise.
*/
int ComputeProlongation(const SparseMatrix& Af, Vector& xf)
{
    hipLaunchKernelGGL((kernel_prolongation),
                       dim3((Af.mgData->rc->localLength - 1) / 1024 + 1),
                       dim3(1024),
                       0,
                       0,
                       Af.mgData->rc->localLength,
                       Af.mgData->d_f2cOperator,
                       Af.mgData->xc->d_values,
                       xf.d_values,
                       Af.perm,
                       Af.Ac->perm);

    return 0;
}
