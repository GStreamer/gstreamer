/* G-Streamer hardware MJPEG video source plugin
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

#ifndef __V4L_MJPEG_SRC_CALLS_H__
#define __V4L_MJPEG_SRC_CALLS_H__

#include "gstv4lmjpegsrc.h"
#include "v4l_calls.h"

#ifdef __cplusplus
extern "C"
{
#endif				/* __cplusplus */


/* frame grabbing/capture */
  gboolean gst_v4lmjpegsrc_set_buffer (GstV4lMjpegSrc * v4lmjpegsrc,
      gint numbufs, gint bufsize);
  gboolean gst_v4lmjpegsrc_set_capture (GstV4lMjpegSrc * v4lmjpegsrc,
      gint decimation, gint quality);
  gboolean gst_v4lmjpegsrc_set_capture_m (GstV4lMjpegSrc * v4lmjpegsrc,
      gint x_offset,
      gint y_offset,
      gint width,
      gint height, gint h_decimation, gint v_decimation, gint quality);
  gboolean gst_v4lmjpegsrc_capture_init (GstV4lMjpegSrc * v4lmjpegsrc);
  gboolean gst_v4lmjpegsrc_capture_start (GstV4lMjpegSrc * v4lmjpegsrc);
  gboolean gst_v4lmjpegsrc_grab_frame (GstV4lMjpegSrc * v4lmjpegsrc,
      gint * num, gint * size);
  guint8 *gst_v4lmjpegsrc_get_buffer (GstV4lMjpegSrc * v4lmjpegsrc, gint num);
  gboolean gst_v4lmjpegsrc_requeue_frame (GstV4lMjpegSrc * v4lmjpegsrc,
      gint num);
  gboolean gst_v4lmjpegsrc_capture_stop (GstV4lMjpegSrc * v4lmjpegsrc);
  gboolean gst_v4lmjpegsrc_capture_deinit (GstV4lMjpegSrc * v4lmjpegsrc);


#ifdef __cplusplus
}
#endif				/* __cplusplus */

#endif				/* __V4L_MJPEG_SRC_CALLS_H__ */
