/*
 * Copyright (c) 2011, The University of Oxford
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

#include "interferometry/oskar_xyz_to_uvw.h"
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Single precision. */
void oskar_xyz_to_uvw_f(int n, const float* x, const float* y,
        const float* z, double ha0, double dec0, float* u, float* v,
        float* w)
{
    int i;
    double sinHa0, cosHa0, sinDec0, cosDec0;

    /* Precompute trig. */
    sinHa0  = sin(ha0);
    cosHa0  = cos(ha0);
    sinDec0 = sin(dec0);
    cosDec0 = cos(dec0);

    /* Loop over points. */
    for (i = 0; i < n; ++i)
    {
        double xi, yi, zi, ut, vt, wt;

        /* Get the input coordinates. */
        xi = (double) (x[i]);
        yi = (double) (y[i]);
        zi = (double) (z[i]);

        /* Apply rotation matrix. */
        ut =  xi * sinHa0 + yi * cosHa0;
        vt = sinDec0 * (-xi * cosHa0 + yi * sinHa0) + zi * cosDec0;
        wt = cosDec0 * (xi * cosHa0 - yi * sinHa0) + zi * sinDec0;

        /* Save the rotated values. */
        u[i] = (float)ut;
        v[i] = (float)vt;
        w[i] = (float)wt;
    }
}

/* Double precision. */
void oskar_xyz_to_uvw_d(int n, const double* x, const double* y,
        const double* z, double ha0, double dec0, double* u, double* v,
        double* w)
{
    int i;
    double sinHa0, cosHa0, sinDec0, cosDec0;

    /* Precompute trig. */
    sinHa0  = sin(ha0);
    cosHa0  = cos(ha0);
    sinDec0 = sin(dec0);
    cosDec0 = cos(dec0);

    /* Loop over points. */
    for (i = 0; i < n; ++i)
    {
        double xi, yi, zi, ut, vt, wt;

        /* Get the input coordinates. */
        xi = x[i];
        yi = y[i];
        zi = z[i];

        /* Apply rotation matrix. */
        ut =  xi * sinHa0 + yi * cosHa0;
        vt = sinDec0 * (-xi * cosHa0 + yi * sinHa0) + zi * cosDec0;
        wt = cosDec0 * (xi * cosHa0 - yi * sinHa0) + zi * sinDec0;

        /* Save the rotated values. */
        u[i] = ut;
        v[i] = vt;
        w[i] = wt;
    }
}

#ifdef __cplusplus
}
#endif
