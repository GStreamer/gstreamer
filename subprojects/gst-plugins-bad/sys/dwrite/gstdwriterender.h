/* GStreamer
 * Copyright (C) 2024 Seungha Yang <seungha@centricular.com>
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

#pragma once

#include "gstdwriteoverlayobject.h"

G_BEGIN_DECLS

#define GST_TYPE_DWRITE_RENDER (gst_dwrite_render_get_type ())
G_DECLARE_DERIVABLE_TYPE (GstDWriteRender,
    gst_dwrite_render, GST, DWRITE_RENDER, GstObject);

struct _GstDWriteRenderClass
{
  GstObjectClass parent_class;

  GstBuffer * (*draw_layout) (GstDWriteRender * render,
                              IDWriteTextLayout * layout,
                              gint x,
                              gint y);

  gboolean    (*blend)       (GstDWriteRender * render,
                              GstBuffer * layout_buf,
                              gint x,
                              gint y,
                              GstBuffer * output);

  gboolean    (*update_device) (GstDWriteRender * renderer,
                                GstBuffer * buffer);

  gboolean    (*handle_allocation_query)  (GstDWriteRender * render,
                                           GstElement * elem,
                                           GstQuery * query);

  gboolean    (*can_inplace)              (GstDWriteRender * render,
                                           GstBuffer * buffer);

  gboolean    (*upload)                   (GstDWriteRender * render,
                                           const GstVideoInfo * info,
                                           GstBuffer * in_buf,
                                           GstBuffer * out_buf);
};

GstBuffer * gst_dwrite_render_draw_layout (GstDWriteRender * render,
                                           IDWriteTextLayout * layout,
                                           gint x,
                                           gint y);

gboolean    gst_dwrite_render_blend       (GstDWriteRender * render,
                                           GstBuffer * layout_buf,
                                           gint x,
                                           gint y,
                                           GstBuffer * output);

gboolean    gst_dwrite_render_update_device (GstDWriteRender * render,
                                             GstBuffer * buffer);

gboolean    gst_dwrite_render_handle_allocation_query (GstDWriteRender * render,
                                                       GstElement * elem,
                                                       GstQuery * query);

gboolean    gst_dwrite_render_can_inplace (GstDWriteRender * render,
                                           GstBuffer * buffer);

gboolean    gst_dwrite_render_upload      (GstDWriteRender * render,
                                           const GstVideoInfo * info,
                                           GstBuffer * in_buf,
                                           GstBuffer * out_buf);

G_END_DECLS
