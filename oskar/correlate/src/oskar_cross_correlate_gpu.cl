/* Copyright (c) 2018-2019, The University of Oxford. See LICENSE file. */

OSKAR_XCORR_GPU( M_CAT(xcorr_point_, Real), false, false, false, Real, Real2, Real4c)
OSKAR_XCORR_GPU( M_CAT(xcorr_point_bs_, Real), true, false, false, Real, Real2, Real4c)
OSKAR_XCORR_GPU( M_CAT(xcorr_point_ts_, Real), false, true, false, Real, Real2, Real4c)
OSKAR_XCORR_GPU( M_CAT(xcorr_point_bs_ts_, Real), true, true, false, Real, Real2, Real4c)
OSKAR_XCORR_GPU( M_CAT(xcorr_gaussian_, Real), false, false, true, Real, Real2, Real4c)
OSKAR_XCORR_GPU( M_CAT(xcorr_gaussian_bs_, Real), true, false, true, Real, Real2, Real4c)
OSKAR_XCORR_GPU( M_CAT(xcorr_gaussian_ts_, Real), false, true, true, Real, Real2, Real4c)
OSKAR_XCORR_GPU( M_CAT(xcorr_gaussian_bs_ts_, Real), true, true, true, Real, Real2, Real4c)
OSKAR_XCORR_SCALAR_GPU( M_CAT(xcorr_scalar_point_, Real), false, false, false, Real, Real2)
OSKAR_XCORR_SCALAR_GPU( M_CAT(xcorr_scalar_point_bs_, Real), true, false, false, Real, Real2)
OSKAR_XCORR_SCALAR_GPU( M_CAT(xcorr_scalar_point_ts_, Real), false, true, false, Real, Real2)
OSKAR_XCORR_SCALAR_GPU( M_CAT(xcorr_scalar_point_bs_ts_, Real), true, true, false, Real, Real2)
OSKAR_XCORR_SCALAR_GPU( M_CAT(xcorr_scalar_gaussian_, Real), false, false, true, Real, Real2)
OSKAR_XCORR_SCALAR_GPU( M_CAT(xcorr_scalar_gaussian_bs_, Real), true, false, true, Real, Real2)
OSKAR_XCORR_SCALAR_GPU( M_CAT(xcorr_scalar_gaussian_ts_, Real), false, true, true, Real, Real2)
OSKAR_XCORR_SCALAR_GPU( M_CAT(xcorr_scalar_gaussian_bs_ts_, Real), true, true, true, Real, Real2)
