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

#ifndef __GST_GL_CONTEXT_GPU_PROCESS_H__
#define __GST_GL_CONTEXT_GPU_PROCESS_H__

#include <gst/gl/gl.h>

G_BEGIN_DECLS

#define GST_GL_TYPE_CONTEXT_GPU_PROCESS (gst_gl_context_gpu_process_get_type())
#define GST_GL_CONTEXT_GPU_PROCESS(o)           (G_TYPE_CHECK_INSTANCE_CAST((o), GST_GL_TYPE_CONTEXT_GPU_PROCESS, GstGLContextGPUProcess))
#define GST_GL_CONTEXT_GPU_PROCESS_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GST_GL_TYPE_CONTEXT_GPU_PROCESS, GstGLContextGPUProcessClass))
#define GST_GL_IS_CONTEXT_GPU_PROCESS(o)        (G_TYPE_CHECK_INSTANCE_TYPE((o), GST_GL_TYPE_CONTEXT_GPU_PROCESS))
#define GST_GL_IS_CONTEXT_GPU_PROCESS_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE((k), GST_GL_TYPE_CONTEXT_GPU_PROCESS))
#define GST_GL_CONTEXT_GPU_PROCESS_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS((o), GST_GL_TYPE_CONTEXT_GPU_PROCESS, GstGLContextGPUProcessClass))

typedef struct _GstGLContextGPUProcess GstGLContextGPUProcess;
typedef struct _GstGLContextGPUProcessPrivate GstGLContextGPUProcessPrivate;
typedef struct _GstGLContextGPUProcessClass GstGLContextGPUProcessClass;

typedef gpointer (* GstGLProcAddrFunc) (GstGLContext *context, const gchar *name);

/**
 * GstGLContextGPUProcess:
 *
 * Opaque #GstGLContextGPUProcess object
 */
struct _GstGLContextGPUProcess {
  GstGLContext parent;

  /*< private >*/
  GstGLContextGPUProcessPrivate *priv;

  /*< private >*/
  gpointer _reserved[GST_PADDING];
};

struct _GstGLContextGPUProcessClass {
  GstGLContextClass parent;

  /*< private >*/
  gpointer _reserved[GST_PADDING];
};

GType gst_gl_context_gpu_process_get_type (void);

GstGLContext * gst_gl_context_gpu_process_new (GstGLDisplay * display,
    GstGLAPI gl_api, GstGLProcAddrFunc proc_addr);

G_END_DECLS

#endif /* __GST_GL_CONTEXT_GPU_PROCESS_H__ */
