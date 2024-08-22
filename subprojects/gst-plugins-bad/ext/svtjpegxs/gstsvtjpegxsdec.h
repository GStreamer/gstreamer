/* GStreamer SVT JPEG XS decoder
 * Copyright (C) 2024 Tim-Philipp Müller <tim centricular com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <SvtJpegxsDec.h>

#include <gst/video/video.h>

G_BEGIN_DECLS

#define GST_TYPE_SVT_JPEG_XS_DEC (gst_svt_jpeg_xs_dec_get_type())

G_DECLARE_FINAL_TYPE (GstSvtJpegXsDec, gst_svt_jpeg_xs_dec, GST, SVT_JPEG_XS_DEC, GstVideoDecoder);

GST_ELEMENT_REGISTER_DECLARE (svtjpegxsdec);

G_END_DECLS
