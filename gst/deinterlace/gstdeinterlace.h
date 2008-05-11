/* GStreamer simple deinterlacing plugin
 * Copyright (C) 1999 Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) 2006 Tim-Philipp Müller <tim centricular net>
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
#include <gst/base/gstbasetransform.h>

G_BEGIN_DECLS

#define GST_TYPE_DEINTERLACE            (gst_deinterlace_get_type())
#define GST_DEINTERLACE(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DEINTERLACE,GstDeinterlace))
#define GST_DEINTERLACE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DEINTERLACE,GstDeinterlaceClass))
#define GST_IS_DEINTERLACE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DEINTERLACE))
#define GST_IS_DEINTERLACE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DEINTERLACE))

typedef struct _GstDeinterlace GstDeinterlace;
typedef struct _GstDeinterlaceClass GstDeinterlaceClass;

struct _GstDeinterlace {
  GstBaseTransform basetransform;

  /*< private >*/
  gint         width;
  gint         height;
  gint         uv_height;
  guint32      fourcc;

  gboolean     show_deinterlaced_area_only;
  gboolean     show_noninterlaced_area_only;
  gboolean     blend;
  gboolean     deinterlace;
  gint         threshold_blend; /* here we start blending */
  gint         threshold;       /* here we start interpolating TODO FIXME */
  gint         edge_detect;

  gint         picsize;
  gint         y_stride;
  gint         u_stride;
  gint         v_stride;
  gint         y_off;
  gint         u_off;
  gint         v_off;

  guchar      *src;
};

struct _GstDeinterlaceClass {
  GstBaseTransformClass basetransformclass;
};

GType   gst_deinterlace_get_type (void);

G_END_DECLS

#endif /* __GST_DEINTERLACE_H__ */
