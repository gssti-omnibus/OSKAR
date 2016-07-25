/*
 * Copyright (c) 2012-2016, The University of Oxford
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

#include <oskar_sim_beam_pattern.h>

#include <oskar_beam_pattern_generate_coordinates.h>
#include <oskar_cmath.h>
#include <oskar_convert_fov_to_cellsize.h>
#include <oskar_convert_mjd_to_gast_fast.h>
#include <oskar_cuda_mem_log.h>
#include <oskar_device_utils.h>
#include <oskar_evaluate_auto_power.h>
#include <oskar_evaluate_cross_power.h>
#include <oskar_evaluate_station_beam.h>
#include <oskar_file_exists.h>
#include <oskar_log.h>
#include <oskar_set_up_telescope.h>
#include <oskar_SettingsTree.hpp>
#include <oskar_SettingsDeclareXml.hpp>
#include <oskar_SettingsFileHandlerQSettings.hpp>
#include <oskar_settings_old_free.h>
#include <oskar_settings_load.h>
#include <oskar_settings_log.h>
#include <oskar_station_work.h>
#include <oskar_telescope.h>
#include <oskar_timer.h>
#include <oskar_version.h>
#include <private_cond2_2x2.h>

#include <fitsio.h>

#include "settings/xml/oskar_sim_beam_pattern_xml_all.h"

#ifdef _OPENMP
#include <omp.h>
#endif

#include <stdlib.h>
#include <string.h>

#if __STDC_VERSION__ >= 199901L
#define SNPRINTF(BUF, SIZE, FMT, ...) snprintf(BUF, SIZE, FMT, __VA_ARGS__);
#else
#define SNPRINTF(BUF, SIZE, FMT, ...) sprintf(BUF, FMT, __VA_ARGS__);
#endif


#ifdef __cplusplus
extern "C" {
#endif

/* Memory allocated per GPU. */
struct DeviceData
{
    /* Host memory. */
    /* Chunks have dimension max_chunk_size * num_active_stations. */
    /* Cross power beams have dimension max_chunk_size. */
    oskar_Mem* jones_data_cpu[2]; /* On host, for copy back & write. */

    /* Per Stokes parameter. */
    oskar_Mem* auto_power_cpu[4][2]; /* On host, for copy back & write. */
    oskar_Mem* auto_power_time_avg[4];
    oskar_Mem* auto_power_channel_avg[4];
    oskar_Mem* auto_power_channel_and_time_avg[4];
    oskar_Mem* cross_power_cpu[4][2]; /* On host, for copy back & write. */
    oskar_Mem* cross_power_time_avg[4];
    oskar_Mem* cross_power_channel_avg[4];
    oskar_Mem* cross_power_channel_and_time_avg[4];

    /* Device memory. */
    int previous_chunk_index;
    oskar_Telescope* tel;
    oskar_StationWork* work;
    oskar_Mem *x, *y, *z, *jones_data;
    oskar_Mem *auto_power[4], *cross_power[4];

    /* Timers. */
    oskar_Timer* tmr_compute;   /* Total time spent calculating pixels. */
};
typedef struct DeviceData DeviceData;

struct DataProduct
{
    int type;
    int stokes_in; /* Source polarisation type. */
    int stokes_out; /* Image polarisation type. */
    int i_station;
    int time_average;
    int channel_average;
    fitsfile* fits_file;
    FILE* text_file;
};
typedef struct DataProduct DataProduct;

/* Memory allocated once, on the host. */
struct HostData
{
    int num_gpus;

    /* Input data (settings, pixel positions, telescope model). */
    oskar_Mem *x, *y, *z;
    oskar_Telescope* tel;
    oskar_Settings_old s;
    oskar_SettingsFileHandlerQSettings* handler;
    oskar_Settings* s2;

    /* Temporary arrays. */
    oskar_Mem* pix; /* Real-valued pixel array to write to file. */
    oskar_Mem* ctemp; /* Complex-valued array used for reordering. */

    /* Metadata. */
    int coord_type, max_chunk_size;
    int num_times, num_channels, num_chunks;
    int pol_mode, precision, width, height, num_pixels;
    int num_active_stations, *station_ids;
    int auto_power[4], cross_power[4], raw_data; /* Flags. */
    int separate_time_and_channel, average_time_and_channel; /* Flags. */
    double lon0, lat0, phase_centre_deg[2], fov_deg[2];
    double start_mjd_utc, length_sec, delta_t, start_freq_hz, delta_f;
    char average_single_axis, coord_frame_type, coord_grid_type, *root_path;

    /* Settings log data. */
    char* settings_log;
    size_t settings_log_length;

    /* Data product list. */
    int num_data_products;
    DataProduct* data_products;

    /* Timers. */
    oskar_Timer* tmr_load;
    oskar_Timer* tmr_sim;
    oskar_Timer* tmr_write;
};
typedef struct HostData HostData;

enum OSKAR_BEAM_PATTERN_DATA_PRODUCT_TYPE
{
    RAW_COMPLEX,
    AMP,
    PHASE,
    AUTO_POWER,
    CROSS_POWER_RAW_COMPLEX,
    CROSS_POWER_AMP,
    CROSS_POWER_PHASE,
    IXR
};

enum OSKAR_BEAM_DATA_TYPE
{
    JONES_DATA,
    AUTO_POWER_DATA,
    CROSS_POWER_DATA
};

enum OSKAR_STOKES
{
    /* IQUV must be 0 to 3. */
    I  = 0,
    Q  = 1,
    U  = 2,
    V  = 3,
    XX = 4,
    XY = 5,
    YX = 6,
    YY = 7
};

#define RAD2DEG (180.0 / M_PI)

static void sim_chunks(int gpu_id, DeviceData* d, const HostData* h,
        int i_chunk_start, int i_time, int i_channel, int i_active,
        oskar_Log* log, int* status);
static void write_chunks(DeviceData* d, HostData* h, int i_chunk_start,
        int i_time, int i_channel, int i_active, int* status);
static void write_pixels(HostData* h, int i_chunk, int i_time, int i_channel,
        int num_pix, int channel_average, int time_average,
        const oskar_Mem* in, int chunk_desc, int stokes_in, int* status);
static void complex_to_amp(const oskar_Mem* complex_in, const int offset,
        const int stride, const int num_points, oskar_Mem* output, int* status);
static void complex_to_phase(const oskar_Mem* complex_in, const int offset,
        const int stride, const int num_points, oskar_Mem* output, int* status);
static void jones_to_ixr(const oskar_Mem* complex_in, const int offset,
        const int num_points, oskar_Mem* output, int* status);
static void power_to_stokes_I(const oskar_Mem* power_in, const int offset,
        const int num_points, oskar_Mem* output, int* status);
static void power_to_stokes_Q(const oskar_Mem* power_in, const int offset,
        const int num_points, oskar_Mem* output, int* status);
static void power_to_stokes_U(const oskar_Mem* power_in, const int offset,
        const int num_points, oskar_Mem* output, int* status);
static void power_to_stokes_V(const oskar_Mem* power_in, const int offset,
        const int num_points, oskar_Mem* output, int* status);
static void set_up_host_data(HostData* h, oskar_Log* log, int *status);
static void create_averaged_products(HostData* h, int ta, int ca, int* status);
static void set_up_device_data(DeviceData* d, const HostData* h, int* status);
static void free_device_data(int num_gpus, int* cuda_device_ids,
        DeviceData* d, int* status);
static void free_host_data(HostData* h, int* status);
static void write_axis(fitsfile* fptr, int axis_id, const char* ctype,
        const char* ctype_comment, double crval, double cdelt, double crpix,
        int* status);
static fitsfile* create_fits_file(const char* filename, int precision,
        int width, int height, int num_times, int num_channels,
        double centre_deg[2], double fov_deg[2], double start_time_mjd,
        double delta_time_sec, double start_freq_hz, double delta_freq_hz,
        int horizon_mode, const char* settings_log, size_t settings_log_length,
        int* status);
static int data_product_index(HostData* h, int data_product_type,
        int stokes_in, int stokes_out, int i_station, int time_average,
        int channel_average);
static char* construct_filename(int data_product_type,
        int stokes_in, int stokes_out, int i_station, int time_average,
        int channel_average, const char* rootname, const char* ext);
static void new_fits_file(HostData* h, int data_product_type, int stokes_in,
        int stokes_out, int i_station, int channel_average, int time_average,
        const char* rootname, int* status);
static void new_text_file(HostData* h, int data_product_type, int stokes_in,
        int stokes_out, int i_station, int channel_average, int time_average,
        const char* rootname, int* status);
static const char* data_type_to_string(int type);
static const char* stokes_type_to_string(int type);
static void record_timing(int num_gpus, int* cuda_device_ids,
        DeviceData* d, HostData* h, oskar_Log* log);
static unsigned int disp_width(unsigned int value);


void oskar_sim_beam_pattern(const char* settings_file, oskar_Log* log,
        int* status)
{
    int i = 0, i_global = 0, num_gpus_avail = 0, num_threads = 1;
    int cp = 0, tp = 0, fp = 0; /* Loop indices for previous iteration. */
    DeviceData* d = 0;
    HostData* h = 0;
    oskar_Settings_old* s = 0;
    int num_failed_keys = 0;
    char** failed_keys = 0;

    /* Create the host data structure (initialised with all bits zero). */
    h = (HostData*) calloc(1, sizeof(HostData));
    s = &h->s;

    /* Start the load timer. */
    h->tmr_load  = oskar_timer_create(OSKAR_TIMER_NATIVE);
    h->tmr_sim   = oskar_timer_create(OSKAR_TIMER_NATIVE);
    h->tmr_write = oskar_timer_create(OSKAR_TIMER_NATIVE);
    oskar_timer_start(h->tmr_load);

    /* Load the settings file. */
    oskar_log_section(log, 'M', "Loading settings file '%s'", settings_file);
    h->s2 = oskar_settings_create();
    oskar_settings_declare_xml(h->s2, oskar_sim_beam_pattern_XML_STR);
    h->handler = oskar_settings_file_handler_qsettings_create();
    oskar_settings_set_file_handler(h->s2, h->handler);
    oskar_settings_load(h->s2, settings_file, &num_failed_keys, &failed_keys,
            status);
    for (i = 0; i < num_failed_keys; ++i)
    {
        oskar_log_warning(log, "Ignoring '%s'", failed_keys[i]);
        free(failed_keys[i]);
    }
    free(failed_keys);
    if (*status) { free_host_data(h, status); return; }

    /* Load the settings file. */
    oskar_log_section(log, 'M', "Loading settings file '%s'", settings_file);
    oskar_settings_old_load(s, log, settings_file, status);
    if (*status) { free_host_data(h, status); return; }

    /* Log the relevant settings. (TODO fix/automate these functions) */
    oskar_log_set_keep_file(log, s->sim.keep_log_file);
    oskar_log_set_file_priority(log, s->sim.write_status_to_log_file ?
            OSKAR_LOG_STATUS : OSKAR_LOG_MESSAGE);
    oskar_log_settings_simulator(log, s);
    oskar_log_settings_observation(log, s);
    oskar_log_settings_telescope(log, s);
    oskar_log_settings_beam_pattern(log, s);

    /* Get the number of requested GPUs.
     * If OpenMP is not available, this can only be 1. */
    h->num_gpus = s->sim.num_cuda_devices;
#ifdef _OPENMP
    num_threads = h->num_gpus + 1;
    omp_set_num_threads(num_threads);
#else
    h->num_gpus = 1;
    oskar_log_warning(log, "OpenMP not available: Ignoring CUDA device list.");
#endif

    /* Find out how many GPUs are in the system. */
    num_gpus_avail = oskar_device_count(status);
    if (*status) { free_host_data(h, status); return; }
    if (num_gpus_avail < h->num_gpus)
    {
        oskar_log_error(log, "More CUDA devices were requested than found.");
        free_host_data(h, status);
        *status = OSKAR_ERR_CUDA_DEVICES;
        return;
    }

    /* Set up host data and check for errors. */
    set_up_host_data(h, log, status);
    if (*status) { free_host_data(h, status); return; }

    /* Initialise each of the requested GPUs and set up per-GPU memory. */
    d = (DeviceData*) calloc(h->num_gpus, sizeof(DeviceData));
    for (i = 0; i < h->num_gpus; ++i)
    {
        oskar_device_set(s->sim.cuda_device_ids[i], status);
        if (*status)
        {
            free_device_data(h->num_gpus, s->sim.cuda_device_ids, d, status);
            free_host_data(h, status);
            return;
        }
        set_up_device_data(&d[i], h, status);
        oskar_device_synchronize();
    }

    /* Record memory usage. */
    oskar_log_section(log, 'M', "Initial memory usage");
    for (i = 0; i < h->num_gpus; ++i)
        oskar_cuda_mem_log(log, 0, s->sim.cuda_device_ids[i]);

    /* Start simulation timer and stop the load timer. */
    oskar_timer_pause(h->tmr_load);
    oskar_log_section(log, 'M', "Starting simulation...");
    oskar_timer_start(h->tmr_sim);

    /*-----------------------------------------------------------------------
     *-- START OF MULTITHREADED SIMULATION CODE -----------------------------
     *-----------------------------------------------------------------------*/
    /* Loop over image pixel chunks, running simulation and file writing one
     * chunk at a time. Simulation and file output are overlapped by using
     * double buffering, and a dedicated thread is used for file output.
     *
     * Thread 0 is used for file writes.
     * Threads 1 to n (mapped to GPUs) execute the simulation.
     */
#pragma omp parallel
    {
        int i_inner, i_outer, num_inner, num_outer, c, t, f;
        int thread_id = 0, gpu_id = 0;

        /* Get host thread ID, and set CUDA device used by this thread. */
#ifdef _OPENMP
        thread_id = omp_get_thread_num();
        gpu_id = thread_id - 1;
#endif
        if (gpu_id >= 0)
            oskar_device_set(s->sim.cuda_device_ids[gpu_id], status);

        /* Set ranges of inner and outer loops based on averaging mode. */
        if (h->average_single_axis != 'T')
        {
            num_outer = h->num_times;
            num_inner = h->num_channels; /* Channel on inner loop. */
        }
        else
        {
            num_outer = h->num_channels;
            num_inner = h->num_times; /* Time on inner loop. */
        }

        /* Loop over chunks, times and channels. */
        for (c = 0; c < h->num_chunks; c += h->num_gpus)
        {
            for (i_outer = 0; i_outer < num_outer; ++i_outer)
            {
                for (i_inner = 0; i_inner < num_inner; ++i_inner)
                {
                    /* Set time and channel indices based on averaging mode. */
                    if (h->average_single_axis != 'T')
                    {
                        t = i_outer;
                        f = i_inner;
                    }
                    else
                    {
                        f = i_outer;
                        t = i_inner;
                    }
                    if (thread_id > 0 || num_threads == 1)
                        sim_chunks(gpu_id, &d[gpu_id], h, c, t, f,
                                i_global & 1, log, status);
                    if (thread_id == 0 && i_global > 0)
                        write_chunks(d, h, cp, tp, fp, i_global & 1, status);

                    /* Barrier1: Set indices of the previous chunk(s). */
#pragma omp barrier
                    if (thread_id == 0)
                    {
                        cp = c;
                        tp = t;
                        fp = f;
                        i_global++;
                    }
                    /* Barrier2: Check sim and write are done. */
#pragma omp barrier
                }
            }
        }
    }
    /*-----------------------------------------------------------------------
     *-- END OF MULTITHREADED SIMULATION CODE -------------------------------
     *-----------------------------------------------------------------------*/

    /* Write the very last chunk(s). */
    write_chunks(d, h, cp, tp, fp, i_global & 1, status);

    /* Record memory usage. */
    oskar_log_section(log, 'M', "Final memory usage");
    for (i = 0; i < h->num_gpus; ++i)
        oskar_cuda_mem_log(log, 0, s->sim.cuda_device_ids[i]);

    /* Record time taken. */
    oskar_log_set_value_width(log, 25);
    record_timing(h->num_gpus, s->sim.cuda_device_ids, d, h, log);

    /* Free device and host memory (and close output files). */
    free_device_data(h->num_gpus, s->sim.cuda_device_ids, d, status);
    free_host_data(h, status);
}


static void sim_chunks(int gpu_id, DeviceData* d, const HostData* h,
        int i_chunk_start, int i_time, int i_channel, int i_active,
        oskar_Log* log, int* status)
{
    int chunk_size, i_chunk, i;
    double dt_dump, mjd, gast, freq_hz;
    oskar_Mem *input_alias, *output_alias;

    /* Check if safe to proceed. */
    if (*status) return;

    /* Get chunk index from GPU ID and chunk start,
     * and return immediately if it's out of range. */
    i_chunk = i_chunk_start + gpu_id;
    if (i_chunk >= h->num_chunks) return;

    /* Get time and frequency values. */
    oskar_timer_resume(d->tmr_compute);
    dt_dump = h->delta_t / 86400.0;
    mjd = h->start_mjd_utc + dt_dump * (i_time + 0.5);
    gast = oskar_convert_mjd_to_gast_fast(mjd);
    freq_hz = h->start_freq_hz + i_channel * h->delta_f;

    /* Work out the size of the chunk. */
    chunk_size = h->max_chunk_size;
    if ((i_chunk + 1) * h->max_chunk_size > h->num_pixels)
        chunk_size = h->num_pixels - i_chunk * h->max_chunk_size;

    /* Copy pixel chunk coordinate data to GPU only if chunk is different. */
    if (i_chunk != d->previous_chunk_index)
    {
        d->previous_chunk_index = i_chunk;
        oskar_mem_copy_contents(d->x, h->x, 0,
                i_chunk * h->max_chunk_size, chunk_size, status);
        oskar_mem_copy_contents(d->y, h->y, 0,
                i_chunk * h->max_chunk_size, chunk_size, status);
        oskar_mem_copy_contents(d->z, h->z, 0,
                i_chunk * h->max_chunk_size, chunk_size, status);
    }

    /* Generate beam for this pixel chunk, for all active stations. */
    input_alias  = oskar_mem_create_alias(0, 0, 0, status);
    output_alias = oskar_mem_create_alias(0, 0, 0, status);
    for (i = 0; i < h->num_active_stations; ++i)
    {
        oskar_mem_set_alias(input_alias, d->jones_data,
                i * chunk_size, chunk_size, status);
        oskar_mem_set_alias(output_alias, d->jones_data,
                i * chunk_size, chunk_size, status);
        oskar_evaluate_station_beam(output_alias, chunk_size,
                h->coord_type, d->x, d->y, d->z,
                oskar_telescope_phase_centre_ra_rad(d->tel),
                oskar_telescope_phase_centre_dec_rad(d->tel),
                oskar_telescope_station_const(d->tel, h->station_ids[i]),
                d->work, i_time, freq_hz, gast, status);
        if (d->auto_power[I])
        {
            oskar_mem_set_alias(output_alias, d->auto_power[I],
                    i * chunk_size, chunk_size, status);
            oskar_evaluate_auto_power(chunk_size,
                    input_alias, output_alias, status);
        }
#if 0
        if (d->auto_power[Q])
        {
            oskar_mem_set_alias(output_alias, d->auto_power[Q],
                    i * chunk_size, chunk_size, status);
            oskar_evaluate_auto_power_stokes_q(chunk_size,
                    input_alias, output_alias, status);
        }
        if (d->auto_power[U])
        {
            oskar_mem_set_alias(output_alias, d->auto_power[U],
                    i * chunk_size, chunk_size, status);
            oskar_evaluate_auto_power_stokes_u(chunk_size,
                    input_alias, output_alias, status);
        }
#endif
    }
    if (d->cross_power[I])
        oskar_evaluate_cross_power(chunk_size, h->num_active_stations,
                d->jones_data, d->cross_power[I], status);
    oskar_mem_free(input_alias, status);
    oskar_mem_free(output_alias, status);

    /* Copy the output data into host memory. */
    if (d->jones_data_cpu[i_active])
        oskar_mem_copy_contents(d->jones_data_cpu[i_active], d->jones_data,
                0, 0, chunk_size * h->num_active_stations, status);
    for (i = 0; i < 4; ++i)
    {
        if (d->auto_power[i])
            oskar_mem_copy_contents(d->auto_power_cpu[i][i_active],
                    d->auto_power[i], 0, 0,
                    chunk_size * h->num_active_stations, status);
        if (d->cross_power[i])
            oskar_mem_copy_contents(d->cross_power_cpu[i][i_active],
                    d->cross_power[i], 0, 0, chunk_size, status);
    }

    oskar_log_message(log, 'S', 1, "Chunk %*i/%i, "
            "Time %*i/%i, Channel %*i/%i [GPU %i]",
            disp_width(h->num_chunks), i_chunk+1, h->num_chunks,
            disp_width(h->num_times), i_time+1, h->num_times,
            disp_width(h->num_channels), i_channel+1, h->num_channels, gpu_id);
    oskar_timer_pause(d->tmr_compute);
}


static void write_chunks(DeviceData* d, HostData* h, int i_chunk_start,
        int i_time, int i_channel, int i_active, int* status)
{
    int i, i_chunk, chunk_sources, chunk_size, stokes;
    if (*status) return;

    /* Write inactive chunk(s) from all GPUs. */
    oskar_timer_resume(h->tmr_write);
    for (i = 0; i < h->num_gpus; ++i)
    {
        DeviceData* dd;

        /* Get chunk index from GPU ID & chunk start. Stop if out of range. */
        i_chunk = i_chunk_start + i;
        if (i_chunk >= h->num_chunks || *status) break;
        dd = &d[i];

        /* Get the size of the chunk. */
        chunk_sources = h->max_chunk_size;
        if ((i_chunk + 1) * h->max_chunk_size > h->num_pixels)
            chunk_sources = h->num_pixels - i_chunk * h->max_chunk_size;
        chunk_size = chunk_sources * h->num_active_stations;

        /* Write non-averaged raw data, if required. */
        write_pixels(h, i_chunk, i_time, i_channel, chunk_sources, 0, 0,
                dd->jones_data_cpu[!i_active], JONES_DATA, -1, status);

        /* Loop over Stokes parameters. */
        for (stokes = 0; stokes < 4; ++stokes)
        {
            /* Write non-averaged data, if required. */
            write_pixels(h, i_chunk, i_time, i_channel, chunk_sources, 0, 0,
                    dd->auto_power_cpu[stokes][!i_active],
                    AUTO_POWER_DATA, stokes, status);
            write_pixels(h, i_chunk, i_time, i_channel, chunk_sources, 0, 0,
                    dd->cross_power_cpu[stokes][!i_active],
                    CROSS_POWER_DATA, stokes, status);

            /* Time-average the data if required. */
            if (dd->auto_power_time_avg[stokes])
                oskar_mem_add(dd->auto_power_time_avg[stokes],
                        dd->auto_power_time_avg[stokes],
                        dd->auto_power_cpu[stokes][!i_active], chunk_size,
                        status);
            if (dd->cross_power_time_avg[stokes])
                oskar_mem_add(dd->cross_power_time_avg[stokes],
                        dd->cross_power_time_avg[stokes],
                        dd->cross_power_cpu[stokes][!i_active], chunk_sources,
                        status);

            /* Channel-average the data if required. */
            if (dd->auto_power_channel_avg[stokes])
                oskar_mem_add(dd->auto_power_channel_avg[stokes],
                        dd->auto_power_channel_avg[stokes],
                        dd->auto_power_cpu[stokes][!i_active], chunk_size,
                        status);
            if (dd->cross_power_channel_avg[stokes])
                oskar_mem_add(dd->cross_power_channel_avg[stokes],
                        dd->cross_power_channel_avg[stokes],
                        dd->cross_power_cpu[stokes][!i_active], chunk_sources,
                        status);

            /* Channel- and time-average the data if required. */
            if (dd->auto_power_channel_and_time_avg[stokes])
                oskar_mem_add(dd->auto_power_channel_and_time_avg[stokes],
                        dd->auto_power_channel_and_time_avg[stokes],
                        dd->auto_power_cpu[stokes][!i_active], chunk_size,
                        status);
            if (dd->cross_power_channel_and_time_avg[stokes])
                oskar_mem_add(dd->cross_power_channel_and_time_avg[stokes],
                        dd->cross_power_channel_and_time_avg[stokes],
                        dd->cross_power_cpu[stokes][!i_active], chunk_sources,
                        status);

            /* Write time-averaged data. */
            if (i_time == h->num_times - 1)
            {
                if (dd->auto_power_time_avg[stokes])
                {
                    oskar_mem_scale_real(dd->auto_power_time_avg[stokes],
                            1.0 / h->num_times, status);
                    write_pixels(h, i_chunk, 0, i_channel, chunk_sources, 0, 1,
                            dd->auto_power_time_avg[stokes],
                            AUTO_POWER_DATA, stokes, status);
                    oskar_mem_clear_contents(dd->auto_power_time_avg[stokes],
                            status);
                }
                if (dd->cross_power_time_avg[stokes])
                {
                    oskar_mem_scale_real(dd->cross_power_time_avg[stokes],
                            1.0 / h->num_times, status);
                    write_pixels(h, i_chunk, 0, i_channel, chunk_sources, 0, 1,
                            dd->cross_power_time_avg[stokes],
                            CROSS_POWER_DATA, stokes, status);
                    oskar_mem_clear_contents(dd->cross_power_time_avg[stokes],
                            status);
                }
            }

            /* Write channel-averaged data. */
            if (i_channel == h->num_channels - 1)
            {
                if (dd->auto_power_channel_avg[stokes])
                {
                    oskar_mem_scale_real(dd->auto_power_channel_avg[stokes],
                            1.0 / h->num_channels, status);
                    write_pixels(h, i_chunk, i_time, 0, chunk_sources, 1, 0,
                            dd->auto_power_channel_avg[stokes],
                            AUTO_POWER_DATA, stokes, status);
                    oskar_mem_clear_contents(dd->auto_power_channel_avg[stokes],
                            status);
                }
                if (dd->cross_power_channel_avg[stokes])
                {
                    oskar_mem_scale_real(dd->cross_power_channel_avg[stokes],
                            1.0 / h->num_channels, status);
                    write_pixels(h, i_chunk, i_time, 0, chunk_sources, 1, 0,
                            dd->cross_power_channel_avg[stokes],
                            CROSS_POWER_DATA, stokes, status);
                    oskar_mem_clear_contents(
                            dd->cross_power_channel_avg[stokes], status);
                }
            }

            /* Write channel- and time-averaged data. */
            if ((i_time == h->num_times - 1) &&
                    (i_channel == h->num_channels - 1))
            {
                if (dd->auto_power_channel_and_time_avg[stokes])
                {
                    oskar_mem_scale_real(
                            dd->auto_power_channel_and_time_avg[stokes],
                            1.0 / (h->num_channels * h->num_times), status);
                    write_pixels(h, i_chunk, 0, 0, chunk_sources, 1, 1,
                            dd->auto_power_channel_and_time_avg[stokes],
                            AUTO_POWER_DATA, stokes, status);
                    oskar_mem_clear_contents(
                            dd->auto_power_channel_and_time_avg[stokes],
                            status);
                }
                if (dd->cross_power_channel_and_time_avg[stokes])
                {
                    oskar_mem_scale_real(
                            dd->cross_power_channel_and_time_avg[stokes],
                            1.0 / (h->num_channels * h->num_times), status);
                    write_pixels(h, i_chunk, 0, 0, chunk_sources, 1, 1,
                            dd->cross_power_channel_and_time_avg[stokes],
                            CROSS_POWER_DATA, stokes, status);
                    oskar_mem_clear_contents(
                            dd->cross_power_channel_and_time_avg[stokes],
                            status);
                }
            }
        }
    }
    oskar_timer_pause(h->tmr_write);
}


static void write_pixels(HostData* h, int i_chunk, int i_time, int i_channel,
        int num_pix, int channel_average, int time_average,
        const oskar_Mem* in, int chunk_desc, int stokes_in, int* status)
{
    int i, num_pol;
    if (!in) return;

    /* Loop over data products. */
    num_pol = h->pol_mode == OSKAR_POL_MODE_FULL ? 4 : 1;
    for (i = 0; i < h->num_data_products; ++i)
    {
        fitsfile* f;
        FILE* t;
        int dp, stokes_out, i_station, off;

        /* Get data product info. */
        f          = h->data_products[i].fits_file;
        t          = h->data_products[i].text_file;
        dp         = h->data_products[i].type;
        stokes_out = h->data_products[i].stokes_out;
        i_station  = h->data_products[i].i_station;

        /* Check averaging mode and polarisation input type. */
        if (h->data_products[i].time_average != time_average ||
                h->data_products[i].channel_average != channel_average ||
                h->data_products[i].stokes_in != stokes_in)
            continue;

        /* Treat raw data output as special case, as it doesn't go via pix. */
        if (dp == RAW_COMPLEX && chunk_desc == JONES_DATA && t)
        {
            oskar_Mem* station_data;
            station_data = oskar_mem_create_alias(in, i_station * num_pix,
                    num_pix, status);
            oskar_mem_save_ascii(t, 1, num_pix, status, station_data);
            oskar_mem_free(station_data, status);
            continue;
        }
        if (dp == CROSS_POWER_RAW_COMPLEX &&
                chunk_desc == CROSS_POWER_DATA && t)
        {
            oskar_mem_save_ascii(t, 1, num_pix, status, in);
            continue;
        }

        /* Convert complex values to pixel data. */
        oskar_mem_clear_contents(h->pix, status);
        if (chunk_desc == JONES_DATA && dp == AMP)
        {
            off = i_station * num_pix * num_pol;
            if (stokes_out == XX || stokes_out == -1)
                complex_to_amp(in, off, num_pol, num_pix, h->pix, status);
            else if (stokes_out == XY)
                complex_to_amp(in, off + 1, num_pol, num_pix, h->pix, status);
            else if (stokes_out == YX)
                complex_to_amp(in, off + 2, num_pol, num_pix, h->pix, status);
            else if (stokes_out == YY)
                complex_to_amp(in, off + 3, num_pol, num_pix, h->pix, status);
            else continue;
        }
        else if (chunk_desc == JONES_DATA && dp == PHASE)
        {
            off = i_station * num_pix * num_pol;
            if (stokes_out == XX || stokes_out == -1)
                complex_to_phase(in, off, num_pol, num_pix, h->pix, status);
            else if (stokes_out == XY)
                complex_to_phase(in, off + 1, num_pol, num_pix, h->pix, status);
            else if (stokes_out == XY)
                complex_to_phase(in, off + 2, num_pol, num_pix, h->pix, status);
            else if (stokes_out == YX)
                complex_to_phase(in, off + 3, num_pol, num_pix, h->pix, status);
            else continue;
        }
        else if (chunk_desc == JONES_DATA && dp == IXR)
            jones_to_ixr(in, i_station * num_pix, num_pix, h->pix, status);
        else if (chunk_desc == AUTO_POWER_DATA ||
                chunk_desc == CROSS_POWER_DATA)
        {
            off = i_station * num_pix; /* Station offset. */
            if (off < 0 || chunk_desc == CROSS_POWER_DATA) off = 0;
            if (chunk_desc == CROSS_POWER_DATA && dp == AUTO_POWER)
                continue;
            if (chunk_desc == AUTO_POWER_DATA &&
                    (dp == CROSS_POWER_AMP || dp == CROSS_POWER_PHASE))
                continue;
            if (stokes_out == I)
                power_to_stokes_I(in, off, num_pix, h->ctemp, status);
            else if (stokes_out == Q)
                power_to_stokes_Q(in, off, num_pix, h->ctemp, status);
            else if (stokes_out == U)
                power_to_stokes_U(in, off, num_pix, h->ctemp, status);
            else if (stokes_out == V)
                power_to_stokes_V(in, off, num_pix, h->ctemp, status);
            else continue;
            if (dp == AUTO_POWER || dp == CROSS_POWER_AMP)
                complex_to_amp(h->ctemp, 0, 1, num_pix, h->pix, status);
            else if (dp == CROSS_POWER_PHASE)
                complex_to_phase(h->ctemp, 0, 1, num_pix, h->pix, status);
            else continue;
        }
        else continue;

        /* Check for FITS file. */
        if (f && h->width && h->height)
        {
            long firstpix[4];
            firstpix[0] = 1 + (i_chunk * h->max_chunk_size) % h->width;
            firstpix[1] = 1 + (i_chunk * h->max_chunk_size) / h->width;
            firstpix[2] = 1 + i_channel;
            firstpix[3] = 1 + i_time;
            fits_write_pix(f, (h->precision == OSKAR_DOUBLE ? TDOUBLE : TFLOAT),
                    firstpix, num_pix, oskar_mem_void(h->pix), status);
        }

        /* Check for text file. */
        if (t) oskar_mem_save_ascii(t, 1, num_pix, status, h->pix);
    }
}


static void complex_to_amp(const oskar_Mem* complex_in, const int offset,
        const int stride, const int num_points, oskar_Mem* output, int* status)
{
    int i, j;
    if (oskar_mem_precision(output) == OSKAR_SINGLE)
    {
        float *out, x, y;
        const float2* in;
        in = oskar_mem_float2_const(complex_in, status) + offset;
        out = oskar_mem_float(output, status);
        for (i = 0; i < num_points; ++i)
        {
            j = i * stride;
            x = in[j].x;
            y = in[j].y;
            out[i] = sqrt(x*x + y*y);
        }
    }
    else
    {
        double *out, x, y;
        const double2* in;
        in = oskar_mem_double2_const(complex_in, status) + offset;
        out = oskar_mem_double(output, status);
        for (i = 0; i < num_points; ++i)
        {
            j = i * stride;
            x = in[j].x;
            y = in[j].y;
            out[i] = sqrt(x*x + y*y);
        }
    }
}


static void complex_to_phase(const oskar_Mem* complex_in, const int offset,
        const int stride, const int num_points, oskar_Mem* output, int* status)
{
    int i, j;
    if (oskar_mem_precision(output) == OSKAR_SINGLE)
    {
        float *out;
        const float2* in;
        in = oskar_mem_float2_const(complex_in, status) + offset;
        out = oskar_mem_float(output, status);
        for (i = 0; i < num_points; ++i)
        {
            j = i * stride;
            out[i] = atan2(in[j].y, in[j].x);
        }
    }
    else
    {
        double *out;
        const double2* in;
        in = oskar_mem_double2_const(complex_in, status) + offset;
        out = oskar_mem_double(output, status);
        for (i = 0; i < num_points; ++i)
        {
            j = i * stride;
            out[i] = atan2(in[j].y, in[j].x);
        }
    }
}


static void jones_to_ixr(const oskar_Mem* jones, const int offset,
        const int num_points, oskar_Mem* output, int* status)
{
    int i;

    /* Check for fully polarised data. */
    if (!oskar_mem_is_matrix(jones) || !oskar_mem_is_complex(jones)) return;

    if (oskar_mem_precision(output) == OSKAR_SINGLE)
    {
        float *out, cond, ixr;
        const float4c* in;
        in = oskar_mem_float4c_const(jones, status) + offset;
        out = oskar_mem_float(output, status);
        for (i = 0; i < num_points; ++i)
        {
            cond = oskar_cond2_2x2_inline_f(in + i);
            ixr = (cond + 1.0f) / (cond - 1.0f);
            ixr *= ixr;
            if (ixr > 1e6) ixr = 1e6;
            out[i] = ixr;
        }
    }
    else
    {
        double *out, cond, ixr;
        const double4c* in;
        in = oskar_mem_double4c_const(jones, status) + offset;
        out = oskar_mem_double(output, status);
        for (i = 0; i < num_points; ++i)
        {
            cond = oskar_cond2_2x2_inline_d(in + i);
            ixr = (cond + 1.0) / (cond - 1.0);
            ixr *= ixr;
            if (ixr > 1e8) ixr = 1e8;
            out[i] = ixr;
        }
    }
}


static void power_to_stokes_I(const oskar_Mem* power_in, const int offset,
        const int num_points, oskar_Mem* output, int* status)
{
    /* Both arrays must be complex: this allows cross-power Stokes I. */
    if (!oskar_mem_is_complex(power_in) || !oskar_mem_is_complex(output))
        return;

    /* Generate 0.5 * (XX + YY) from input. */
    if (!oskar_mem_is_matrix(power_in))
        oskar_mem_copy_contents(output, power_in, 0, offset, num_points,
                status);
    else
    {
        int i;

        if (oskar_mem_is_double(power_in))
        {
            double2* out;
            const double4c* in;
            out = oskar_mem_double2(output, status);
            in = oskar_mem_double4c_const(power_in, status) + offset;
            for (i = 0; i < num_points; ++i)
            {
                out[i].x = 0.5 * (in[i].a.x + in[i].d.x);
                out[i].y = 0.5 * (in[i].a.y + in[i].d.y);
            }
        }
        else
        {
            float2* out;
            const float4c* in;
            out = oskar_mem_float2(output, status);
            in = oskar_mem_float4c_const(power_in, status) + offset;
            for (i = 0; i < num_points; ++i)
            {
                out[i].x = 0.5 * (in[i].a.x + in[i].d.x);
                out[i].y = 0.5 * (in[i].a.y + in[i].d.y);
            }
        }
    }
}


static void power_to_stokes_Q(const oskar_Mem* power_in, const int offset,
        const int num_points, oskar_Mem* output, int* status)
{
    int i;

    /* Both arrays must be complex: this allows cross-power Stokes Q. */
    if (!oskar_mem_is_complex(power_in) || !oskar_mem_is_matrix(power_in) ||
            !oskar_mem_is_complex(output))
        return;

    /* Generate 0.5 * (XX - YY) from input. */
    if (oskar_mem_is_double(power_in))
    {
        double2* out;
        const double4c* in;
        out = oskar_mem_double2(output, status);
        in = oskar_mem_double4c_const(power_in, status) + offset;
        for (i = 0; i < num_points; ++i)
        {
            out[i].x = 0.5 * (in[i].a.x - in[i].d.x);
            out[i].y = 0.5 * (in[i].a.y - in[i].d.y);
        }
    }
    else
    {
        float2* out;
        const float4c* in;
        out = oskar_mem_float2(output, status);
        in = oskar_mem_float4c_const(power_in, status) + offset;
        for (i = 0; i < num_points; ++i)
        {
            out[i].x = 0.5 * (in[i].a.x - in[i].d.x);
            out[i].y = 0.5 * (in[i].a.y - in[i].d.y);
        }
    }
}


static void power_to_stokes_U(const oskar_Mem* power_in, const int offset,
        const int num_points, oskar_Mem* output, int* status)
{
    int i;

    /* Both arrays must be complex: this allows cross-power Stokes U. */
    if (!oskar_mem_is_complex(power_in) || !oskar_mem_is_matrix(power_in) ||
            !oskar_mem_is_complex(output))
        return;

    /* Generate 0.5 * (XY + YX) from input. */
    if (oskar_mem_is_double(power_in))
    {
        double2* out;
        const double4c* in;
        out = oskar_mem_double2(output, status);
        in = oskar_mem_double4c_const(power_in, status) + offset;
        for (i = 0; i < num_points; ++i)
        {
            out[i].x = 0.5 * (in[i].b.x + in[i].c.x);
            out[i].y = 0.5 * (in[i].b.y + in[i].c.y);
        }
    }
    else
    {
        float2* out;
        const float4c* in;
        out = oskar_mem_float2(output, status);
        in = oskar_mem_float4c_const(power_in, status) + offset;
        for (i = 0; i < num_points; ++i)
        {
            out[i].x = 0.5 * (in[i].b.x + in[i].c.x);
            out[i].y = 0.5 * (in[i].b.y + in[i].c.y);
        }
    }
}


static void power_to_stokes_V(const oskar_Mem* power_in, const int offset,
        const int num_points, oskar_Mem* output, int* status)
{
    int i;

    /* Both arrays must be complex: this allows cross-power Stokes V. */
    if (!oskar_mem_is_complex(power_in) || !oskar_mem_is_matrix(power_in) ||
            !oskar_mem_is_complex(output))
        return;

    /* Generate -0.5i * (XY - YX) from input. */
    if (oskar_mem_is_double(power_in))
    {
        double2* out;
        const double4c* in;
        out = oskar_mem_double2(output, status);
        in = oskar_mem_double4c_const(power_in, status) + offset;
        for (i = 0; i < num_points; ++i)
        {
            out[i].x =  0.5 * (in[i].b.y - in[i].c.y);
            out[i].y = -0.5 * (in[i].b.x - in[i].c.x);
        }
    }
    else
    {
        float2* out;
        const float4c* in;
        out = oskar_mem_float2(output, status);
        in = oskar_mem_float4c_const(power_in, status) + offset;
        for (i = 0; i < num_points; ++i)
        {
            out[i].x =  0.5 * (in[i].b.y - in[i].c.y);
            out[i].y = -0.5 * (in[i].b.x - in[i].c.x);
        }
    }
}


static void set_up_host_data(HostData* h, oskar_Log* log, int *status)
{
    int i, k, items = 0, *tmp_i;
    size_t j;
    const char* r;
    double *tmp_d;

    /* Set up telescope model. */
    h->tel = oskar_set_up_telescope(&h->s, log, status);
    oskar_telescope_analyse(h->tel, status);
    if (*status) return;

    /* Get values from settings. */
    h->precision = oskar_settings_to_int(h->s2, "simulator/double_precision",
            status) ? OSKAR_DOUBLE : OSKAR_SINGLE;
    h->max_chunk_size = oskar_settings_to_int(h->s2,
            "simulator/max_sources_per_chunk", status);
    oskar_settings_begin_group(h->s2, "beam_pattern");
    h->root_path = oskar_settings_to_string(h->s2, "root_path", status);
    r = h->root_path;
    h->coord_frame_type =
            oskar_settings_first_letter(h->s2, "coordinate_frame", status);
    h->coord_grid_type =
            oskar_settings_first_letter(h->s2, "coordinate_type", status);
    oskar_settings_begin_group(h->s2, "beam_image");
    tmp_i = oskar_settings_to_int_list(h->s2, "size", &items, status);
    if (items == 1)
    {
        h->width = tmp_i[0];
        h->height = tmp_i[0];
    }
    else if (items > 1)
    {
        h->width = tmp_i[0];
        h->height = tmp_i[1];
    }
    free(tmp_i);
    tmp_d = oskar_settings_to_double_list(h->s2, "fov_deg", &items, status);
    if (items == 1)
    {
        h->fov_deg[0] = tmp_d[0];
        h->fov_deg[1] = tmp_d[0];
    }
    else if (items > 1)
    {
        h->fov_deg[0] = tmp_d[0];
        h->fov_deg[1] = tmp_d[1];
    }
    free(tmp_d);
    oskar_settings_end_group(h->s2); /* End beam_image */
    oskar_settings_begin_group(h->s2, "output");
    h->separate_time_and_channel = oskar_settings_to_int(h->s2,
            "separate_time_and_channel", status);
    h->average_time_and_channel = oskar_settings_to_int(h->s2,
            "average_time_and_channel", status);
    h->average_single_axis = oskar_settings_first_letter(h->s2,
            "average_single_axis", status);
    oskar_settings_end_group(h->s2); /* End output */
    oskar_settings_end_group(h->s2); /* End beam_pattern */

    oskar_settings_begin_group(h->s2, "observation");
    h->num_times = oskar_settings_to_int(h->s2, "num_time_steps", status);
    h->num_channels = oskar_settings_to_int(h->s2, "num_channels", status);
    h->start_mjd_utc = oskar_settings_to_double(h->s2,
            "start_time_utc", status);
    h->start_freq_hz = oskar_settings_to_double(h->s2,
            "start_frequency_hz", status);
    h->length_sec = oskar_settings_to_double(h->s2, "length", status);
    h->delta_t = h->length_sec / h->num_times;
    h->delta_f = oskar_settings_to_double(h->s2, "frequency_inc_hz", status);
    h->phase_centre_deg[0] = oskar_settings_to_double(h->s2,
            "phase_centre_ra_deg", status);
    h->phase_centre_deg[1] = oskar_settings_to_double(h->s2,
            "phase_centre_dec_deg", status);
    oskar_settings_end_group(h->s2); /* End observation */

    /* Check for settings errors. */
    if (*status)
    {
        oskar_log_error(log, "Unable to read settings (code = %d).", *status);
        return;
    }

    /*
    h->precision = s->sim.double_precision ? OSKAR_DOUBLE : OSKAR_SINGLE;
    h->width                     = s->beam_pattern.size[0];
    h->height                    = s->beam_pattern.size[1];
    h->phase_centre_deg[0]       = s->obs.phase_centre_lon_rad[0] * RAD2DEG;
    h->phase_centre_deg[1]       = s->obs.phase_centre_lat_rad[0] * RAD2DEG;
    h->fov_deg[0]                = s->beam_pattern.fov_deg[0];
    h->fov_deg[1]                = s->beam_pattern.fov_deg[1];
    h->separate_time_and_channel = s->beam_pattern.separate_time_and_channel;
    h->average_time_and_channel  = s->beam_pattern.average_time_and_channel;
    h->average_single_axis       = s->beam_pattern.average_single_axis;
    h->num_times                 = s->obs.num_time_steps;
    h->num_channels              = s->obs.num_channels;
    h->start_mjd_utc             = s->obs.start_mjd_utc;
    h->start_freq_hz             = s->obs.start_frequency_hz;
    h->delta_t                   = s->obs.dt_dump_days * 86400.0;
    h->delta_f                   = s->obs.frequency_inc_hz;
    h->max_chunk_size            = s->sim.max_sources_per_chunk;
    r                            = s->beam_pattern.root_path;
    */
    h->pol_mode                  = oskar_telescope_pol_mode(h->tel);

    /* Check root name exists. */
    if (!r)
    {
        oskar_log_error(log, "No output file name specified.");
        *status = OSKAR_ERR_SETTINGS_BEAM_PATTERN;
        return;
    }

    /* Get station ID(s) and simulation flags. */
    h->num_active_stations = h->s.beam_pattern.num_active_stations;
    if (h->num_active_stations <= 0)
    {
        h->num_active_stations = oskar_telescope_num_stations(h->tel);
        h->station_ids = calloc(h->num_active_stations, sizeof(int));
        for (i = 0; i < h->num_active_stations; ++i) h->station_ids[i] = i;
    }
    else
    {
        h->station_ids = calloc(h->num_active_stations, sizeof(int));
        for (i = 0; i < h->num_active_stations; ++i)
            h->station_ids[i] = h->s.beam_pattern.station_ids[i];
    }
    oskar_settings_begin_group(h->s2, "beam_pattern/station_outputs");
    h->raw_data =
            oskar_settings_to_int(h->s2, "text_file/raw_complex", status) ||
            oskar_settings_to_int(h->s2, "text_file/amp", status) ||
            oskar_settings_to_int(h->s2, "text_file/phase", status) ||
            oskar_settings_to_int(h->s2, "fits_image/amp", status) ||
            oskar_settings_to_int(h->s2, "fits_image/phase", status);
#ifdef ENABLE_IXR
    h->raw_data |=
            oskar_settings_to_int(h->s2, "text_file/ixr", status) ||
            oskar_settings_to_int(h->s2, "fits_image/ixr", status);
#endif
    oskar_settings_end_group(h->s2);
    oskar_settings_begin_group(h->s2, "beam_pattern/telescope_outputs");
    h->cross_power[I] =
            oskar_settings_to_int(h->s2,
                    "fits_image/cross_power_stokes_i_amp", status) ||
            oskar_settings_to_int(h->s2,
                    "fits_image/cross_power_stokes_i_phase", status) ||
            oskar_settings_to_int(h->s2,
                    "text_file/cross_power_stokes_i_amp", status) ||
            oskar_settings_to_int(h->s2,
                    "text_file/cross_power_stokes_i_phase", status) ||
            oskar_settings_to_int(h->s2,
                    "text_file/cross_power_stokes_i_raw_complex", status);
#if 0
    h->cross_power[Q] =
            oskar_settings_to_int(h->s2,
                    "fits_image/cross_power_stokes_q_amp", status) ||
            oskar_settings_to_int(h->s2,
                    "fits_image/cross_power_stokes_q_phase", status) ||
            oskar_settings_to_int(h->s2,
                    "text_file/cross_power_stokes_q_amp", status) ||
            oskar_settings_to_int(h->s2,
                    "text_file/cross_power_stokes_q_phase", status) ||
            oskar_settings_to_int(h->s2,
                    "text_file/cross_power_stokes_q_raw_complex", status);
    h->cross_power[U] =
            oskar_settings_to_int(h->s2,
                    "fits_image/cross_power_stokes_u_amp", status) ||
            oskar_settings_to_int(h->s2,
                    "fits_image/cross_power_stokes_u_phase", status) ||
            oskar_settings_to_int(h->s2,
                    "text_file/cross_power_stokes_u_amp", status) ||
            oskar_settings_to_int(h->s2,
                    "text_file/cross_power_stokes_u_phase", status) ||
            oskar_settings_to_int(h->s2,
                    "text_file/cross_power_stokes_u_raw_complex", status);
    h->cross_power[V] =
            oskar_settings_to_int(h->s2,
                    "fits_image/cross_power_stokes_v_amp", status) ||
            oskar_settings_to_int(h->s2,
                    "fits_image/cross_power_stokes_v_phase", status) ||
            oskar_settings_to_int(h->s2,
                    "text_file/cross_power_stokes_v_amp", status) ||
            oskar_settings_to_int(h->s2,
                    "text_file/cross_power_stokes_v_phase", status) ||
            oskar_settings_to_int(h->s2,
                    "text_file/cross_power_stokes_v_raw_complex", status);
#endif
    oskar_settings_end_group(h->s2);
    oskar_settings_begin_group(h->s2, "beam_pattern/station_outputs");
    h->auto_power[I] =
            oskar_settings_to_int(h->s2,
                    "fits_image/auto_power_stokes_i", status) ||
            oskar_settings_to_int(h->s2,
                    "text_file/auto_power_stokes_i", status);
#if 0
    h->auto_power[Q] =
            oskar_settings_to_int(h->s2,
                    "fits_image/auto_power_stokes_q", status) ||
            oskar_settings_to_int(h->s2,
                    "text_file/auto_power_stokes_q", status);
    h->auto_power[U] =
            oskar_settings_to_int(h->s2,
                    "fits_image/auto_power_stokes_u", status) ||
            oskar_settings_to_int(h->s2,
                    "text_file/auto_power_stokes_u", status);
    h->auto_power[V] =
            oskar_settings_to_int(h->s2,
                    "fits_image/auto_power_stokes_v", status) ||
            oskar_settings_to_int(h->s2,
                    "text_file/auto_power_stokes_v", status);
#endif
    oskar_settings_end_group(h->s2);

    /* Check settings make logical sense. */
    if (h->num_active_stations < 2 && (h->cross_power[I] ||
            h->cross_power[Q] || h->cross_power[U] || h->cross_power[V]))
    {
        oskar_log_error(log, "Cannot create cross-power beam "
                "using less than two active stations.");
        *status = OSKAR_ERR_SETTINGS_BEAM_PATTERN;
        return;
    }

    /* Set up pixel positions. */
    h->x = oskar_mem_create(h->precision, OSKAR_CPU, 0, status);
    h->y = oskar_mem_create(h->precision, OSKAR_CPU, 0, status);
    h->z = oskar_mem_create(h->precision, OSKAR_CPU, 0, status);
    h->num_pixels = oskar_beam_pattern_generate_coordinates(
            OSKAR_SPHERICAL_TYPE_EQUATORIAL,
            oskar_telescope_phase_centre_ra_rad(h->tel),
            oskar_telescope_phase_centre_dec_rad(h->tel),
            h->s2, &h->coord_type, &h->lon0, &h->lat0,
            h->x, h->y, h->z, status);

    /* Work out how many pixel chunks have to be processed. */
    h->num_chunks = (h->num_pixels + h->max_chunk_size - 1) / h->max_chunk_size;

    /* Create scratch arrays for output pixel data. */
    h->pix = oskar_mem_create(h->precision, OSKAR_CPU,
            h->max_chunk_size, status);
    h->ctemp = oskar_mem_create(h->precision | OSKAR_COMPLEX, OSKAR_CPU,
            h->max_chunk_size, status);

    /* Get the contents of the log at this point so we can write a
     * reasonable file header. Replace newlines with zeros. */
    h->settings_log = oskar_log_file_data(log, &h->settings_log_length);
    for (j = 0; j < h->settings_log_length; ++j)
    {
        if (h->settings_log[j] == '\n') h->settings_log[j] = 0;
        if (h->settings_log[j] == '\r') h->settings_log[j] = ' ';
    }

    /* Create a file for each requested data product. */
    /* Voltage amplitude and phase can only be generated if there is
     * no averaging. */
    if (h->separate_time_and_channel)
    {
        /* Create station-level data products. */
        oskar_settings_begin_group(h->s2, "beam_pattern");
        oskar_settings_begin_group(h->s2, "station_outputs");
        for (i = 0; i < h->num_active_stations; ++i)
        {
            /* Text file. */
            oskar_settings_begin_group(h->s2, "text_file");
            if (oskar_settings_to_int(h->s2, "raw_complex", status))
                new_text_file(h, RAW_COMPLEX, -1, -1, i, 0, 0, r, status);
            if (oskar_settings_to_int(h->s2, "amp", status))
            {
                if (h->pol_mode == OSKAR_POL_MODE_SCALAR)
                    new_text_file(h, AMP, -1, -1, i, 0, 0, r, status);
                else for (k = XX; k <= YY; ++k)
                    new_text_file(h, AMP, -1, k, i, 0, 0, r, status);
            }
            if (oskar_settings_to_int(h->s2, "phase", status))
            {
                if (h->pol_mode == OSKAR_POL_MODE_SCALAR)
                    new_text_file(h, PHASE, -1, -1, i, 0, 0, r, status);
                else for (k = XX; k <= YY; ++k)
                    new_text_file(h, PHASE, -1, k, i, 0, 0, r, status);
            }
#ifdef ENABLE_IXR
            if (oskar_settings_to_int(h->s2, "ixr", status) &&
                    h->pol_mode == OSKAR_POL_MODE_FULL)
                new_text_file(h, IXR, -1, -1, i, 0, 0, r, status);
#endif
            oskar_settings_end_group(h->s2); /* End text_file */

            /* Can only create images if coordinates are on a grid. */
            if (h->coord_grid_type != 'B') continue;

            /* FITS file. */
            oskar_settings_begin_group(h->s2, "fits_image");
            if (oskar_settings_to_int(h->s2, "amp", status))
            {
                if (h->pol_mode == OSKAR_POL_MODE_SCALAR)
                    new_fits_file(h, AMP, -1, -1, i, 0, 0, r, status);
                else for (k = XX; k <= YY; ++k)
                    new_fits_file(h, AMP, -1, k, i, 0, 0, r, status);
            }
            if (oskar_settings_to_int(h->s2, "phase", status))
            {
                if (h->pol_mode == OSKAR_POL_MODE_SCALAR)
                    new_fits_file(h, PHASE, -1, -1, i, 0, 0, r, status);
                else for (k = XX; k <= YY; ++k)
                    new_fits_file(h, PHASE, -1, k, i, 0, 0, r, status);
            }
#ifdef ENABLE_IXR
            if (oskar_settings_to_int(h->s2, "ixr", status) &&
                    h->pol_mode == OSKAR_POL_MODE_FULL)
                new_fits_file(h, IXR, -1, -1, i, 0, 0, r, status);
#endif
            oskar_settings_end_group(h->s2); /* End fits_image */
        } /* End loop over stations. */
        oskar_settings_end_group(h->s2); /* End station_outputs */
        oskar_settings_end_group(h->s2); /* End beam_pattern */
    } /* End check on averaging mode. */

    /* Create data products that can be averaged. */
    if (h->separate_time_and_channel)
        create_averaged_products(h, 0, 0, status);
    if (h->average_time_and_channel)
        create_averaged_products(h, 1, 1, status);
    if (h->average_single_axis == 'C')
        create_averaged_products(h, 0, 1, status);
    else if (h->average_single_axis == 'T')
        create_averaged_products(h, 1, 0, status);

    /* Check that at least one output file will be generated. */
    if (h->num_data_products == 0 && !*status)
    {
        *status = OSKAR_ERR_SETTINGS_BEAM_PATTERN;
        oskar_log_error(log, "No output file(s) selected.");
    }
}


static void create_averaged_products(HostData* h, int ta, int ca, int* status)
{
    int i, k;
    const char* r;

    /* Create station-level data products that can be averaged. */
    if (*status) return;
    r = h->root_path;
    oskar_settings_begin_group(h->s2, "beam_pattern");
    oskar_settings_begin_group(h->s2, "station_outputs");
    for (i = 0; i < h->num_active_stations; ++i)
    {
        /* Text file. */
        oskar_settings_begin_group(h->s2, "text_file");
        for (k = I; k <= V; ++k)
        {
            if (oskar_settings_to_int(h->s2, "auto_power_stokes_i", status))
                new_text_file(h, AUTO_POWER, I, k, i, ta, ca, r, status);
#if 0
            if (oskar_settings_to_int(h->s2, "auto_power_stokes_q", status))
                new_text_file(h, AUTO_POWER, Q, k, i, ta, ca, r, status);
            if (oskar_settings_to_int(h->s2, "auto_power_stokes_u", status))
                new_text_file(h, AUTO_POWER, U, k, i, ta, ca, r, status);
            if (oskar_settings_to_int(h->s2, "auto_power_stokes_v", status))
                new_text_file(h, AUTO_POWER, V, k, i, ta, ca, r, status);
#endif
        }
        oskar_settings_end_group(h->s2); /* End text_file */

        /* Can only create images if coordinates are on a grid. */
        if (h->coord_grid_type != 'B') continue;

        /* FITS file. */
        oskar_settings_begin_group(h->s2, "fits_image");
        for (k = I; k <= V; ++k)
        {
            if (oskar_settings_to_int(h->s2, "auto_power_stokes_i", status))
                new_fits_file(h, AUTO_POWER, I, k, i, ta, ca, r, status);
#if 0
            if (oskar_settings_to_int(h->s2, "auto_power_stokes_q", status))
                new_fits_file(h, AUTO_POWER, Q, k, i, ta, ca, r, status);
            if (oskar_settings_to_int(h->s2, "auto_power_stokes_u", status))
                new_fits_file(h, AUTO_POWER, U, k, i, ta, ca, r, status);
            if (oskar_settings_to_int(h->s2, "auto_power_stokes_v", status))
                new_fits_file(h, AUTO_POWER, V, k, i, ta, ca, r, status);
#endif
        }
        oskar_settings_end_group(h->s2); /* End fits_image */
    } /* End loop over stations. */
    oskar_settings_end_group(h->s2); /* End station_outputs */

    /* Text file. */
    oskar_settings_begin_group(h->s2, "telescope_outputs");
    oskar_settings_begin_group(h->s2, "text_file");
    if (oskar_settings_to_int(h->s2,
            "cross_power_stokes_i_raw_complex", status))
        new_text_file(h, CROSS_POWER_RAW_COMPLEX, I, -1, -1, ta, ca, r, status);
#if 0
    if (oskar_settings_to_int(h->s2,
            "cross_power_stokes_q_raw_complex", status))
        new_text_file(h, CROSS_POWER_RAW_COMPLEX, Q, -1, -1, ta, ca, r, status);
    if (oskar_settings_to_int(h->s2,
            "cross_power_stokes_u_raw_complex", status))
        new_text_file(h, CROSS_POWER_RAW_COMPLEX, U, -1, -1, ta, ca, r, status);
    if (oskar_settings_to_int(h->s2,
            "cross_power_stokes_v_raw_complex", status))
        new_text_file(h, CROSS_POWER_RAW_COMPLEX, V, -1, -1, ta, ca, r, status);
#endif
    for (k = I; k <= V; ++k)
    {
        if (oskar_settings_to_int(h->s2, "cross_power_stokes_i_amp", status))
            new_text_file(h, CROSS_POWER_AMP, I, k, -1, ta, ca, r, status);
        if (oskar_settings_to_int(h->s2, "cross_power_stokes_i_phase", status))
            new_text_file(h, CROSS_POWER_PHASE, I, k, -1, ta, ca, r, status);
#if 0
        if (oskar_settings_to_int(h->s2, "cross_power_stokes_q_amp", status))
            new_text_file(h, CROSS_POWER_AMP, Q, k, -1, ta, ca, r, status);
        if (oskar_settings_to_int(h->s2, "cross_power_stokes_q_phase", status))
            new_text_file(h, CROSS_POWER_PHASE, Q, k, -1, ta, ca, r, status);
        if (oskar_settings_to_int(h->s2, "cross_power_stokes_u_amp", status))
            new_text_file(h, CROSS_POWER_AMP, U, k, -1, ta, ca, r, status);
        if (oskar_settings_to_int(h->s2, "cross_power_stokes_u_phase", status))
            new_text_file(h, CROSS_POWER_PHASE, U, k, -1, ta, ca, r, status);
        if (oskar_settings_to_int(h->s2, "cross_power_stokes_v_amp", status))
            new_text_file(h, CROSS_POWER_AMP, V, k, -1, ta, ca, r, status);
        if (oskar_settings_to_int(h->s2, "cross_power_stokes_v_phase", status))
            new_text_file(h, CROSS_POWER_PHASE, V, k, -1, ta, ca, r, status);
#endif
    }
    oskar_settings_end_group(h->s2); /* End text_file */

    /* Can only create images if coordinates are on a grid. */
    if (h->coord_grid_type == 'B')
    {
        /* FITS file. */
        oskar_settings_begin_group(h->s2, "fits_image");
        for (k = I; k <= V; ++k)
        {
            if (oskar_settings_to_int(h->s2, "cross_power_stokes_i_amp", status))
                new_fits_file(h, CROSS_POWER_AMP, I, k, -1, ta, ca, r, status);
            if (oskar_settings_to_int(h->s2, "cross_power_stokes_i_phase", status))
                new_fits_file(h, CROSS_POWER_PHASE, I, k, -1, ta, ca, r, status);
#if 0
            if (oskar_settings_to_int(h->s2, "cross_power_stokes_q_amp", status))
                new_fits_file(h, CROSS_POWER_AMP, Q, k, -1, ta, ca, r, status);
            if (oskar_settings_to_int(h->s2, "cross_power_stokes_q_phase", status))
                new_fits_file(h, CROSS_POWER_PHASE, Q, k, -1, ta, ca, r, status);
            if (oskar_settings_to_int(h->s2, "cross_power_stokes_u_amp", status))
                new_fits_file(h, CROSS_POWER_AMP, U, k, -1, ta, ca, r, status);
            if (oskar_settings_to_int(h->s2, "cross_power_stokes_u_phase", status))
                new_fits_file(h, CROSS_POWER_PHASE, U, k, -1, ta, ca, r, status);
            if (oskar_settings_to_int(h->s2, "cross_power_stokes_v_amp", status))
                new_fits_file(h, CROSS_POWER_AMP, V, k, -1, ta, ca, r, status);
            if (oskar_settings_to_int(h->s2, "cross_power_stokes_v_phase", status))
                new_fits_file(h, CROSS_POWER_PHASE, V, k, -1, ta, ca, r, status);
#endif
        }
        oskar_settings_end_group(h->s2); /* End fits_image */
    }
    oskar_settings_end_group(h->s2); /* End telescope_outputs */
    oskar_settings_end_group(h->s2); /* End beam_pattern */
}


static void write_axis(fitsfile* fptr, int axis_id, const char* ctype,
        const char* ctype_comment, double crval, double cdelt, double crpix,
        int* status)
{
    char key[FLEN_KEYWORD], value[FLEN_VALUE], comment[FLEN_COMMENT];
    int decimals = 10;
    strncpy(comment, ctype_comment, FLEN_COMMENT-1);
    strncpy(value, ctype, FLEN_VALUE-1);
    fits_make_keyn("CTYPE", axis_id, key, status);
    fits_write_key_str(fptr, key, value, comment, status);
    fits_make_keyn("CRVAL", axis_id, key, status);
    fits_write_key_dbl(fptr, key, crval, decimals, NULL, status);
    fits_make_keyn("CDELT", axis_id, key, status);
    fits_write_key_dbl(fptr, key, cdelt, decimals, NULL, status);
    fits_make_keyn("CRPIX", axis_id, key, status);
    fits_write_key_dbl(fptr, key, crpix, decimals, NULL, status);
    fits_make_keyn("CROTA", axis_id, key, status);
    fits_write_key_dbl(fptr, key, 0.0, decimals, NULL, status);
}


static fitsfile* create_fits_file(const char* filename, int precision,
        int width, int height, int num_times, int num_channels,
        double centre_deg[2], double fov_deg[2], double start_time_mjd,
        double delta_time_sec, double start_freq_hz, double delta_freq_hz,
        int horizon_mode, const char* settings_log, size_t settings_log_length,
        int* status)
{
    int imagetype;
    long naxes[4], naxes_dummy[4] = {1l, 1l, 1l, 1l};
    double delta;
    const double deg2rad = M_PI / 180.0;
    const double rad2deg = 180.0 / M_PI;
    fitsfile* f = 0;
    const char* line;
    size_t length;
    if (*status) return 0;

    /* Create a new FITS file and write the image headers. */
    if (oskar_file_exists(filename)) remove(filename);
    imagetype = (precision == OSKAR_DOUBLE ? DOUBLE_IMG : FLOAT_IMG);
    naxes[0]  = width;
    naxes[1]  = height;
    naxes[2]  = num_channels;
    naxes[3]  = num_times;
    fits_create_file(&f, filename, status);
    fits_create_img(f, imagetype, 4, naxes_dummy, status);
    fits_write_date(f, status);
    fits_write_key_str(f, "TELESCOP",
            "OSKAR " OSKAR_VERSION_STR, NULL, status);

    /* Write axis headers. */
    if (horizon_mode)
    {
        delta = oskar_convert_fov_to_cellsize(M_PI, width);
        write_axis(f, 1, "-----SIN", "Azimuthal angle",
                0.0, -delta * rad2deg, (width + 1) / 2.0, status);
        delta = oskar_convert_fov_to_cellsize(M_PI, height);
        write_axis(f, 2, "-----SIN", "Elevation",
                90.0, delta * rad2deg, (height + 1) / 2.0, status);
    }
    else
    {
        delta = oskar_convert_fov_to_cellsize(fov_deg[0] * deg2rad, width);
        write_axis(f, 1, "RA---SIN", "Right Ascension",
                centre_deg[0], -delta * rad2deg, (width + 1) / 2.0, status);
        delta = oskar_convert_fov_to_cellsize(fov_deg[1] * deg2rad, height);
        write_axis(f, 2, "DEC--SIN", "Declination",
                centre_deg[1], delta * rad2deg, (height + 1) / 2.0, status);
    }
    write_axis(f, 3, "FREQ", "Frequency",
            start_freq_hz, delta_freq_hz, 1.0, status);
    write_axis(f, 4, "UTC", "Time",
            start_time_mjd, delta_time_sec, 1.0, status);

    /* Write other headers. */
    fits_write_key_str(f, "TIMESYS", "UTC", NULL, status);
    fits_write_key_str(f, "TIMEUNIT", "s", "Time axis units", status);
    fits_write_key_dbl(f, "MJD-OBS", start_time_mjd, 10, "Start time", status);
    if (!horizon_mode)
    {
        fits_write_key_dbl(f, "OBSRA", centre_deg[0], 10, "RA", status);
        fits_write_key_dbl(f, "OBSDEC", centre_deg[1], 10, "DEC", status);
    }

    /* Write the settings log up to this point as HISTORY comments. */
    line = settings_log;
    length = settings_log_length;
    for (;;)
    {
        const char* eol;
        fits_write_history(f, line, status);
        eol = (const char*) memchr(line, '\0', length);
        if (!eol) break;
        eol += 1;
        length -= (eol - line);
        line = eol;
    }

    /* Update header keywords with the correct axis lengths.
     * Needs to be done here because CFITSIO doesn't let us write only the
     * file header with the correct axis lengths to start with. This trick
     * allows us to create a small dummy image block to write only the headers,
     * and not waste effort moving a huge block of zeros within the file. */
    fits_update_key_lng(f, "NAXIS1", naxes[0], 0, status);
    fits_update_key_lng(f, "NAXIS2", naxes[1], 0, status);
    fits_update_key_lng(f, "NAXIS3", naxes[2], 0, status);
    fits_update_key_lng(f, "NAXIS4", naxes[3], 0, status);

    return f;
}


static int data_product_index(HostData* h, int data_product_type,
        int stokes_in, int stokes_out, int i_station, int time_average,
        int channel_average)
{
    int i;
    for (i = 0; i < h->num_data_products; ++i)
        if (h->data_products[i].type == data_product_type &&
                h->data_products[i].stokes_in == stokes_in &&
                h->data_products[i].stokes_out == stokes_out &&
                h->data_products[i].i_station == i_station &&
                h->data_products[i].time_average == time_average &&
                h->data_products[i].channel_average == channel_average) break;
    if (i == h->num_data_products)
    {
        i = h->num_data_products++;
        h->data_products = realloc(h->data_products,
                h->num_data_products * sizeof(DataProduct));
        memset(&(h->data_products[i]), 0, sizeof(DataProduct));
        h->data_products[i].type = data_product_type;
        h->data_products[i].stokes_in = stokes_in;
        h->data_products[i].stokes_out = stokes_out;
        h->data_products[i].i_station = i_station;
        h->data_products[i].time_average = time_average;
        h->data_products[i].channel_average = channel_average;
    }
    return i;
}


static char* construct_filename(int data_product_type,
        int stokes_in, int stokes_out, int i_station, int time_average,
        int channel_average, const char* rootname, const char* ext)
{
    int buflen, start = 0;
    char* name = 0;

    /* Construct the filename. */
    buflen = strlen(rootname) + 100;
    name = calloc(buflen, 1);
    start += SNPRINTF(name + start, buflen - start, "%s", rootname);
    if (i_station >= 0)
        start += SNPRINTF(name + start, buflen - start, "_S%04d", i_station);
    start += SNPRINTF(name + start, buflen - start, "_%s",
            time_average ? "TIME_AVG" : "TIME_SEP");
    start += SNPRINTF(name + start, buflen - start, "_%s",
            channel_average ? "CHAN_AVG" : "CHAN_SEP");
    start += SNPRINTF(name + start, buflen - start, "_%s",
            data_type_to_string(data_product_type));
    if (stokes_in >= 0)
        start += SNPRINTF(name + start, buflen - start, "_%s",
                stokes_type_to_string(stokes_in));
    if (stokes_out >= 0)
        start += SNPRINTF(name + start, buflen - start, "_%s",
                stokes_type_to_string(stokes_out));
    start += SNPRINTF(name + start, buflen - start, ".%s", ext);
    return name;
}


static void new_fits_file(HostData* h, int data_product_type, int stokes_in,
        int stokes_out, int i_station, int time_average, int channel_average,
        const char* rootname, int* status)
{
    int i, horizon_mode;
    char* name;
    fitsfile* f;
    if (*status) return;

    /* Check polarisation type is possible. */
    if ((stokes_in > I || stokes_out > I) && h->pol_mode != OSKAR_POL_MODE_FULL)
        return;

    /* Construct the filename. */
    name = construct_filename(data_product_type, stokes_in, stokes_out,
            i_station, time_average, channel_average, rootname, "fits");

    /* Open the file. */
    horizon_mode = h->coord_frame_type == 'H';
    f = create_fits_file(name, h->precision, h->width, h->height,
            (time_average ? 1 : h->num_times),
            (channel_average ? 1 : h->num_channels),
            h->phase_centre_deg, h->fov_deg, h->start_mjd_utc,
            h->delta_t, h->start_freq_hz, h->delta_f,
            horizon_mode, h->settings_log, h->settings_log_length, status);
    if (!f || *status)
    {
        *status = OSKAR_ERR_FILE_IO;
        free(name);
        return;
    }
    i = data_product_index(h, data_product_type, stokes_in, stokes_out,
            i_station, time_average, channel_average);
    h->data_products[i].fits_file = f;
    free(name);
}


static void new_text_file(HostData* h, int data_product_type, int stokes_in,
        int stokes_out, int i_station, int time_average, int channel_average,
        const char* rootname, int* status)
{
    int i;
    char* name;
    FILE* f;
    if (*status) return;

    /* Check polarisation type is possible. */
    if ((stokes_in > I || stokes_out > I) && h->pol_mode != OSKAR_POL_MODE_FULL)
        return;

    /* Construct the filename. */
    name = construct_filename(data_product_type, stokes_in, stokes_out,
            i_station, time_average, channel_average, rootname, "txt");

    /* Open the file. */
    f = fopen(name, "w");
    if (!f)
    {
        *status = OSKAR_ERR_FILE_IO;
        free(name);
        return;
    }
    if (i_station >= 0)
        fprintf(f, "# Beam pixel list for station %d\n",
                h->station_ids[i_station]);
    else
        fprintf(f, "# Beam pixel list for telescope (interferometer)\n");
    fprintf(f, "# Filename is '%s'\n", name);
    fprintf(f, "# Dimension order (slowest to fastest) is:\n");
    if (h->average_single_axis != 'T')
        fprintf(f, "#     [pixel chunk], [time], [channel], [pixel index]\n");
    else
        fprintf(f, "#     [pixel chunk], [channel], [time], [pixel index]\n");
    fprintf(f, "# Number of pixel chunks: %d\n", h->num_chunks);
    fprintf(f, "# Number of times (output): %d\n",
            time_average ? 1 : h->num_times);
    fprintf(f, "# Number of channels (output): %d\n",
            channel_average ? 1 : h->num_channels);
    fprintf(f, "# Maximum pixel chunk size: %d\n", h->max_chunk_size);
    fprintf(f, "# Total number of pixels: %d\n", h->num_pixels);
    i = data_product_index(h, data_product_type, stokes_in, stokes_out,
            i_station, time_average, channel_average);
    h->data_products[i].text_file = f;
    free(name);
}


static const char* data_type_to_string(int type)
{
    switch (type)
    {
    case RAW_COMPLEX:                return "RAW_COMPLEX";
    case AMP:                        return "AMP";
    case PHASE:                      return "PHASE";
    case AUTO_POWER:                 return "AUTO_POWER";
    case CROSS_POWER_RAW_COMPLEX:    return "CROSS_POWER_RAW_COMPLEX";
    case CROSS_POWER_AMP:            return "CROSS_POWER_AMP";
    case CROSS_POWER_PHASE:          return "CROSS_POWER_PHASE";
    case IXR:                        return "IXR";
    default:                         return "";
    }
}


static const char* stokes_type_to_string(int type)
{
    switch (type)
    {
    case I:  return "I";
    case Q:  return "Q";
    case U:  return "U";
    case V:  return "V";
    case XX: return "XX";
    case XY: return "XY";
    case YX: return "YX";
    case YY: return "YY";
    default: return "";
    }
}


static void set_up_device_data(DeviceData* d, const HostData* h, int* status)
{
    int beam_type, cmplx, max_chunk_sources, max_chunk_size, prec, stokes;

    /* Get local variables. */
    max_chunk_sources = h->max_chunk_size;
    max_chunk_size    = h->num_active_stations * max_chunk_sources;
    prec              = h->precision;
    beam_type = cmplx = prec | OSKAR_COMPLEX;
    if (h->pol_mode == OSKAR_POL_MODE_FULL)
        beam_type |= OSKAR_MATRIX;

    /* Device memory. */
    d->previous_chunk_index = -1;
    d->jones_data = oskar_mem_create(beam_type, OSKAR_GPU, max_chunk_size,
            status);
    d->x    = oskar_mem_create(prec, OSKAR_GPU, 1 + max_chunk_sources, status);
    d->y    = oskar_mem_create(prec, OSKAR_GPU, 1 + max_chunk_sources, status);
    d->z    = oskar_mem_create(prec, OSKAR_GPU, 1 + max_chunk_sources, status);
    d->tel  = oskar_telescope_create_copy(h->tel, OSKAR_GPU, status);
    d->work = oskar_station_work_create(prec, OSKAR_GPU, status);

    /* Host memory. */
    if (h->raw_data)
    {
        d->jones_data_cpu[0] = oskar_mem_create(beam_type, OSKAR_CPU,
                max_chunk_size, status);
        d->jones_data_cpu[1] = oskar_mem_create(beam_type, OSKAR_CPU,
                max_chunk_size, status);
    }

    /* Auto-correlation beam output arrays. */
    for (stokes = 0; stokes < 4; ++stokes)
    {
        if (h->auto_power[stokes])
        {
            /* Device memory. */
            d->auto_power[stokes] = oskar_mem_create(beam_type, OSKAR_GPU,
                    max_chunk_size, status);
            oskar_mem_clear_contents(d->auto_power[stokes], status);

            /* Host memory. */
            d->auto_power_cpu[stokes][0] = oskar_mem_create(beam_type,
                    OSKAR_CPU, max_chunk_size, status);
            d->auto_power_cpu[stokes][1] = oskar_mem_create(beam_type,
                    OSKAR_CPU, max_chunk_size, status);
            if (h->average_single_axis == 'T')
                d->auto_power_time_avg[stokes] = oskar_mem_create(beam_type,
                        OSKAR_CPU, max_chunk_size, status);
            if (h->average_single_axis == 'C')
                d->auto_power_channel_avg[stokes] = oskar_mem_create(
                        beam_type, OSKAR_CPU, max_chunk_size, status);
            if (h->average_time_and_channel)
                d->auto_power_channel_and_time_avg[stokes] = oskar_mem_create(
                        beam_type, OSKAR_CPU, max_chunk_size, status);
        }

        /* Cross-correlation beam output arrays. */
        if (h->cross_power[stokes])
        {
            /* Device memory. */
            d->cross_power[stokes] = oskar_mem_create(beam_type, OSKAR_GPU,
                    max_chunk_sources, status);
            oskar_mem_clear_contents(d->cross_power[stokes], status);

            /* Host memory. */
            d->cross_power_cpu[stokes][0] = oskar_mem_create(beam_type,
                    OSKAR_CPU, max_chunk_sources, status);
            d->cross_power_cpu[stokes][1] = oskar_mem_create(beam_type,
                    OSKAR_CPU, max_chunk_sources, status);
            if (h->average_single_axis == 'T')
                d->cross_power_time_avg[stokes] = oskar_mem_create(beam_type,
                        OSKAR_CPU, max_chunk_sources, status);
            if (h->average_single_axis == 'C')
                d->cross_power_channel_avg[stokes] = oskar_mem_create(
                        beam_type, OSKAR_CPU, max_chunk_sources, status);
            if (h->average_time_and_channel)
                d->cross_power_channel_and_time_avg[stokes] = oskar_mem_create(
                        beam_type, OSKAR_CPU, max_chunk_sources, status);
        }
    }

    /* Timers. */
    d->tmr_compute = oskar_timer_create(OSKAR_TIMER_NATIVE);
}


static void free_device_data(int num_gpus, int* cuda_device_ids,
        DeviceData* d, int* status)
{
    int i, j;
    if (!d) return;
    for (i = 0; i < num_gpus; ++i)
    {
        DeviceData* dd = &d[i];
        if (!dd) continue;
        oskar_device_set(cuda_device_ids[i], status);
        oskar_mem_free(dd->jones_data_cpu[0], status);
        oskar_mem_free(dd->jones_data_cpu[1], status);
        oskar_mem_free(dd->jones_data, status);
        oskar_mem_free(dd->x, status);
        oskar_mem_free(dd->y, status);
        oskar_mem_free(dd->z, status);
        for (j = 0; j < 4; ++j)
        {
            oskar_mem_free(dd->auto_power_cpu[j][0], status);
            oskar_mem_free(dd->auto_power_cpu[j][1], status);
            oskar_mem_free(dd->auto_power_time_avg[j], status);
            oskar_mem_free(dd->auto_power_channel_avg[j], status);
            oskar_mem_free(dd->auto_power_channel_and_time_avg[j], status);
            oskar_mem_free(dd->auto_power[j], status);
            oskar_mem_free(dd->cross_power_cpu[j][0], status);
            oskar_mem_free(dd->cross_power_cpu[j][1], status);
            oskar_mem_free(dd->cross_power_time_avg[j], status);
            oskar_mem_free(dd->cross_power_channel_avg[j], status);
            oskar_mem_free(dd->cross_power_channel_and_time_avg[j], status);
            oskar_mem_free(dd->cross_power[j], status);
        }
        oskar_telescope_free(dd->tel, status);
        oskar_station_work_free(dd->work, status);
        oskar_timer_free(dd->tmr_compute);
        oskar_device_reset();
    }
    free(d);
}


static void free_host_data(HostData* h, int* status)
{
    int i;
    for (i = 0; i < h->num_data_products; ++i)
    {
        if (h->data_products[i].text_file)
            fclose(h->data_products[i].text_file);
        if (h->data_products[i].fits_file)
            ffclos(h->data_products[i].fits_file, status);
    }
    oskar_telescope_free(h->tel, status);
    oskar_mem_free(h->x, status);
    oskar_mem_free(h->y, status);
    oskar_mem_free(h->z, status);
    oskar_mem_free(h->pix, status);
    oskar_mem_free(h->ctemp, status);
    oskar_timer_free(h->tmr_load);
    oskar_timer_free(h->tmr_sim);
    oskar_timer_free(h->tmr_write);
    oskar_settings_file_handler_qsettings_free(h->handler);
    oskar_settings_free(h->s2);
    oskar_settings_old_free(&h->s);
    free(h->root_path);
    free(h->settings_log);
    free(h->station_ids);
    free(h->data_products);
    free(h);
}


static void record_timing(int num_gpus, int* cuda_device_ids,
        DeviceData* d, HostData* h, oskar_Log* log)
{
    int i, status = 0;
    oskar_log_section(log, 'M', "Simulation timing");
    oskar_log_value(log, 'M', 0, "Total wall time", "%.3f s",
            oskar_timer_elapsed(h->tmr_sim) + oskar_timer_elapsed(h->tmr_load));
    oskar_log_value(log, 'M', 0, "Load", "%.3f s",
            oskar_timer_elapsed(h->tmr_load));
    for (i = 0; i < num_gpus; ++i)
    {
        oskar_device_set(cuda_device_ids[i], &status);
        oskar_log_value(log, 'M', 0, "Compute", "%.3f s [GPU %i]",
                oskar_timer_elapsed(d[i].tmr_compute), i);
    }
    oskar_log_value(log, 'M', 0, "Write", "%.3f s",
            oskar_timer_elapsed(h->tmr_write));
}


static unsigned int disp_width(unsigned int v)
{
    return (v >= 100000u) ? 6 : (v >= 10000u) ? 5 : (v >= 1000u) ? 4 :
            (v >= 100u) ? 3 : (v >= 10u) ? 2u : 1u;
    /* return v == 1u ? 1u : (unsigned)log10(v)+1 */
}


#ifdef __cplusplus
}
#endif
