/*
 * GStreamer
 * Copyright (C) 2009 Carl-Anton Ingmarsson <ca.ingmarsson@gmail.com>
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

#ifndef _GST_VDP_BUFFER_POOL_H_
#define _GST_VDP_BUFFER_POOL_H_

#include <gst/gst.h>

typedef struct _GstVdpBufferPool GstVdpBufferPool;

#include "gstvdpdevice.h"
#include "gstvdpbuffer.h"

G_BEGIN_DECLS

#define GST_TYPE_VDP_BUFFER_POOL             (gst_vdp_buffer_pool_get_type ())
#define GST_VDP_BUFFER_POOL(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_VDP_BUFFER_POOL, GstVdpBufferPool))
#define GST_VDP_BUFFER_POOL_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_VDP_BUFFER_POOL, GstVdpBufferPoolClass))
#define GST_IS_VDP_BUFFER_POOL(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VDP_BUFFER_POOL))
#define GST_IS_VDP_BUFFER_POOL_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_VDP_BUFFER_POOL))
#define GST_VDP_BUFFER_POOL_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_VDP_BUFFER_POOL, GstVdpBufferPoolClass))

typedef struct _GstVdpBufferPoolClass GstVdpBufferPoolClass;
typedef struct _GstVdpBufferPoolPrivate GstVdpBufferPoolPrivate;

struct _GstVdpBufferPool
{
  GObject object;

  GstVdpBufferPoolPrivate *priv;
};

struct _GstVdpBufferPoolClass
{
  GObjectClass object_class;

  GstVdpBuffer *(*alloc_buffer) (GstVdpBufferPool *bpool, GError **error);
  gboolean (*set_caps) (GstVdpBufferPool *bpool, const GstCaps *caps, gboolean *clear_bufs);
  gboolean (*check_caps) (GstVdpBufferPool *bpool, const GstCaps *caps);
};

gboolean gst_vdp_buffer_pool_put_buffer (GstVdpBufferPool *bpool, GstVdpBuffer *buf);
GstVdpBuffer *gst_vdp_buffer_pool_get_buffer (GstVdpBufferPool * bpool, GError **error);

void gst_vdp_buffer_pool_set_max_buffers (GstVdpBufferPool *bpool, guint max_buffers);
guint gst_vdp_buffer_pool_get_max_buffers (GstVdpBufferPool *bpool);

void gst_vdp_buffer_pool_set_caps (GstVdpBufferPool *bpool, const GstCaps *caps);
const GstCaps *gst_vdp_buffer_pool_get_caps (GstVdpBufferPool * bpool);

GstVdpDevice *gst_vdp_buffer_pool_get_device (GstVdpBufferPool * bpool);

GType gst_vdp_buffer_pool_get_type (void) G_GNUC_CONST;

G_END_DECLS

#endif /* _GST_VDP_BUFFER_POOL_H_ */
