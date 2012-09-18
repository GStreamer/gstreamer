/*
 * GStreamer
 * Copyright (C) 2012 Matthew Waters <ystreet00@gmail.com>
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

#ifndef _GST_GL_BUFFER_POOL_H_
#define _GST_GL_BUFFER_POOL_H_

#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>
#include <gst/video/gstvideopool.h>

#include "gstgldisplay.h"
#include "gstglmemory.h"

G_BEGIN_DECLS

typedef struct _GstGLBufferPool GstGLBufferPool;
typedef struct _GstGLBufferPoolClass GstGLBufferPoolClass;
typedef struct _GstGLBufferPoolPrivate GstGLBufferPoolPrivate;

/* buffer pool functions */
#define GST_TYPE_GL_BUFFER_POOL      (gst_gl_buffer_pool_get_type())
#define GST_IS_GL_BUFFER_POOL(obj)   (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_GL_BUFFER_POOL))
#define GST_GL_BUFFER_POOL(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_GL_BUFFER_POOL, GstGLBufferPool))
#define GST_GL_BUFFER_POOL_CAST(obj) ((GstGLBufferPool*)(obj))

struct _GstGLBufferPool
{
  GstBufferPool bufferpool;

  GstGLDisplay *display;

  GstGLBufferPoolPrivate *priv;
};

struct _GstGLBufferPoolClass
{
  GstBufferPoolClass parent_class;
};

GType gst_gl_buffer_pool_get_type (void);
GstBufferPool *gst_gl_buffer_pool_new (GstGLDisplay * display);

G_END_DECLS

#endif /* _GST_GL_BUFFER_POOL_H_ */
