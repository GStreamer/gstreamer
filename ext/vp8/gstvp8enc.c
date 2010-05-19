/* VP8
 * Copyright (C) 2006 David Schleef <ds@schleef.org>
 * Copyright (C) 2010 Entropy Wave Inc
 * Copyright (C) 2010 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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
#include <gst/tag/tag.h>
#include <string.h>
#include <math.h>


#include <vpx/vpx_encoder.h>
#include <vpx/vp8cx.h>

#include "gstvp8utils.h"

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
  int max_keyframe_distance;
  int speed;

  /* state */

  gboolean force_keyframe;
  gboolean inited;

  int resolution_id;
  int n_frames;
  int keyframe_distance;

  GstPadEventFunction base_sink_event_func;
};

struct _GstVP8EncClass
{
  GstBaseVideoEncoderClass base_video_encoder_class;
};

typedef struct
{
  vpx_image_t *image;
  GList *invisible;
} GstVP8EncCoderHook;

/* GstVP8Enc signals and args */
enum
{
  LAST_SIGNAL
};

#define DEFAULT_BITRATE 0
#define DEFAULT_QUALITY 5
#define DEFAULT_ERROR_RESILIENT FALSE
#define DEFAULT_MAX_LATENCY 10
#define DEFAULT_MAX_KEYFRAME_DISTANCE 60
#define DEFAULT_SPEED 0

enum
{
  PROP_0,
  PROP_BITRATE,
  PROP_QUALITY,
  PROP_ERROR_RESILIENT,
  PROP_MAX_LATENCY,
  PROP_MAX_KEYFRAME_DISTANCE,
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

static gboolean gst_vp8_enc_sink_event (GstPad * pad, GstEvent * event);

GType gst_vp8_enc_get_type (void);

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

static void
do_init (GType vp8enc_type)
{
  static const GInterfaceInfo tag_setter_info = { NULL, NULL, NULL };
  const GInterfaceInfo preset_interface_info = {
    NULL,                       /* interface_init */
    NULL,                       /* interface_finalize */
    NULL                        /* interface_data */
  };

  g_type_add_interface_static (vp8enc_type, GST_TYPE_TAG_SETTER,
      &tag_setter_info);
  g_type_add_interface_static (vp8enc_type, GST_TYPE_PRESET,
      &preset_interface_info);
}

GST_BOILERPLATE_FULL (GstVP8Enc, gst_vp8_enc, GstBaseVideoEncoder,
    GST_TYPE_BASE_VIDEO_ENCODER, do_init);

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

  g_object_class_install_property (gobject_class, PROP_MAX_KEYFRAME_DISTANCE,
      g_param_spec_int ("max-keyframe-distance", "Maximum Key frame distance",
          "Maximum distance between key frames",
          1, 9999, DEFAULT_MAX_KEYFRAME_DISTANCE,
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

  GST_DEBUG_OBJECT (gst_vp8_enc, "init");

  gst_vp8_enc->bitrate = DEFAULT_BITRATE;
  gst_vp8_enc->quality = DEFAULT_QUALITY;
  gst_vp8_enc->error_resilient = DEFAULT_ERROR_RESILIENT;
  gst_vp8_enc->max_latency = DEFAULT_MAX_LATENCY;
  gst_vp8_enc->max_keyframe_distance = DEFAULT_MAX_KEYFRAME_DISTANCE;

  /* FIXME: Add sink/src event vmethods */
  gst_vp8_enc->base_sink_event_func =
      GST_PAD_EVENTFUNC (GST_BASE_VIDEO_CODEC_SINK_PAD (gst_vp8_enc));
  gst_pad_set_event_function (GST_BASE_VIDEO_CODEC_SINK_PAD (gst_vp8_enc),
      gst_vp8_enc_sink_event);
}

static void
gst_vp8_enc_finalize (GObject * object)
{
  GstVP8Enc *gst_vp8_enc;

  GST_DEBUG_OBJECT (object, "finalize");

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

  GST_DEBUG_OBJECT (object, "gst_vp8_enc_set_property");
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
    case PROP_MAX_KEYFRAME_DISTANCE:
      gst_vp8_enc->max_keyframe_distance = g_value_get_int (value);
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
    case PROP_MAX_KEYFRAME_DISTANCE:
      g_value_set_int (value, gst_vp8_enc->max_keyframe_distance);
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

  GST_DEBUG_OBJECT (base_video_encoder, "start");

  encoder = GST_VP8_ENC (base_video_encoder);

  return TRUE;
}

static gboolean
gst_vp8_enc_stop (GstBaseVideoEncoder * base_video_encoder)
{
  GstVP8Enc *encoder;

  GST_DEBUG_OBJECT (base_video_encoder, "stop");

  encoder = GST_VP8_ENC (base_video_encoder);

  if (encoder->inited) {
    vpx_codec_destroy (&encoder->encoder);
    encoder->inited = FALSE;
  }

  gst_tag_setter_reset_tags (GST_TAG_SETTER (encoder));

  return TRUE;
}

static gboolean
gst_vp8_enc_set_format (GstBaseVideoEncoder * base_video_encoder,
    GstVideoState * state)
{
  GstVP8Enc *encoder;

  GST_DEBUG_OBJECT (base_video_encoder, "set_format");

  encoder = GST_VP8_ENC (base_video_encoder);

  return TRUE;
}

static GstCaps *
gst_vp8_enc_get_caps (GstBaseVideoEncoder * base_video_encoder)
{
  GstCaps *caps;
  const GstVideoState *state;
  GstVP8Enc *encoder;
  GstTagList *tags = NULL;
  const GstTagList *iface_tags;
  GstBuffer *stream_hdr, *vorbiscomment;
  guint8 *data;
  GstStructure *s;
  GValue array = { 0 };
  GValue value = { 0 };

  encoder = GST_VP8_ENC (base_video_encoder);

  state = gst_base_video_encoder_get_state (base_video_encoder);

  caps = gst_caps_new_simple ("video/x-vp8",
      "width", G_TYPE_INT, state->width,
      "height", G_TYPE_INT, state->height,
      "framerate", GST_TYPE_FRACTION, state->fps_n,
      state->fps_d,
      "pixel-aspect-ratio", GST_TYPE_FRACTION, state->par_n,
      state->par_d, NULL);

  s = gst_caps_get_structure (caps, 0);

  /* put buffers in a fixed list */
  g_value_init (&array, GST_TYPE_ARRAY);
  g_value_init (&value, GST_TYPE_BUFFER);

  /* Create Ogg stream-info */
  stream_hdr = gst_buffer_new_and_alloc (24);
  data = GST_BUFFER_DATA (stream_hdr);

  GST_WRITE_UINT32_BE (data, 0x2F565038);       /* "/VP8" */
  GST_WRITE_UINT8 (data + 4, 1);        /* Major version 1 */
  GST_WRITE_UINT8 (data + 5, 0);        /* Minor version 0 */
  GST_WRITE_UINT16_BE (data + 6, state->width);
  GST_WRITE_UINT16_BE (data + 8, state->height);
  GST_WRITE_UINT24_BE (data + 10, state->par_n);
  GST_WRITE_UINT24_BE (data + 13, state->par_d);
  GST_WRITE_UINT32_BE (data + 16, state->fps_n);
  GST_WRITE_UINT32_BE (data + 20, state->fps_d);

  GST_BUFFER_FLAG_SET (stream_hdr, GST_BUFFER_FLAG_IN_CAPS);
  gst_value_set_buffer (&value, stream_hdr);
  gst_value_array_append_value (&array, &value);
  g_value_unset (&value);
  gst_buffer_unref (stream_hdr);

  iface_tags =
      gst_tag_setter_get_tag_list (GST_TAG_SETTER (base_video_encoder));
  if (iface_tags) {
    vorbiscomment =
        gst_tag_list_to_vorbiscomment_buffer ((iface_tags) ? iface_tags : tags,
        (const guint8 *) "OggVP8 ", 7, NULL);

    GST_BUFFER_FLAG_SET (vorbiscomment, GST_BUFFER_FLAG_IN_CAPS);

    g_value_init (&value, GST_TYPE_BUFFER);
    gst_value_set_buffer (&value, vorbiscomment);
    gst_value_array_append_value (&array, &value);
    g_value_unset (&value);
    gst_buffer_unref (vorbiscomment);
  }

  gst_structure_set_value (s, "streamheader", &array);
  g_value_unset (&array);

  return caps;
}

static gboolean
gst_vp8_enc_finish (GstBaseVideoEncoder * base_video_encoder)
{
  GstVP8Enc *encoder;
  GstVideoFrame *frame;
  int flags = 0;
  vpx_codec_err_t status;
  vpx_codec_iter_t iter = NULL;
  const vpx_codec_cx_pkt_t *pkt;

  GST_DEBUG_OBJECT (base_video_encoder, "finish");

  encoder = GST_VP8_ENC (base_video_encoder);

  status =
      vpx_codec_encode (&encoder->encoder, NULL, encoder->n_frames, 1, flags,
      0);
  if (status != 0) {
    GST_ERROR_OBJECT (encoder, "encode returned %d %s", status,
        gst_vpx_error_name (status));
    return FALSE;
  }

  pkt = vpx_codec_get_cx_data (&encoder->encoder, &iter);
  while (pkt != NULL) {
    GstBuffer *buffer;
    GstVP8EncCoderHook *hook;
    gboolean invisible, keyframe;

    GST_DEBUG_OBJECT (encoder, "packet %d type %d", pkt->data.frame.sz,
        pkt->kind);

    if (pkt->kind != VPX_CODEC_CX_FRAME_PKT) {
      GST_ERROR_OBJECT (encoder, "non frame pkt");
      continue;
    }

    invisible = (pkt->data.frame.flags & VPX_FRAME_IS_INVISIBLE) != 0;
    keyframe = (pkt->data.frame.flags & VPX_FRAME_IS_KEY) != 0;
    frame = gst_base_video_encoder_get_oldest_frame (base_video_encoder);
    g_assert (frame != NULL);
    hook = frame->coder_hook;

    buffer = gst_buffer_new_and_alloc (pkt->data.frame.sz);

    memcpy (GST_BUFFER_DATA (buffer), pkt->data.frame.buf, pkt->data.frame.sz);
    frame->is_sync_point = frame->is_sync_point || keyframe;

    if (hook->image)
      g_slice_free (vpx_image_t, hook->image);
    hook->image = NULL;

    if (invisible) {
      hook->invisible = g_list_append (hook->invisible, buffer);
    } else {
      frame->src_buffer = buffer;
      gst_base_video_encoder_finish_frame (base_video_encoder, frame);
      frame = NULL;
    }

    pkt = vpx_codec_get_cx_data (&encoder->encoder, &iter);
  }

  return TRUE;
}

static vpx_image_t *
gst_vp8_enc_buffer_to_image (GstVP8Enc * enc, GstBuffer * buffer)
{
  vpx_image_t *image = g_slice_new0 (vpx_image_t);
  GstBaseVideoEncoder *encoder = (GstBaseVideoEncoder *) enc;
  guint8 *data = GST_BUFFER_DATA (buffer);

  image->fmt = IMG_FMT_I420;
  image->bps = 12;
  image->x_chroma_shift = image->y_chroma_shift = 1;
  image->img_data = data;
  image->w = image->d_w = encoder->state.width;
  image->h = image->d_h = encoder->state.height;

  image->stride[PLANE_Y] =
      gst_video_format_get_row_stride (encoder->state.format, 0,
      encoder->state.width);
  image->stride[PLANE_U] =
      gst_video_format_get_row_stride (encoder->state.format, 1,
      encoder->state.width);
  image->stride[PLANE_V] =
      gst_video_format_get_row_stride (encoder->state.format, 2,
      encoder->state.width);
  image->planes[PLANE_Y] =
      data + gst_video_format_get_component_offset (encoder->state.format, 0,
      encoder->state.width, encoder->state.height);
  image->planes[PLANE_U] =
      data + gst_video_format_get_component_offset (encoder->state.format, 1,
      encoder->state.width, encoder->state.height);
  image->planes[PLANE_V] =
      data + gst_video_format_get_component_offset (encoder->state.format, 2,
      encoder->state.width, encoder->state.height);

  return image;
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
  vpx_codec_err_t status;
  int flags = 0;
  vpx_codec_iter_t iter = NULL;
  const vpx_codec_cx_pkt_t *pkt;
  vpx_image_t *image;
  GstVP8EncCoderHook *hook;

  GST_DEBUG_OBJECT (base_video_encoder, "handle_frame");

  encoder = GST_VP8_ENC (base_video_encoder);
  src = GST_BUFFER_DATA (frame->sink_buffer);

  state = gst_base_video_encoder_get_state (base_video_encoder);
  encoder->n_frames++;

  GST_DEBUG_OBJECT (base_video_encoder, "res id %d size %d %d",
      encoder->resolution_id, state->width, state->height);

  if (!encoder->inited) {
    vpx_codec_enc_cfg_t cfg;

    status = vpx_codec_enc_config_default (&vpx_codec_vp8_cx_algo, &cfg, 0);
    if (status != VPX_CODEC_OK) {
      GST_ELEMENT_ERROR (encoder, LIBRARY, INIT,
          ("Failed to get default encoder configuration"), ("%s",
              gst_vpx_error_name (status)));
      return FALSE;
    }

    cfg.g_w = base_video_encoder->state.width;
    cfg.g_h = base_video_encoder->state.height;
    cfg.g_timebase.num = base_video_encoder->state.fps_d;
    cfg.g_timebase.den = base_video_encoder->state.fps_n;

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
    }

    cfg.kf_mode = VPX_KF_AUTO;
    cfg.kf_min_dist = 0;
    cfg.kf_max_dist = encoder->max_keyframe_distance;

    status = vpx_codec_enc_init (&encoder->encoder, &vpx_codec_vp8_cx_algo,
        &cfg, 0);
    if (status) {
      GST_ELEMENT_ERROR (encoder, LIBRARY, INIT,
          ("Failed to initialize encoder"), ("%s",
              gst_vpx_error_name (status)));
      return GST_FLOW_ERROR;
    }

    gst_base_video_encoder_set_latency (base_video_encoder, 0,
        gst_util_uint64_scale (encoder->max_latency,
            base_video_encoder->state.fps_d * GST_SECOND,
            base_video_encoder->state.fps_n));
    encoder->inited = TRUE;
  }

  image = gst_vp8_enc_buffer_to_image (encoder, frame->sink_buffer);

  hook = g_slice_new0 (GstVP8EncCoderHook);
  hook->image = image;
  frame->coder_hook = hook;

  if (encoder->force_keyframe) {
    flags |= VPX_EFLAG_FORCE_KF;
  }

  status = vpx_codec_encode (&encoder->encoder, image,
      encoder->n_frames, 1, flags, speed_table[encoder->speed]);
  if (status != 0) {
    GST_ELEMENT_ERROR (encoder, LIBRARY, ENCODE,
        ("Failed to encode frame"), ("%s", gst_vpx_error_name (status)));
    g_slice_free (GstVP8EncCoderHook, hook);
    frame->coder_hook = NULL;
    g_slice_free (vpx_image_t, image);
    return FALSE;
  }

  pkt = vpx_codec_get_cx_data (&encoder->encoder, &iter);
  while (pkt != NULL) {
    GstBuffer *buffer;
    gboolean invisible;

    GST_DEBUG_OBJECT (encoder, "packet %d type %d", pkt->data.frame.sz,
        pkt->kind);

    if (pkt->kind != VPX_CODEC_CX_FRAME_PKT) {
      GST_ERROR_OBJECT (encoder, "non frame pkt");
      continue;
    }

    invisible = (pkt->data.frame.flags & VPX_FRAME_IS_INVISIBLE) != 0;
    frame = gst_base_video_encoder_get_oldest_frame (base_video_encoder);
    g_assert (frame != NULL);
    frame->is_sync_point = (pkt->data.frame.flags & VPX_FRAME_IS_KEY) != 0;
    hook = frame->coder_hook;

    buffer = gst_buffer_new_and_alloc (pkt->data.frame.sz);

    memcpy (GST_BUFFER_DATA (buffer), pkt->data.frame.buf, pkt->data.frame.sz);

    if (hook->image)
      g_slice_free (vpx_image_t, hook->image);
    hook->image = NULL;

    if (invisible) {
      hook->invisible = g_list_append (hook->invisible, buffer);
    } else {
      frame->src_buffer = buffer;
      gst_base_video_encoder_finish_frame (base_video_encoder, frame);
    }

    pkt = vpx_codec_get_cx_data (&encoder->encoder, &iter);
  }

  return TRUE;
}

static guint64
_to_granulepos (guint64 frame_end_number, guint inv_count, guint keyframe_dist)
{
  guint64 granulepos;
  guint inv;

  inv = (inv_count == 0) ? 0x3 : inv_count - 1;

  granulepos = (frame_end_number << 32) | (inv << 30) | (keyframe_dist << 3);
  return granulepos;
}

static GstFlowReturn
gst_vp8_enc_shape_output (GstBaseVideoEncoder * base_video_encoder,
    GstVideoFrame * frame)
{
  GstVP8Enc *encoder;
  GstBuffer *buf;
  const GstVideoState *state;
  GstFlowReturn ret;
  GstVP8EncCoderHook *hook = frame->coder_hook;
  GList *l;
  gint inv_count;

  GST_DEBUG_OBJECT (base_video_encoder, "shape_output");

  encoder = GST_VP8_ENC (base_video_encoder);

  state = gst_base_video_encoder_get_state (base_video_encoder);

  for (inv_count = 0, l = hook->invisible; l; inv_count++, l = l->next) {
    buf = l->data;

    if (l == hook->invisible && frame->is_sync_point) {
      GST_BUFFER_FLAG_UNSET (buf, GST_BUFFER_FLAG_DELTA_UNIT);
      encoder->keyframe_distance = 0;
    } else {
      GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DELTA_UNIT);
      encoder->keyframe_distance++;
    }

    GST_BUFFER_TIMESTAMP (buf) = gst_video_state_get_timestamp (state,
        &base_video_encoder->segment, frame->presentation_frame_number);
    GST_BUFFER_DURATION (buf) = 0;
    GST_BUFFER_OFFSET_END (buf) =
        _to_granulepos (frame->presentation_frame_number + 1,
        inv_count, encoder->keyframe_distance);
    GST_BUFFER_OFFSET (buf) =
        gst_util_uint64_scale (frame->presentation_frame_number + 1,
        GST_SECOND * state->fps_d, state->fps_n);

    gst_buffer_set_caps (buf, base_video_encoder->caps);
    ret = gst_pad_push (GST_BASE_VIDEO_CODEC_SRC_PAD (base_video_encoder), buf);

    if (ret != GST_FLOW_OK) {
      GST_WARNING_OBJECT (encoder, "flow error %d", ret);
      goto done;
    }
  }

  buf = frame->src_buffer;
  frame->src_buffer = NULL;

  if (!hook->invisible && frame->is_sync_point) {
    GST_BUFFER_FLAG_UNSET (buf, GST_BUFFER_FLAG_DELTA_UNIT);
    encoder->keyframe_distance = 0;
  } else {
    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DELTA_UNIT);
    encoder->keyframe_distance++;
  }

  GST_BUFFER_TIMESTAMP (buf) = gst_video_state_get_timestamp (state,
      &base_video_encoder->segment, frame->presentation_frame_number);
  GST_BUFFER_DURATION (buf) = gst_video_state_get_timestamp (state,
      &base_video_encoder->segment,
      frame->presentation_frame_number + 1) - GST_BUFFER_TIMESTAMP (buf);
  GST_BUFFER_OFFSET_END (buf) =
      _to_granulepos (frame->presentation_frame_number + 1,
      0, encoder->keyframe_distance);
  GST_BUFFER_OFFSET (buf) =
      gst_util_uint64_scale (frame->presentation_frame_number + 1,
      GST_SECOND * state->fps_d, state->fps_n);

  gst_buffer_set_caps (buf, base_video_encoder->caps);

  ret = gst_pad_push (GST_BASE_VIDEO_CODEC_SRC_PAD (base_video_encoder), buf);
  if (ret != GST_FLOW_OK) {
    GST_WARNING_OBJECT (encoder, "flow error %d", ret);
  }

done:
  g_list_foreach (hook->invisible, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (hook->invisible);
  g_slice_free (GstVP8EncCoderHook, hook);
  frame->coder_hook = NULL;

  return ret;
}

static gboolean
gst_vp8_enc_sink_event (GstPad * pad, GstEvent * event)
{
  GstVP8Enc *enc = GST_VP8_ENC (gst_pad_get_parent (pad));
  gboolean ret;

  if (GST_EVENT_TYPE (event) == GST_EVENT_TAG) {
    GstTagList *list;
    GstTagSetter *setter = GST_TAG_SETTER (enc);
    const GstTagMergeMode mode = gst_tag_setter_get_tag_merge_mode (setter);

    gst_event_parse_tag (event, &list);
    gst_tag_setter_merge_tags (setter, list, mode);
  }

  ret = enc->base_sink_event_func (pad, event);
  gst_object_unref (enc);

  return ret;
}
