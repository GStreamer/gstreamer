/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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


#ifndef __GST_MPEG2DEC_H__
#define __GST_MPEG2DEC_H__


#include <config.h>
#include <gst/gst.h>
#include <mpeg2dec/mpeg2.h>
#include <mpeg2dec/mm_accel.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define GST_TYPE_MPEG2DEC \
  (gst_mpeg2dec_get_type())
#define GST_MPEG2DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MPEG2DEC,GstMpeg2dec))
#define GST_MPEG2DEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MPEG2DEC,GstMpeg2dec))
#define GST_IS_MPEG2DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MPEG2DEC))
#define GST_IS_MPEG2DEC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MPEG2DEC))

typedef struct _GstMpeg2dec GstMpeg2dec;
typedef struct _GstMpeg2decClass GstMpeg2decClass;

struct _GstMpeg2dec {
  GstElement element;

  /* pads */
  GstPad *sinkpad,*srcpad;
  GstBufferPool *peerpool;

  mpeg2dec_t *decoder;
  guint32 accel;
  vo_instance_t *vo;
  gboolean closed;

  /* the timestamp of the next frame */
  gboolean first;
  gint64 next_time;
  gint64 last_PTS;
  gint frames_per_PTS;
  gint adjust;

  /* video state */
  gint format;
  gint width;
  gint height;
  gint frame_rate_code;
};

struct _GstMpeg2decClass {
  GstElementClass parent_class;
};

GType gst_mpeg2dec_get_type(void);


#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_MPEG2DEC_H__ */
