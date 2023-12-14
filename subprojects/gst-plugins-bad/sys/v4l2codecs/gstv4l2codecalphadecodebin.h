/* GStreamer
 * Copyright (C) <2021> Collabora Ltd.
 *   Author: Daniel Almeida <daniel.almeida@collabora.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#pragma once

#include <gstv4l2decoder.h>

G_BEGIN_DECLS

#define GST_TYPE_V4L2_CODEC_ALPHA_DECODE_BIN (gst_v4l2_codec_alpha_decode_bin_get_type())
G_DECLARE_DERIVABLE_TYPE (GstV4l2CodecAlphaDecodeBin,
    gst_v4l2_codec_alpha_decode_bin, GST, V4L2_CODEC_ALPHA_DECODE_BIN, GstBin);

struct _GstV4l2CodecAlphaDecodeBinClass
{
  GstBinClass parent_class;
  gchar *decoder_name;
};

void gst_v4l2_codec_alpha_decode_bin_register (GstPlugin * plugin,
                                               GClassInitFunc class_init,
                                               gconstpointer class_data,
                                               const gchar * element_name_tmpl,
                                               GstV4l2CodecDevice * device,
                                               guint rank);

G_END_DECLS
