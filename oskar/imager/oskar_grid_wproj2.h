/*
 * Copyright (c) 2018, The University of Oxford
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of the University of Oxford nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef OSKAR_GRID_WPROJ_2_H_
#define OSKAR_GRID_WPROJ_2_H_

/**
 * @file oskar_grid_wproj.h
 */

#include <oskar_global.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief
 * Gridding function for W-projection (double precision).
 *
 * @details
 * Gridding function for W-projection.
 *
 * @param[in] num_w_planes   Number of W-projection planes.
 * @param[in] support        GCF support size per W-plane.
 * @param[in] oversample     GCF oversample factor.
 * @param[in] wkernel_start  Start index of each convolution kernel.
 * @param[in] wkernel        The rearranged convolution kernels.
 * @param[in] num_points     Number of visibility points.
 * @param[in] uu             Visibility baseline uu coordinates, in wavelengths.
 * @param[in] vv             Visibility baseline vv coordinates, in wavelengths.
 * @param[in] ww             Visibility baseline ww coordinates, in wavelengths.
 * @param[in] vis            Complex visibilities for each baseline.
 * @param[in] weight         Visibility weight for each baseline.
 * @param[in] cell_size_rad  Cell size, in radians.
 * @param[in] w_scale        Scaling factor used to find W-plane index.
 * @param[in] grid_size      Side length of grid.
 * @param[out] num_skipped   Number of visibilities that fell outside the grid.
 * @param[in,out] norm       Updated grid normalisation factor.
 * @param[in,out] grid       Updated complex visibility grid.
 */
OSKAR_EXPORT
void oskar_grid_wproj2_d(
        const size_t num_w_planes,
        const int* RESTRICT support,
        const int oversample,
        const int* wkernel_start,
        const double* RESTRICT wkernel,
        const size_t num_points,
        const double* RESTRICT uu,
        const double* RESTRICT vv,
        const double* RESTRICT ww,
        const double* RESTRICT vis,
        const double* RESTRICT weight,
        const double cell_size_rad,
        const double w_scale,
        const int grid_size,
        size_t* RESTRICT num_skipped,
        double* RESTRICT norm,
        double* RESTRICT grid);

/**
 * @brief
 * Gridding function for W-projection (single precision).
 *
 * @details
 * Gridding function for W-projection.
 *
 * @param[in] num_w_planes   Number of W-projection planes.
 * @param[in] support        GCF support size per W-plane.
 * @param[in] oversample     GCF oversample factor.
 * @param[in] wkernel_start  Start index of each convolution kernel.
 * @param[in] wkernel        The rearranged convolution kernels.
 * @param[in] num_points     Number of visibility points.
 * @param[in] uu             Visibility baseline uu coordinates, in wavelengths.
 * @param[in] vv             Visibility baseline vv coordinates, in wavelengths.
 * @param[in] ww             Visibility baseline ww coordinates, in wavelengths.
 * @param[in] vis            Complex visibilities for each baseline.
 * @param[in] weight         Visibility weight for each baseline.
 * @param[in] cell_size_rad  Cell size, in radians.
 * @param[in] w_scale        Scaling factor used to find W-plane index.
 * @param[in] grid_size      Side length of grid.
 * @param[out] num_skipped   Number of visibilities that fell outside the grid.
 * @param[in,out] norm       Updated grid normalisation factor.
 * @param[in,out] grid       Updated complex visibility grid.
 */
OSKAR_EXPORT
void oskar_grid_wproj2_f(
        const size_t num_w_planes,
        const int* RESTRICT support,
        const int oversample,
        const int* wkernel_start,
        const float* RESTRICT wkernel,
        const size_t num_points,
        const float* RESTRICT uu,
        const float* RESTRICT vv,
        const float* RESTRICT ww,
        const float* RESTRICT vis,
        const float* RESTRICT weight,
        const float cell_size_rad,
        const float w_scale,
        const int grid_size,
        size_t* RESTRICT num_skipped,
        double* RESTRICT norm,
        float* RESTRICT grid);

#ifdef __cplusplus
}
#endif

#endif /* include guard */
