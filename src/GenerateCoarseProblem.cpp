
//@HEADER
// ***************************************************
//
// HPCG: High Performance Conjugate Gradient Benchmark
//
// Contact:
// Michael A. Heroux ( maherou@sandia.gov)
// Jack Dongarra     (dongarra@eecs.utk.edu)
// Piotr Luszczek    (luszczek@eecs.utk.edu)
//
// ***************************************************
//@HEADER

/* ************************************************************************
 * Modifications (c) 2019-2021 Advanced Micro Devices, Inc.
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
 @file GenerateCoarseProblem.cpp

 HPCG routine
 */

#include <hip/hip_runtime.h>
#include <cassert>

#include "GenerateCoarseProblem.hpp"
#include "GenerateGeometry.hpp"
#include "GenerateProblem.hpp"
#include "SetupHalo.hpp"

__global__ void kernel_f2c_operator(local_int_t nxc,
                                    local_int_t nyc,
                                    local_int_t nzc,
                                    global_int_t nxf,
                                    global_int_t nyf,
                                    global_int_t nzf,
                                    local_int_t* f2cOperator,
                                    local_int_t* c2fOperator)
{
    // Local index in x, y and z direction
    local_int_t ixc = blockIdx.x * blockDim.x + threadIdx.x;
    local_int_t iyc = blockIdx.y * blockDim.y + threadIdx.y;
    local_int_t izc = blockIdx.z * blockDim.z + threadIdx.z;

    // Do not run out of bounds
    if(izc >= nzc || iyc >= nyc || ixc >= nxc)
    {
        return;
    }

    local_int_t ixf = ixc << 1;
    local_int_t iyf = iyc << 1;
    local_int_t izf = izc << 1;

    local_int_t currentCoarseRow = izc * nxc * nyc + iyc * nxc + ixc;
    local_int_t currentFineRow = izf * nxf * nyf + iyf * nxf + ixf;

    f2cOperator[currentCoarseRow] = currentFineRow;
    c2fOperator[currentFineRow]   = currentCoarseRow;
}

/*!
  Routine to construct a prolongation/restriction operator for a given fine grid matrix
  solution (as computed by a direct solver).

  @param[inout]  Af - The known system matrix, on output its coarse operator, fine-to-coarse operator and auxiliary vectors will be defined.

  Note that the matrix Af is considered const because the attributes we are modifying are declared as mutable.

*/

void GenerateCoarseProblem(const SparseMatrix & Af) {

    // Make local copies of geometry information.  Use global_int_t since the RHS products in the calculations
    // below may result in global range values.
    global_int_t nxf = Af.geom->nx;
    global_int_t nyf = Af.geom->ny;
    global_int_t nzf = Af.geom->nz;

    // Need fine grid dimensions to be divisible by 2
    assert(nxf % 2 == 0);
    assert(nyf % 2 == 0);
    assert(nzf % 2 == 0);

    //Coarse nx, ny, nz
    local_int_t nxc = nxf / 2;
    local_int_t nyc = nyf / 2;
    local_int_t nzc = nzf / 2;

    // This is the size of our subblock
    local_int_t localNumberOfRows = nxc * nyc * nzc;

    // If this assert fails, it most likely means that the local_int_t is set to int and should be set to long long
    // Throw an exception of the number of rows is less than zero (can happen if "int" overflows)
    assert(localNumberOfRows > 0);

    // f2c & c2f Operator
    local_int_t* d_f2cOperator;
    local_int_t* d_c2fOperator;

    HIP_CHECK(deviceMalloc((void**)&d_f2cOperator, sizeof(local_int_t) * localNumberOfRows));
    HIP_CHECK(deviceMalloc((void**)&d_c2fOperator, sizeof(local_int_t) * nxf * nyf * nzf));

    HIP_CHECK(hipMemset(d_c2fOperator, -1, sizeof(local_int_t) * nxf * nyf * nzf));

    dim3 f2c_blocks((nxc - 1) / 2 + 1,
                    (nyc - 1) / 2 + 1,
                    (nzc - 1) / 2 + 1);
    dim3 f2c_threads(2, 2, 2);

    kernel_f2c_operator<<<f2c_blocks, f2c_threads>>>(nxc,
                                                     nyc,
                                                     nzc,
                                                     nxf,
                                                     nyf,
                                                     nzf,
                                                     d_f2cOperator,
                                                     d_c2fOperator);

    // Construct the geometry and linear system
    Geometry * geomc = new Geometry;

    // Coarsen nz for the lower block in the z processor dimension
    local_int_t zlc = 0;

    // Coarsen nz for the upper block in the z processor dimension
    local_int_t zuc = 0;

    if(Af.geom->pz > 0)
    {
        // Coarsen nz for the lower block in the z processor dimension
        zlc = Af.geom->partz_nz[0] / 2;
        // Coarsen nz for the upper block in the z processor dimension
        zuc = Af.geom->partz_nz[1] / 2;
    }

    GenerateGeometry(Af.geom->size, Af.geom->rank, Af.geom->numThreads, Af.geom->pz, zlc, zuc, nxc, nyc, nzc, Af.geom->npx, Af.geom->npy, Af.geom->npz, geomc);

    SparseMatrix* Ac = new SparseMatrix;
    InitializeSparseMatrix(*Ac, geomc);
    GenerateProblem(*Ac, 0, 0, 0);
    SetupHalo(*Ac);
    Vector* rc = new Vector;
    Vector* xc = new Vector;
    Vector* Axf = new Vector;
    HIPInitializeVector(*rc, Ac->localNumberOfRows);
    HIPInitializeVector(*xc, Ac->localNumberOfColumns);
#ifdef HPCG_REFERENCE
    HIPInitializeVector(*Axf, Af.localNumberOfColumns);
#endif

    Af.Ac = Ac;
    MGData* mgData = new MGData;
    InitializeMGData(d_f2cOperator, d_c2fOperator, rc, xc, Axf, *mgData);
    Af.mgData = mgData;

    return;
}

void CopyCoarseProblemToHost(SparseMatrix& A)
{
    // Copy problem to host
    CopyProblemToHost(*A.Ac, NULL, NULL, NULL);

    // Copy halo to host
    CopyHaloToHost(*A.Ac);

    // Allocate additional host vectors
    InitializeVector(*A.mgData->rc, A.Ac->localNumberOfRows);
    InitializeVector(*A.mgData->xc, A.Ac->localNumberOfColumns);
    InitializeVector(*A.mgData->Axf, A.localNumberOfColumns);

    // Copy f2c operator to host
    A.mgData->f2cOperator = new local_int_t[A.Ac->localNumberOfRows];
    HIP_CHECK(hipMemcpy(A.mgData->f2cOperator, A.mgData->d_f2cOperator, sizeof(local_int_t) * A.Ac->localNumberOfRows, hipMemcpyDeviceToHost));
}
