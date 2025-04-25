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
#include "gsthipbasefilter.h"

G_BEGIN_DECLS

#define GST_TYPE_HIP_MEMORY_COPY             (gst_hip_memory_copy_get_type())
#define GST_HIP_MEMORY_COPY(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_HIP_MEMORY_COPY,GstHipMemoryCopy))
#define GST_HIP_MEMORY_COPY_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_HIP_MEMORY_COPY,GstHipMemoryCopyClass))
#define GST_HIP_MEMORY_COPY_GET_CLASS(obj)   (GST_HIP_MEMORY_COPY_CLASS(G_OBJECT_GET_CLASS(obj)))
#define GST_IS_HIP_MEMORY_COPY(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_HIP_MEMORY_COPY))
#define GST_IS_HIP_MEMORY_COPY_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_HIP_MEMORY_COPY))

typedef struct _GstHipMemoryCopy GstHipMemoryCopy;
typedef struct _GstHipMemoryCopyClass GstHipMemoryCopyClass;
typedef struct _GstHipMemoryCopyPrivate GstHipMemoryCopyPrivate;

struct _GstHipMemoryCopy
{
  GstHipBaseFilter parent;

  GstHipMemoryCopyPrivate *priv;
};

struct _GstHipMemoryCopyClass
{
  GstHipBaseFilterClass parent_class;
};

GType gst_hip_memory_copy_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstHipMemoryCopy, gst_object_unref)

#define GST_TYPE_HIP_UPLOAD (gst_hip_upload_get_type())
G_DECLARE_FINAL_TYPE (GstHipUpload,
    gst_hip_upload, GST, HIP_UPLOAD, GstHipMemoryCopy);

#define GST_TYPE_HIP_DOWNLOAD (gst_hip_download_get_type())
G_DECLARE_FINAL_TYPE (GstHipDownload,
    gst_hip_download, GST, HIP_DOWNLOAD, GstHipMemoryCopy);

G_END_DECLS

