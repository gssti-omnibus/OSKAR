/*
 * Copyright (c) 2013-2019, The University of Oxford
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

#include "convert/define_convert_ecef_to_station_uvw.h"
#include "convert/oskar_convert_ecef_to_station_uvw.h"
#include "utility/oskar_device.h"
#include "utility/oskar_kernel_macros.h"

#ifdef __cplusplus
extern "C" {
#endif

OSKAR_CONVERT_ECEF_TO_STATION_UVW(convert_ecef_to_station_uvw_float, float)
OSKAR_CONVERT_ECEF_TO_STATION_UVW(convert_ecef_to_station_uvw_double, double)

void oskar_convert_ecef_to_station_uvw(int num_stations,
        const oskar_Mem* x, const oskar_Mem* y, const oskar_Mem* z,
        double ra0_rad, double dec0_rad, double gast, int offset_out,
        oskar_Mem* u, oskar_Mem* v, oskar_Mem* w, int* status)
{
    if (*status) return;
    const int type = oskar_mem_type(x);
    const int location = oskar_mem_location(x);
    if (oskar_mem_location(y) != location ||
            oskar_mem_location(z) != location ||
            oskar_mem_location(u) != location ||
            oskar_mem_location(v) != location ||
            oskar_mem_location(w) != location)
    {
        *status = OSKAR_ERR_LOCATION_MISMATCH;
        return;
    }
    if (oskar_mem_type(y) != type || oskar_mem_type(z) != type ||
            oskar_mem_type(u) != type || oskar_mem_type(v) != type ||
            oskar_mem_type(w) != type)
    {
        *status = OSKAR_ERR_TYPE_MISMATCH;
        return;
    }
    const double ha0_rad = gast - ra0_rad;
    const double sin_ha0  = sin(ha0_rad);
    const double cos_ha0  = cos(ha0_rad);
    const double sin_dec0 = sin(dec0_rad);
    const double cos_dec0 = cos(dec0_rad);
    const float sin_ha0_f = (float) sin_ha0;
    const float cos_ha0_f = (float) cos_ha0;
    const float sin_dec0_f = (float) sin_dec0;
    const float cos_dec0_f = (float) cos_dec0;
    if (location == OSKAR_CPU)
    {
        if (type == OSKAR_SINGLE)
            convert_ecef_to_station_uvw_float(num_stations,
                    oskar_mem_float_const(x, status),
                    oskar_mem_float_const(y, status),
                    oskar_mem_float_const(z, status),
                    sin_ha0_f, cos_ha0_f, sin_dec0_f, cos_dec0_f, offset_out,
                    oskar_mem_float(u, status),
                    oskar_mem_float(v, status),
                    oskar_mem_float(w, status));
        else if (type == OSKAR_DOUBLE)
            convert_ecef_to_station_uvw_double(num_stations,
                    oskar_mem_double_const(x, status),
                    oskar_mem_double_const(y, status),
                    oskar_mem_double_const(z, status),
                    sin_ha0, cos_ha0, sin_dec0, cos_dec0, offset_out,
                    oskar_mem_double(u, status),
                    oskar_mem_double(v, status),
                    oskar_mem_double(w, status));
        else
            *status = OSKAR_ERR_BAD_DATA_TYPE;
    }
    else
    {
        size_t local_size[] = {256, 1, 1}, global_size[] = {1, 1, 1};
        const char* k = 0;
        const int is_dbl = oskar_mem_is_double(x);
        if (type == OSKAR_SINGLE)
            k = "convert_ecef_to_station_uvw_float";
        else if (type == OSKAR_DOUBLE)
            k = "convert_ecef_to_station_uvw_double";
        else
        {
            *status = OSKAR_ERR_BAD_DATA_TYPE;
            return;
        }
        oskar_device_check_local_size(location, 0, local_size);
        global_size[0] = oskar_device_global_size(
                (size_t) num_stations, local_size[0]);
        const oskar_Arg args[] = {
                {INT_SZ, &num_stations},
                {PTR_SZ, oskar_mem_buffer_const(x)},
                {PTR_SZ, oskar_mem_buffer_const(y)},
                {PTR_SZ, oskar_mem_buffer_const(z)},
                {is_dbl ? DBL_SZ : FLT_SZ, is_dbl ?
                        (const void*)&sin_ha0 : (const void*)&sin_ha0_f},
                {is_dbl ? DBL_SZ : FLT_SZ, is_dbl ?
                        (const void*)&cos_ha0 : (const void*)&cos_ha0_f},
                {is_dbl ? DBL_SZ : FLT_SZ, is_dbl ?
                        (const void*)&sin_dec0 : (const void*)&sin_dec0_f},
                {is_dbl ? DBL_SZ : FLT_SZ, is_dbl ?
                        (const void*)&cos_dec0 : (const void*)&cos_dec0_f},
                {INT_SZ, &offset_out},
                {PTR_SZ, oskar_mem_buffer(u)},
                {PTR_SZ, oskar_mem_buffer(v)},
                {PTR_SZ, oskar_mem_buffer(w)}
        };
        oskar_device_launch_kernel(k, location, 1, local_size, global_size,
                sizeof(args) / sizeof(oskar_Arg), args, 0, 0, status);
    }
}

#ifdef __cplusplus
}
#endif
