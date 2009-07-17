/* cairorender: CAIRO plugin for GStreamer
 * 
 * Copyright (C) 2006-2009 Lutz Mueller <lutz@topfrose.de>
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

#ifndef __GST_CAIRO_RENDER_H_
#define __GST_CAIRO_RENDER_H__

#include <gst/gst.h>
#include <cairo.h>

G_BEGIN_DECLS

#define GST_TYPE_CAIRO_RENDER         (gst_cairo_render_get_type())
#define GST_CAIRO_RENDER(obj)         (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CAIRO_RENDER,GstCairoRender))
#define GST_CAIRO_RENDER_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CAIRO_RENDER,GstCairoRenderClass))

typedef struct _GstCairoRender      GstCairoRender;
typedef struct _GstCairoRenderClass GstCairoRenderClass;

struct _GstCairoRender
{
  GstElement parent;

  GstPad *snk, *src;

  /* < private > */

  /* Source */
  cairo_surface_t *surface;
  gint width, height, stride;

  /* Sink */
  gint64 offset, duration;
  gboolean png;
  cairo_format_t format;
};

struct _GstCairoRenderClass
{
  GstElementClass parent_class;
};

GType gst_cairo_render_get_type (void) G_GNUC_CONST;

G_END_DECLS

#endif /* __GST_CAIRO_RENDER_H__ */
