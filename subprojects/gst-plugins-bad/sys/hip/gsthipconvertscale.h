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

#define GST_TYPE_HIP_BASE_CONVERT             (gst_hip_base_convert_get_type())
#define GST_HIP_BASE_CONVERT(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_HIP_BASE_CONVERT,GstHipBaseConvert))
#define GST_HIP_BASE_CONVERT_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_HIP_BASE_CONVERT,GstHipBaseConvertClass))
#define GST_HIP_BASE_CONVERT_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_HIP_BASE_CONVERT,GstHipBaseConvertClass))
#define GST_IS_HIP_BASE_CONVERT(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_HIP_BASE_CONVERT))
#define GST_IS_HIP_BASE_CONVERT_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_HIP_BASE_CONVERT))

typedef struct _GstHipBaseConvert GstHipBaseConvert;
typedef struct _GstHipBaseConvertClass GstHipBaseConvertClass;
typedef struct _GstHipBaseConvertPrivate GstHipBaseConvertPrivate;

struct _GstHipBaseConvert
{
  GstHipBaseFilter parent;

  GstHipBaseConvertPrivate *priv;
};

struct _GstHipBaseConvertClass
{
  GstHipBaseFilter parent_class;
};

GType gst_hip_base_convert_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstHipBaseConvert, gst_object_unref)

#define GST_TYPE_HIP_CONVERT_SCALE (gst_hip_convert_scale_get_type())
G_DECLARE_FINAL_TYPE (GstHipConvertScale, gst_hip_convert_scale,
    GST, HIP_CONVERT_SCALE, GstHipBaseConvert)

#define GST_TYPE_HIP_CONVERT (gst_hip_convert_get_type())
G_DECLARE_FINAL_TYPE (GstHipConvert, gst_hip_convert,
    GST, HIP_CONVERT, GstHipBaseConvert)

#define GST_TYPE_HIP_SCALE (gst_hip_scale_get_type())
G_DECLARE_FINAL_TYPE (GstHipScale, gst_hip_scale,
    GST, HIP_SCALE, GstHipBaseConvert)

G_END_DECLS

