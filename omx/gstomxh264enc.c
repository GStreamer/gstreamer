/*
 * Copyright (C) 2011, Hewlett-Packard Development Company, L.P.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>, Collabora Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>

#include "gstomxh264enc.h"

#ifdef USE_OMX_TARGET_RPI
#include <OMX_Broadcom.h>
#include <OMX_Index.h>
#endif

GST_DEBUG_CATEGORY_STATIC (gst_omx_h264_enc_debug_category);
#define GST_CAT_DEFAULT gst_omx_h264_enc_debug_category

/* prototypes */
static gboolean gst_omx_h264_enc_set_format (GstOMXVideoEnc * enc,
    GstOMXPort * port, GstVideoCodecState * state);
static GstCaps *gst_omx_h264_enc_get_caps (GstOMXVideoEnc * enc,
    GstOMXPort * port, GstVideoCodecState * state);
static GstFlowReturn gst_omx_h264_enc_handle_output_frame (GstOMXVideoEnc *
    self, GstOMXPort * port, GstOMXBuffer * buf, GstVideoCodecFrame * frame);
static gboolean gst_omx_h264_enc_flush (GstVideoEncoder * enc);
static gboolean gst_omx_h264_enc_stop (GstVideoEncoder * enc);
static void gst_omx_h264_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_omx_h264_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

enum
{
  PROP_0,
#ifdef USE_OMX_TARGET_RPI
  PROP_INLINESPSPPSHEADERS,
#endif
  PROP_PERIODICITYOFIDRFRAMES,
  PROP_INTERVALOFCODINGINTRAFRAMES
};

#ifdef USE_OMX_TARGET_RPI
#define GST_OMX_H264_VIDEO_ENC_INLINE_SPS_PPS_HEADERS_DEFAULT      TRUE
#endif
#define GST_OMX_H264_VIDEO_ENC_PERIODICITY_OF_IDR_FRAMES_DEFAULT    (0xffffffff)
#define GST_OMX_H264_VIDEO_ENC_INTERVAL_OF_CODING_INTRA_FRAMES_DEFAULT (0xffffffff)


/* class initialization */

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_omx_h264_enc_debug_category, "omxh264enc", 0, \
      "debug category for gst-omx video encoder base class");

#define parent_class gst_omx_h264_enc_parent_class
G_DEFINE_TYPE_WITH_CODE (GstOMXH264Enc, gst_omx_h264_enc,
    GST_TYPE_OMX_VIDEO_ENC, DEBUG_INIT);

static void
gst_omx_h264_enc_class_init (GstOMXH264EncClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstVideoEncoderClass *basevideoenc_class = GST_VIDEO_ENCODER_CLASS (klass);
  GstOMXVideoEncClass *videoenc_class = GST_OMX_VIDEO_ENC_CLASS (klass);

  videoenc_class->set_format = GST_DEBUG_FUNCPTR (gst_omx_h264_enc_set_format);
  videoenc_class->get_caps = GST_DEBUG_FUNCPTR (gst_omx_h264_enc_get_caps);

  gobject_class->set_property = gst_omx_h264_enc_set_property;
  gobject_class->get_property = gst_omx_h264_enc_get_property;

#ifdef USE_OMX_TARGET_RPI
  g_object_class_install_property (gobject_class, PROP_INLINESPSPPSHEADERS,
      g_param_spec_boolean ("inline-header",
          "Inline SPS/PPS headers before IDR",
          "Inline SPS/PPS header before IDR",
          GST_OMX_H264_VIDEO_ENC_INLINE_SPS_PPS_HEADERS_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));
#endif

  g_object_class_install_property (gobject_class, PROP_PERIODICITYOFIDRFRAMES,
      g_param_spec_uint ("periodicty-idr", "Target Bitrate",
          "Periodicity of IDR frames (0xffffffff=component default)",
          0, G_MAXUINT,
          GST_OMX_H264_VIDEO_ENC_PERIODICITY_OF_IDR_FRAMES_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class,
      PROP_INTERVALOFCODINGINTRAFRAMES,
      g_param_spec_uint ("interval-intraframes",
          "Interval of coding Intra frames",
          "Interval of coding Intra frames (0xffffffff=component default)", 0,
          G_MAXUINT,
          GST_OMX_H264_VIDEO_ENC_INTERVAL_OF_CODING_INTRA_FRAMES_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  basevideoenc_class->flush = gst_omx_h264_enc_flush;
  basevideoenc_class->stop = gst_omx_h264_enc_stop;

  videoenc_class->cdata.default_src_template_caps = "video/x-h264, "
      "width=(int) [ 16, 4096 ], " "height=(int) [ 16, 4096 ]";
  videoenc_class->handle_output_frame =
      GST_DEBUG_FUNCPTR (gst_omx_h264_enc_handle_output_frame);

  gst_element_class_set_static_metadata (element_class,
      "OpenMAX H.264 Video Encoder",
      "Codec/Encoder/Video",
      "Encode H.264 video streams",
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");

  gst_omx_set_default_role (&videoenc_class->cdata, "video_encoder.avc");
}

static void
gst_omx_h264_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstOMXH264Enc *self = GST_OMX_H264_ENC (object);

  switch (prop_id) {
#ifdef USE_OMX_TARGET_RPI
    case PROP_INLINESPSPPSHEADERS:
      self->inline_sps_pps_headers = g_value_get_boolean (value);
      break;
#endif
    case PROP_PERIODICITYOFIDRFRAMES:
      self->periodicty_idr = g_value_get_uint (value);
      break;
    case PROP_INTERVALOFCODINGINTRAFRAMES:
      self->interval_intraframes = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_omx_h264_enc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstOMXH264Enc *self = GST_OMX_H264_ENC (object);

  switch (prop_id) {
#ifdef USE_OMX_TARGET_RPI
    case PROP_INLINESPSPPSHEADERS:
      g_value_set_boolean (value, self->inline_sps_pps_headers);
      break;
#endif
    case PROP_PERIODICITYOFIDRFRAMES:
      g_value_set_uint (value, self->periodicty_idr);
      break;
    case PROP_INTERVALOFCODINGINTRAFRAMES:
      g_value_set_uint (value, self->interval_intraframes);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_omx_h264_enc_init (GstOMXH264Enc * self)
{
#ifdef USE_OMX_TARGET_RPI
  self->inline_sps_pps_headers =
      GST_OMX_H264_VIDEO_ENC_INLINE_SPS_PPS_HEADERS_DEFAULT;
#endif
  self->periodicty_idr =
      GST_OMX_H264_VIDEO_ENC_PERIODICITY_OF_IDR_FRAMES_DEFAULT;
  self->interval_intraframes =
      GST_OMX_H264_VIDEO_ENC_INTERVAL_OF_CODING_INTRA_FRAMES_DEFAULT;
}

static gboolean
gst_omx_h264_enc_flush (GstVideoEncoder * enc)
{
  GstOMXH264Enc *self = GST_OMX_H264_ENC (enc);

  g_list_free_full (self->headers, (GDestroyNotify) gst_buffer_unref);
  self->headers = NULL;

  return GST_VIDEO_ENCODER_CLASS (parent_class)->flush (enc);
}

static gboolean
gst_omx_h264_enc_stop (GstVideoEncoder * enc)
{
  GstOMXH264Enc *self = GST_OMX_H264_ENC (enc);

  g_list_free_full (self->headers, (GDestroyNotify) gst_buffer_unref);
  self->headers = NULL;

  return GST_VIDEO_ENCODER_CLASS (parent_class)->stop (enc);
}

static gboolean
gst_omx_h264_enc_set_format (GstOMXVideoEnc * enc, GstOMXPort * port,
    GstVideoCodecState * state)
{
  GstOMXH264Enc *self = GST_OMX_H264_ENC (enc);
  GstCaps *peercaps;
  OMX_PARAM_PORTDEFINITIONTYPE port_def;
  OMX_VIDEO_PARAM_PROFILELEVELTYPE param;
  OMX_VIDEO_CONFIG_AVCINTRAPERIOD config_avcintraperiod;
#ifdef USE_OMX_TARGET_RPI
  OMX_CONFIG_PORTBOOLEANTYPE config_inline_header;
#endif
  OMX_ERRORTYPE err;
  const gchar *profile_string, *level_string;

#ifdef USE_OMX_TARGET_RPI
  GST_OMX_INIT_STRUCT (&config_inline_header);
  config_inline_header.nPortIndex =
      GST_OMX_VIDEO_ENC (self)->enc_out_port->index;
  err =
      gst_omx_component_get_parameter (GST_OMX_VIDEO_ENC (self)->enc,
      OMX_IndexParamBrcmVideoAVCInlineHeaderEnable, &config_inline_header);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self,
        "can't get OMX_IndexParamBrcmVideoAVCInlineHeaderEnable %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  if (self->inline_sps_pps_headers) {
    config_inline_header.bEnabled = OMX_TRUE;
  } else {
    config_inline_header.bEnabled = OMX_FALSE;
  }

  err =
      gst_omx_component_set_parameter (GST_OMX_VIDEO_ENC (self)->enc,
      OMX_IndexParamBrcmVideoAVCInlineHeaderEnable, &config_inline_header);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self,
        "can't set OMX_IndexParamBrcmVideoAVCInlineHeaderEnable %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }
#endif

  if (self->periodicty_idr !=
      GST_OMX_H264_VIDEO_ENC_PERIODICITY_OF_IDR_FRAMES_DEFAULT
      || self->interval_intraframes !=
      GST_OMX_H264_VIDEO_ENC_INTERVAL_OF_CODING_INTRA_FRAMES_DEFAULT) {


    GST_OMX_INIT_STRUCT (&config_avcintraperiod);
    config_avcintraperiod.nPortIndex =
        GST_OMX_VIDEO_ENC (self)->enc_out_port->index;
    err =
        gst_omx_component_get_parameter (GST_OMX_VIDEO_ENC (self)->enc,
        OMX_IndexConfigVideoAVCIntraPeriod, &config_avcintraperiod);
    if (err != OMX_ErrorNone) {
      GST_ERROR_OBJECT (self,
          "can't get OMX_IndexConfigVideoAVCIntraPeriod %s (0x%08x)",
          gst_omx_error_to_string (err), err);
      return FALSE;
    }

    GST_DEBUG_OBJECT (self, "default nPFrames:%u, nIDRPeriod:%u",
        (guint) config_avcintraperiod.nPFrames,
        (guint) config_avcintraperiod.nIDRPeriod);

    if (self->periodicty_idr !=
        GST_OMX_H264_VIDEO_ENC_PERIODICITY_OF_IDR_FRAMES_DEFAULT) {
      config_avcintraperiod.nIDRPeriod = self->periodicty_idr;
    }

    if (self->interval_intraframes !=
        GST_OMX_H264_VIDEO_ENC_INTERVAL_OF_CODING_INTRA_FRAMES_DEFAULT) {
      config_avcintraperiod.nPFrames = self->interval_intraframes;
    }

    err =
        gst_omx_component_set_parameter (GST_OMX_VIDEO_ENC (self)->enc,
        OMX_IndexConfigVideoAVCIntraPeriod, &config_avcintraperiod);
    if (err != OMX_ErrorNone) {
      GST_ERROR_OBJECT (self,
          "can't set OMX_IndexConfigVideoAVCIntraPeriod %s (0x%08x)",
          gst_omx_error_to_string (err), err);
      return FALSE;
    }
  }

  gst_omx_port_get_port_definition (GST_OMX_VIDEO_ENC (self)->enc_out_port,
      &port_def);
  port_def.format.video.eCompressionFormat = OMX_VIDEO_CodingAVC;
  err =
      gst_omx_port_update_port_definition (GST_OMX_VIDEO_ENC
      (self)->enc_out_port, &port_def);
  if (err != OMX_ErrorNone)
    return FALSE;

  GST_OMX_INIT_STRUCT (&param);
  param.nPortIndex = GST_OMX_VIDEO_ENC (self)->enc_out_port->index;

  err =
      gst_omx_component_get_parameter (GST_OMX_VIDEO_ENC (self)->enc,
      OMX_IndexParamVideoProfileLevelCurrent, &param);
  if (err != OMX_ErrorNone) {
    GST_WARNING_OBJECT (self,
        "Setting profile/level not supported by component");
    return TRUE;
  }

  peercaps = gst_pad_peer_query_caps (GST_VIDEO_ENCODER_SRC_PAD (enc),
      gst_pad_get_pad_template_caps (GST_VIDEO_ENCODER_SRC_PAD (enc)));
  if (peercaps) {
    GstStructure *s;

    if (gst_caps_is_empty (peercaps)) {
      gst_caps_unref (peercaps);
      GST_ERROR_OBJECT (self, "Empty caps");
      return FALSE;
    }

    s = gst_caps_get_structure (peercaps, 0);
    profile_string = gst_structure_get_string (s, "profile");
    if (profile_string) {
      if (g_str_equal (profile_string, "baseline")) {
        param.eProfile = OMX_VIDEO_AVCProfileBaseline;
      } else if (g_str_equal (profile_string, "main")) {
        param.eProfile = OMX_VIDEO_AVCProfileMain;
      } else if (g_str_equal (profile_string, "extended")) {
        param.eProfile = OMX_VIDEO_AVCProfileExtended;
      } else if (g_str_equal (profile_string, "high")) {
        param.eProfile = OMX_VIDEO_AVCProfileHigh;
      } else if (g_str_equal (profile_string, "high-10")) {
        param.eProfile = OMX_VIDEO_AVCProfileHigh10;
      } else if (g_str_equal (profile_string, "high-4:2:2")) {
        param.eProfile = OMX_VIDEO_AVCProfileHigh422;
      } else if (g_str_equal (profile_string, "high-4:4:4")) {
        param.eProfile = OMX_VIDEO_AVCProfileHigh444;
      } else {
        goto unsupported_profile;
      }
    }
    level_string = gst_structure_get_string (s, "level");
    if (level_string) {
      if (g_str_equal (level_string, "1")) {
        param.eLevel = OMX_VIDEO_AVCLevel1;
      } else if (g_str_equal (level_string, "1b")) {
        param.eLevel = OMX_VIDEO_AVCLevel1b;
      } else if (g_str_equal (level_string, "1.1")) {
        param.eLevel = OMX_VIDEO_AVCLevel11;
      } else if (g_str_equal (level_string, "1.2")) {
        param.eLevel = OMX_VIDEO_AVCLevel12;
      } else if (g_str_equal (level_string, "1.3")) {
        param.eLevel = OMX_VIDEO_AVCLevel13;
      } else if (g_str_equal (level_string, "2")) {
        param.eLevel = OMX_VIDEO_AVCLevel2;
      } else if (g_str_equal (level_string, "2.1")) {
        param.eLevel = OMX_VIDEO_AVCLevel21;
      } else if (g_str_equal (level_string, "2.2")) {
        param.eLevel = OMX_VIDEO_AVCLevel22;
      } else if (g_str_equal (level_string, "3")) {
        param.eLevel = OMX_VIDEO_AVCLevel3;
      } else if (g_str_equal (level_string, "3.1")) {
        param.eLevel = OMX_VIDEO_AVCLevel31;
      } else if (g_str_equal (level_string, "3.2")) {
        param.eLevel = OMX_VIDEO_AVCLevel32;
      } else if (g_str_equal (level_string, "4")) {
        param.eLevel = OMX_VIDEO_AVCLevel4;
      } else if (g_str_equal (level_string, "4.1")) {
        param.eLevel = OMX_VIDEO_AVCLevel41;
      } else if (g_str_equal (level_string, "4.2")) {
        param.eLevel = OMX_VIDEO_AVCLevel42;
      } else if (g_str_equal (level_string, "5")) {
        param.eLevel = OMX_VIDEO_AVCLevel5;
      } else if (g_str_equal (level_string, "5.1")) {
        param.eLevel = OMX_VIDEO_AVCLevel51;
      } else {
        goto unsupported_level;
      }
    }
    gst_caps_unref (peercaps);
  }

  err =
      gst_omx_component_set_parameter (GST_OMX_VIDEO_ENC (self)->enc,
      OMX_IndexParamVideoProfileLevelCurrent, &param);
  if (err == OMX_ErrorUnsupportedIndex) {
    GST_WARNING_OBJECT (self,
        "Setting profile/level not supported by component");
  } else if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self,
        "Error setting profile %u and level %u: %s (0x%08x)",
        (guint) param.eProfile, (guint) param.eLevel,
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  return TRUE;

unsupported_profile:
  GST_ERROR_OBJECT (self, "Unsupported profile %s", profile_string);
  gst_caps_unref (peercaps);
  return FALSE;

unsupported_level:
  GST_ERROR_OBJECT (self, "Unsupported level %s", level_string);
  gst_caps_unref (peercaps);
  return FALSE;
}

static GstCaps *
gst_omx_h264_enc_get_caps (GstOMXVideoEnc * enc, GstOMXPort * port,
    GstVideoCodecState * state)
{
  GstOMXH264Enc *self = GST_OMX_H264_ENC (enc);
  GstCaps *caps;
  OMX_ERRORTYPE err;
  OMX_VIDEO_PARAM_PROFILELEVELTYPE param;
  const gchar *profile, *level;

  caps = gst_caps_new_simple ("video/x-h264",
      "stream-format", G_TYPE_STRING, "byte-stream",
      "alignment", G_TYPE_STRING, "au", NULL);

  GST_OMX_INIT_STRUCT (&param);
  param.nPortIndex = GST_OMX_VIDEO_ENC (self)->enc_out_port->index;

  err =
      gst_omx_component_get_parameter (GST_OMX_VIDEO_ENC (self)->enc,
      OMX_IndexParamVideoProfileLevelCurrent, &param);
  if (err != OMX_ErrorNone && err != OMX_ErrorUnsupportedIndex)
    return NULL;

  if (err == OMX_ErrorNone) {
    switch (param.eProfile) {
      case OMX_VIDEO_AVCProfileBaseline:
        profile = "baseline";
        break;
      case OMX_VIDEO_AVCProfileMain:
        profile = "main";
        break;
      case OMX_VIDEO_AVCProfileExtended:
        profile = "extended";
        break;
      case OMX_VIDEO_AVCProfileHigh:
        profile = "high";
        break;
      case OMX_VIDEO_AVCProfileHigh10:
        profile = "high-10";
        break;
      case OMX_VIDEO_AVCProfileHigh422:
        profile = "high-4:2:2";
        break;
      case OMX_VIDEO_AVCProfileHigh444:
        profile = "high-4:4:4";
        break;
      default:
        g_assert_not_reached ();
        return NULL;
    }

    switch (param.eLevel) {
      case OMX_VIDEO_AVCLevel1:
        level = "1";
        break;
      case OMX_VIDEO_AVCLevel1b:
        level = "1b";
        break;
      case OMX_VIDEO_AVCLevel11:
        level = "1.1";
        break;
      case OMX_VIDEO_AVCLevel12:
        level = "1.2";
        break;
      case OMX_VIDEO_AVCLevel13:
        level = "1.3";
        break;
      case OMX_VIDEO_AVCLevel2:
        level = "2";
        break;
      case OMX_VIDEO_AVCLevel21:
        level = "2.1";
        break;
      case OMX_VIDEO_AVCLevel22:
        level = "2.2";
        break;
      case OMX_VIDEO_AVCLevel3:
        level = "3";
        break;
      case OMX_VIDEO_AVCLevel31:
        level = "3.1";
        break;
      case OMX_VIDEO_AVCLevel32:
        level = "3.2";
        break;
      case OMX_VIDEO_AVCLevel4:
        level = "4";
        break;
      case OMX_VIDEO_AVCLevel41:
        level = "4.1";
        break;
      case OMX_VIDEO_AVCLevel42:
        level = "4.2";
        break;
      case OMX_VIDEO_AVCLevel5:
        level = "5";
        break;
      case OMX_VIDEO_AVCLevel51:
        level = "5.1";
        break;
      default:
        g_assert_not_reached ();
        return NULL;
    }
    gst_caps_set_simple (caps,
        "profile", G_TYPE_STRING, profile, "level", G_TYPE_STRING, level, NULL);
  }

  return caps;
}

static GstFlowReturn
gst_omx_h264_enc_handle_output_frame (GstOMXVideoEnc * enc, GstOMXPort * port,
    GstOMXBuffer * buf, GstVideoCodecFrame * frame)
{
  GstOMXH264Enc *self = GST_OMX_H264_ENC (enc);

  if (buf->omx_buf->nFlags & OMX_BUFFERFLAG_CODECCONFIG) {
    /* The codec data is SPS/PPS with a startcode => bytestream stream format
     * For bytestream stream format the SPS/PPS is only in-stream and not
     * in the caps!
     */
    if (buf->omx_buf->nFilledLen >= 4 &&
        GST_READ_UINT32_BE (buf->omx_buf->pBuffer +
            buf->omx_buf->nOffset) == 0x00000001) {
      GstBuffer *hdrs;
      GstMapInfo map = GST_MAP_INFO_INIT;

      GST_DEBUG_OBJECT (self, "got codecconfig in byte-stream format");

      hdrs = gst_buffer_new_and_alloc (buf->omx_buf->nFilledLen);

      gst_buffer_map (hdrs, &map, GST_MAP_WRITE);
      memcpy (map.data,
          buf->omx_buf->pBuffer + buf->omx_buf->nOffset,
          buf->omx_buf->nFilledLen);
      gst_buffer_unmap (hdrs, &map);
      self->headers = g_list_append (self->headers, hdrs);

      if (frame)
        gst_video_codec_frame_unref (frame);

      return GST_FLOW_OK;
    }
  } else if (self->headers) {
    gst_video_encoder_set_headers (GST_VIDEO_ENCODER (self), self->headers);
    self->headers = NULL;
  }

  return
      GST_OMX_VIDEO_ENC_CLASS
      (gst_omx_h264_enc_parent_class)->handle_output_frame (enc, port, buf,
      frame);
}
