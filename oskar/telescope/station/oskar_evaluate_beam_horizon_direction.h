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

#ifndef OSKAR_EVALUATE_BEAM_HORIZON_DIRECTION_H_
#define OSKAR_EVALUATE_BEAM_HORIZON_DIRECTION_H_

/**
 * @file oskar_evaluate_beam_horizon_direction.h
 */

#include <oskar_global.h>
#include <telescope/station/oskar_station.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Wrapper function to evaluate the beam phase centre coordinates as
 * horizontal direction cosines for the specified Greenwich apparent sidereal
 * time.
 *
 * @details
 * Converts the station beam phase centre from equatorial (RA, Dec) coordinates
 * stored in the station model to horizontal lmn coordinates stored in the
 * E-Jones work buffer for the specified time and the location of the
 * station as described by the longitude and latitude fields of the station
 * structure.
 *
 * @param[out] x         Horizontal x-direction-cosine of the beam phase centre.
 * @param[out] y         Horizontal y-direction-cosine of the beam phase centre.
 * @param[out] z         Horizontal z-direction-cosine of the beam phase centre.
 * @param[in]  station   OSKAR Station structure.
 * @param[in]  gast      The Greenwich apparent sidereal time, in radians.
 * @param[in,out] status Status return code.
 */
OSKAR_EXPORT
void oskar_evaluate_beam_horizon_direction(double* x, double* y, double* z,
        const oskar_Station* station, const double gast, int* status);

#ifdef __cplusplus
}
#endif

#endif /* OSKAR_EVALUATE_BEAM_HORIZON_DIRECTION_H_ */
