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


#ifndef __GST_JPEGDEC_H__
#define __GST_JPEGDEC_H__


#include <gst/gst.h>
/* this is a hack hack hack to get around jpeglib header bugs... */
#ifdef HAVE_STDLIB_H
# undef HAVE_STDLIB_H
#endif
#include <jpeglib.h>

#ifdef __cplusplus
extern "C"
{
#endif				/* __cplusplus */


#define GST_TYPE_JPEGDEC \
  (gst_jpegdec_get_type())
#define GST_JPEGDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_JPEGDEC,GstJpegDec))
#define GST_JPEGDEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_JPEGDEC,GstJpegDec))
#define GST_IS_JPEGDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_JPEGDEC))
#define GST_IS_JPEGDEC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_JPEGDEC))

  typedef struct _GstJpegDec GstJpegDec;
  typedef struct _GstJpegDecClass GstJpegDecClass;

  struct _GstJpegDec
  {
    GstElement element;

    /* pads */
    GstPad *sinkpad, *srcpad;

    int parse_state;
    /* the timestamp of the next frame */
    guint64 next_time;
    /* the interval between frames */
    guint64 time_interval;

    /* video state */
    gint format;
    gint width;
    gint height;
    gdouble fps;
    /* the size of the output buffer */
    gint outsize;
    /* the jpeg line buffer */
    guchar **line[3];

    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    struct jpeg_source_mgr jsrc;
  };

  struct _GstJpegDecClass
  {
    GstElementClass parent_class;
  };

  GType gst_jpegdec_get_type (void);


#ifdef __cplusplus
}
#endif				/* __cplusplus */


#endif				/* __GST_JPEGDEC_H__ */
