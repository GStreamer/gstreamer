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


#ifndef __GST_Y4MENCODE_H__
#define __GST_Y4MENCODE_H__


#include <gst/gst.h>
/* #include <gst/meta/audioraw.h> */


#ifdef __cplusplus
extern "C"
{
#endif				/* __cplusplus */


#define GST_TYPE_Y4MENCODE \
  (gst_y4mencode_get_type())
#define GST_Y4MENCODE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_Y4MENCODE,GstY4mEncode))
#define GST_Y4MENCODE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ULAW,GstY4mEncode))
#define GST_IS_Y4MENCODE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_Y4MENCODE))
#define GST_IS_Y4MENCODE_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_Y4MENCODE))

  typedef struct _GstY4mEncode GstY4mEncode;
  typedef struct _GstY4mEncodeClass GstY4mEncodeClass;

  struct _GstY4mEncode
  {
    GstElement element;

    GstPad *sinkpad, *srcpad;

    gint width, height;
    gfloat fps_idx;

    gboolean init;

  };

  struct _GstY4mEncodeClass
  {
    GstElementClass parent_class;
  };

  GType gst_y4mencode_get_type (void);

#ifdef __cplusplus
}
#endif				/* __cplusplus */


#endif				/* __GST_Y4MENCODE_H__ */
