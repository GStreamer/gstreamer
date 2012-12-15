/* 
 * Copyright (C) 2012 Collabora Ltd.
 *     Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
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

GST_DEBUG_CATEGORY_STATIC (gst_openjpeg_enc_debug);
#define GST_CAT_DEFAULT gst_openjpeg_enc_debug

enum
{
  PROP_0,
};

static void gst_openjpeg_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_openjpeg_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_openjpeg_enc_start (GstVideoEncoder * encoder);
static gboolean gst_openjpeg_enc_stop (GstVideoEncoder * encoder);
static gboolean gst_openjpeg_enc_set_format (GstVideoEncoder * encoder,
    GstVideoCodecState * state);
static gboolean gst_openjpeg_enc_reset (GstVideoEncoder * encoder,
    gboolean hard);
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
    GST_STATIC_CAPS ("image/x-j2c; image/x-jpc; image/jp2")
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

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_openjpeg_enc_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_openjpeg_enc_sink_template));

  gst_element_class_set_static_metadata (element_class,
      "OpenJPEG JPEG2000 encoder",
      "Codec/Encoder/Video",
      "Encode JPEG2000 streams",
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");

  video_encoder_class->start = GST_DEBUG_FUNCPTR (gst_openjpeg_enc_start);
  video_encoder_class->stop = GST_DEBUG_FUNCPTR (gst_openjpeg_enc_stop);
  video_encoder_class->reset = GST_DEBUG_FUNCPTR (gst_openjpeg_enc_reset);
  video_encoder_class->set_format =
      GST_DEBUG_FUNCPTR (gst_openjpeg_enc_set_format);
  video_encoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_openjpeg_enc_handle_frame);
  video_encoder_class->propose_allocation = gst_openjpeg_enc_propose_allocation;

  GST_DEBUG_CATEGORY_INIT (gst_openjpeg_enc_debug, "openjpegenc", 0,
      "VP8 Encoder");
}

static void
gst_openjpeg_enc_init (GstOpenJPEGEnc * self)
{
  opj_set_default_encoder_parameters (&self->params);

  /* TODO: Add properties for these */
  self->params.cp_fixed_quality = 1;
  self->params.tcp_numlayers = 1;
}

static void
gst_openjpeg_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  /* GstOpenJPEGEnc *self = GST_OPENJPEG_ENC (object); */

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_openjpeg_enc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  /* GstOpenJPEGEnc *self = GST_OPENJPEG_ENC (object); */

  switch (prop_id) {
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
  guint16 *data;
  gint sindex;

  w = GST_VIDEO_FRAME_WIDTH (frame);
  h = GST_VIDEO_FRAME_HEIGHT (frame);
  data = GST_VIDEO_FRAME_PLANE_DATA (frame, 0);

  sindex = 0;

  for (y = 0; y < h; y++) {
    for (x = 0; x < w; x++, sindex++) {
      image->comps[3].data[sindex] = data[x * 4 + 0];
      image->comps[0].data[sindex] = data[x * 4 + 1];
      image->comps[1].data[sindex] = data[x * 4 + 2];
      image->comps[2].data[sindex] = data[x * 4 + 3];
    }
    data += GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0) / 2;
  }
}

static void
fill_image_packed8_4 (opj_image_t * image, GstVideoFrame * frame)
{
  gint x, y, w, h;
  guint8 *data;
  gint sindex;

  w = GST_VIDEO_FRAME_WIDTH (frame);
  h = GST_VIDEO_FRAME_HEIGHT (frame);
  data = GST_VIDEO_FRAME_PLANE_DATA (frame, 0);

  sindex = 0;

  for (y = 0; y < h; y++) {
    for (x = 0; x < w; x++, sindex++) {
      image->comps[3].data[sindex] = data[x * 4 + 0];
      image->comps[0].data[sindex] = data[x * 4 + 1];
      image->comps[1].data[sindex] = data[x * 4 + 2];
      image->comps[2].data[sindex] = data[x * 4 + 3];
    }
    data += GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0);
  }
}

static void
fill_image_packed8_3 (opj_image_t * image, GstVideoFrame * frame)
{
  gint x, y, w, h;
  guint8 *data;
  gint sindex;

  w = GST_VIDEO_FRAME_WIDTH (frame);
  h = GST_VIDEO_FRAME_HEIGHT (frame);
  data = GST_VIDEO_FRAME_PLANE_DATA (frame, 0);

  sindex = 0;

  for (y = 0; y < h; y++) {
    for (x = 0; x < w; x++, sindex++) {
      image->comps[0].data[sindex] = data[x * 4 + 1];
      image->comps[1].data[sindex] = data[x * 4 + 2];
      image->comps[2].data[sindex] = data[x * 4 + 3];
    }
    data += GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0);
  }
}

static void
fill_image_planar16_3 (opj_image_t * image, GstVideoFrame * frame)
{
  gint c, x, y, w, h;
  guint16 *data;
  gint sindex;

  w = GST_VIDEO_FRAME_WIDTH (frame);
  h = GST_VIDEO_FRAME_HEIGHT (frame);
  data = GST_VIDEO_FRAME_PLANE_DATA (frame, 0);

  for (c = 0; c < 3; c++) {
    w = GST_VIDEO_FRAME_COMP_WIDTH (frame, c);
    h = GST_VIDEO_FRAME_COMP_HEIGHT (frame, c);
    data = (guint16 *) GST_VIDEO_FRAME_COMP_DATA (frame, c);

    sindex = 0;
    for (y = 0; y < h; y++) {
      for (x = 0; x < w; x++, sindex++) {
        image->comps[c].data[sindex] = data[x];
      }
      data += GST_VIDEO_FRAME_PLANE_STRIDE (frame, c) / 2;
    }
  }
}

static void
fill_image_planar8_3 (opj_image_t * image, GstVideoFrame * frame)
{
  gint c, x, y, w, h;
  guint8 *data;
  gint sindex;

  w = GST_VIDEO_FRAME_WIDTH (frame);
  h = GST_VIDEO_FRAME_HEIGHT (frame);
  data = GST_VIDEO_FRAME_PLANE_DATA (frame, 0);

  for (c = 0; c < 3; c++) {
    w = GST_VIDEO_FRAME_COMP_WIDTH (frame, c);
    h = GST_VIDEO_FRAME_COMP_HEIGHT (frame, c);
    data = GST_VIDEO_FRAME_COMP_DATA (frame, c);

    sindex = 0;
    for (y = 0; y < h; y++) {
      for (x = 0; x < w; x++, sindex++) {
        image->comps[c].data[sindex] = data[x];
      }
      data += GST_VIDEO_FRAME_PLANE_STRIDE (frame, c);
    }
  }
}

static void
fill_image_planar8_1 (opj_image_t * image, GstVideoFrame * frame)
{
  gint x, y, w, h;
  guint8 *data;
  gint sindex;

  w = GST_VIDEO_FRAME_WIDTH (frame);
  h = GST_VIDEO_FRAME_HEIGHT (frame);
  data = GST_VIDEO_FRAME_PLANE_DATA (frame, 0);

  sindex = 0;

  for (y = 0; y < h; y++) {
    for (x = 0; x < w; x++, sindex++) {
      image->comps[0].data[sindex] = data[x];
    }
    data += GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0);
  }
}

static void
fill_image_planar16_1 (opj_image_t * image, GstVideoFrame * frame)
{
  gint x, y, w, h;
  guint16 *data;
  gint sindex;

  w = GST_VIDEO_FRAME_WIDTH (frame);
  h = GST_VIDEO_FRAME_HEIGHT (frame);
  data = GST_VIDEO_FRAME_PLANE_DATA (frame, 0);

  sindex = 0;

  for (y = 0; y < h; y++) {
    for (x = 0; x < w; x++, sindex++) {
      image->comps[0].data[sindex] = data[x];
    }
    data += GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0) / 2;
  }
}

static gboolean
gst_openjpeg_enc_set_format (GstVideoEncoder * encoder,
    GstVideoCodecState * state)
{
  GstOpenJPEGEnc *self = GST_OPENJPEG_ENC (encoder);
  GstCaps *allowed_caps, *caps;
  GstStructure *s;
  const gchar *colorspace;

  GST_DEBUG_OBJECT (self, "Setting format: %" GST_PTR_FORMAT, state->caps);

  if (self->input_state)
    gst_video_codec_state_unref (self->input_state);
  self->input_state = gst_video_codec_state_ref (state);

  allowed_caps = gst_pad_get_allowed_caps (GST_VIDEO_ENCODER_SRC_PAD (encoder));
  allowed_caps = gst_caps_truncate (allowed_caps);
  s = gst_caps_get_structure (allowed_caps, 0);
  if (gst_structure_has_name (s, "image/jp2")) {
    self->codec_format = CODEC_JP2;
    self->is_jp2c = FALSE;
  } else if (gst_structure_has_name (s, "image/x-j2c")) {
    self->codec_format = CODEC_J2K;
    self->is_jp2c = TRUE;
  } else if (gst_structure_has_name (s, "image/x-jpc")) {
    self->codec_format = CODEC_J2K;
    self->is_jp2c = FALSE;
  } else {
    g_return_val_if_reached (FALSE);
  }

  switch (state->info.finfo->format) {
    case GST_VIDEO_FORMAT_ARGB64:
      self->fill_image = fill_image_packed16_4;
      break;
    case GST_VIDEO_FORMAT_ARGB:
      self->fill_image = fill_image_packed8_4;
      break;
    case GST_VIDEO_FORMAT_xRGB:
      self->fill_image = fill_image_packed8_3;
      break;
    case GST_VIDEO_FORMAT_AYUV64:
      self->fill_image = fill_image_packed16_4;
      break;
    case GST_VIDEO_FORMAT_Y444_10LE:
    case GST_VIDEO_FORMAT_Y444_10BE:
    case GST_VIDEO_FORMAT_I422_10LE:
    case GST_VIDEO_FORMAT_I422_10BE:
    case GST_VIDEO_FORMAT_I420_10LE:
    case GST_VIDEO_FORMAT_I420_10BE:
      self->fill_image = fill_image_planar16_3;
      break;
    case GST_VIDEO_FORMAT_AYUV:
      self->fill_image = fill_image_packed8_3;
      break;
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_Y41B:
    case GST_VIDEO_FORMAT_YUV9:
      self->fill_image = fill_image_planar8_3;
      break;
    case GST_VIDEO_FORMAT_GRAY8:
      self->fill_image = fill_image_planar8_1;
      break;
    case GST_VIDEO_FORMAT_GRAY16_LE:
    case GST_VIDEO_FORMAT_GRAY16_BE:
      self->fill_image = fill_image_planar16_1;
      break;
    default:
      g_assert_not_reached ();
  }

  if ((state->info.finfo->flags & GST_VIDEO_FORMAT_FLAG_YUV))
    colorspace = "sYUV";
  else if ((state->info.finfo->flags & GST_VIDEO_FORMAT_FLAG_RGB))
    colorspace = "sRGB";
  else if ((state->info.finfo->flags & GST_VIDEO_FORMAT_FLAG_GRAY))
    colorspace = "GRAY";
  else
    g_return_val_if_reached (FALSE);

  caps = gst_caps_new_simple (gst_structure_get_name (s),
      "colorspace", G_TYPE_STRING, colorspace, NULL);
  gst_caps_unref (allowed_caps);

  if (self->output_state)
    gst_video_codec_state_unref (self->output_state);
  self->output_state =
      gst_video_encoder_set_output_state (encoder, caps, state);

  gst_video_encoder_negotiate (GST_VIDEO_ENCODER (encoder));

  return TRUE;
}

static gboolean
gst_openjpeg_enc_reset (GstVideoEncoder * encoder, gboolean hard)
{
  GstOpenJPEGEnc *self = GST_OPENJPEG_ENC (encoder);

  GST_DEBUG_OBJECT (self, "Resetting");

  if (self->output_state) {
    gst_video_codec_state_unref (self->output_state);
    self->output_state = NULL;
  }

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
    colorspace = CLRSPC_SYCC;
  else if ((frame->info.finfo->flags & GST_VIDEO_FORMAT_FLAG_RGB))
    colorspace = CLRSPC_SRGB;
  else if ((frame->info.finfo->flags & GST_VIDEO_FORMAT_FLAG_GRAY))
    colorspace = CLRSPC_GRAY;
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

static GstFlowReturn
gst_openjpeg_enc_handle_frame (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame)
{
  GstOpenJPEGEnc *self = GST_OPENJPEG_ENC (encoder);
  GstFlowReturn ret = GST_FLOW_OK;
  GstMapInfo map;
  opj_cinfo_t *enc;
  opj_cio_t *io;
  opj_image_t *image;
  GstVideoFrame vframe;
  gint length;

  GST_DEBUG_OBJECT (self, "Handling frame");

  enc = opj_create_compress (self->codec_format);
  if (!enc)
    goto initialization_error;

  opj_set_event_mgr ((opj_common_ptr) enc, NULL, NULL);

  if (!gst_video_frame_map (&vframe, &self->input_state->info,
          frame->input_buffer, GST_MAP_READ))
    goto map_read_error;

  image = gst_openjpeg_enc_fill_image (self, &vframe);
  if (!image)
    goto fill_image_error;
  gst_video_frame_unmap (&vframe);

  opj_setup_encoder (enc, &self->params, image);

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
    opj_destroy_compress (enc);
    gst_video_codec_frame_unref (frame);

    GST_ELEMENT_ERROR (self, CORE, FAILED,
        ("Failed to map input buffer"), (NULL));
    return GST_FLOW_ERROR;
  }
fill_image_error:
  {
    opj_destroy_compress (enc);
    gst_video_frame_unmap (&vframe);
    gst_video_codec_frame_unref (frame);

    GST_ELEMENT_ERROR (self, LIBRARY, INIT,
        ("Failed to fill OpenJPEG image"), (NULL));
    return GST_FLOW_ERROR;
  }
open_error:
  {
    opj_image_destroy (image);
    opj_destroy_compress (enc);
    gst_video_codec_frame_unref (frame);

    GST_ELEMENT_ERROR (self, LIBRARY, INIT,
        ("Failed to open OpenJPEG data"), (NULL));
    return GST_FLOW_ERROR;
  }
encode_error:
  {
    opj_cio_close (io);
    opj_image_destroy (image);
    opj_destroy_compress (enc);
    gst_video_codec_frame_unref (frame);

    GST_ELEMENT_ERROR (self, STREAM, ENCODE,
        ("Failed to encode OpenJPEG stream"), (NULL));
    return GST_FLOW_ERROR;
  }
allocate_error:
  {
    opj_cio_close (io);
    opj_destroy_compress (enc);
    gst_video_codec_frame_unref (frame);

    GST_ELEMENT_ERROR (self, CORE, FAILED,
        ("Failed to allocate output buffer"), (NULL));
    return ret;
  }
}

static gboolean
gst_openjpeg_enc_propose_allocation (GstVideoEncoder * encoder,
    GstQuery * query)
{
  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

  return GST_VIDEO_ENCODER_CLASS (parent_class)->propose_allocation (encoder,
      query);
}
