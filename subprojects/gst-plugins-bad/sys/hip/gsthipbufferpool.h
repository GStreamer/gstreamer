/* GStreamer
 * Copyright (C) 2025 Seungha Yang <seungha@centricular.com>
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

#include <gst/gst.h>
#include <gst/video/video.h>
#include "gsthip_fwd.h"

G_BEGIN_DECLS

#define GST_TYPE_HIP_BUFFER_POOL             (gst_hip_buffer_pool_get_type ())
#define GST_HIP_BUFFER_POOL(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj),GST_TYPE_HIP_BUFFER_POOL,GstHipBufferPool))
#define GST_HIP_BUFFER_POOL_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_HIP_BUFFER_POOL,GstHipBufferPoolClass))
#define GST_HIP_BUFFER_POOL_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj),  GST_TYPE_HIP_BUFFER_POOL,GstHipBufferPoolClass))
#define GST_IS_HIP_BUFFER_POOL(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj),GST_TYPE_HIP_BUFFER_POOL))
#define GST_IS_HIP_BUFFER_POOL_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_HIP_BUFFER_POOL))
#define GST_HIP_BUFFER_POOL_CAST(obj)        ((GstHipBufferPool*)(obj))

struct _GstHipBufferPool
{
  GstBufferPool parent;

  GstHipDevice *device;

  GstHipBufferPoolPrivate *priv;
};

struct _GstHipBufferPoolClass
{
  GstBufferPoolClass parent_class;
};

GType           gst_hip_buffer_pool_get_type (void);

GstBufferPool * gst_hip_buffer_pool_new (GstHipDevice * device);

G_END_DECLS

