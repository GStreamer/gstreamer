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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstglcontext_gpu_process.h"
#include "gstglwindow_gpu_process.h"

#define GST_GL_CONTEXT_GPU_PROCESS_GET_PRIVATE(o)  \
  (G_TYPE_INSTANCE_GET_PRIVATE((o), GST_GL_TYPE_CONTEXT_GPU_PROCESS, GstGLContextGPUProcessPrivate))

#define GST_CAT_DEFAULT gst_gl_context_debug

G_DEFINE_TYPE (GstGLContextGPUProcess, gst_gl_context_gpu_process,
    GST_GL_TYPE_CONTEXT);

struct _GstGLContextGPUProcessPrivate
{
  GstGLAPI gl_api;
};

static guintptr
gst_gl_context_gpu_process_get_gl_context (GstGLContext * context)
{
  return 0;
}

static GstGLAPI
gst_gl_context_gpu_process_get_gl_api (GstGLContext * context)
{
  return GST_GL_CONTEXT_GPU_PROCESS (context)->priv->gl_api;
}

static GstGLPlatform
gst_gl_context_gpu_process_get_gl_platform (GstGLContext * context)
{
  return GST_GL_PLATFORM_GPU_PROCESS;
}

static gboolean
gst_gl_context_gpu_process_activate (GstGLContext * context, gboolean activate)
{
  return TRUE;
}

static void
gst_gl_context_gpu_process_class_init (GstGLContextGPUProcessClass * klass)
{
  GstGLContextClass *context_class = (GstGLContextClass *) klass;

  g_type_class_add_private (klass, sizeof (GstGLContextGPUProcessPrivate));

  context_class->get_gl_context =
      GST_DEBUG_FUNCPTR (gst_gl_context_gpu_process_get_gl_context);
  context_class->get_gl_api =
      GST_DEBUG_FUNCPTR (gst_gl_context_gpu_process_get_gl_api);
  context_class->get_gl_platform =
      GST_DEBUG_FUNCPTR (gst_gl_context_gpu_process_get_gl_platform);
  context_class->activate =
      GST_DEBUG_FUNCPTR (gst_gl_context_gpu_process_activate);
}

static void
gst_gl_context_gpu_process_init (GstGLContextGPUProcess * context)
{
  context->priv = GST_GL_CONTEXT_GPU_PROCESS_GET_PRIVATE (context);
}

GstGLContext *
gst_gl_context_gpu_process_new (GstGLDisplay * display,
    GstGLAPI gl_api, GstGLProcAddrFunc proc_addr)
{
  GstGLContext *context = NULL;
  GstGLContextGPUProcess *gpu_context = NULL;
  GstGLContextClass *context_class = NULL;
  GstGLWindow *window = NULL;
  GError *error = NULL;
  g_return_val_if_fail ((gst_gl_display_get_gl_api (display) & gl_api) !=
      GST_GL_API_NONE, NULL);

  gpu_context = g_object_new (GST_GL_TYPE_CONTEXT_GPU_PROCESS, NULL);
  gpu_context->priv->gl_api = gl_api;

  context = GST_GL_CONTEXT (gpu_context);

  gst_gl_context_set_display (context, display);
  gst_gl_display_add_context (display, context);

  context_class = GST_GL_CONTEXT_GET_CLASS (context);

  context_class->get_current_context = NULL;
  context_class->get_proc_address = GST_DEBUG_FUNCPTR (proc_addr);

  gst_gl_context_activate (context, TRUE);
  gst_gl_context_fill_info (context, &error);

  if (error) {
    GST_ERROR_OBJECT (context, "Failed to create gpu process context: %s",
        error->message);
    g_error_free (error);
    gst_object_unref (context);
    return NULL;
  }

  window = GST_GL_WINDOW (gst_gl_window_gpu_process_new (display));
  gst_gl_context_set_window (context, window);
  GST_GL_WINDOW_GET_CLASS (window)->open (context->window, NULL);
  gst_object_unref (window);

  return context;
}
