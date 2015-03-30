/*
 * Copyright (c) 2012-2015, The University of Oxford
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

#ifndef OSKAR_SIM_BEAM_PATTERN_NEW_H_
#define OSKAR_SIM_BEAM_PATTERN_NEW_H_

/**
 * @file oskar_sim_beam_pattern.h
 */

#include <oskar_global.h>
#include <oskar_log.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief
 * Main OSKAR beam pattern simulation function.
 *
 * @details
 * This function acts as though it were a stand-alone executable.
 *
 * @param[in]     settings_file Path to a settings file.
 * @param[in,out] log           Pointer to a log structure to use.
 * @param[in,out] status        Error status code.
 */
OSKAR_APPS_EXPORT
void oskar_sim_beam_pattern_new(const char* settings_file, oskar_Log* log,
        int* status);

#ifdef __cplusplus
}
#endif

#endif /* OSKAR_SIM_BEAM_PATTERN_NEW_H_ */