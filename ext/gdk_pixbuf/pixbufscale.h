/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2004> Jan Schmidt <thaytan@mad.scientist.com>
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


#ifndef __GST_PIXBUFSCALE_H__
#define __GST_PIXBUFSCALE_H__

#include <gtk/gtk.h>
#include <gst/gst.h>

G_BEGIN_DECLS

typedef enum {
  GST_PIXBUFSCALE_NEAREST,
  GST_PIXBUFSCALE_TILES,
  GST_PIXBUFSCALE_BILINEAR,
  GST_PIXBUFSCALE_HYPER
} GstPixbufScaleMethod;

#define GST_TYPE_PIXBUFSCALE \
  (gst_pixbufscale_get_type())
#define GST_PIXBUFSCALE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_PIXBUFSCALE,GstPixbufScale))
#define GST_PIXBUFSCALE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_PIXBUFSCALE,GstPixbufScale))
#define GST_IS_PIXBUFSCALE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_PIXBUFSCALE))
#define GST_IS_PIXBUFSCALE_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_PIXBUFSCALE))

typedef struct _GstPixbufScale GstPixbufScale;
typedef struct _GstPixbufScaleClass GstPixbufScaleClass;

struct _GstPixbufScale {
  GstElement element;

  GstPad *sinkpad,*srcpad;

  /* video state */
  gboolean inited;
  gint to_width;
  gint to_height;
  gint from_width;
  gint from_height;
  gboolean passthru;

  GstPixbufScaleMethod method;
  GdkInterpType gdk_method;
  
  /* private */
  gint from_buf_size;
  gint to_buf_size;
};

struct _GstPixbufScaleClass {
  GstElementClass parent_class;
};

static GType gst_pixbufscale_get_type(void);

G_END_DECLS

#endif /* __GST_PIXBUFSCALE_H__ */
