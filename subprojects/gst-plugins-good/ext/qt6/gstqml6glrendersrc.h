/*
 * GStreamer
 * Copyright (C) 2025 Matthew Waters, <matthew@centricular.com>
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

#ifndef __GST_QML6_GL_RENDER_SRC_H__
#define __GST_QML6_GL_RENDER_SRC_H__

#include <gst/gst.h>
#include <gst/gl/gl.h>
#include "qt6glrenderer.h"

G_BEGIN_DECLS

#define GST_TYPE_QML6_GL_RENDER_SRC (gst_qml6_gl_render_src_get_type())
G_DECLARE_FINAL_TYPE (GstQml6GLRenderSrc, gst_qml6_gl_render_src, GST, QML6_GL_RENDER_SRC, GstGLBaseSrc)
#define GST_QML6_GL_RENDER_SRC_CAST(obj) ((GstQml6GLRenderSrc*)(obj))

/**
 * GstQml6GLRenderSrc:
 *
 * Opaque #GstQml6GLRenderSrc object
 */
struct _GstQml6GLRenderSrc
{
  /* <private> */
  GstGLBaseSrc            parent;

  GstQt6QuickRenderer    *renderer;
  GstGLDisplay           *display;

  /* properties */
  QQuickItem             *root_item;
  char                   *qml_scene;
  // GST_TYPE_FRACTION
  GValue                  max_framerate;

  gboolean                initted;

  gboolean                render_on_demand;
  gboolean                flushing;
  GCond                   update_cond;
  GMutex                  update_lock;
  GstClockTime            last_render_time;
};

G_END_DECLS

#endif /* __GST_QML6_GL_RENDER_SRC_H__ */
