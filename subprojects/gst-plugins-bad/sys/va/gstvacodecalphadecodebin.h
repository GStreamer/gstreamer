/* GStreamer
 * Copyright (C) <2021> Collabora Ltd.
 *   Author: Daniel Almeida <daniel.almeida@collabora.com>
 * Copyright (C) <2024> Harmonic Inc.
 *   Author: Cheung Yik Pang <pang.cheung@harmonicinc.com>
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

#ifndef __GST_VA_CODEC_ALPHA_DECODE_BIN_H__
#define __GST_VA_CODEC_ALPHA_DECODE_BIN_H__

#include <gst/gst.h>
#include <gstvadevice.h>

/* When wrapping, use the original rank plus this offset. The ad-hoc rules is
 * that hardware implementation will use PRIMARY+1 or +2 to override the
 * software decoder, so the offset must be large enough to jump over those.
 * This should also be small enough so that a marginal (64) or secondary
 * wrapper does not cross the PRIMARY line.
 */
#define GST_VA_CODEC_ALPHA_DECODE_BIN_RANK_OFFSET 10

G_BEGIN_DECLS

#define GST_TYPE_VA_CODEC_ALPHA_DECODE_BIN (gst_va_codec_alpha_decode_bin_get_type())
G_DECLARE_DERIVABLE_TYPE (GstVaCodecAlphaDecodeBin,
    gst_va_codec_alpha_decode_bin, GST, VA_CODEC_ALPHA_DECODE_BIN, GstBin);

struct _GstVaCodecAlphaDecodeBinClass
{
  GstBinClass parent_class;
  gchar *decoder_name;
};

gboolean gst_va_codec_alpha_decode_bin_register (GstPlugin * plugin,
                                                 GClassInitFunc class_init,
                                                 gconstpointer class_data,
                                                 const gchar * type_name_default,
                                                 const gchar * type_name_templ,
                                                 const gchar * feature_name_default,
                                                 const gchar * feature_name_templ,
                                                 GstVaDevice * device,
                                                 guint rank);

G_END_DECLS
#endif /* __GST_VA_CODEC_ALPHA_DECODE_BIN_H__ */
