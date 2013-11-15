/*
 * Copyright (c) 2011-2013, The University of Oxford
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

#include <private_vis.h>
#include <oskar_vis.h>
#include <oskar_binary_file_read.h>
#include <oskar_binary_tag_index_free.h>
#include <oskar_BinaryTag.h>
#include <oskar_mem_binary_file_read.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

oskar_Vis* oskar_vis_read(const char* filename, int* status)
{
    /* Visibility metadata. */
    int num_channels = 0, num_times = 0, num_stations = 0, tag_error = 0;
    int amp_type = 0;
    oskar_BinaryTagIndex* index = NULL;
    unsigned char grp = OSKAR_TAG_GROUP_VISIBILITY;
    oskar_Vis* vis = 0;

    /* Check all inputs. */
    if (!filename || !status)
    {
        oskar_set_invalid_argument(status);
        return 0;
    }

    /* Check if safe to proceed. */
    if (*status) return 0;

    /* Read visibility dimensions. */
    oskar_binary_file_read_int(filename, &index, grp,
            OSKAR_VIS_TAG_NUM_CHANNELS, 0, &num_channels, status);
    oskar_binary_file_read_int(filename, &index, grp,
            OSKAR_VIS_TAG_NUM_TIMES, 0, &num_times, status);
    oskar_binary_file_read_int(filename, &index, grp,
            OSKAR_VIS_TAG_NUM_STATIONS, 0, &num_stations, &tag_error);
    if (tag_error == OSKAR_ERR_BINARY_TAG_NOT_FOUND)
    {
        /* Check for number of baselines if number of stations not present. */
        int num_baselines = 0;
        oskar_binary_file_read_int(filename, &index, grp,
                OSKAR_VIS_TAG_NUM_BASELINES, 0, &num_baselines, status);

        /* Convert baselines to stations (care using floating point here). */
        num_stations = (int) floor(0.5 +
                (1.0 + sqrt(1.0 + 8.0 * num_baselines)) / 2.0);
    }
    else if (tag_error) *status = tag_error;
    oskar_binary_file_read_int(filename, &index, grp,
            OSKAR_VIS_TAG_AMP_TYPE, 0, &amp_type, status);

    /* Check if safe to proceed. */
    if (*status)
    {
        oskar_binary_tag_index_free(index, status);
        return 0;
    }

    /* Create the visibility structure. */
    vis = oskar_vis_create(amp_type, OSKAR_LOCATION_CPU,
            num_channels, num_times, num_stations, status);

    /* Optionally read the settings path (ignore the error code). */
    tag_error = 0;
    oskar_mem_binary_file_read(&vis->settings_path, filename, &index,
            OSKAR_TAG_GROUP_SETTINGS, OSKAR_TAG_SETTINGS_PATH, 0, &tag_error);

    /* Optionally read the telescope model path (ignore the error code). */
    tag_error = 0;
    oskar_mem_binary_file_read(&vis->telescope_path, filename, &index,
            grp, OSKAR_VIS_TAG_TELESCOPE_PATH, 0, &tag_error);

    /* Read visibility metadata. */
    oskar_binary_file_read_double(filename, &index, grp,
            OSKAR_VIS_TAG_FREQ_START_HZ, 0, &vis->freq_start_hz, status);
    oskar_binary_file_read_double(filename, &index, grp,
            OSKAR_VIS_TAG_FREQ_INC_HZ, 0, &vis->freq_inc_hz, status);
    oskar_binary_file_read_double(filename, &index, grp,
            OSKAR_VIS_TAG_TIME_START_MJD_UTC, 0, &vis->time_start_mjd_utc,
            status);
    oskar_binary_file_read_double(filename, &index, grp,
            OSKAR_VIS_TAG_TIME_INC_SEC, 0, &vis->time_inc_seconds, status);
    oskar_binary_file_read_double(filename, &index, grp,
            OSKAR_VIS_TAG_PHASE_CENTRE_RA, 0, &vis->phase_centre_ra_deg,
            status);
    oskar_binary_file_read_double(filename, &index, grp,
            OSKAR_VIS_TAG_PHASE_CENTRE_DEC, 0, &vis->phase_centre_dec_deg,
            status);

    /* Read the baseline coordinate arrays. */
    oskar_mem_binary_file_read(&vis->uu_metres, filename, &index, grp,
            OSKAR_VIS_TAG_BASELINE_UU, 0, status);
    oskar_mem_binary_file_read(&vis->vv_metres, filename, &index, grp,
            OSKAR_VIS_TAG_BASELINE_VV, 0, status);
    oskar_mem_binary_file_read(&vis->ww_metres, filename, &index, grp,
            OSKAR_VIS_TAG_BASELINE_WW, 0, status);

    /* Read the visibility data. */
    oskar_mem_binary_file_read(&vis->amplitude, filename, &index, grp,
            OSKAR_VIS_TAG_AMPLITUDE, 0, status);

    /* Optionally read station coordinates (ignore the error code). */
    tag_error = 0;
    oskar_mem_binary_file_read(&vis->x_metres, filename, &index, grp,
            OSKAR_VIS_TAG_STATION_X, 0, &tag_error);
    oskar_mem_binary_file_read(&vis->y_metres, filename, &index, grp,
            OSKAR_VIS_TAG_STATION_Y, 0, &tag_error);
    oskar_mem_binary_file_read(&vis->z_metres, filename, &index, grp,
            OSKAR_VIS_TAG_STATION_Z, 0, &tag_error);

    /* Optionally read station lon., lat. and orientation angles
     * (ignore the error code). */
    tag_error = 0;
    oskar_mem_binary_file_read(&vis->station_lon, filename, &index, grp,
            OSKAR_VIS_TAG_STATION_LON, 0, &tag_error);
    oskar_mem_binary_file_read(&vis->station_lat, filename, &index, grp,
            OSKAR_VIS_TAG_STATION_LAT, 0, &tag_error);
    oskar_mem_binary_file_read(&vis->station_orientation_x, filename, &index,
            grp, OSKAR_VIS_TAG_STATION_ORIENTATION_X, 0, &tag_error);
    oskar_mem_binary_file_read(&vis->station_orientation_y, filename, &index,
            grp, OSKAR_VIS_TAG_STATION_ORIENTATION_Y, 0, &tag_error);

    /* Optionally read telescope lon., lat. (ignore the error code) */
    tag_error = 0;
    oskar_binary_file_read_double(filename, &index, grp,
            OSKAR_VIS_TAG_TELESCOPE_LON, 0, &vis->telescope_lon_deg, &tag_error);
    oskar_binary_file_read_double(filename, &index, grp,
            OSKAR_VIS_TAG_TELESCOPE_LAT, 0, &vis->telescope_lat_deg, &tag_error);

    /* Optionally read the channel bandwidth value. */
    tag_error = 0;
    oskar_binary_file_read_double(filename, &index, grp,
            OSKAR_VIS_TAG_CHANNEL_BANDWIDTH_HZ, 0, &vis->channel_bandwidth_hz,
            &tag_error);

    /* Optionally read the time integration value. */
    tag_error = 0;
    oskar_binary_file_read_double(filename, &index, grp,
            OSKAR_VIS_TAG_TIME_INT_SEC, 0, &vis->time_int_seconds, &tag_error);

    /* Free the tag index. */
    oskar_binary_tag_index_free(index, status);

    /* Return a handle to the new structure. */
    return vis;
}

#ifdef __cplusplus
}
#endif