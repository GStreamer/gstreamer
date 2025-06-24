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

#include "gsthip.h"

G_BEGIN_DECLS

#define GST_TYPE_HIP_CONVERTER             (gst_hip_converter_get_type())
#define GST_HIP_CONVERTER(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_HIP_CONVERTER,GstHipConverter))
#define GST_HIP_CONVERTER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_HIP_CONVERTER,GstHipConverterClass))
#define GST_HIP_CONVERTER_GET_CLASS(obj)   (GST_HIP_CONVERTER_CLASS(G_OBJECT_GET_CLASS(obj)))
#define GST_IS_HIP_CONVERTER(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_HIP_CONVERTER))
#define GST_IS_HIP_CONVERTER_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_HIP_CONVERTER))
#define GST_HIP_CONVERTER_CAST(obj)        ((GstHipConverter*)(obj))

typedef struct _GstHipConverter GstHipConverter;
typedef struct _GstHipConverterClass GstHipConverterClass;
typedef struct _GstHipConverterPrivate GstHipConverterPrivate;

struct _GstHipConverter
{
  GstObject parent;

  GstHipDevice *device;

  /*< private >*/
  GstHipConverterPrivate *priv;
  gpointer _gst_reserved[GST_PADDING];
};

struct _GstHipConverterClass
{
  GstObjectClass parent_class;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

GType gst_hip_converter_get_type (void);

GstHipConverter *  gst_hip_converter_new (GstHipDevice * device,
                                          const GstVideoInfo * in_info,
                                          const GstVideoInfo * out_info,
                                          GstStructure * config);

gboolean           gst_hip_converter_convert_frame (GstHipConverter * converter,
                                                    GstBuffer * in_buf,
                                                    GstBuffer * out_buf);

G_END_DECLS
