/* Gnome-Streamer
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


#ifndef __GST_VIDEOSCALE_H__
#define __GST_VIDEOSCALE_H__


#include <config.h>
#include <gst/gst.h>


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define GST_TYPE_VIDEOSCALE \
  (gst_videoscale_get_type())
#define GST_VIDEOSCALE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VIDEOSCALE,GstVideoscale))
#define GST_VIDEOSCALE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VIDEOSCALE,GstVideoscale))
#define GST_IS_VIDEOSCALE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VIDEOSCALE))
#define GST_IS_VIDEOSCALE_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VIDEOSCALE))

typedef enum {
  GST_VIDEOSCALE_POINT_SAMPLE,
  GST_VIDEOSCALE_NEAREST,
  GST_VIDEOSCALE_BILINEAR,
  GST_VIDEOSCALE_BICUBIC
} GstVideoScaleMethod;

typedef struct _GstVideoscale GstVideoscale;
typedef struct _GstVideoscaleClass GstVideoscaleClass;

struct _GstVideoscale {
  GstElement element;

  GstPad *sinkpad,*srcpad;

  /* video state */
  gint format;
  gint width;
  gint height;
  gint targetwidth;
  gint targetheight;
  GstVideoScaleMethod method;
  guint scale_bytes;
  
  /* private */
  guchar *temp;
  void (*scale_cc) (GstVideoscale *scale, guchar *src, guchar *dest);
  void (*scaler) (GstVideoscale *scale, guchar *src, guchar *dest,int,int,int,int);
  guchar (*filter) (guchar *src, gdouble x, gdouble y, gint sw, gint sh);
  guchar copy_row[8192];
};

struct _GstVideoscaleClass {
  GstElementClass parent_class;
};

GType gst_videoscale_get_type(void);

void gst_videoscale_setup(GstVideoscale *);
#define gst_videoscale_scale(scale, src, dest) (scale)->scale_cc((scale), (src), (dest))

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_VIDEOSCALE_H__ */
