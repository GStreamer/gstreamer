/* 
 * Copyright (C) 2012 Collabora Ltd.
 *     Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
 * Copyright (C) 2013 Sebastian Dröge <slomo@circular-chaos.org>
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
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstopenjpegenc.h"
#include <gst/codecparsers/gstjpeg2000sampling.h>

#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_openjpeg_enc_debug);
#define GST_CAT_DEFAULT gst_openjpeg_enc_debug

#define GST_OPENJPEG_ENC_TYPE_PROGRESSION_ORDER (gst_openjpeg_enc_progression_order_get_type())
static GType
gst_openjpeg_enc_progression_order_get_type (void)
{
  static const GEnumValue values[] = {
    {OPJ_LRCP, "LRCP", "lrcp"},
    {OPJ_RLCP, "RLCP", "rlcp"},
    {OPJ_RPCL, "RPCL", "rpcl"},
    {OPJ_PCRL, "PCRL", "pcrl"},
    {OPJ_CPRL, "CPRL", "crpl"},
    {0, NULL, NULL}
  };
  static volatile GType id = 0;

  if (g_once_init_enter ((gsize *) & id)) {
    GType _id;

    _id = g_enum_register_static ("GstOpenJPEGEncProgressionOrder", values);

    g_once_init_leave ((gsize *) & id, _id);
  }

  return id;
}

enum
{
  PROP_0,
  PROP_NUM_LAYERS,
  PROP_NUM_RESOLUTIONS,
  PROP_PROGRESSION_ORDER,
  PROP_TILE_OFFSET_X,
  PROP_TILE_OFFSET_Y,
  PROP_TILE_WIDTH,
  PROP_TILE_HEIGHT
};

#define DEFAULT_NUM_LAYERS 1
#define DEFAULT_NUM_RESOLUTIONS 6
#define DEFAULT_PROGRESSION_ORDER OPJ_LRCP
#define DEFAULT_TILE_OFFSET_X 0
#define DEFAULT_TILE_OFFSET_Y 0
#define DEFAULT_TILE_WIDTH 0
#define DEFAULT_TILE_HEIGHT 0

static void gst_openjpeg_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_openjpeg_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_openjpeg_enc_start (GstVideoEncoder * encoder);
static gboolean gst_openjpeg_enc_stop (GstVideoEncoder * encoder);
static gboolean gst_openjpeg_enc_set_format (GstVideoEncoder * encoder,
    GstVideoCodecState * state);
static GstFlowReturn gst_openjpeg_enc_handle_frame (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame);
static gboolean gst_openjpeg_enc_propose_allocation (GstVideoEncoder * encoder,
    GstQuery * query);

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
#define GRAY16 "GRAY16_LE"
#define YUV10 "Y444_10LE, I422_10LE, I420_10LE"
#else
#define GRAY16 "GRAY16_BE"
#define YUV10 "Y444_10BE, I422_10BE, I420_10BE"
#endif

static GstStaticPadTemplate gst_openjpeg_enc_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{ ARGB64, ARGB, xRGB, "
            "AYUV64, " YUV10 ", "
            "AYUV, Y444, Y42B, I420, Y41B, YUV9, " "GRAY8, " GRAY16 " }"))
    );

static GstStaticPadTemplate gst_openjpeg_enc_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("image/x-j2c, "
        "width = (int) [1, MAX], "
        "height = (int) [1, MAX], "
        "num-components = (int) [1, 4], "
        GST_JPEG2000_SAMPLING_LIST ","
        GST_JPEG2000_COLORSPACE_LIST "; "
        "image/x-jpc, "
        "width = (int) [1, MAX], "
        "height = (int) [1, MAX], "
        "num-components = (int) [1, 4], "
        GST_JPEG2000_SAMPLING_LIST ","
        GST_JPEG2000_COLORSPACE_LIST "; "
        "image/jp2, " "width = (int) [1, MAX], " "height = (int) [1, MAX]")
    );

#define parent_class gst_openjpeg_enc_parent_class
G_DEFINE_TYPE (GstOpenJPEGEnc, gst_openjpeg_enc, GST_TYPE_VIDEO_ENCODER);

static void
gst_openjpeg_enc_class_init (GstOpenJPEGEncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstVideoEncoderClass *video_encoder_class;

  gobject_class = (GObjectClass *) klass;
  element_class = (GstElementClass *) klass;
  video_encoder_class = (GstVideoEncoderClass *) klass;

  gobject_class->set_property = gst_openjpeg_enc_set_property;
  gobject_class->get_property = gst_openjpeg_enc_get_property;

  g_object_class_install_property (gobject_class, PROP_NUM_LAYERS,
      g_param_spec_int ("num-layers", "Number of layers",
          "Number of layers", 1, 10, DEFAULT_NUM_LAYERS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_NUM_RESOLUTIONS,
      g_param_spec_int ("num-resolutions", "Number of resolutions",
          "Number of resolutions", 1, 10, DEFAULT_NUM_RESOLUTIONS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_PROGRESSION_ORDER,
      g_param_spec_enum ("progression-order", "Progression Order",
          "Progression order", GST_OPENJPEG_ENC_TYPE_PROGRESSION_ORDER,
          DEFAULT_PROGRESSION_ORDER,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TILE_OFFSET_X,
      g_param_spec_int ("tile-offset-x", "Tile Offset X",
          "Tile Offset X", G_MININT, G_MAXINT, DEFAULT_TILE_OFFSET_X,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TILE_OFFSET_Y,
      g_param_spec_int ("tile-offset-y", "Tile Offset Y",
          "Tile Offset Y", G_MININT, G_MAXINT, DEFAULT_TILE_OFFSET_Y,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TILE_WIDTH,
      g_param_spec_int ("tile-width", "Tile Width",
          "Tile Width", 0, G_MAXINT, DEFAULT_TILE_WIDTH,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TILE_HEIGHT,
      g_param_spec_int ("tile-height", "Tile Height",
          "Tile Height", 0, G_MAXINT, DEFAULT_TILE_HEIGHT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_static_pad_template (element_class,
      &gst_openjpeg_enc_src_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_openjpeg_enc_sink_template);

  gst_element_class_set_static_metadata (element_class,
      "OpenJPEG JPEG2000 encoder",
      "Codec/Encoder/Video",
      "Encode JPEG2000 streams",
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");

  video_encoder_class->start = GST_DEBUG_FUNCPTR (gst_openjpeg_enc_start);
  video_encoder_class->stop = GST_DEBUG_FUNCPTR (gst_openjpeg_enc_stop);
  video_encoder_class->set_format =
      GST_DEBUG_FUNCPTR (gst_openjpeg_enc_set_format);
  video_encoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_openjpeg_enc_handle_frame);
  video_encoder_class->propose_allocation = gst_openjpeg_enc_propose_allocation;

  GST_DEBUG_CATEGORY_INIT (gst_openjpeg_enc_debug, "openjpegenc", 0,
      "OpenJPEG Encoder");
}

static void
gst_openjpeg_enc_init (GstOpenJPEGEnc * self)
{
  GST_PAD_SET_ACCEPT_TEMPLATE (GST_VIDEO_ENCODER_SINK_PAD (self));

  opj_set_default_encoder_parameters (&self->params);

  self->params.cp_fixed_quality = 1;
  self->params.cp_disto_alloc = 0;
  self->params.cp_fixed_alloc = 0;

  /*
   * TODO: Add properties / caps fields for these
   *
   * self->params.csty;
   * self->params.tcp_rates;
   * self->params.tcp_distoratio;
   * self->params.mode;
   * self->params.irreversible;
   * self->params.cp_cinema;
   * self->params.cp_rsiz;
   */

  self->params.tcp_numlayers = DEFAULT_NUM_LAYERS;
  self->params.numresolution = DEFAULT_NUM_RESOLUTIONS;
  self->params.prog_order = DEFAULT_PROGRESSION_ORDER;
  self->params.cp_tx0 = DEFAULT_TILE_OFFSET_X;
  self->params.cp_ty0 = DEFAULT_TILE_OFFSET_Y;
  self->params.cp_tdx = DEFAULT_TILE_WIDTH;
  self->params.cp_tdy = DEFAULT_TILE_HEIGHT;
  self->params.tile_size_on = (self->params.cp_tdx != 0
      && self->params.cp_tdy != 0);
}

static void
gst_openjpeg_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstOpenJPEGEnc *self = GST_OPENJPEG_ENC (object);

  switch (prop_id) {
    case PROP_NUM_LAYERS:
      self->params.tcp_numlayers = g_value_get_int (value);
      break;
    case PROP_NUM_RESOLUTIONS:
      self->params.numresolution = g_value_get_int (value);
      break;
    case PROP_PROGRESSION_ORDER:
      self->params.prog_order = g_value_get_enum (value);
      break;
    case PROP_TILE_OFFSET_X:
      self->params.cp_tx0 = g_value_get_int (value);
      break;
    case PROP_TILE_OFFSET_Y:
      self->params.cp_ty0 = g_value_get_int (value);
      break;
    case PROP_TILE_WIDTH:
      self->params.cp_tdx = g_value_get_int (value);
      self->params.tile_size_on = (self->params.cp_tdx != 0
          && self->params.cp_tdy != 0);
      break;
    case PROP_TILE_HEIGHT:
      self->params.cp_tdy = g_value_get_int (value);
      self->params.tile_size_on = (self->params.cp_tdx != 0
          && self->params.cp_tdy != 0);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_openjpeg_enc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstOpenJPEGEnc *self = GST_OPENJPEG_ENC (object);

  switch (prop_id) {
    case PROP_NUM_LAYERS:
      g_value_set_int (value, self->params.tcp_numlayers);
      break;
    case PROP_NUM_RESOLUTIONS:
      g_value_set_int (value, self->params.numresolution);
      break;
    case PROP_PROGRESSION_ORDER:
      g_value_set_enum (value, self->params.prog_order);
      break;
    case PROP_TILE_OFFSET_X:
      g_value_set_int (value, self->params.cp_tx0);
      break;
    case PROP_TILE_OFFSET_Y:
      g_value_set_int (value, self->params.cp_ty0);
      break;
    case PROP_TILE_WIDTH:
      g_value_set_int (value, self->params.cp_tdx);
      break;
    case PROP_TILE_HEIGHT:
      g_value_set_int (value, self->params.cp_tdy);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_openjpeg_enc_start (GstVideoEncoder * encoder)
{
  GstOpenJPEGEnc *self = GST_OPENJPEG_ENC (encoder);

  GST_DEBUG_OBJECT (self, "Starting");

  return TRUE;
}

static gboolean
gst_openjpeg_enc_stop (GstVideoEncoder * video_encoder)
{
  GstOpenJPEGEnc *self = GST_OPENJPEG_ENC (video_encoder);

  GST_DEBUG_OBJECT (self, "Stopping");

  if (self->output_state) {
    gst_video_codec_state_unref (self->output_state);
    self->output_state = NULL;
  }

  if (self->input_state) {
    gst_video_codec_state_unref (self->input_state);
    self->input_state = NULL;
  }

  GST_DEBUG_OBJECT (self, "Stopped");

  return TRUE;
}

static void
fill_image_packed16_4 (opj_image_t * image, GstVideoFrame * frame)
{
  gint x, y, w, h;
  const guint16 *data_in, *tmp;
  gint *data_out[4];
  gint sstride;

  w = GST_VIDEO_FRAME_WIDTH (frame);
  h = GST_VIDEO_FRAME_HEIGHT (frame);
  data_in = (guint16 *) GST_VIDEO_FRAME_PLANE_DATA (frame, 0);
  sstride = GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0) / 2;

  data_out[0] = image->comps[0].data;
  data_out[1] = image->comps[1].data;
  data_out[2] = image->comps[2].data;
  data_out[3] = image->comps[3].data;

  for (y = 0; y < h; y++) {
    tmp = data_in;

    for (x = 0; x < w; x++) {
      *data_out[3] = tmp[0];
      *data_out[0] = tmp[1];
      *data_out[1] = tmp[2];
      *data_out[2] = tmp[3];

      tmp += 4;
      data_out[0]++;
      data_out[1]++;
      data_out[2]++;
      data_out[3]++;
    }
    data_in += sstride;
  }
}

static void
fill_image_packed8_4 (opj_image_t * image, GstVideoFrame * frame)
{
  gint x, y, w, h;
  const guint8 *data_in, *tmp;
  gint *data_out[4];
  gint sstride;

  w = GST_VIDEO_FRAME_WIDTH (frame);
  h = GST_VIDEO_FRAME_HEIGHT (frame);
  data_in = GST_VIDEO_FRAME_PLANE_DATA (frame, 0);
  sstride = GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0);

  data_out[0] = image->comps[0].data;
  data_out[1] = image->comps[1].data;
  data_out[2] = image->comps[2].data;
  data_out[3] = image->comps[3].data;

  for (y = 0; y < h; y++) {
    tmp = data_in;

    for (x = 0; x < w; x++) {
      *data_out[3] = tmp[0];
      *data_out[0] = tmp[1];
      *data_out[1] = tmp[2];
      *data_out[2] = tmp[3];

      tmp += 4;
      data_out[0]++;
      data_out[1]++;
      data_out[2]++;
      data_out[3]++;
    }
    data_in += sstride;
  }
}

static void
fill_image_packed8_3 (opj_image_t * image, GstVideoFrame * frame)
{
  gint x, y, w, h;
  const guint8 *data_in, *tmp;
  gint *data_out[3];
  gint sstride;

  w = GST_VIDEO_FRAME_WIDTH (frame);
  h = GST_VIDEO_FRAME_HEIGHT (frame);
  data_in = GST_VIDEO_FRAME_PLANE_DATA (frame, 0);
  sstride = GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0);

  data_out[0] = image->comps[0].data;
  data_out[1] = image->comps[1].data;
  data_out[2] = image->comps[2].data;

  for (y = 0; y < h; y++) {
    tmp = data_in;

    for (x = 0; x < w; x++) {
      *data_out[0] = tmp[1];
      *data_out[1] = tmp[2];
      *data_out[2] = tmp[3];

      tmp += 4;
      data_out[0]++;
      data_out[1]++;
      data_out[2]++;
    }
    data_in += sstride;
  }
}

static void
fill_image_planar16_3 (opj_image_t * image, GstVideoFrame * frame)
{
  gint c, x, y, w, h;
  const guint16 *data_in, *tmp;
  gint *data_out;
  gint sstride;

  for (c = 0; c < 3; c++) {
    w = GST_VIDEO_FRAME_COMP_WIDTH (frame, c);
    h = GST_VIDEO_FRAME_COMP_HEIGHT (frame, c);
    data_in = (guint16 *) GST_VIDEO_FRAME_COMP_DATA (frame, c);
    sstride = GST_VIDEO_FRAME_PLANE_STRIDE (frame, c) / 2;
    data_out = image->comps[c].data;

    for (y = 0; y < h; y++) {
      tmp = data_in;
      for (x = 0; x < w; x++) {
        *data_out = *tmp;
        data_out++;
        tmp++;
      }
      data_in += sstride;
    }
  }
}

static void
fill_image_planar8_3 (opj_image_t * image, GstVideoFrame * frame)
{
  gint c, x, y, w, h;
  const guint8 *data_in, *tmp;
  gint *data_out;
  gint sstride;

  for (c = 0; c < 3; c++) {
    w = GST_VIDEO_FRAME_COMP_WIDTH (frame, c);
    h = GST_VIDEO_FRAME_COMP_HEIGHT (frame, c);
    data_in = GST_VIDEO_FRAME_COMP_DATA (frame, c);
    sstride = GST_VIDEO_FRAME_PLANE_STRIDE (frame, c);
    data_out = image->comps[c].data;

    for (y = 0; y < h; y++) {
      tmp = data_in;
      for (x = 0; x < w; x++) {
        *data_out = *tmp;
        data_out++;
        tmp++;
      }
      data_in += sstride;
    }
  }
}

static void
fill_image_planar8_1 (opj_image_t * image, GstVideoFrame * frame)
{
  gint x, y, w, h;
  const guint8 *data_in, *tmp;
  gint *data_out;
  gint sstride;

  w = GST_VIDEO_FRAME_COMP_WIDTH (frame, 0);
  h = GST_VIDEO_FRAME_COMP_HEIGHT (frame, 0);
  data_in = GST_VIDEO_FRAME_COMP_DATA (frame, 0);
  sstride = GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0);
  data_out = image->comps[0].data;

  for (y = 0; y < h; y++) {
    tmp = data_in;
    for (x = 0; x < w; x++) {
      *data_out = *tmp;
      data_out++;
      tmp++;
    }
    data_in += sstride;
  }
}

static void
fill_image_planar16_1 (opj_image_t * image, GstVideoFrame * frame)
{
  gint x, y, w, h;
  const guint16 *data_in, *tmp;
  gint *data_out;
  gint sstride;

  w = GST_VIDEO_FRAME_COMP_WIDTH (frame, 0);
  h = GST_VIDEO_FRAME_COMP_HEIGHT (frame, 0);
  data_in = (guint16 *) GST_VIDEO_FRAME_COMP_DATA (frame, 0);
  sstride = GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0) / 2;
  data_out = image->comps[0].data;

  for (y = 0; y < h; y++) {
    tmp = data_in;
    for (x = 0; x < w; x++) {
      *data_out = *tmp;
      data_out++;
      tmp++;
    }
    data_in += sstride;
  }
}

static gboolean
gst_openjpeg_enc_set_format (GstVideoEncoder * encoder,
    GstVideoCodecState * state)
{
  GstOpenJPEGEnc *self = GST_OPENJPEG_ENC (encoder);
  GstCaps *allowed_caps, *caps;
  GstStructure *s;
  const gchar *colorspace = NULL;
  GstJPEG2000Sampling sampling = GST_JPEG2000_SAMPLING_NONE;
  gint ncomps;

  GST_DEBUG_OBJECT (self, "Setting format: %" GST_PTR_FORMAT, state->caps);

  if (self->input_state)
    gst_video_codec_state_unref (self->input_state);
  self->input_state = gst_video_codec_state_ref (state);

  allowed_caps = gst_pad_get_allowed_caps (GST_VIDEO_ENCODER_SRC_PAD (encoder));
  allowed_caps = gst_caps_truncate (allowed_caps);
  s = gst_caps_get_structure (allowed_caps, 0);
  if (gst_structure_has_name (s, "image/jp2")) {
    self->codec_format = OPJ_CODEC_JP2;
    self->is_jp2c = FALSE;
  } else if (gst_structure_has_name (s, "image/x-j2c")) {
    self->codec_format = OPJ_CODEC_J2K;
    self->is_jp2c = TRUE;
  } else if (gst_structure_has_name (s, "image/x-jpc")) {
    self->codec_format = OPJ_CODEC_J2K;
    self->is_jp2c = FALSE;
  } else {
    g_return_val_if_reached (FALSE);
  }

  switch (state->info.finfo->format) {
    case GST_VIDEO_FORMAT_ARGB64:
      self->fill_image = fill_image_packed16_4;
      ncomps = 4;
      break;
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_AYUV:
      self->fill_image = fill_image_packed8_4;
      ncomps = 4;
      break;
    case GST_VIDEO_FORMAT_xRGB:
      self->fill_image = fill_image_packed8_3;
      ncomps = 3;
      break;
    case GST_VIDEO_FORMAT_AYUV64:
      self->fill_image = fill_image_packed16_4;
      ncomps = 4;
      break;
    case GST_VIDEO_FORMAT_Y444_10LE:
    case GST_VIDEO_FORMAT_Y444_10BE:
    case GST_VIDEO_FORMAT_I422_10LE:
    case GST_VIDEO_FORMAT_I422_10BE:
    case GST_VIDEO_FORMAT_I420_10LE:
    case GST_VIDEO_FORMAT_I420_10BE:
      self->fill_image = fill_image_planar16_3;
      ncomps = 3;
      break;
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_Y41B:
    case GST_VIDEO_FORMAT_YUV9:
      self->fill_image = fill_image_planar8_3;
      ncomps = 3;
      break;
    case GST_VIDEO_FORMAT_GRAY8:
      self->fill_image = fill_image_planar8_1;
      ncomps = 1;
      break;
    case GST_VIDEO_FORMAT_GRAY16_LE:
    case GST_VIDEO_FORMAT_GRAY16_BE:
      self->fill_image = fill_image_planar16_1;
      ncomps = 1;
      break;
    default:
      g_assert_not_reached ();
  }


  /* sampling */
  /* note: encoder re-orders channels so that alpha channel is encoded as the last channel */
  switch (state->info.finfo->format) {
    case GST_VIDEO_FORMAT_ARGB64:
    case GST_VIDEO_FORMAT_ARGB:
      sampling = GST_JPEG2000_SAMPLING_RGBA;
      break;
    case GST_VIDEO_FORMAT_AYUV64:
    case GST_VIDEO_FORMAT_AYUV:
      sampling = GST_JPEG2000_SAMPLING_YBRA4444_EXT;
      break;
    case GST_VIDEO_FORMAT_xRGB:
      sampling = GST_JPEG2000_SAMPLING_RGB;
      break;
    case GST_VIDEO_FORMAT_Y444_10LE:
    case GST_VIDEO_FORMAT_Y444_10BE:
    case GST_VIDEO_FORMAT_Y444:
      sampling = GST_JPEG2000_SAMPLING_YBR444;
      break;

    case GST_VIDEO_FORMAT_I422_10LE:
    case GST_VIDEO_FORMAT_I422_10BE:
    case GST_VIDEO_FORMAT_Y42B:
      sampling = GST_JPEG2000_SAMPLING_YBR422;
      break;
    case GST_VIDEO_FORMAT_YUV9:
      sampling = GST_JPEG2000_SAMPLING_YBR410;
      break;
    case GST_VIDEO_FORMAT_I420_10LE:
    case GST_VIDEO_FORMAT_I420_10BE:
    case GST_VIDEO_FORMAT_I420:
      sampling = GST_JPEG2000_SAMPLING_YBR420;
      break;
    case GST_VIDEO_FORMAT_GRAY8:
    case GST_VIDEO_FORMAT_GRAY16_LE:
    case GST_VIDEO_FORMAT_GRAY16_BE:
      sampling = GST_JPEG2000_SAMPLING_GRAYSCALE;
      break;
    default:
      break;
  }



  if ((state->info.finfo->flags & GST_VIDEO_FORMAT_FLAG_YUV)) {
    colorspace = "sYUV";
  } else if ((state->info.finfo->flags & GST_VIDEO_FORMAT_FLAG_RGB)) {
    colorspace = "sRGB";
  } else if ((state->info.finfo->flags & GST_VIDEO_FORMAT_FLAG_GRAY)) {
    colorspace = "GRAY";
  } else
    g_return_val_if_reached (FALSE);

  if (sampling != GST_JPEG2000_SAMPLING_NONE) {
    caps = gst_caps_new_simple (gst_structure_get_name (s),
        "colorspace", G_TYPE_STRING, colorspace,
        "sampling", G_TYPE_STRING, gst_jpeg2000_sampling_to_string (sampling),
        "num-components", G_TYPE_INT, ncomps, NULL);
  } else {
    caps = gst_caps_new_simple (gst_structure_get_name (s),
        "colorspace", G_TYPE_STRING, colorspace,
        "num-components", G_TYPE_INT, ncomps, NULL);

  }
  gst_caps_unref (allowed_caps);

  if (self->output_state)
    gst_video_codec_state_unref (self->output_state);
  self->output_state =
      gst_video_encoder_set_output_state (encoder, caps, state);

  gst_video_encoder_negotiate (GST_VIDEO_ENCODER (encoder));

  return TRUE;
}

static opj_image_t *
gst_openjpeg_enc_fill_image (GstOpenJPEGEnc * self, GstVideoFrame * frame)
{
  gint i, ncomps;
  opj_image_cmptparm_t *comps;
  OPJ_COLOR_SPACE colorspace;
  opj_image_t *image;

  ncomps = GST_VIDEO_FRAME_N_COMPONENTS (frame);
  comps = g_new0 (opj_image_cmptparm_t, ncomps);

  for (i = 0; i < ncomps; i++) {
    comps[i].prec = GST_VIDEO_FRAME_COMP_DEPTH (frame, i);
    comps[i].bpp = GST_VIDEO_FRAME_COMP_DEPTH (frame, i);
    comps[i].sgnd = 0;
    comps[i].w = GST_VIDEO_FRAME_COMP_WIDTH (frame, i);
    comps[i].h = GST_VIDEO_FRAME_COMP_HEIGHT (frame, i);
    comps[i].dx =
        GST_VIDEO_FRAME_WIDTH (frame) / GST_VIDEO_FRAME_COMP_WIDTH (frame, i);
    comps[i].dy =
        GST_VIDEO_FRAME_HEIGHT (frame) / GST_VIDEO_FRAME_COMP_HEIGHT (frame, i);
  }

  if ((frame->info.finfo->flags & GST_VIDEO_FORMAT_FLAG_YUV))
    colorspace = OPJ_CLRSPC_SYCC;
  else if ((frame->info.finfo->flags & GST_VIDEO_FORMAT_FLAG_RGB))
    colorspace = OPJ_CLRSPC_SRGB;
  else if ((frame->info.finfo->flags & GST_VIDEO_FORMAT_FLAG_GRAY))
    colorspace = OPJ_CLRSPC_GRAY;
  else
    g_return_val_if_reached (NULL);

  image = opj_image_create (ncomps, comps, colorspace);
  g_free (comps);

  image->x0 = image->y0 = 0;
  image->x1 = GST_VIDEO_FRAME_WIDTH (frame);
  image->y1 = GST_VIDEO_FRAME_HEIGHT (frame);

  self->fill_image (image, frame);

  return image;
}

static void
gst_openjpeg_enc_opj_error (const char *msg, void *userdata)
{
  GstOpenJPEGEnc *self = GST_OPENJPEG_ENC (userdata);
  gchar *trimmed = g_strchomp (g_strdup (msg));
  GST_TRACE_OBJECT (self, "openjpeg error: %s", trimmed);
  g_free (trimmed);
}

static void
gst_openjpeg_enc_opj_warning (const char *msg, void *userdata)
{
  GstOpenJPEGEnc *self = GST_OPENJPEG_ENC (userdata);
  gchar *trimmed = g_strchomp (g_strdup (msg));
  GST_TRACE_OBJECT (self, "openjpeg warning: %s", trimmed);
  g_free (trimmed);
}

static void
gst_openjpeg_enc_opj_info (const char *msg, void *userdata)
{
  GstOpenJPEGEnc *self = GST_OPENJPEG_ENC (userdata);
  gchar *trimmed = g_strchomp (g_strdup (msg));
  GST_TRACE_OBJECT (self, "openjpeg info: %s", trimmed);
  g_free (trimmed);
}


#ifndef HAVE_OPENJPEG_1
typedef struct
{
  guint8 *data;
  guint allocsize;
  guint offset;
  guint size;
} MemStream;

static OPJ_SIZE_T
read_fn (void *p_buffer, OPJ_SIZE_T p_nb_bytes, void *p_user_data)
{
  g_return_val_if_reached (-1);
}

static OPJ_SIZE_T
write_fn (void *p_buffer, OPJ_SIZE_T p_nb_bytes, void *p_user_data)
{
  MemStream *mstream = p_user_data;

  if (mstream->offset + p_nb_bytes > mstream->allocsize) {
    while (mstream->offset + p_nb_bytes > mstream->allocsize)
      mstream->allocsize *= 2;
    mstream->data = g_realloc (mstream->data, mstream->allocsize);
  }

  memcpy (mstream->data + mstream->offset, p_buffer, p_nb_bytes);

  if (mstream->offset + p_nb_bytes > mstream->size)
    mstream->size = mstream->offset + p_nb_bytes;
  mstream->offset += p_nb_bytes;

  return p_nb_bytes;
}

static OPJ_OFF_T
skip_fn (OPJ_OFF_T p_nb_bytes, void *p_user_data)
{
  MemStream *mstream = p_user_data;

  if (mstream->offset + p_nb_bytes > mstream->allocsize) {
    while (mstream->offset + p_nb_bytes > mstream->allocsize)
      mstream->allocsize *= 2;
    mstream->data = g_realloc (mstream->data, mstream->allocsize);
  }

  if (mstream->offset + p_nb_bytes > mstream->size)
    mstream->size = mstream->offset + p_nb_bytes;

  mstream->offset += p_nb_bytes;

  return p_nb_bytes;
}

static OPJ_BOOL
seek_fn (OPJ_OFF_T p_nb_bytes, void *p_user_data)
{
  MemStream *mstream = p_user_data;

  if (p_nb_bytes > mstream->size)
    return OPJ_FALSE;

  mstream->offset = p_nb_bytes;

  return OPJ_TRUE;
}
#endif

static GstFlowReturn
gst_openjpeg_enc_handle_frame (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame)
{
  GstOpenJPEGEnc *self = GST_OPENJPEG_ENC (encoder);
  GstFlowReturn ret = GST_FLOW_OK;
#ifdef HAVE_OPENJPEG_1
  opj_cinfo_t *enc;
  GstMapInfo map;
  guint length;
  opj_cio_t *io;
#else
  opj_codec_t *enc;
  opj_stream_t *stream;
  MemStream mstream;
#endif
  opj_image_t *image;
  GstVideoFrame vframe;

  GST_DEBUG_OBJECT (self, "Handling frame");

  enc = opj_create_compress (self->codec_format);
  if (!enc)
    goto initialization_error;

#ifdef HAVE_OPENJPEG_1
  if (G_UNLIKELY (gst_debug_category_get_threshold (GST_CAT_DEFAULT) >=
          GST_LEVEL_TRACE)) {
    opj_event_mgr_t callbacks;

    callbacks.error_handler = gst_openjpeg_enc_opj_error;
    callbacks.warning_handler = gst_openjpeg_enc_opj_warning;
    callbacks.info_handler = gst_openjpeg_enc_opj_info;
    opj_set_event_mgr ((opj_common_ptr) enc, &callbacks, self);
  } else {
    opj_set_event_mgr ((opj_common_ptr) enc, NULL, NULL);
  }
#else
  if (G_UNLIKELY (gst_debug_category_get_threshold (GST_CAT_DEFAULT) >=
          GST_LEVEL_TRACE)) {
    opj_set_info_handler (enc, gst_openjpeg_enc_opj_info, self);
    opj_set_warning_handler (enc, gst_openjpeg_enc_opj_warning, self);
    opj_set_error_handler (enc, gst_openjpeg_enc_opj_error, self);
  } else {
    opj_set_info_handler (enc, NULL, NULL);
    opj_set_warning_handler (enc, NULL, NULL);
    opj_set_error_handler (enc, NULL, NULL);
  }
#endif

  if (!gst_video_frame_map (&vframe, &self->input_state->info,
          frame->input_buffer, GST_MAP_READ))
    goto map_read_error;

  image = gst_openjpeg_enc_fill_image (self, &vframe);
  if (!image)
    goto fill_image_error;
  gst_video_frame_unmap (&vframe);

  if (vframe.info.finfo->flags & GST_VIDEO_FORMAT_FLAG_RGB) {
    self->params.tcp_mct = 1;
  }
  opj_setup_encoder (enc, &self->params, image);

#ifdef HAVE_OPENJPEG_1
  io = opj_cio_open ((opj_common_ptr) enc, NULL, 0);
  if (!io)
    goto open_error;

  if (!opj_encode (enc, io, image, NULL))
    goto encode_error;

  opj_image_destroy (image);

  length = cio_tell (io);

  ret =
      gst_video_encoder_allocate_output_frame (encoder, frame,
      length + (self->is_jp2c ? 8 : 0));
  if (ret != GST_FLOW_OK)
    goto allocate_error;

  gst_buffer_fill (frame->output_buffer, self->is_jp2c ? 8 : 0, io->buffer,
      length);
  if (self->is_jp2c) {
    gst_buffer_map (frame->output_buffer, &map, GST_MAP_WRITE);
    GST_WRITE_UINT32_BE (map.data, length + 8);
    GST_WRITE_UINT32_BE (map.data + 4, GST_MAKE_FOURCC ('j', 'p', '2', 'c'));
    gst_buffer_unmap (frame->output_buffer, &map);
  }

  opj_cio_close (io);
  opj_destroy_compress (enc);
#else
  stream = opj_stream_create (4096, OPJ_FALSE);
  if (!stream)
    goto open_error;

  mstream.allocsize = 4096;
  mstream.data = g_malloc (mstream.allocsize);
  mstream.offset = 0;
  mstream.size = 0;

  opj_stream_set_read_function (stream, read_fn);
  opj_stream_set_write_function (stream, write_fn);
  opj_stream_set_skip_function (stream, skip_fn);
  opj_stream_set_seek_function (stream, seek_fn);
#ifdef HAVE_OPENJPEG_2_1
  opj_stream_set_user_data (stream, &mstream, NULL);
#else
  opj_stream_set_user_data (stream, &mstream);
#endif
  opj_stream_set_user_data_length (stream, mstream.size);

  if (!opj_start_compress (enc, image, stream))
    goto encode_error;

  if (!opj_encode (enc, stream))
    goto encode_error;

  if (!opj_end_compress (enc, stream))
    goto encode_error;

  opj_image_destroy (image);
  opj_stream_destroy (stream);
  opj_destroy_codec (enc);

  frame->output_buffer = gst_buffer_new ();

  if (self->is_jp2c) {
    GstMapInfo map;
    GstMemory *mem;

    mem = gst_allocator_alloc (NULL, 8, NULL);
    gst_memory_map (mem, &map, GST_MAP_WRITE);
    GST_WRITE_UINT32_BE (map.data, mstream.size + 8);
    GST_WRITE_UINT32_BE (map.data + 4, GST_MAKE_FOURCC ('j', 'p', '2', 'c'));
    gst_memory_unmap (mem, &map);
    gst_buffer_append_memory (frame->output_buffer, mem);
  }

  gst_buffer_append_memory (frame->output_buffer,
      gst_memory_new_wrapped (0, mstream.data, mstream.allocsize, 0,
          mstream.size, NULL, (GDestroyNotify) g_free));
#endif

  GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT (frame);
  ret = gst_video_encoder_finish_frame (encoder, frame);

  return ret;

initialization_error:
  {
    gst_video_codec_frame_unref (frame);
    GST_ELEMENT_ERROR (self, LIBRARY, INIT,
        ("Failed to initialize OpenJPEG encoder"), (NULL));
    return GST_FLOW_ERROR;
  }
map_read_error:
  {
#ifdef HAVE_OPENJPEG_1
    opj_destroy_compress (enc);
#else
    opj_destroy_codec (enc);
#endif
    gst_video_codec_frame_unref (frame);

    GST_ELEMENT_ERROR (self, CORE, FAILED,
        ("Failed to map input buffer"), (NULL));
    return GST_FLOW_ERROR;
  }
fill_image_error:
  {
#ifdef HAVE_OPENJPEG_1
    opj_destroy_compress (enc);
#else
    opj_destroy_codec (enc);
#endif
    gst_video_frame_unmap (&vframe);
    gst_video_codec_frame_unref (frame);

    GST_ELEMENT_ERROR (self, LIBRARY, INIT,
        ("Failed to fill OpenJPEG image"), (NULL));
    return GST_FLOW_ERROR;
  }
open_error:
  {
    opj_image_destroy (image);
#ifdef HAVE_OPENJPEG_1
    opj_destroy_compress (enc);
#else
    opj_destroy_codec (enc);
#endif
    gst_video_codec_frame_unref (frame);

    GST_ELEMENT_ERROR (self, LIBRARY, INIT,
        ("Failed to open OpenJPEG data"), (NULL));
    return GST_FLOW_ERROR;
  }
encode_error:
  {
#ifdef HAVE_OPENJPEG_1
    opj_cio_close (io);
    opj_image_destroy (image);
    opj_destroy_compress (enc);
#else
    opj_stream_destroy (stream);
    g_free (mstream.data);
    opj_image_destroy (image);
    opj_destroy_codec (enc);
#endif
    gst_video_codec_frame_unref (frame);

    GST_ELEMENT_ERROR (self, STREAM, ENCODE,
        ("Failed to encode OpenJPEG stream"), (NULL));
    return GST_FLOW_ERROR;
  }
#ifdef HAVE_OPENJPEG_1
allocate_error:
  {
    opj_cio_close (io);
    opj_destroy_compress (enc);
    gst_video_codec_frame_unref (frame);

    GST_ELEMENT_ERROR (self, CORE, FAILED,
        ("Failed to allocate output buffer"), (NULL));
    return ret;
  }
#endif
}

static gboolean
gst_openjpeg_enc_propose_allocation (GstVideoEncoder * encoder,
    GstQuery * query)
{
  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

  return GST_VIDEO_ENCODER_CLASS (parent_class)->propose_allocation (encoder,
      query);
}
