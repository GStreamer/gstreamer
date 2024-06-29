/* GStreamer
 * Copyright (C) 2024 Seungha Yang <seungha@centricular.com>
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

#include <gst/base/gstbasetransform.h>

G_BEGIN_DECLS

#define GST_TYPE_D3D12_MEMORY_COPY             (gst_d3d12_memory_copy_get_type())
#define GST_D3D12_MEMORY_COPY(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_D3D12_MEMORY_COPY,GstD3D12MemoryCopy))
#define GST_D3D12_MEMORY_COPY_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_D3D12_MEMORY_COPY,GstD3D12MemoryCopyClass))
#define GST_D3D12_MEMORY_COPY_GET_CLASS(obj)   (GST_D3D12_MEMORY_COPY_CLASS(G_OBJECT_GET_CLASS(obj)))
#define GST_IS_D3D12_MEMORY_COPY(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_D3D12_MEMORY_COPY))
#define GST_IS_D3D12_MEMORY_COPY_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_D3D12_MEMORY_COPY))

typedef struct _GstD3D12MemoryCopy GstD3D12MemoryCopy;
typedef struct _GstD3D12MemoryCopyClass GstD3D12MemoryCopyClass;
typedef struct _GstD3D12MemoryCopyPrivate GstD3D12MemoryCopyPrivate;

struct _GstD3D12MemoryCopy
{
  GstBaseTransform parent;

  GstD3D12MemoryCopyPrivate *priv;
};

struct _GstD3D12MemoryCopyClass
{
  GstBaseTransformClass parent_class;
};

GType gst_d3d12_memory_copy_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstD3D12MemoryCopy, gst_object_unref)

#define GST_TYPE_D3D12_UPLOAD (gst_d3d12_upload_get_type())
G_DECLARE_FINAL_TYPE (GstD3D12Upload,
    gst_d3d12_upload, GST, D3D12_UPLOAD, GstD3D12MemoryCopy);

#define GST_TYPE_D3D12_DOWNLOAD (gst_d3d12_download_get_type())
G_DECLARE_FINAL_TYPE (GstD3D12Download,
    gst_d3d12_download, GST, D3D12_DOWNLOAD, GstD3D12MemoryCopy);

G_END_DECLS

