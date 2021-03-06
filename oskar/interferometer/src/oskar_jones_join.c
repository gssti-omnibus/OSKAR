/*
 * Copyright (c) 2011-2015, The University of Oxford
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

#include "interferometer/private_jones.h"
#include "interferometer/oskar_jones.h"

#ifdef __cplusplus
extern "C" {
#endif

void oskar_jones_join(oskar_Jones* j3, oskar_Jones* j1, const oskar_Jones* j2,
        int* status)
{
    if (*status) return;

    /* Get the dimensions of the input data. */
    if (!j3) j3 = j1;
    const int n_sources1 = j1->num_sources;
    const int n_sources2 = j2->num_sources;
    const int n_sources3 = j3->num_sources;
    const int n_stations1 = j1->num_stations;
    const int n_stations2 = j2->num_stations;
    const int n_stations3 = j3->num_stations;

    /* Check the data dimensions. */
    if (n_sources1 != n_sources2 || n_sources1 != n_sources3)
        *status = OSKAR_ERR_DIMENSION_MISMATCH;
    if (n_stations1 != n_stations2 || n_stations1 != n_stations3)
        *status = OSKAR_ERR_DIMENSION_MISMATCH;

    /* Multiply the array elements. */
    const size_t num_elements = n_sources1 * n_stations1;
    oskar_mem_multiply(j3->data, j1->data, j2->data,
            0, 0, 0, num_elements, status);
}

#ifdef __cplusplus
}
#endif
