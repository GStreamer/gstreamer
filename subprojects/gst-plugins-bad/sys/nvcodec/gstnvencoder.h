/* GStreamer
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

#ifdef HAVE_NVCODEC_GST_D3D11
#include <gst/d3d11/gstd3d11.h>
#endif

#include <string.h>

#include "nvEncodeAPI.h"
#include "gstnvenc.h"
#include "gstcudamemory.h"

G_BEGIN_DECLS

#define GST_TYPE_NV_ENCODER            (gst_nv_encoder_get_type())
#define GST_NV_ENCODER(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_NV_ENCODER, GstNvEncoder))
#define GST_NV_ENCODER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_NV_ENCODER, GstNvEncoderClass))
#define GST_IS_NV_ENCODER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_NV_ENCODER))
#define GST_IS_NV_ENCODER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_NV_ENCODER))
#define GST_NV_ENCODER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_NV_ENCODER, GstNvEncoderClass))
#define GST_NV_ENCODER_CAST(obj)       ((GstNvEncoder *)obj)

typedef struct _GstNvEncoder GstNvEncoder;
typedef struct _GstNvEncoderClass GstNvEncoderClass;
typedef struct _GstNvEncoderPrivate GstNvEncoderPrivate;

typedef enum
{
  GST_NV_ENCODER_RECONFIGURE_NONE,
  GST_NV_ENCODER_RECONFIGURE_BITRATE,
  GST_NV_ENCODER_RECONFIGURE_FULL,
} GstNvEncoderReconfigure;

#define GST_TYPE_NV_ENCODER_PRESET (gst_nv_encoder_preset_get_type())
GType gst_nv_encoder_preset_get_type (void);
typedef enum
{
  GST_NV_ENCODER_PRESET_DEFAULT,
  GST_NV_ENCODER_PRESET_HP,
  GST_NV_ENCODER_PRESET_HQ,
  GST_NV_ENCODER_PRESET_LOW_LATENCY_DEFAULT,
  GST_NV_ENCODER_PRESET_LOW_LATENCY_HQ,
  GST_NV_ENCODER_PRESET_LOW_LATENCY_HP,
  GST_NV_ENCODER_PRESET_LOSSLESS_DEFAULT,
  GST_NV_ENCODER_PRESET_LOSSLESS_HP,
} GstNvEncoderPreset;

#define GST_TYPE_NV_ENCODER_RC_MODE (gst_nv_encoder_rc_mode_get_type())
GType gst_nv_encoder_rc_mode_get_type (void);

typedef enum
{
  GST_NV_ENCODER_RC_MODE_CONSTQP,
  GST_NV_ENCODER_RC_MODE_VBR,
  GST_NV_ENCODER_RC_MODE_CBR,
  GST_NV_ENCODER_RC_MODE_CBR_LOWDELAY_HQ,
  GST_NV_ENCODER_RC_MODE_CBR_HQ,
  GST_NV_ENCODER_RC_MODE_VBR_HQ,
} GstNvEncoderRCMode;

typedef struct
{
  /* without ref */
  GstNvEncoder *encoder;

  /* Holds ownership */
  GstBuffer *buffer;
  GstMapInfo map_info;

  NV_ENC_REGISTER_RESOURCE register_resource;
  NV_ENC_MAP_INPUT_RESOURCE mapped_resource;

  /* Used when input resource cannot be registered */
  NV_ENC_CREATE_INPUT_BUFFER input_buffer;
  NV_ENC_LOCK_INPUT_BUFFER lk_input_buffer;

  NV_ENC_OUTPUT_PTR output_ptr;
  gpointer event_handle;
  gboolean is_eos;
} GstNvEncoderTask;

struct _GstNvEncoder
{
  GstVideoEncoder parent;

  GstNvEncoderPrivate *priv;
};

struct _GstNvEncoderClass
{
  GstVideoEncoderClass parent_class;

  gboolean    (*set_format)           (GstNvEncoder * encoder,
                                       GstVideoCodecState * state,
                                       gpointer session,
                                       NV_ENC_INITIALIZE_PARAMS * init_params,
                                       NV_ENC_CONFIG * config);

  gboolean    (*set_output_state)     (GstNvEncoder * encoder,
                                       GstVideoCodecState * state,
                                       gpointer session);

  GstBuffer * (*create_output_buffer) (GstNvEncoder * encoder,
                                       NV_ENC_LOCK_BITSTREAM * bitstream);

  GstNvEncoderReconfigure (*check_reconfigure)  (GstNvEncoder * encoder,
                                                 NV_ENC_CONFIG * config);
};

GType gst_nv_encoder_get_type (void);

guint gst_nv_encoder_get_task_size (GstNvEncoder * encoder);

const gchar * gst_nv_encoder_status_to_string (NVENCSTATUS status);
#define GST_NVENC_STATUS_FORMAT "s (%d)"
#define GST_NVENC_STATUS_ARGS(s) gst_nv_encoder_status_to_string (s), s

void gst_nv_encoder_preset_to_guid (GstNvEncoderPreset preset,
                                    GUID * guid);

NV_ENC_PARAMS_RC_MODE gst_nv_encoder_rc_mode_to_native (GstNvEncoderRCMode rc_mode);

void gst_nv_encoder_set_cuda_device_id (GstNvEncoder * encoder,
                                        guint device_id);

void gst_nv_encoder_set_dxgi_adapter_luid (GstNvEncoder * encoder,
                                           gint64 adapter_luid);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstNvEncoder, gst_object_unref)

G_END_DECLS

#ifdef __cplusplus
#ifndef G_OS_WIN32
inline bool is_equal_guid(const GUID & lhs, const GUID & rhs)
{
  return memcmp(&lhs, &rhs, sizeof (GUID)) == 0;
}

inline bool operator==(const GUID & lhs, const GUID & rhs)
{
  return is_equal_guid(lhs, rhs);
}

inline bool operator!=(const GUID & lhs, const GUID & rhs)
{
  return !is_equal_guid(lhs, rhs);
}
#endif /* G_OS_WIN32 */
#endif /* __cplusplus */
