/* VP8
 * Copyright (C) 2006 David Schleef <ds@schleef.org>
 * Copyright (C) 2010 Entropy Wave Inc
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/video/gstbasevideoencoder.h>
#include <gst/video/gstbasevideoutils.h>
#include <gst/base/gstbasetransform.h>
#include <gst/base/gstadapter.h>
#include <gst/video/video.h>
#include <string.h>
#include <math.h>


#include <vpx/vpx_encoder.h>
#include <vpx/vp8cx.h>


GST_DEBUG_CATEGORY (gst_vp8enc_debug);
#define GST_CAT_DEFAULT gst_vp8enc_debug

#define GST_TYPE_VP8_ENC \
  (gst_vp8_enc_get_type())
#define GST_VP8_ENC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VP8_ENC,GstVP8Enc))
#define GST_VP8_ENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VP8_ENC,GstVP8EncClass))
#define GST_IS_GST_VP8_ENC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VP8_ENC))
#define GST_IS_GST_VP8_ENC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VP8_ENC))

typedef struct _GstVP8Enc GstVP8Enc;
typedef struct _GstVP8EncClass GstVP8EncClass;

struct _GstVP8Enc
{
  GstBaseVideoEncoder base_video_encoder;

  vpx_codec_ctx_t encoder;

  /* properties */
  int bitrate;
  double quality;
  gboolean error_resilient;
  int max_latency;
  int keyframe_interval;
  int speed;

  /* state */

  gboolean inited;

  int resolution_id;
  int n_frames;

};

struct _GstVP8EncClass
{
  GstBaseVideoEncoderClass base_video_encoder_class;
};

/* GstVP8Enc signals and args */
enum
{
  LAST_SIGNAL
};

#define DEFAULT_BITRATE 0
#define DEFAULT_QUALITY 5
#define DEFAULT_ERROR_RESILIENT FALSE
#define DEFAULT_MAX_LATENCY 10
#define DEFAULT_KEYFRAME_INTERVAL 60
#define DEFAULT_SPEED 0

enum
{
  PROP_0,
  PROP_BITRATE,
  PROP_QUALITY,
  PROP_ERROR_RESILIENT,
  PROP_MAX_LATENCY,
  PROP_KEYFRAME_INTERVAL,
  PROP_SPEED
};

static void gst_vp8_enc_finalize (GObject * object);
static void gst_vp8_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_vp8_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_vp8_enc_start (GstBaseVideoEncoder * encoder);
static gboolean gst_vp8_enc_stop (GstBaseVideoEncoder * encoder);
static gboolean gst_vp8_enc_set_format (GstBaseVideoEncoder *
    base_video_encoder, GstVideoState * state);
static gboolean gst_vp8_enc_finish (GstBaseVideoEncoder * base_video_encoder);
static gboolean gst_vp8_enc_handle_frame (GstBaseVideoEncoder *
    base_video_encoder, GstVideoFrame * frame);
static GstFlowReturn gst_vp8_enc_shape_output (GstBaseVideoEncoder * encoder,
    GstVideoFrame * frame);
static GstCaps *gst_vp8_enc_get_caps (GstBaseVideoEncoder * base_video_encoder);

GType gst_vp8_enc_get_type (void);

static const char *vpx_error_name (vpx_codec_err_t status);


static GstStaticPadTemplate gst_vp8_enc_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-yuv,format=(fourcc)I420,"
        "width=[1,max],height=[1,max],framerate=(fraction)[0,max],"
        "interlaced=(boolean){TRUE,FALSE}")
    );

static GstStaticPadTemplate gst_vp8_enc_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-vp8")
    );

GST_BOILERPLATE (GstVP8Enc, gst_vp8_enc, GstBaseVideoEncoder,
    GST_TYPE_BASE_VIDEO_ENCODER);

static void
gst_vp8_enc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_vp8_enc_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_vp8_enc_sink_template));

  gst_element_class_set_details_simple (element_class,
      "On2 VP8 Encoder",
      "Codec/Encoder/Video",
      "Encode VP8 video streams", "David Schleef <ds@entropywave.com>");
}

static void
gst_vp8_enc_class_init (GstVP8EncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstBaseVideoEncoderClass *base_video_encoder_class;

  gobject_class = G_OBJECT_CLASS (klass);
  element_class = GST_ELEMENT_CLASS (klass);
  base_video_encoder_class = GST_BASE_VIDEO_ENCODER_CLASS (klass);

  gobject_class->set_property = gst_vp8_enc_set_property;
  gobject_class->get_property = gst_vp8_enc_get_property;
  gobject_class->finalize = gst_vp8_enc_finalize;

  base_video_encoder_class->start = gst_vp8_enc_start;
  base_video_encoder_class->stop = gst_vp8_enc_stop;
  base_video_encoder_class->handle_frame = gst_vp8_enc_handle_frame;
  base_video_encoder_class->set_format = gst_vp8_enc_set_format;
  base_video_encoder_class->finish = gst_vp8_enc_finish;
  base_video_encoder_class->shape_output = gst_vp8_enc_shape_output;
  base_video_encoder_class->get_caps = gst_vp8_enc_get_caps;

  g_object_class_install_property (gobject_class, PROP_BITRATE,
      g_param_spec_int ("bitrate", "Bit rate",
          "Bit rate",
          0, 1000000000, DEFAULT_BITRATE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_QUALITY,
      g_param_spec_double ("quality", "Quality",
          "Quality",
          0, 10.0, DEFAULT_QUALITY,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_ERROR_RESILIENT,
      g_param_spec_boolean ("error-resilient", "Error Resilient",
          "Encode streams that are error resilient",
          DEFAULT_ERROR_RESILIENT,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_MAX_LATENCY,
      g_param_spec_int ("max-latency", "Max latency",
          "Number of frames in encoder queue",
          0, 100, DEFAULT_MAX_LATENCY,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_KEYFRAME_INTERVAL,
      g_param_spec_int ("keyframe-interval", "Key frame interval",
          "Maximum distance between key frames",
          1, 1000, DEFAULT_KEYFRAME_INTERVAL,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_SPEED,
      g_param_spec_int ("speed", "Speed",
          "Speed",
          0, 2, DEFAULT_SPEED,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

}

static void
gst_vp8_enc_init (GstVP8Enc * gst_vp8_enc, GstVP8EncClass * klass)
{

  GST_DEBUG ("init");

  gst_vp8_enc->bitrate = DEFAULT_BITRATE;
  gst_vp8_enc->quality = DEFAULT_QUALITY;
  gst_vp8_enc->error_resilient = DEFAULT_ERROR_RESILIENT;
  gst_vp8_enc->max_latency = DEFAULT_MAX_LATENCY;
  gst_vp8_enc->keyframe_interval = DEFAULT_KEYFRAME_INTERVAL;
}

static void
gst_vp8_enc_finalize (GObject * object)
{
  GstVP8Enc *gst_vp8_enc;

  GST_DEBUG ("finalize");

  g_return_if_fail (GST_IS_GST_VP8_ENC (object));
  gst_vp8_enc = GST_VP8_ENC (object);

  G_OBJECT_CLASS (parent_class)->finalize (object);

}

static void
gst_vp8_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVP8Enc *gst_vp8_enc;

  g_return_if_fail (GST_IS_GST_VP8_ENC (object));
  gst_vp8_enc = GST_VP8_ENC (object);

  GST_DEBUG ("gst_vp8_enc_set_property");
  switch (prop_id) {
    case PROP_BITRATE:
      gst_vp8_enc->bitrate = g_value_get_int (value);
      break;
    case PROP_QUALITY:
      gst_vp8_enc->quality = g_value_get_double (value);
      break;
    case PROP_ERROR_RESILIENT:
      gst_vp8_enc->error_resilient = g_value_get_boolean (value);
      break;
    case PROP_MAX_LATENCY:
      gst_vp8_enc->max_latency = g_value_get_int (value);
      break;
    case PROP_KEYFRAME_INTERVAL:
      gst_vp8_enc->keyframe_interval = g_value_get_int (value);
      break;
    case PROP_SPEED:
      gst_vp8_enc->speed = g_value_get_int (value);
      break;
    default:
      break;
  }
}

static void
gst_vp8_enc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstVP8Enc *gst_vp8_enc;

  g_return_if_fail (GST_IS_GST_VP8_ENC (object));
  gst_vp8_enc = GST_VP8_ENC (object);

  switch (prop_id) {
    case PROP_BITRATE:
      g_value_set_int (value, gst_vp8_enc->bitrate);
      break;
    case PROP_QUALITY:
      g_value_set_double (value, gst_vp8_enc->quality);
      break;
    case PROP_ERROR_RESILIENT:
      g_value_set_boolean (value, gst_vp8_enc->error_resilient);
      break;
    case PROP_MAX_LATENCY:
      g_value_set_int (value, gst_vp8_enc->max_latency);
      break;
    case PROP_KEYFRAME_INTERVAL:
      g_value_set_int (value, gst_vp8_enc->keyframe_interval);
      break;
    case PROP_SPEED:
      g_value_set_int (value, gst_vp8_enc->speed);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static gboolean
gst_vp8_enc_start (GstBaseVideoEncoder * base_video_encoder)
{
  GstVP8Enc *encoder;

  GST_DEBUG ("start");

  encoder = GST_VP8_ENC (base_video_encoder);


  return TRUE;
}

static gboolean
gst_vp8_enc_stop (GstBaseVideoEncoder * base_video_encoder)
{
  GstVP8Enc *encoder;

  encoder = GST_VP8_ENC (base_video_encoder);

  if (encoder->inited) {
    vpx_codec_destroy (&encoder->encoder);
    encoder->inited = FALSE;
  }

  return TRUE;
}

static gboolean
gst_vp8_enc_set_format (GstBaseVideoEncoder * base_video_encoder,
    GstVideoState * state)
{
  GstVP8Enc *encoder;

  GST_DEBUG ("set_format");

  encoder = GST_VP8_ENC (base_video_encoder);


  return TRUE;
}

static GstCaps *
gst_vp8_enc_get_caps (GstBaseVideoEncoder * base_video_encoder)
{
  GstCaps *caps;
  const GstVideoState *state;
  GstVP8Enc *encoder;

  encoder = GST_VP8_ENC (base_video_encoder);

  state = gst_base_video_encoder_get_state (base_video_encoder);

  caps = gst_caps_new_simple ("video/x-vp8",
      "width", G_TYPE_INT, state->width,
      "height", G_TYPE_INT, state->height,
      "framerate", GST_TYPE_FRACTION, state->fps_n,
      state->fps_d,
      "pixel-aspect-ratio", GST_TYPE_FRACTION, state->par_n,
      state->par_d, NULL);

  return caps;
}

static gboolean
gst_vp8_enc_finish (GstBaseVideoEncoder * base_video_encoder)
{
  GstVP8Enc *encoder;
  GstVideoFrame *frame;
  int flags = 0;
  int status;
  vpx_codec_iter_t iter = NULL;
  const vpx_codec_cx_pkt_t *pkt;

  GST_DEBUG ("finish");

  encoder = GST_VP8_ENC (base_video_encoder);

  status =
      vpx_codec_encode (&encoder->encoder, NULL, encoder->n_frames, 1, flags,
      0);
  if (status != 0) {
    GST_ERROR ("encode returned %d %s", status, vpx_error_name (status));
  }

  pkt = vpx_codec_get_cx_data (&encoder->encoder, &iter);
  while (pkt != NULL) {
    gboolean invisible, keyframe;

    GST_DEBUG ("packet %d type %d", pkt->data.frame.sz, pkt->kind);

    if (pkt->kind != VPX_CODEC_CX_FRAME_PKT) {
      GST_ERROR ("non frame pkt");
      continue;
    }

    invisible = (pkt->data.frame.flags & VPX_FRAME_IS_INVISIBLE) != 0;
    keyframe = (pkt->data.frame.flags & VPX_FRAME_IS_KEY) != 0;
    /* FIXME: This is wrong for invisible frames, we need to get
     * a new frame that is not in the encoder list */
    frame = gst_base_video_encoder_get_oldest_frame (base_video_encoder);

    /* FIXME: If frame is NULL something went really wrong! */

    frame->src_buffer = gst_buffer_new_and_alloc (pkt->data.frame.sz);

    memcpy (GST_BUFFER_DATA (frame->src_buffer),
        pkt->data.frame.buf, pkt->data.frame.sz);
    frame->is_sync_point = keyframe;

    if (frame->coder_hook)
      g_free (frame->coder_hook);

    gst_base_video_encoder_finish_frame (base_video_encoder, frame);

    pkt = vpx_codec_get_cx_data (&encoder->encoder, &iter);
  }

  return TRUE;
}

static const char *
vpx_error_name (vpx_codec_err_t status)
{
  switch (status) {
    case VPX_CODEC_OK:
      return "OK";
    case VPX_CODEC_ERROR:
      return "error";
    case VPX_CODEC_MEM_ERROR:
      return "mem error";
    case VPX_CODEC_ABI_MISMATCH:
      return "abi mismatch";
    case VPX_CODEC_INCAPABLE:
      return "incapable";
    case VPX_CODEC_UNSUP_BITSTREAM:
      return "unsupported bitstream";
    case VPX_CODEC_UNSUP_FEATURE:
      return "unsupported feature";
    case VPX_CODEC_CORRUPT_FRAME:
      return "corrupt frame";
    case VPX_CODEC_INVALID_PARAM:
      return "invalid parameter";
    default:
      return "unknown";
  }
}

static const int speed_table[] = {
  0,
  100000,
  1,
};

static gboolean
gst_vp8_enc_handle_frame (GstBaseVideoEncoder * base_video_encoder,
    GstVideoFrame * frame)
{
  GstVP8Enc *encoder;
  const GstVideoState *state;
  guint8 *src;
  long status;
  int flags = 0;
  vpx_codec_iter_t iter = NULL;
  const vpx_codec_cx_pkt_t *pkt;
  vpx_image_t *image;

  GST_DEBUG ("handle_frame");

  encoder = GST_VP8_ENC (base_video_encoder);
  src = GST_BUFFER_DATA (frame->sink_buffer);

  state = gst_base_video_encoder_get_state (base_video_encoder);
  encoder->n_frames++;

  GST_DEBUG ("res id %d size %d %d", encoder->resolution_id,
      state->width, state->height);

  if (!encoder->inited) {
    vpx_codec_enc_cfg_t cfg;

    vpx_codec_enc_config_default (&vpx_codec_vp8_cx_algo, &cfg, 0);

    cfg.g_w = base_video_encoder->state.width;
    cfg.g_h = base_video_encoder->state.height;
    //cfg.g_timebase.num = base_video_encoder->state.fps_n;
    //cfg.g_timebase.den = base_video_encoder->state.fps_d;

    cfg.g_error_resilient = encoder->error_resilient;
    cfg.g_pass = VPX_RC_ONE_PASS;
    cfg.g_lag_in_frames = encoder->max_latency;

    if (encoder->bitrate) {
      cfg.rc_end_usage = VPX_CBR;
      cfg.rc_target_bitrate = encoder->bitrate / 1000;
    } else {
      cfg.rc_end_usage = VPX_VBR;
      cfg.rc_min_quantizer = 63 - encoder->quality * 5.0;
      cfg.rc_max_quantizer = 63 - encoder->quality * 5.0;
      cfg.rc_target_bitrate = encoder->bitrate;
      cfg.rc_buf_sz = 1000;     // FIXME 1000 ms
      cfg.rc_buf_initial_sz = 1000;     // FIXME 1000 ms
    }

    cfg.kf_mode = VPX_KF_AUTO;
    cfg.kf_min_dist = 0;
    cfg.kf_max_dist = encoder->keyframe_interval;

    status = vpx_codec_enc_init (&encoder->encoder, &vpx_codec_vp8_cx_algo,
        &cfg, 0);
    if (status) {
      GST_ERROR ("encoder input error");
      return GST_FLOW_ERROR;
    }

    encoder->inited = TRUE;
  }

  image = g_malloc0 (sizeof (vpx_image_t));
  vpx_img_wrap (image, IMG_FMT_I420,
      base_video_encoder->state.width,
      base_video_encoder->state.height, 1,
      GST_BUFFER_DATA (frame->sink_buffer));
  frame->coder_hook = image;

#if 0
  if (encoder->force_keyframe) {
    flags |= VPX_EFLAG_FORCE_KF;
  }
#endif

  status = vpx_codec_encode (&encoder->encoder, image,
      encoder->n_frames, 1, flags, speed_table[encoder->speed]);
  if (status != 0) {
    GST_ERROR ("encode returned %d %s", status, vpx_error_name (status));
  }

  pkt = vpx_codec_get_cx_data (&encoder->encoder, &iter);
  while (pkt != NULL) {
    gboolean invisible, keyframe;

    GST_DEBUG ("packet %d type %d", pkt->data.frame.sz, pkt->kind);

    if (pkt->kind != VPX_CODEC_CX_FRAME_PKT) {
      GST_ERROR ("non frame pkt");
      continue;
    }

    invisible = (pkt->data.frame.flags & VPX_FRAME_IS_INVISIBLE) != 0;
    keyframe = (pkt->data.frame.flags & VPX_FRAME_IS_KEY) != 0;
    /* FIXME: This is wrong for invisible frames, we need to get
     * a new frame that is not in the encoder list */
    frame = gst_base_video_encoder_get_oldest_frame (base_video_encoder);
    /* FIXME: If frame is NULL something went really wrong! */

    frame->src_buffer = gst_buffer_new_and_alloc (pkt->data.frame.sz);

    memcpy (GST_BUFFER_DATA (frame->src_buffer),
        pkt->data.frame.buf, pkt->data.frame.sz);
    frame->is_sync_point = keyframe;

    if (frame->coder_hook)
      g_free (frame->coder_hook);

    gst_base_video_encoder_finish_frame (base_video_encoder, frame);

    pkt = vpx_codec_get_cx_data (&encoder->encoder, &iter);
  }

  return TRUE;
}

static GstFlowReturn
gst_vp8_enc_shape_output (GstBaseVideoEncoder * base_video_encoder,
    GstVideoFrame * frame)
{
  GstBuffer *buf = frame->src_buffer;
  const GstVideoState *state;
  GstFlowReturn ret;

  GST_DEBUG ("shape_output");

  state = gst_base_video_encoder_get_state (base_video_encoder);

  GST_BUFFER_TIMESTAMP (buf) = gst_video_state_get_timestamp (state,
      &base_video_encoder->segment, frame->presentation_frame_number);
  GST_BUFFER_DURATION (buf) = gst_video_state_get_timestamp (state,
      &base_video_encoder->segment,
      frame->presentation_frame_number + 1) - GST_BUFFER_TIMESTAMP (buf);
  GST_BUFFER_OFFSET_END (buf) = GST_BUFFER_OFFSET_NONE;
  GST_BUFFER_OFFSET (buf) = GST_BUFFER_OFFSET_NONE;

  if (frame->is_sync_point) {
    GST_BUFFER_FLAG_UNSET (buf, GST_BUFFER_FLAG_DELTA_UNIT);
  } else {
    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DELTA_UNIT);
  }

  gst_buffer_set_caps (buf, base_video_encoder->caps);

  ret = gst_pad_push (GST_BASE_VIDEO_CODEC_SRC_PAD (base_video_encoder), buf);
  if (ret != GST_FLOW_OK) {
    GST_ERROR ("flow error %d", ret);
  }

  frame->src_buffer = NULL;

  return ret;
}
