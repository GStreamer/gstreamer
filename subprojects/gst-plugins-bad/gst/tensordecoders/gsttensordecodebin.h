/* GStreamer object detection overlay
 * Copyright (C) <2025> Collabora Ltd.
 *  @author: Daniel Morin <daniel.morin@collabora.com>
 *
 * gsttensordecodebin.h
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

#ifndef __GST_TENSOR_DECODE_BIN__
#define __GST_TENSOR_DECODE_BIN__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_TENSORDECODEBIN (gst_tensordecodebin_get_type ())
G_DECLARE_FINAL_TYPE (GstTensorDecodeBin, gst_tensordecodebin,
    GST, TENSORDECODEBIN, GstBin)

struct _GstTensorDecodeBin
{
  GstBin basebin;
  GstPad *sinkpad;
  GstPad *srcpad;

  // only change under object lock
  guint32 factories_cookie;

  // only change under object lock
  GList *tensordec_factories;

  GstCaps *last_event_caps;
  GstCaps *aggregated_caps;
};

struct _GstTensorDecodeBinClass
{
  GstBin parent_class;
};

GST_ELEMENT_REGISTER_DECLARE (tensordecodebin)

G_END_DECLS
#endif /* __GST_TENSOR_DECODE_BIN__ */
