/* GStreamer
 *
 * v4lsrc_calls.h: functions for V4L video source
 *
 * Copyright (C) 2001-2002 Ronald Bultje <rbultje@ronald.bitfreak.net>
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

#ifndef __V4L_SRC_CALLS_H__
#define __V4L_SRC_CALLS_H__

#include "gstv4lsrc.h"
#include "v4l_calls.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* frame grabbing/capture (palette = VIDEO_PALETTE_* - see videodev.h) */
gboolean gst_v4lsrc_set_capture    (GstV4lSrc *v4lsrc, gint width, gint height, gint palette);
gboolean gst_v4lsrc_capture_init   (GstV4lSrc *v4lsrc);
gboolean gst_v4lsrc_capture_start  (GstV4lSrc *v4lsrc);
gboolean gst_v4lsrc_grab_frame     (GstV4lSrc *v4lsrc, gint *num);
guint8 * gst_v4lsrc_get_buffer     (GstV4lSrc *v4lsrc, gint  num);
gboolean gst_v4lsrc_requeue_frame  (GstV4lSrc *v4lsrc, gint  num);
gboolean gst_v4lsrc_capture_stop   (GstV4lSrc *v4lsrc);
gboolean gst_v4lsrc_capture_deinit (GstV4lSrc *v4lsrc);

/* "the ugliest hack ever, now available at your local mirror" */
gboolean gst_v4lsrc_try_palette    (GstV4lSrc *v4lsrc, gint palette);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __V4L_SRC_CALLS_H__ */
