/* GStreamer divx decoder plugin
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
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

#ifndef __GST_DIVXDEC_H__
#define __GST_DIVXDEC_H__

#include <gst/gst.h>
#include <decore.h>

#ifdef __cplusplus
extern "C"
{
#endif				/* __cplusplus */


#define GST_TYPE_DIVXDEC \
  (gst_divxdec_get_type())
#define GST_DIVXDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_DIVXDEC, GstDivxDec))
#define GST_DIVXDEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_DIVXDEC, GstDivxDec))
#define GST_IS_DIVXDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_DIVXDEC))
#define GST_IS_DIVXDEC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_DIVXDEC))

  typedef struct _GstDivxDec GstDivxDec;
  typedef struct _GstDivxDecClass GstDivxDecClass;

  struct _GstDivxDec
  {
    GstElement element;

    /* pads */
    GstPad *sinkpad, *srcpad;

    /* divx handle */
    void *handle;

    /* video (output) settings */
    guint32 csp;
    int bitcnt, bpp;
    int version;
    int width, height;
    gdouble fps;
  };

  struct _GstDivxDecClass
  {
    GstElementClass parent_class;
  };

  GType gst_divxdec_get_type (void);

#ifdef __cplusplus
}
#endif				/* __cplusplus */

#endif				/* __GST_DIVXDEC_H__ */
