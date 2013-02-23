/* GStreamer
 * Copyright (C) <2005> Julien Moutte <julien@moutte.net>
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

#ifndef __GST_XVIMAGEPOOL_H__
#define __GST_XVIMAGEPOOL_H__

#ifdef HAVE_XSHM
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#endif /* HAVE_XSHM */

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#ifdef HAVE_XSHM
#include <X11/extensions/XShm.h>
#endif /* HAVE_XSHM */

#include <string.h>
#include <math.h>


G_BEGIN_DECLS

typedef struct _GstXvImageMemory GstXvImageMemory;

typedef struct _GstXvImageBufferPool GstXvImageBufferPool;
typedef struct _GstXvImageBufferPoolClass GstXvImageBufferPoolClass;
typedef struct _GstXvImageBufferPoolPrivate GstXvImageBufferPoolPrivate;

#include "xvimagesink.h"

/**
 * GstXvImageMemory:
 * @sink: a reference to the our #GstXvImageSink
 * @xvimage: the XvImage of this buffer
 * @width: the width in pixels of XvImage @xvimage
 * @height: the height in pixels of XvImage @xvimage
 * @im_format: the format of XvImage @xvimage
 * @size: the size in bytes of XvImage @xvimage
 *
 * Subclass of #GstMemory containing additional information about an XvImage.
 */
struct _GstXvImageMemory
{
  GstMemory parent;

  /* Reference to the xvimagesink we belong to */
  GstXvImageSink *sink;

  XvImage *xvimage;

#ifdef HAVE_XSHM
  XShmSegmentInfo SHMInfo;
#endif                          /* HAVE_XSHM */

  gint x, y;
  gint width, height;
  gint im_format;
  size_t size;
};

/* buffer pool functions */
#define GST_TYPE_XVIMAGE_BUFFER_POOL      (gst_xvimage_buffer_pool_get_type())
#define GST_IS_XVIMAGE_BUFFER_POOL(obj)   (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_XVIMAGE_BUFFER_POOL))
#define GST_XVIMAGE_BUFFER_POOL(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_XVIMAGE_BUFFER_POOL, GstXvImageBufferPool))
#define GST_XVIMAGE_BUFFER_POOL_CAST(obj) ((GstXvImageBufferPool*)(obj))

struct _GstXvImageBufferPool
{
  GstBufferPool bufferpool;

  GstXvImageSink *sink;
  GstAllocator *allocator;

  GstXvImageBufferPoolPrivate *priv;
};

struct _GstXvImageBufferPoolClass
{
  GstBufferPoolClass parent_class;
};

GType gst_xvimage_buffer_pool_get_type (void);

GstBufferPool *gst_xvimage_buffer_pool_new (GstXvImageSink * xvimagesink);

gboolean gst_xvimagesink_check_xshm_calls (GstXvImageSink * xvimagesink,
      GstXContext * xcontext);

gint gst_xvimagesink_get_format_from_info (GstXvImageSink * xvimagesink,
    GstVideoInfo * info);

G_END_DECLS

#endif /*__GST_XVIMAGEPOOL_H__*/
