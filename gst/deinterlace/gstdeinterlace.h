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


#ifndef __GST_DEINTERLACE_H__
#define __GST_DEINTERLACE_H__


#include <gst/gst.h>
/* #include <gst/meta/audioraw.h> */

G_BEGIN_DECLS

#define GST_TYPE_DEINTERLACE \
  (gst_deinterlace_get_type())
#define GST_DEINTERLACE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DEINTERLACE,GstDeInterlace))
#define GST_DEINTERLACE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ULAW,GstDeInterlace))
#define GST_IS_DEINTERLACE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DEINTERLACE))
#define GST_IS_DEINTERLACE_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DEINTERLACE))

typedef struct _GstDeInterlace GstDeInterlace;
typedef struct _GstDeInterlaceClass GstDeInterlaceClass;

struct _GstDeInterlace {
  GstElement element;

  GstPad *sinkpad, *srcpad;

  gint width, height;

  gboolean show_deinterlaced_area_only;
  gboolean blend;
  gint threshold_blend; /* here we start blending */
  gint threshold;         /* here we start interpolating TODO FIXME */
  gint edge_detect;

  gint picsize;
  guchar *src;

};

struct _GstDeInterlaceClass {
  GstElementClass parent_class;
};

G_END_DECLS

#endif /* __GST_DEINTERLACE_H__ */
