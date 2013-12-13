/*
 *  gstvaapipluginbase.h - Base GStreamer VA-API Plugin element
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@splitted-desktop.com>
 *  Copyright (C) 2011-2013 Intel Corporation
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

#ifndef GST_VAAPI_PLUGIN_BASE_H
#define GST_VAAPI_PLUGIN_BASE_H

#include <gst/base/gstbasetransform.h>
#include <gst/video/gstvideodecoder.h>
#include <gst/video/gstvideoencoder.h>
#include <gst/video/gstvideosink.h>
#include <gst/vaapi/gstvaapidisplay.h>

G_BEGIN_DECLS

typedef struct _GstVaapiPluginBase GstVaapiPluginBase;
typedef struct _GstVaapiPluginBaseClass GstVaapiPluginBaseClass;

#define GST_VAAPI_PLUGIN_BASE(plugin) \
  ((GstVaapiPluginBase *)(plugin))
#define GST_VAAPI_PLUGIN_BASE_CLASS(plugin) \
  ((GstVaapiPluginBaseClass *)(plugin))
#define GST_VAAPI_PLUGIN_BASE_GET_CLASS(plugin) \
  GST_VAAPI_PLUGIN_BASE_CLASS(GST_ELEMENT_GET_CLASS( \
      GST_VAAPI_PLUGIN_BASE_ELEMENT(plugin)))
#define GST_VAAPI_PLUGIN_BASE_PARENT(plugin) \
  (&GST_VAAPI_PLUGIN_BASE(plugin)->parent_instance)
#define GST_VAAPI_PLUGIN_BASE_PARENT_CLASS(plugin) \
  (&GST_VAAPI_PLUGIN_BASE_CLASS(plugin)->parent_class)
#define GST_VAAPI_PLUGIN_BASE_ELEMENT(plugin) \
  (&GST_VAAPI_PLUGIN_BASE_PARENT(plugin)->element)
#define GST_VAAPI_PLUGIN_BASE_ELEMENT_CLASS(plugin) \
  (&GST_VAAPI_PLUGIN_BASE_PARENT_CLASS(plugin)->element)
#define GST_VAAPI_PLUGIN_BASE_DECODER(plugin) \
  (&GST_VAAPI_PLUGIN_BASE_PARENT(plugin)->decoder)
#define GST_VAAPI_PLUGIN_BASE_DECODER_CLASS(plugin) \
  (&GST_VAAPI_PLUGIN_BASE_PARENT_CLASS(plugin)->decoder)
#define GST_VAAPI_PLUGIN_BASE_ENCODER(plugin) \
  (&GST_VAAPI_PLUGIN_BASE_PARENT(plugin)->encoder)
#define GST_VAAPI_PLUGIN_BASE_ENCODER_CLASS(plugin) \
  (&GST_VAAPI_PLUGIN_BASE_PARENT_CLASS(plugin)->encoder)
#define GST_VAAPI_PLUGIN_BASE_TRANSFORM(plugin) \
  (&GST_VAAPI_PLUGIN_BASE_PARENT(plugin)->transform)
#define GST_VAAPI_PLUGIN_BASE_TRANSFORM_CLASS(plugin) \
  (&GST_VAAPI_PLUGIN_BASE_PARENT_CLASS(plugin)->transform)
#define GST_VAAPI_PLUGIN_BASE_SINK(plugin) \
  (&GST_VAAPI_PLUGIN_BASE_PARENT(plugin)->sink)
#define GST_VAAPI_PLUGIN_BASE_SINK_CLASS(plugin) \
  (&GST_VAAPI_PLUGIN_BASE_PARENT_CLASS(plugin)->sink)

#define GST_VAAPI_PLUGIN_BASE_DISPLAY(plugin) \
  (GST_VAAPI_PLUGIN_BASE(plugin)->display)
#define GST_VAAPI_PLUGIN_BASE_DISPLAY_REPLACE(plugin, new_display) \
  (gst_vaapi_display_replace(&GST_VAAPI_PLUGIN_BASE_DISPLAY(plugin), \
       (new_display)))

struct _GstVaapiPluginBase
{
  /*< private >*/
  union
  {
    GstElement element;
    GstVideoDecoder decoder;
    GstVideoEncoder encoder;
    GstBaseTransform transform;
    GstVideoSink sink;
  } parent_instance;

  GstDebugCategory *debug_category;

  GstVaapiDisplay *display;
};

struct _GstVaapiPluginBaseClass
{
  /*< private >*/
  union
  {
    GstElementClass element;
    GstVideoDecoderClass decoder;
    GstVideoEncoderClass encoder;
    GstBaseTransformClass transform;
    GstVideoSinkClass sink;
  } parent_class;
};

G_GNUC_INTERNAL
void
gst_vaapi_plugin_base_class_init (GstVaapiPluginBaseClass * klass);

G_GNUC_INTERNAL
void
gst_vaapi_plugin_base_init (GstVaapiPluginBase * plugin,
    GstDebugCategory * debug_category);

G_GNUC_INTERNAL
void
gst_vaapi_plugin_base_finalize (GstVaapiPluginBase * plugin);

G_GNUC_INTERNAL
gboolean
gst_vaapi_plugin_base_open (GstVaapiPluginBase * plugin);

G_GNUC_INTERNAL
void
gst_vaapi_plugin_base_close (GstVaapiPluginBase * plugin);

G_END_DECLS

#endif /* GST_VAAPI_PLUGIN_BASE_H */
