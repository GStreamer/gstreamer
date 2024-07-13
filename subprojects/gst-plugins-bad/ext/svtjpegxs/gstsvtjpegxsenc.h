/* GStreamer SVT JPEG XS encoder
 * Copyright (C) 2024 Tim-Philipp Müller <tim centricular com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <SvtJpegxsEnc.h>

#include <gst/video/video.h>

G_BEGIN_DECLS

#define GST_TYPE_SVT_JPEG_XS_ENC (gst_svt_jpeg_xs_enc_get_type())

G_DECLARE_FINAL_TYPE (GstSvtJpegXsEnc, gst_svt_jpeg_xs_enc, GST, SVT_JPEG_XS_ENC, GstVideoEncoder);

GST_ELEMENT_REGISTER_DECLARE (svtjpegxsenc);

G_END_DECLS
