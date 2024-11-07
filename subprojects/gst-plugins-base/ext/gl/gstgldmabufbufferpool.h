/*
 * GStreamer
 * Copyright © 2024 Advanced Micro Devices, Inc.
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

#ifndef _GST_GL_DMABUF_BUFFER_POOL_H_
#define _GST_GL_DMABUF_BUFFER_POOL_H_

#include <gst/gl/gstglbufferpool.h>

G_BEGIN_DECLS

typedef struct _GstGLDMABufBufferPoolPrivate GstGLDMABufBufferPoolPrivate;

/**
 * GST_TYPE_GL_DMABUF_BUFFER_POOL:
 *
 * Since: 1.26
 */
#define GST_TYPE_GL_DMABUF_BUFFER_POOL (gst_gl_dmabuf_buffer_pool_get_type())
G_DECLARE_FINAL_TYPE (GstGLDMABufBufferPool, gst_gl_dmabuf_buffer_pool, GST, GL_DMABUF_BUFFER_POOL, GstGLBufferPool)

/**
 * GstGLDMABufBufferPool:
 *
 * Opaque GstGLDMABufBufferPool struct
 *
 * Since: 1.26
 */
struct _GstGLDMABufBufferPool
{
  GstGLBufferPool parent;

  /*< private >*/
  GstGLDMABufBufferPoolPrivate *priv;
};

GstBufferPool *gst_gl_dmabuf_buffer_pool_new (GstGLContext * context, GstBufferPool * dmabuf_pool,
                                              GstCaps * dmabuf_caps);

gboolean gst_is_gl_dmabuf_buffer (GstBuffer * buffer);

GstBuffer *gst_gl_dmabuf_buffer_unwrap (GstBuffer * buffer);

G_END_DECLS

#endif /* _GST_GL_DMABUF_BUFFER_POOL_H_ */
