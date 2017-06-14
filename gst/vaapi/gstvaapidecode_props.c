/*
 *  gstvaapidecode_props.c - VA-API decoders specific properties
 *
 *  Copyright (C) 2017 Intel Corporation
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
 *    Author: Victor Jaquez <vjaquez@igalia.com>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this program; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

#include "gstvaapidecode_props.h"

enum
{
  GST_VAAPI_DECODER_H264_PROP_FORCE_LOW_LATENCY = 1,
};

static gint h264_private_offset;

static void
gst_vaapi_decode_h264_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVaapiDecodeH264Private *priv;

  priv = gst_vaapi_decode_h264_get_instance_private (object);

  switch (prop_id) {
    case GST_VAAPI_DECODER_H264_PROP_FORCE_LOW_LATENCY:
      g_value_set_boolean (value, priv->is_low_latency);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vaapi_decode_h264_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVaapiDecodeH264Private *priv;

  priv = gst_vaapi_decode_h264_get_instance_private (object);

  switch (prop_id) {
    case GST_VAAPI_DECODER_H264_PROP_FORCE_LOW_LATENCY:
      priv->is_low_latency = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

void
gst_vaapi_decode_h264_install_properties (GObjectClass * klass)
{
  h264_private_offset = sizeof (GstVaapiDecodeH264Private);
  g_type_class_adjust_private_offset (klass, &h264_private_offset);

  klass->get_property = gst_vaapi_decode_h264_get_property;
  klass->set_property = gst_vaapi_decode_h264_set_property;

  g_object_class_install_property (klass,
      GST_VAAPI_DECODER_H264_PROP_FORCE_LOW_LATENCY,
      g_param_spec_boolean ("low-latency", "Force low latency mode",
          "When enabled, frames will be pushed as soon as they are available. "
          "It might violate the H.264 spec.", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT));
}

GstVaapiDecodeH264Private *
gst_vaapi_decode_h264_get_instance_private (gpointer self)
{
  if (h264_private_offset == 0)
    return NULL;
  return (G_STRUCT_MEMBER_P (self, h264_private_offset));
}
