/*
 * GStreamer
 * Copyright (C) 2015 Julien Isorce <j.isorce@samsung.com>
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

#define GLIB_DISABLE_DEPRECATION_WARNINGS

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstglwindow_gpu_process.h"

#define GST_GL_WINDOW_GPU_PROCESS_GET_PRIVATE(o)  \
  (G_TYPE_INSTANCE_GET_PRIVATE((o), GST_GL_TYPE_WINDOW_GPU_PROCESS, GstGLWindowGPUProcessPrivate))

#define GST_CAT_DEFAULT gst_gl_window_debug

#define gst_gl_window_gpu_process_parent_class parent_class
G_DEFINE_TYPE (GstGLWindowGPUProcess, gst_gl_window_gpu_process,
    GST_GL_TYPE_WINDOW);

struct _GstGLWindowGPUProcessPrivate
{
  int empty;
};

static void
gst_gl_window_gpu_process_class_init (GstGLWindowGPUProcessClass * klass)
{
  g_type_class_add_private (klass, sizeof (GstGLWindowGPUProcessPrivate));
}

static void
gst_gl_window_gpu_process_init (GstGLWindowGPUProcess * window)
{
  window->priv = GST_GL_WINDOW_GPU_PROCESS_GET_PRIVATE (window);
}

GstGLWindowGPUProcess *
gst_gl_window_gpu_process_new (GstGLDisplay * display)
{
  GstGLWindowGPUProcess *window =
      g_object_new (GST_GL_TYPE_WINDOW_GPU_PROCESS, NULL);

  GST_GL_WINDOW (window)->display = gst_object_ref (display);

  return window;
}
