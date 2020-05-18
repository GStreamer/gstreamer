/* GStreamer
 * Copyright (C) 2020 Seungha Yang <seungha.yang@navercorp.com>
 * Copyright (C) 2020 Seungha Yang <seungha@centricular.com>
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

#ifndef __GST_MF_TRANSFORM_OBJECT_H__
#define __GST_MF_TRANSFORM_OBJECT_H__

#include <gst/gst.h>
#include "gstmfutils.h"
#include <codecapi.h>
#include "gststrmif.h"

G_BEGIN_DECLS

#define GST_TYPE_MF_TRANSFORM_OBJECT  (gst_mf_transform_get_type())
G_DECLARE_FINAL_TYPE (GstMFTransform, gst_mf_transform,
    GST, MF_TRANSFORM, GstObject);

#define GST_MF_TRANSFORM_FLOW_NEED_DATA GST_FLOW_CUSTOM_SUCCESS

typedef struct _GstMFTransformEnumParams
{
  GUID category;
  guint32 enum_flags;
  MFT_REGISTER_TYPE_INFO *input_typeinfo;
  MFT_REGISTER_TYPE_INFO *output_typeinfo;

  guint device_index;
} GstMFTransformEnumParams;

GstMFTransform * gst_mf_transform_new             (GstMFTransformEnumParams * params);

gboolean        gst_mf_transform_open             (GstMFTransform * object);

IMFActivate *   gst_mf_transform_get_activate_handle (GstMFTransform * object);

IMFTransform *  gst_mf_transform_get_transform_handle (GstMFTransform * object);

ICodecAPI *     gst_mf_transform_get_codec_api_handle (GstMFTransform * object);

gboolean        gst_mf_transform_process_input    (GstMFTransform * object,
                                                   IMFSample * sample);

GstFlowReturn   gst_mf_transform_get_output       (GstMFTransform * object,
                                                   IMFSample ** sample);

gboolean        gst_mf_transform_flush            (GstMFTransform * object);

gboolean        gst_mf_transform_drain            (GstMFTransform * object);

gboolean        gst_mf_transform_get_input_available_types  (GstMFTransform * object,
                                                             GList ** input_types);

gboolean        gst_mf_transform_get_output_available_types (GstMFTransform * object,
                                                             GList ** output_types);

gboolean        gst_mf_transform_set_input_type  (GstMFTransform * object,
                                                  IMFMediaType * input_type);

gboolean        gst_mf_transform_set_output_type (GstMFTransform * object,
                                                  IMFMediaType * output_type);

gboolean        gst_mf_transform_get_input_current_type  (GstMFTransform * object,
                                                          IMFMediaType ** input_type);

gboolean        gst_mf_transform_get_output_current_type (GstMFTransform * object,
                                                          IMFMediaType ** output_type);

gboolean        gst_mf_transform_set_codec_api_uint32  (GstMFTransform * object,
                                                        const GUID * api,
                                                        guint32 value);

gboolean        gst_mf_transform_set_codec_api_uint64  (GstMFTransform * object,
                                                        const GUID * api,
                                                        guint64 value);

gboolean        gst_mf_transform_set_codec_api_boolean (GstMFTransform * object,
                                                        const GUID * api,
                                                        gboolean value);

G_END_DECLS

#endif /* __GST_MF_TRANSFORM_OBJECT_H__ */