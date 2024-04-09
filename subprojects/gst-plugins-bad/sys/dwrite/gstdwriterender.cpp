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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstdwriterender.h"

GST_DEBUG_CATEGORY_EXTERN (dwrite_overlay_object_debug);
#define GST_CAT_DEFAULT dwrite_overlay_object_debug

static gboolean gst_dwrite_render_upload_default (GstDWriteRender * render,
    const GstVideoInfo * info, GstBuffer * in_buf, GstBuffer * out_buf);

#define gst_dwrite_render_parent_class parent_class
G_DEFINE_ABSTRACT_TYPE (GstDWriteRender, gst_dwrite_render, GST_TYPE_OBJECT);

static void
gst_dwrite_render_class_init (GstDWriteRenderClass * klass)
{
  klass->upload = gst_dwrite_render_upload_default;
}

static void
gst_dwrite_render_init (GstDWriteRender * self)
{
}

GstBuffer *
gst_dwrite_render_draw_layout (GstDWriteRender * render,
    IDWriteTextLayout * layout, gint x, gint y)
{
  auto klass = GST_DWRITE_RENDER_GET_CLASS (render);

  return klass->draw_layout (render, layout, x, y);
}

gboolean
gst_dwrite_render_blend (GstDWriteRender * render, GstBuffer * layout_buf,
    gint x, gint y, GstBuffer * output)
{
  auto klass = GST_DWRITE_RENDER_GET_CLASS (render);

  return klass->blend (render, layout_buf, x, y, output);
}

gboolean
gst_dwrite_render_update_device (GstDWriteRender * render, GstBuffer * buffer)
{
  auto klass = GST_DWRITE_RENDER_GET_CLASS (render);

  return klass->update_device (render, buffer);
}

gboolean
gst_dwrite_render_handle_allocation_query (GstDWriteRender * render,
    GstElement * elem, GstQuery * query)
{
  auto klass = GST_DWRITE_RENDER_GET_CLASS (render);

  return klass->handle_allocation_query (render, elem, query);
}

gboolean
gst_dwrite_render_can_inplace (GstDWriteRender * render, GstBuffer * buffer)
{
  auto klass = GST_DWRITE_RENDER_GET_CLASS (render);

  return klass->can_inplace (render, buffer);
}

static gboolean
gst_dwrite_render_upload_default (GstDWriteRender * self,
    const GstVideoInfo * info, GstBuffer * in_buf, GstBuffer * out_buf)
{
  GstVideoFrame in_frame, out_frame;
  gboolean ret;

  GST_TRACE_OBJECT (self, "system copy");

  if (!gst_video_frame_map (&in_frame, info, in_buf, GST_MAP_READ)) {
    GST_ERROR_OBJECT (self, "Couldn't map input frame");
    return FALSE;
  }

  if (!gst_video_frame_map (&out_frame, info, out_buf, GST_MAP_WRITE)) {
    gst_video_frame_unmap (&in_frame);
    GST_ERROR_OBJECT (self, "Couldn't map output frame");
    return FALSE;
  }

  ret = gst_video_frame_copy (&out_frame, &in_frame);

  gst_video_frame_unmap (&in_frame);
  gst_video_frame_unmap (&out_frame);

  return ret;
}

gboolean
gst_dwrite_render_upload (GstDWriteRender * render, const GstVideoInfo * info,
    GstBuffer * in_buf, GstBuffer * out_buf)
{
  auto klass = GST_DWRITE_RENDER_GET_CLASS (render);

  return klass->upload (render, info, in_buf, out_buf);
}
