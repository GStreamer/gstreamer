/*
 * GStreamer
 * Copyright (C) 2022 Seungha Yang <seungha@centricular.com>
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
#include <gst/d3d11/gstd3d11_fwd.h>

G_BEGIN_DECLS

#define GST_TYPE_D3D11_STAGING_BUFFER_POOL            (gst_d3d11_staging_buffer_pool_get_type())
#define GST_D3D11_STAGING_BUFFER_POOL(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_D3D11_STAGING_BUFFER_POOL, GstD3D11StagingBufferPool))
#define GST_D3D11_STAGING_BUFFER_POOL_CLASS(klass)    (G_TYPE_CHECK_CLASS((klass), GST_TYPE_D3D11_STAGING_BUFFER_POOL, GstD3D11StagingBufferPoolClass))
#define GST_IS_D3D11_STAGING_BUFFER_POOL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_D3D11_STAGING_BUFFER_POOL))
#define GST_IS_D3D11_STAGING_BUFFER_POOL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_D3D11_STAGING_BUFFER_POOL))
#define GST_D3D11_STAGING_BUFFER_POOL_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_D3D11_STAGING_BUFFER_POOL, GstD3D11StagingBufferPoolClass))

struct _GstD3D11StagingBufferPool
{
  GstBufferPool parent;

  GstD3D11Device *device;

  /*< private >*/
  GstD3D11StagingBufferPoolPrivate *priv;

  gpointer _gst_reserved[GST_PADDING];
};

struct _GstD3D11StagingBufferPoolClass
{
  GstBufferPoolClass bufferpool_class;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

GST_D3D11_API
GType           gst_d3d11_staging_buffer_pool_get_type (void);

GST_D3D11_API
GstBufferPool * gst_d3d11_staging_buffer_pool_new      (GstD3D11Device * device);

G_END_DECLS

