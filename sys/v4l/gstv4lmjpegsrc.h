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

#ifndef __GST_V4LMJPEGSRC_H__
#define __GST_V4LMJPEGSRC_H__

#include <gstv4lelement.h>
#include <sys/time.h>
#include <videodev_mjpeg.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define GST_TYPE_V4LMJPEGSRC \
  (gst_v4lmjpegsrc_get_type())
#define GST_V4LMJPEGSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_V4LMJPEGSRC,GstV4lMjpegSrc))
#define GST_V4LMJPEGSRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_V4LMJPEGSRC,GstV4lMjpegSrcClass))
#define GST_IS_V4LMJPEGSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_V4LMJPEGSRC))
#define GST_IS_V4LMJPEGSRC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_V4LMJPEGSRC))

typedef struct _GstV4lMjpegSrc GstV4lMjpegSrc;
typedef struct _GstV4lMjpegSrcClass GstV4lMjpegSrcClass;

struct _GstV4lMjpegSrc {
  GstV4lElement v4lelement;

  /* pads */
  GstPad *srcpad;

  /* the bufferpool */
  GstBufferPool *bufferpool;

  /* buffer/capture info */
  struct mjpeg_sync bsync;
  struct mjpeg_requestbuffers breq;

  /* list of available caps */
  GstCaps *capslist;

  /* caching values */
  gint x_offset;
  gint y_offset;
  gint frame_width;
  gint frame_height;
  gint horizontal_decimation;
  gint vertical_decimation;

  gint end_width;
  gint end_height;

  gint quality;
  gint numbufs;
  gint bufsize; /* in KB */
};

struct _GstV4lMjpegSrcClass {
  GstV4lElementClass parent_class;
};

GType gst_v4lmjpegsrc_get_type(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __GST_V4LMJPEGSRC_H__ */
