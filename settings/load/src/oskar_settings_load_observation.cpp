/*
 * Copyright (c) 2012-2014, The University of Oxford
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

#include <oskar_settings_load_observation.h>
#include <oskar_settings_convert_date_time_to_mjd.h>
#include <oskar_log.h>

#include <oskar_cmath.h>
#include <cstdlib>
#include <cstring>
#include <cfloat>
#include <QtCore/QSettings>
#include <QtCore/QByteArray>
#include <QtCore/QDateTime>
#include <QtCore/QVariant>
#include <QtCore/QDate>
#include <QtCore/QTime>
#include <QtCore/QString>
#include <QtCore/QStringList>

static QStringList get_list(QVariant v)
{
    QStringList list;
    if (v.type() == QVariant::StringList)
        list = v.toStringList();
    else if (v.type() == QVariant::String)
        list = v.toString().split(",");
    return list;
}

extern "C"
void oskar_settings_load_observation(oskar_SettingsObservation* obs,
        oskar_Log* log, const char* filename, int* status)
{
    QByteArray t;
    QVariant v;
    QSettings s(QString(filename), QSettings::IniFormat);

    if (*status) return;

    s.beginGroup("observation");
    {
        // Get pointing direction(s) as string lists.
        QStringList ra_list, dec_list;
        ra_list = get_list(s.value("phase_centre_ra_deg", "0.0"));
        dec_list = get_list(s.value("phase_centre_dec_deg", "0.0"));

        // Check lists are the same length.
        if (ra_list.size() != dec_list.size())
        {
            oskar_log_error(log, "RA and Dec coordinate arrays "
                    "must be the same length.");
            *status = OSKAR_ERR_SETTINGS_OBSERVATION;
            return;
        }

        // Allocate memory for pointing data and copy to settings arrays.
        obs->num_pointing_levels = ra_list.size();
        obs->ra0_rad  = (double*)malloc(ra_list.size() * sizeof(double));
        obs->dec0_rad = (double*)malloc(dec_list.size() * sizeof(double));
        for (int i = 0; i < obs->num_pointing_levels; ++i)
        {
            obs->ra0_rad[i] = ra_list[i].toDouble() * M_PI / 180.0;
            obs->dec0_rad[i] = dec_list[i].toDouble() * M_PI / 180.0;
        }

        // Get station pointing file.
        t = s.value("pointing_file", "").toByteArray();
        if (t.size() > 0)
        {
            obs->pointing_file = (char*)malloc(t.size() + 1);
            strcpy(obs->pointing_file, t.constData());
        }

        // Get frequency / channel data.
        obs->start_frequency_hz = s.value("start_frequency_hz").toDouble();
        if (obs->start_frequency_hz < DBL_MIN)
        {
            *status = OSKAR_ERR_SETTINGS_OBSERVATION;
            return;
        }

        obs->num_channels         = s.value("num_channels", 1).toInt();
        obs->frequency_inc_hz     = s.value("frequency_inc_hz").toDouble();

        // Get observation start time.
        QString str_st = s.value("start_time_utc", "01-01-2000 00:00:00.0").toString();
        QDateTime st = QDateTime::fromString(str_st, "d-M-yyyy h:m:s.z");
        if (!st.isValid())
        {
            oskar_log_error(log, "Invalid date string for 'start_time_utc' "
                    "(format must be: 'd-M-yyyy h:m:s.z').");
            *status = OSKAR_ERR_SETTINGS_OBSERVATION;
            return;
        }
        int year   = st.date().year();
        int month  = st.date().month();
        int day    = st.date().day();
        int hour   = st.time().hour();
        int minute = st.time().minute();
        double second = st.time().second() + st.time().msec() / 1000.0;

        // Compute start time as MJD(UTC).
        double day_fraction = (hour + (minute / 60.0) + (second / 3600.0)) / 24.0;
        obs->start_mjd_utc = oskar_settings_convert_date_time_to_mjd(year,
                month, day, day_fraction);

        // Get number of time steps.
        obs->num_time_steps  = s.value("num_time_steps", 1).toInt();

        // Get observation length, either as number of seconds, or as a string.
        QString length = s.value("length").toString();
        if (length.size() > 0 && !length.contains(":"))
        {
            obs->length_sec = length.toDouble();
        }
        else
        {
            QTime len = QTime::fromString(length, "h:m:s.z");
            if (!len.isValid())
            {
                len = QTime::fromString(length, "h:m:s");
                if (!len.isValid())
                {
                    oskar_log_error(log, "Invalid time string for 'length' "
                            "(format must be hh:mm:ss.z).");
                    *status = OSKAR_ERR_SETTINGS_OBSERVATION;
                    return;
                }
            }
            obs->length_sec = len.hour() * 3600.0 +
                    len.minute() * 60.0 + len.second() + len.msec() / 1000.0;
        }
        obs->length_days = obs->length_sec / 86400.0;

    }
    s.endGroup();

    // Range checks.
    if (obs->num_channels <= 0) obs->num_channels = 1;
    if (obs->num_time_steps <= 0) obs->num_time_steps = 1;

    // Compute interval
    obs->dt_dump_days = obs->length_days / obs->num_time_steps;
}