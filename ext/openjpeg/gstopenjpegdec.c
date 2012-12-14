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

#include "gstopenjpegdec.h"

GST_DEBUG_CATEGORY_STATIC (gst_openjpeg_dec_debug);
#define GST_CAT_DEFAULT gst_openjpeg_dec_debug

enum
{
  PROP_0,
};

static void gst_openjpeg_dec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_openjpeg_dec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_openjpeg_dec_start (GstVideoDecoder * decoder);
static gboolean gst_openjpeg_dec_stop (GstVideoDecoder * decoder);
static gboolean gst_openjpeg_dec_set_format (GstVideoDecoder * decoder,
    GstVideoCodecState * state);
static gboolean gst_openjpeg_dec_reset (GstVideoDecoder * decoder,
    gboolean hard);
static GstFlowReturn gst_openjpeg_dec_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame);
static gboolean gst_openjpeg_dec_decide_allocation (GstVideoDecoder * decoder,
    GstQuery * query);

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
#define GRAY16 "GRAY16_LE"
#define YUV10 "Y444_10LE, I422_10LE, I420_10LE"
#else
#define GRAY16 "GRAY16_BE"
#define YUV10 "Y444_10BE, I422_10BE, I420_10BE"
#endif

static GstStaticPadTemplate gst_openjpeg_dec_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("image/x-j2c; image/x-jpc; image/jp2")
    );

static GstStaticPadTemplate gst_openjpeg_dec_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{ ARGB64, ARGB, xRGB, "
            "AYUV64, " YUV10 ", "
            "AYUV, Y444, Y42B, I420, Y41B, YUV9, " "GRAY8, " GRAY16 " }"))
    );

#define parent_class gst_openjpeg_dec_parent_class
G_DEFINE_TYPE (GstOpenJPEGDec, gst_openjpeg_dec, GST_TYPE_VIDEO_DECODER);

static void
gst_openjpeg_dec_class_init (GstOpenJPEGDecClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstVideoDecoderClass *video_decoder_class;

  gobject_class = (GObjectClass *) klass;
  element_class = (GstElementClass *) klass;
  video_decoder_class = (GstVideoDecoderClass *) klass;

  gobject_class->set_property = gst_openjpeg_dec_set_property;
  gobject_class->get_property = gst_openjpeg_dec_get_property;

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_openjpeg_dec_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_openjpeg_dec_sink_template));

  gst_element_class_set_static_metadata (element_class,
      "OpenJPEG JPEG2000 decoder",
      "Codec/Decoder/Video",
      "Decode JPEG2000 streams",
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");

  video_decoder_class->start = GST_DEBUG_FUNCPTR (gst_openjpeg_dec_start);
  video_decoder_class->stop = GST_DEBUG_FUNCPTR (gst_openjpeg_dec_stop);
  video_decoder_class->reset = GST_DEBUG_FUNCPTR (gst_openjpeg_dec_reset);
  video_decoder_class->set_format =
      GST_DEBUG_FUNCPTR (gst_openjpeg_dec_set_format);
  video_decoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_openjpeg_dec_handle_frame);
  video_decoder_class->decide_allocation = gst_openjpeg_dec_decide_allocation;

  GST_DEBUG_CATEGORY_INIT (gst_openjpeg_dec_debug, "openjpegdec", 0,
      "VP8 Decoder");
}

static void
gst_openjpeg_dec_init (GstOpenJPEGDec * self)
{
  GstVideoDecoder *decoder = (GstVideoDecoder *) self;

  gst_video_decoder_set_packetized (decoder, TRUE);
  opj_set_default_decoder_parameters (&self->params);
}

static void
gst_openjpeg_dec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  /* GstOpenJPEGDec *self = GST_OPENJPEG_DEC (object); */

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_openjpeg_dec_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  /* GstOpenJPEGDec *self = GST_OPENJPEG_DEC (object); */

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_openjpeg_dec_start (GstVideoDecoder * decoder)
{
  GstOpenJPEGDec *self = GST_OPENJPEG_DEC (decoder);

  GST_DEBUG_OBJECT (self, "Starting");

  return TRUE;
}

static gboolean
gst_openjpeg_dec_stop (GstVideoDecoder * video_decoder)
{
  GstOpenJPEGDec *self = GST_OPENJPEG_DEC (video_decoder);

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

static gboolean
gst_openjpeg_dec_set_format (GstVideoDecoder * decoder,
    GstVideoCodecState * state)
{
  GstOpenJPEGDec *self = GST_OPENJPEG_DEC (decoder);
  GstStructure *s;

  GST_DEBUG_OBJECT (self, "Setting format: %" GST_PTR_FORMAT, state->caps);

  s = gst_caps_get_structure (state->caps, 0);

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

  if (self->input_state)
    gst_video_codec_state_unref (self->input_state);
  self->input_state = gst_video_codec_state_ref (state);

  return TRUE;
}

static gboolean
gst_openjpeg_dec_reset (GstVideoDecoder * decoder, gboolean hard)
{
  GstOpenJPEGDec *self = GST_OPENJPEG_DEC (decoder);

  GST_DEBUG_OBJECT (self, "Resetting");

  if (self->output_state) {
    gst_video_codec_state_unref (self->output_state);
    self->output_state = NULL;
  }

  return TRUE;
}

static void
fill_frame_packed8_4 (GstVideoFrame * frame, opj_image_t * image)
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
      data[x * 4 + 0] = image->comps[3].data[sindex];
      data[x * 4 + 1] = image->comps[0].data[sindex];
      data[x * 4 + 2] = image->comps[1].data[sindex];
      data[x * 4 + 3] = image->comps[2].data[sindex];
    }
    data += GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0);
  }
}

static void
fill_frame_packed16_4 (GstVideoFrame * frame, opj_image_t * image)
{
  gint x, y, w, h;
  guint16 *data;
  gint sindex;
  gint shift;

  w = GST_VIDEO_FRAME_WIDTH (frame);
  h = GST_VIDEO_FRAME_HEIGHT (frame);
  data = GST_VIDEO_FRAME_PLANE_DATA (frame, 0);

  shift = 16 - image->comps[0].prec;

  sindex = 0;

  for (y = 0; y < h; y++) {
    for (x = 0; x < w; x++, sindex++) {
      data[x * 4 + 0] = image->comps[3].data[sindex] << shift;
      data[x * 4 + 1] = image->comps[0].data[sindex] << shift;
      data[x * 4 + 2] = image->comps[1].data[sindex] << shift;
      data[x * 4 + 3] = image->comps[2].data[sindex] << shift;
    }
    data += GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0) / 2;
  }
}

static void
fill_frame_packed8_3 (GstVideoFrame * frame, opj_image_t * image)
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
      data[x * 4 + 1] = image->comps[0].data[sindex];
      data[x * 4 + 2] = image->comps[1].data[sindex];
      data[x * 4 + 3] = image->comps[2].data[sindex];
    }
    data += GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0);
  }
}

static void
fill_frame_packed16_3 (GstVideoFrame * frame, opj_image_t * image)
{
  gint x, y, w, h;
  guint16 *data;
  gint sindex;
  gint shift;

  w = GST_VIDEO_FRAME_WIDTH (frame);
  h = GST_VIDEO_FRAME_HEIGHT (frame);
  data = GST_VIDEO_FRAME_PLANE_DATA (frame, 0);

  shift = 16 - image->comps[0].prec;

  sindex = 0;

  for (y = 0; y < h; y++) {
    for (x = 0; x < w; x++, sindex++) {
      data[x * 4 + 0] = 0xffff;
      data[x * 4 + 1] = image->comps[0].data[sindex] << shift;
      data[x * 4 + 2] = image->comps[1].data[sindex] << shift;
      data[x * 4 + 3] = image->comps[2].data[sindex] << shift;
    }
    data += GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0) / 2;
  }
}

static void
fill_frame_planar8_1 (GstVideoFrame * frame, opj_image_t * image)
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
      data[x] = image->comps[0].data[sindex];
    }
    data += GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0);
  }
}

static void
fill_frame_planar16_1 (GstVideoFrame * frame, opj_image_t * image)
{
  gint x, y, w, h;
  guint16 *data;
  gint sindex;
  gint shift;

  w = GST_VIDEO_FRAME_WIDTH (frame);
  h = GST_VIDEO_FRAME_HEIGHT (frame);
  data = GST_VIDEO_FRAME_PLANE_DATA (frame, 0);

  shift = 16 - image->comps[0].prec;

  sindex = 0;

  for (y = 0; y < h; y++) {
    for (x = 0; x < w; x++, sindex++) {
      data[x] = image->comps[0].data[sindex] << shift;
    }
    data += GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0) / 2;
  }
}

static void
fill_frame_planar8_3 (GstVideoFrame * frame, opj_image_t * image)
{
  gint c, x, y, w, h;
  guint8 *data;
  gint sindex;

  for (c = 0; c < image->numcomps; c++) {
    w = GST_VIDEO_FRAME_COMP_WIDTH (frame, c);
    h = GST_VIDEO_FRAME_COMP_HEIGHT (frame, c);
    data = GST_VIDEO_FRAME_COMP_DATA (frame, c);

    sindex = 0;
    for (y = 0; y < h; y++) {
      for (x = 0; x < w; x++, sindex++) {
        data[x] = image->comps[c].data[sindex];
      }
      data += GST_VIDEO_FRAME_PLANE_STRIDE (frame, c);
    }
  }
}

static void
fill_frame_planar8_3_generic (GstVideoFrame * frame, opj_image_t * image)
{
  gint x, y, w, h;
  guint8 *data;

  w = GST_VIDEO_FRAME_WIDTH (frame);
  h = GST_VIDEO_FRAME_HEIGHT (frame);
  data = GST_VIDEO_FRAME_PLANE_DATA (frame, 0);

  for (y = 0; y < h; y++) {
    for (x = 0; x < w; x++) {
      data[x * 4 + 0] = 0xff;
      data[x * 4 + 1] =
          image->comps[0].data[(y / image->comps[0].dy) * (w /
              image->comps[0].dx) + x / image->comps[0].dx];
      data[x * 4 + 2] =
          image->comps[1].data[(y / image->comps[1].dy) * (w /
              image->comps[1].dx) + x / image->comps[1].dx];
      data[x * 4 + 3] =
          image->comps[2].data[(y / image->comps[2].dy) * (w /
              image->comps[2].dx) + x / image->comps[2].dx];
    }
    data += GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0);
  }
}

static void
fill_frame_planar8_4_generic (GstVideoFrame * frame, opj_image_t * image)
{
  gint x, y, w, h;
  guint8 *data;

  w = GST_VIDEO_FRAME_WIDTH (frame);
  h = GST_VIDEO_FRAME_HEIGHT (frame);
  data = GST_VIDEO_FRAME_PLANE_DATA (frame, 0);

  for (y = 0; y < h; y++) {
    for (x = 0; x < w; x++) {
      data[x * 4 + 0] =
          image->comps[3].data[(y / image->comps[3].dy) * (w /
              image->comps[3].dx) + x / image->comps[3].dx];
      data[x * 4 + 1] =
          image->comps[0].data[(y / image->comps[0].dy) * (w /
              image->comps[0].dx) + x / image->comps[0].dx];
      data[x * 4 + 2] =
          image->comps[1].data[(y / image->comps[1].dy) * (w /
              image->comps[1].dx) + x / image->comps[1].dx];
      data[x * 4 + 3] =
          image->comps[2].data[(y / image->comps[2].dy) * (w /
              image->comps[2].dx) + x / image->comps[2].dx];
    }
    data += GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0);
  }
}

static void
fill_frame_planar16_3_generic (GstVideoFrame * frame, opj_image_t * image)
{
  gint x, y, w, h;
  guint16 *data;
  gint shift;

  w = GST_VIDEO_FRAME_WIDTH (frame);
  h = GST_VIDEO_FRAME_HEIGHT (frame);
  data = GST_VIDEO_FRAME_PLANE_DATA (frame, 0);

  shift = 16 - image->comps[0].prec;

  for (y = 0; y < h; y++) {
    for (x = 0; x < w; x++) {
      data[x * 4 + 0] = 0xffff;
      data[x * 4 + 1] =
          image->comps[0].data[(y / image->comps[0].dy) * (w /
              image->comps[0].dx) + x / image->comps[0].dx] << shift;
      data[x * 4 + 2] =
          image->comps[1].data[(y / image->comps[1].dy) * (w /
              image->comps[1].dx) + x / image->comps[1].dx] << shift;
      data[x * 4 + 3] =
          image->comps[2].data[(y / image->comps[2].dy) * (w /
              image->comps[2].dx) + x / image->comps[2].dx] << shift;
    }
    data += GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0) / 2;
  }
}

static void
fill_frame_planar16_4_generic (GstVideoFrame * frame, opj_image_t * image)
{
  gint x, y, w, h;
  guint16 *data;
  gint shift;

  w = GST_VIDEO_FRAME_WIDTH (frame);
  h = GST_VIDEO_FRAME_HEIGHT (frame);
  data = GST_VIDEO_FRAME_PLANE_DATA (frame, 0);

  shift = 16 - image->comps[0].prec;

  for (y = 0; y < h; y++) {
    for (x = 0; x < w; x++) {
      data[x * 4 + 0] =
          image->comps[3].data[(y / image->comps[3].dy) * (w /
              image->comps[3].dx) + x / image->comps[3].dx] << shift;
      data[x * 4 + 1] =
          image->comps[0].data[(y / image->comps[0].dy) * (w /
              image->comps[0].dx) + x / image->comps[0].dx] << shift;
      data[x * 4 + 2] =
          image->comps[1].data[(y / image->comps[1].dy) * (w /
              image->comps[1].dx) + x / image->comps[1].dx] << shift;
      data[x * 4 + 3] =
          image->comps[2].data[(y / image->comps[2].dy) * (w /
              image->comps[2].dx) + x / image->comps[2].dx] << shift;
    }
    data += GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0) / 2;
  }
}

static void
fill_frame_planar16_3 (GstVideoFrame * frame, opj_image_t * image)
{
  gint c, x, y, w, h;
  guint16 *data;
  gint sindex;

  for (c = 0; c < image->numcomps; c++) {
    w = GST_VIDEO_FRAME_COMP_WIDTH (frame, c);
    h = GST_VIDEO_FRAME_COMP_HEIGHT (frame, c);
    data = (guint16 *) GST_VIDEO_FRAME_COMP_DATA (frame, c);

    sindex = 0;
    for (y = 0; y < h; y++) {
      for (x = 0; x < w; x++, sindex++) {
        data[x] = image->comps[c].data[sindex];
      }
      data += GST_VIDEO_FRAME_PLANE_STRIDE (frame, c) / 2;
    }
  }
}

static GstFlowReturn
gst_openjpeg_dec_negotiate (GstOpenJPEGDec * self, opj_image_t * image)
{
  GstVideoFormat format;
  gint width, height;

  switch (image->color_space) {
    case CLRSPC_SRGB:
      if (image->numcomps == 4) {
        if (image->comps[3].dx != 1 && image->comps[3].dy != 1) {
          GST_ERROR_OBJECT (self, "Sub-sampling for RGB not supported");
          return GST_FLOW_NOT_NEGOTIATED;
        }

        if (image->comps[3].prec == 8) {
          self->fill_frame = fill_frame_packed8_4;
          format = GST_VIDEO_FORMAT_ARGB;
        } else if (image->comps[3].prec <= 16) {
          self->fill_frame = fill_frame_packed16_4;
          format = GST_VIDEO_FORMAT_ARGB64;
        } else {
          GST_ERROR_OBJECT (self, "Unsupported depth %d", image->comps[3].prec);
          return GST_FLOW_NOT_NEGOTIATED;
        }
      } else if (image->numcomps == 3) {
        if (image->comps[2].dx != 1 && image->comps[2].dy != 1) {
          GST_ERROR_OBJECT (self, "Sub-sampling for RGB not supported");
          return GST_FLOW_NOT_NEGOTIATED;
        }

        if (image->comps[2].prec == 8) {
          self->fill_frame = fill_frame_packed8_3;
          format = GST_VIDEO_FORMAT_ARGB;
        } else if (image->comps[2].prec <= 16) {
          self->fill_frame = fill_frame_packed16_3;
          format = GST_VIDEO_FORMAT_ARGB64;
        } else {
          GST_ERROR_OBJECT (self, "Unsupported depth %d", image->comps[3].prec);
          return GST_FLOW_NOT_NEGOTIATED;
        }
      } else {
        GST_ERROR_OBJECT (self, "Unsupported number of RGB components: %d",
            image->numcomps);
        return GST_FLOW_NOT_NEGOTIATED;
      }
      break;
    case CLRSPC_GRAY:
      if (image->numcomps == 1) {
        if (image->comps[0].dx != 1 && image->comps[0].dy != 1) {
          GST_ERROR_OBJECT (self, "Sub-sampling for GRAY not supported");
          return GST_FLOW_NOT_NEGOTIATED;
        }

        if (image->comps[0].prec == 8) {
          self->fill_frame = fill_frame_planar8_1;
          format = GST_VIDEO_FORMAT_GRAY8;
        } else if (image->comps[0].prec <= 16) {
          self->fill_frame = fill_frame_planar16_1;
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
          format = GST_VIDEO_FORMAT_GRAY16_LE;
#else
          format = GST_VIDEO_FORMAT_GRAY16_BE;
#endif
        } else {
          GST_ERROR_OBJECT (self, "Unsupported depth %d", image->comps[0].prec);
          return GST_FLOW_NOT_NEGOTIATED;
        }
      } else {
        GST_ERROR_OBJECT (self, "Unsupported number of GRAY components: %d",
            image->numcomps);
        return GST_FLOW_NOT_NEGOTIATED;
      }
      break;
    case CLRSPC_SYCC:
      if (image->comps[0].dx != 1 || image->comps[0].dy != 1) {
        GST_ERROR_OBJECT (self, "Sub-sampling of luma plane not supported");
        return GST_FLOW_NOT_NEGOTIATED;
      }

      if (image->comps[1].dx != image->comps[2].dx ||
          image->comps[1].dy != image->comps[2].dy) {
        GST_ERROR_OBJECT (self,
            "Different sub-sampling of chroma planes not supported");
        return GST_FLOW_ERROR;
      }

      if (image->numcomps == 4) {
        if (image->comps[3].prec == 8) {
          self->fill_frame = fill_frame_planar8_4_generic;
          format = GST_VIDEO_FORMAT_AYUV;
        } else if (image->comps[3].prec <= 16) {
          self->fill_frame = fill_frame_planar16_4_generic;
          format = GST_VIDEO_FORMAT_AYUV64;
        } else {
          GST_ERROR_OBJECT (self, "Unsupported depth %d", image->comps[0].prec);
          return GST_FLOW_NOT_NEGOTIATED;
        }
      } else if (image->numcomps == 3) {
        if (image->comps[2].prec == 8) {
          if (image->comps[1].dx == 1 && image->comps[1].dy == 1) {
            self->fill_frame = fill_frame_planar8_3;
            format = GST_VIDEO_FORMAT_Y444;
          } else if (image->comps[1].dx == 2 && image->comps[1].dy == 1) {
            self->fill_frame = fill_frame_planar8_3;
            format = GST_VIDEO_FORMAT_Y42B;
          } else if (image->comps[1].dx == 2 && image->comps[1].dy == 2) {
            self->fill_frame = fill_frame_planar8_3;
            format = GST_VIDEO_FORMAT_I420;
          } else if (image->comps[1].dx == 4 && image->comps[1].dy == 1) {
            self->fill_frame = fill_frame_planar8_3;
            format = GST_VIDEO_FORMAT_Y41B;
          } else if (image->comps[1].dx == 4 && image->comps[1].dy == 4) {
            self->fill_frame = fill_frame_planar8_3;
            format = GST_VIDEO_FORMAT_YUV9;
          } else {
            self->fill_frame = fill_frame_planar8_3_generic;
            format = GST_VIDEO_FORMAT_AYUV;
          }
        } else if (image->comps[2].prec <= 16) {
          if (image->comps[1].dx == 1 && image->comps[1].dy == 1) {
            self->fill_frame = fill_frame_planar16_3;
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
            format = GST_VIDEO_FORMAT_Y444_10LE;
#else
            format = GST_VIDEO_FORMAT_Y444_10BE;
#endif
          } else if (image->comps[1].dx == 2 && image->comps[1].dy == 1) {
            self->fill_frame = fill_frame_planar16_3;
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
            format = GST_VIDEO_FORMAT_I422_10LE;
#else
            format = GST_VIDEO_FORMAT_I422_10BE;
#endif
          } else if (image->comps[1].dx == 2 && image->comps[1].dy == 2) {
            self->fill_frame = fill_frame_planar16_3;
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
            format = GST_VIDEO_FORMAT_I420_10LE;
#else
            format = GST_VIDEO_FORMAT_I420_10BE;
#endif
          } else {
            self->fill_frame = fill_frame_planar16_3_generic;
            format = GST_VIDEO_FORMAT_AYUV64;
          }
        } else {
          GST_ERROR_OBJECT (self, "Unsupported depth %d", image->comps[0].prec);
          return GST_FLOW_NOT_NEGOTIATED;
        }
      } else {
        GST_ERROR_OBJECT (self, "Unsupported number of YUV components: %d",
            image->numcomps);
        return GST_FLOW_NOT_NEGOTIATED;
      }
      break;
    default:
      GST_ERROR_OBJECT (self, "Unsupported colorspace %d", image->color_space);
      return GST_FLOW_NOT_NEGOTIATED;
  }

  width = image->x1 - image->x0;
  height = image->y1 - image->y0;

  if (!self->output_state ||
      self->output_state->info.finfo->format != format ||
      self->output_state->info.width != width ||
      self->output_state->info.height != height) {
    if (self->output_state)
      gst_video_codec_state_unref (self->output_state);
    self->output_state =
        gst_video_decoder_set_output_state (GST_VIDEO_DECODER (self), format,
        width, height, self->input_state);

    if (!gst_video_decoder_negotiate (GST_VIDEO_DECODER (self)))
      return GST_FLOW_NOT_NEGOTIATED;
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_openjpeg_dec_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame)
{
  GstOpenJPEGDec *self = GST_OPENJPEG_DEC (decoder);
  GstFlowReturn ret = GST_FLOW_OK;
  gint64 deadline;
  GstMapInfo map;
  opj_dinfo_t *dec;
  opj_cio_t *io;
  opj_image_t *image;
  GstVideoFrame vframe;

  GST_DEBUG_OBJECT (self, "Handling frame");

  deadline = gst_video_decoder_get_max_decode_time (decoder, frame);
  if (deadline < 0) {
    GST_LOG_OBJECT (self, "Dropping too late frame: deadline %" G_GINT64_FORMAT,
        deadline);
    ret = gst_video_decoder_drop_frame (decoder, frame);
    return ret;
  }

  dec = opj_create_decompress (self->codec_format);
  if (!dec)
    goto initialization_error;

  opj_set_event_mgr ((opj_common_ptr) dec, NULL, NULL);

  opj_setup_decoder (dec, &self->params);

  if (!gst_buffer_map (frame->input_buffer, &map, GST_MAP_READ))
    goto map_read_error;

  io = opj_cio_open ((opj_common_ptr) dec, map.data + (self->is_jp2c ? 8 : 0),
      map.size - (self->is_jp2c ? 8 : 0));
  if (!io)
    goto open_error;

  image = opj_decode (dec, io);
  if (!image)
    goto decode_error;

  gst_buffer_unmap (frame->input_buffer, &map);

  ret = gst_openjpeg_dec_negotiate (self, image);
  if (ret != GST_FLOW_OK)
    goto negotiate_error;

  ret = gst_video_decoder_allocate_output_frame (decoder, frame);
  if (ret != GST_FLOW_OK)
    goto allocate_error;

  if (!gst_video_frame_map (&vframe, &self->output_state->info,
          frame->output_buffer, GST_MAP_WRITE))
    goto map_write_error;

  self->fill_frame (&vframe, image);

  gst_video_frame_unmap (&vframe);

  opj_image_destroy (image);
  opj_cio_close (io);
  opj_destroy_decompress (dec);

  ret = gst_video_decoder_finish_frame (decoder, frame);

  return ret;

initialization_error:
  {
    gst_video_codec_frame_unref (frame);
    GST_ELEMENT_ERROR (self, LIBRARY, INIT,
        ("Failed to initialize OpenJPEG decoder"), (NULL));
    return GST_FLOW_ERROR;
  }
map_read_error:
  {
    opj_destroy_decompress (dec);
    gst_video_codec_frame_unref (frame);

    GST_ELEMENT_ERROR (self, CORE, FAILED,
        ("Failed to map input buffer"), (NULL));
    return GST_FLOW_ERROR;
  }
open_error:
  {
    opj_destroy_decompress (dec);
    gst_buffer_unmap (frame->input_buffer, &map);
    gst_video_codec_frame_unref (frame);

    GST_ELEMENT_ERROR (self, LIBRARY, INIT,
        ("Failed to open OpenJPEG stream"), (NULL));
    return GST_FLOW_ERROR;
  }
decode_error:
  {
    opj_cio_close (io);
    opj_destroy_decompress (dec);
    gst_buffer_unmap (frame->input_buffer, &map);
    gst_video_codec_frame_unref (frame);

    GST_VIDEO_DECODER_ERROR (self, 1, STREAM, DECODE,
        ("Failed to decode OpenJPEG stream"), (NULL), ret);
    return ret;
  }
negotiate_error:
  {
    opj_image_destroy (image);
    opj_cio_close (io);
    opj_destroy_decompress (dec);
    gst_video_codec_frame_unref (frame);

    GST_ELEMENT_ERROR (self, CORE, NEGOTIATION,
        ("Failed to negotiate"), (NULL));
    return ret;
  }
allocate_error:
  {
    opj_image_destroy (image);
    opj_cio_close (io);
    opj_destroy_decompress (dec);
    gst_video_codec_frame_unref (frame);

    GST_ELEMENT_ERROR (self, CORE, FAILED,
        ("Failed to allocate output buffer"), (NULL));
    return ret;
  }
map_write_error:
  {
    opj_image_destroy (image);
    opj_cio_close (io);
    opj_destroy_decompress (dec);
    gst_video_codec_frame_unref (frame);

    GST_ELEMENT_ERROR (self, CORE, FAILED,
        ("Failed to map output buffer"), (NULL));
    return GST_FLOW_ERROR;
  }
}

static gboolean
gst_openjpeg_dec_decide_allocation (GstVideoDecoder * decoder, GstQuery * query)
{
  GstBufferPool *pool;
  GstStructure *config;

  if (!GST_VIDEO_DECODER_CLASS (parent_class)->decide_allocation (decoder,
          query))
    return FALSE;

  g_assert (gst_query_get_n_allocation_pools (query) > 0);
  gst_query_parse_nth_allocation_pool (query, 0, &pool, NULL, NULL, NULL);
  g_assert (pool != NULL);

  config = gst_buffer_pool_get_config (pool);
  if (gst_query_find_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL)) {
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);
  }
  gst_buffer_pool_set_config (pool, config);
  gst_object_unref (pool);

  return TRUE;
}
