/* GStreamer
 *
 * v4lmjpegsink_calls.c: functions for hardware MJPEG video sink
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

#ifndef __V4L_MJPEG_SINK_CALLS_H__
#define __V4L_MJPEG_SINK_CALLS_H__

#include "gstv4lmjpegsink.h"
#include "v4l_calls.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* frame playback on device */
gboolean gst_v4lmjpegsink_set_buffer      (GstV4lMjpegSink *v4lmjpegsink,
                                           gint            numbufs,
                                           gint            bufsize);
gboolean gst_v4lmjpegsink_set_playback    (GstV4lMjpegSink *v4lmjpegsink,
                                           gint            width,
                                           gint            height,
                                           gint            x_offset,
                                           gint            y_offset,
                                           gint            norm,
                                           gint            interlacing);
gboolean gst_v4lmjpegsink_playback_init   (GstV4lMjpegSink *v4lmjpegsink);
gboolean gst_v4lmjpegsink_playback_start  (GstV4lMjpegSink *v4lmjpegsink);
guint8 * gst_v4lmjpegsink_get_buffer      (GstV4lMjpegSink *v4lmjpegsink,
                                           gint            num);
gboolean gst_v4lmjpegsink_play_frame      (GstV4lMjpegSink *v4lmjpegsink,
                                           gint            num);
gboolean gst_v4lmjpegsink_wait_frame      (GstV4lMjpegSink *v4lmjpegsink,
                                           gint            *num);
gboolean gst_v4lmjpegsink_playback_stop   (GstV4lMjpegSink *v4lmjpegsink);
gboolean gst_v4lmjpegsink_playback_deinit (GstV4lMjpegSink *v4lmjpegsink);



#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __V4L_MJPEG_SINK_CALLS_H__ */
