/*
 * gstgdkpixbuf.h
 * GStreamer
 * Copyright (C) 1999-2001 Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) 2003 David A. Schleef <ds@schleef.org>
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

#ifndef __GST_GDK_PIXBUF_H__
#define __GST_GDK_PIXBUF_H__

#include <gst/gst.h>

G_BEGIN_DECLS
/* #define's don't like whitespacey bits */
#define GST_TYPE_GDK_PIXBUF \
  (gst_gdk_pixbuf_get_type())
#define GST_GDK_PIXBUF(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_GDK_PIXBUF,GstGdkPixbuf))
#define GST_GDK_PIXBUF_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_GDK_PIXBUF,GstGdkPixbuf))
#define GST_IS_GDK_PIXBUF(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_GDK_PIXBUF))
#define GST_IS_GDK_PIXBUF_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_GDK_PIXBUF))
typedef struct _GstGdkPixbuf GstGdkPixbuf;
typedef struct _GstGdkPixbufClass GstGdkPixbufClass;

struct _GstGdkPixbuf
{
  GstElement element;

  GstPad *sinkpad, *srcpad;

  GstClockTime last_timestamp;
  GdkPixbufLoader *pixbuf_loader;

  int width;
  int height;
  int rowstride;
  unsigned int image_size;

  double framerate;
};

struct _GstGdkPixbufClass
{
  GstElementClass parent_class;
};

GType gst_gdk_pixbuf_get_type (void);

G_END_DECLS
#endif /* __GST_GDK_PIXBUF_H__ */
