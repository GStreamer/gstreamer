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


#ifndef __GST_SWFDEC_H__
#define __GST_SWFDEC_H__


#include <gst/gst.h>
#include <swfdec.h>

#ifdef __cplusplus
extern "C"
{
#endif				/* __cplusplus */


#define GST_TYPE_SWFDEC \
  (gst_swfdec_get_type())
#define GST_SWFDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SWFDEC,GstSwfdec))
#define GST_SWFDEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SWFDEC,GstSwfdec))
#define GST_IS_SWFDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SWFDEC))
#define GST_IS_SWFDEC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SWFDEC))

  typedef struct _GstSwfdec GstSwfdec;
  typedef struct _GstSwfdecClass GstSwfdecClass;

  struct _GstSwfdec
  {
    GstElement element;

    /* pads */
    GstPad *sinkpad;
    GstPad *videopad;
    GstPad *audiopad;

    SwfdecDecoder *state;
    gboolean closed;

    /* the timestamp of the next frame */
    gboolean first;
    gboolean have_format;

    double rate;
    gint64 timestamp;
    gint64 interval;
    double frame_rate;

    /* video state */
    gint format;
    gint width;
    gint height;
    gint64 total_frames;

  };

  struct _GstSwfdecClass
  {
    GstElementClass parent_class;
  };

  GType gst_swfdec_get_type (void);


#ifdef __cplusplus
}
#endif				/* __cplusplus */


#endif				/* __GST_SWFDEC_H__ */
