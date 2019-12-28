/* GStreamer
 * Copyright (C) <2019> Eric Marks <bigmarkslp@gmail.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */


#ifndef __GST_CACATV_H__
#define __GST_CACATV_H__

#include <gst/gst.h>
#include <gst/video/gstvideofilter.h>
#include <gst/video/video.h>

#include <caca.h>
#ifdef CACA_API_VERSION_1
#   include <caca0.h>
#endif

G_BEGIN_DECLS

#define GST_TYPE_CACATV \
  (gst_cacatv_get_type())
#define GST_CACATV(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CACATV,GstCACATv))
#define GST_CACATV_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CACATV,GstCACATvClass))
#define GST_IS_CACATV(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CACATV))
#define GST_IS_CACATV_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CACATV))

typedef struct _GstCACATv GstCACATv;
typedef struct _GstCACATvClass GstCACATvClass;

struct _GstCACATv {
  GstVideoFilter videofilter;
  GstVideoInfo info;
  
  gint sink_width, sink_height;
  gint canvas_height, canvas_width;
  gint src_width,  src_height;
  gint font_index;
  
  guint dither_mode;
  gboolean antialiasing;
  
  caca_canvas_t *canvas;
  struct caca_dither *dither;
  caca_font_t *font;
};

struct _GstCACATvClass {
  GstVideoFilterClass parent_class;

  /* signals */
};

GType gst_cacatv_get_type(void);

G_END_DECLS

#endif /* __GST_CACATV_H__ */
