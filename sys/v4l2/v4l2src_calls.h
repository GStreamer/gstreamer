/* G-Streamer Video4linux2 video-capture plugin - system calls
 * Copyright (C) 2002 Ronald Bultje <rbultje@ronald.bitfreak.net>
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

#ifndef __V4L2SRC_CALLS_H__
#define __V4L2SRC_CALLS_H__

#include "gstv4l2src.h"
#include "v4l2_calls.h"


gboolean gst_v4l2src_get_capture (GstV4l2Src * v4l2src);
gboolean gst_v4l2src_set_capture (GstV4l2Src * v4l2src,
    struct v4l2_fmtdesc *fmt, gint width, gint height);
gboolean gst_v4l2src_capture_init (GstV4l2Src * v4l2src);
gboolean gst_v4l2src_capture_start (GstV4l2Src * v4l2src);
gint gst_v4l2src_grab_frame (GstV4l2Src * v4l2src);
guint8 *gst_v4l2src_get_buffer (GstV4l2Src * v4l2src, gint num);
gboolean gst_v4l2src_queue_frame (GstV4l2Src * v4l2src, guint i);
gboolean gst_v4l2src_capture_stop (GstV4l2Src * v4l2src);
gboolean gst_v4l2src_capture_deinit (GstV4l2Src * v4l2src);

gboolean gst_v4l2src_fill_format_list (GstV4l2Src * v4l2src);
gboolean gst_v4l2src_clear_format_list (GstV4l2Src * v4l2src);

/* hacky */
gboolean gst_v4l2src_get_size_limits (GstV4l2Src * v4l2src,
    struct v4l2_fmtdesc *fmt,
    gint * min_w, gint * max_w, gint * min_h, gint * max_h);

void gst_v4l2src_free_buffer (GstBuffer * buffer);

#endif /* __V4L2SRC_CALLS_H__ */
