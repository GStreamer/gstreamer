/* GStreamer
 * Copyright (C) 2026 Azat Nurgaliev <azat.nurg@gmail.com>
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
#include <gst/base/gstbasetransform.h>
#include <components/Component.h>
#include <core/Context.h>
#include <core/Surface.h>
#include "gstamfutils.h"
#include "gstamfplatform.h"

G_BEGIN_DECLS

#define GST_TYPE_AMF_BASE_FILTER            (gst_amf_base_filter_get_type())
#define GST_AMF_BASE_FILTER(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_AMF_BASE_FILTER, GstAmfBaseFilter))
#define GST_AMF_BASE_FILTER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_AMF_BASE_FILTER, GstAmfBaseFilterClass))
#define GST_IS_AMF_BASE_FILTER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_AMF_BASE_FILTER))
#define GST_IS_AMF_BASE_FILTER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_AMF_BASE_FILTER))
#define GST_AMF_BASE_FILTER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_AMF_BASE_FILTER, GstAmfBaseFilterClass))
#define GST_AMF_BASE_FILTER_CAST(obj)       ((GstAmfBaseFilter *)(obj))

typedef struct _GstAmfBaseFilter GstAmfBaseFilter;
typedef struct _GstAmfBaseFilterClass GstAmfBaseFilterClass;
typedef struct _GstAmfBaseFilterPrivate GstAmfBaseFilterPrivate;

/**
 * GstAmfBaseFilter:
 *
 * Abstract base class for AMF VPP-style filter elements (color
 * converter, HQ scaler...). It owns the AMF context and the
 * AMFComponent and implements the GstBaseTransform vmethods that are
 * common to every AMF post-processor.
 */
struct _GstAmfBaseFilter
{
  GstBaseTransform parent;

  /* Negotiated input/output info, filled in set_caps() before the
   * subclass sees configure_component(). Subclasses can read these. */
  GstVideoInfo in_info;
  GstVideoInfo out_info;

  GstAmfBaseFilterPrivate *priv;
};

/**
 * GstAmfBaseFilterClass:
 * @get_component_id: returns the wide-string AMF component name to
 *   instantiate (e.g. AMFVideoConverter, AMFHQScaler).
 * @configure_component: called after the AMFComponent has been
 *   created (but before Init()) so that the subclass can apply its
 *   component-specific properties.
 * @validate_caps: optional; called after caps have been parsed so
 *   the subclass can refuse a particular configuration (e.g. the HQ
 *   scaler refuses downscaling).
 */
struct _GstAmfBaseFilterClass
{
  GstBaseTransformClass parent_class;

  const wchar_t *(*get_component_id)        (GstAmfBaseFilter * self);

  gboolean       (*configure_component)     (GstAmfBaseFilter * self,
                                             amf::AMFComponent * comp,
                                             const GstVideoInfo * in_info,
                                             const GstVideoInfo * out_info);

  gboolean       (*validate_caps)           (GstAmfBaseFilter * self,
                                             const GstVideoInfo * in_info,
                                             const GstVideoInfo * out_info);
};

GType gst_amf_base_filter_get_type (void);

/* Set by registrators: which adapter / Vulkan device to bind to and
 * what the cdata-derived pad templates look like. Mirrors
 * gst_amf_encoder_set_subclass_data(). */
void  gst_amf_base_filter_set_subclass_data (GstAmfBaseFilter * filter,
                                             gint64 adapter_luid,
                                             guint device_index);

/* Returns the live AMFContext (do not unref). NULL until start(). */
amf::AMFContext * gst_amf_base_filter_get_context (GstAmfBaseFilter * filter);

/* Returns the platform device (GstD3D11Device * on Windows,
 * GstVulkanDevice * on Linux). Not reffed. */
GST_AMF_PLATFORM_DEVICE * gst_amf_base_filter_get_device (GstAmfBaseFilter * filter);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstAmfBaseFilter, gst_object_unref)

G_END_DECLS
