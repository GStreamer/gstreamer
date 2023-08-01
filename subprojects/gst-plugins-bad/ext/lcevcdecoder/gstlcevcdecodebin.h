/* GStreamer
 *  Copyright (C) <2024> V-Nova International Limited
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

#ifndef __GST_LCEVC_DECODE_BIN_H__
#define __GST_LCEVC_DECODE_BIN_H__

#include <gst/gst.h>

G_BEGIN_DECLS

/* When wrapping, use the original rank plus this offset. The ad-hoc rules is
 * that hardware implementation will use PRIMARY+1 or +2 to override the
 * software decoder, so the offset must be large enough to jump over those.
 * This should also be small enough so that a marginal (64) or secondary
 * wrapper does not cross the PRIMARY line.
 */
#define GST_LCEVC_DECODE_BIN_RANK_OFFSET 10

#define GST_TYPE_LCEVC_DECODE_BIN (gst_lcevc_decode_bin_get_type())
G_DECLARE_DERIVABLE_TYPE (GstLcevcDecodeBin,
    gst_lcevc_decode_bin, GST, LCEVC_DECODE_BIN, GstBin);

struct _GstLcevcDecodeBinClass
{
  GstBinClass parent_class;

  GstCaps * (*get_base_decoder_sink_caps) (GstLcevcDecodeBin * base);
};

G_END_DECLS

#endif /* __GST_LCEVC_DECODE_BIN_H__ */
