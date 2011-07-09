/* GStreamer
 * Copyright (C) 2008 David Schleef <ds@schleef.org>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef _GST_BASE_VIDEO_UTILS_H_
#define _GST_BASE_VIDEO_UTILS_H_

#ifndef GST_USE_UNSTABLE_API
#warning "GstBaseVideoCodec is unstable API and may change in future."
#warning "You can define GST_USE_UNSTABLE_API to avoid this warning."
#endif

#include <gst/gst.h>
#include <gst/video/video.h>
#include "gstbasevideocodec.h"

G_BEGIN_DECLS

gboolean gst_base_video_rawvideo_convert (GstVideoState *state,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 *dest_value);
gboolean gst_base_video_encoded_video_convert (GstVideoState * state,
    gint64 bytes, gint64 time, GstFormat src_format,
    gint64 src_value, GstFormat * dest_format, gint64 * dest_value);

GstClockTime gst_video_state_get_timestamp (const GstVideoState *state,
    GstSegment *segment, int frame_number);

G_END_DECLS

#endif
