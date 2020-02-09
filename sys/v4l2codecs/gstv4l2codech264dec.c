/* GStreamer
 * Copyright (C) 2020 Nicolas Dufresne <nicolas.dufresne@collabora.com>
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstv4l2codech264dec.h"

GST_DEBUG_CATEGORY_STATIC (v4l2_h264dec_debug);
#define GST_CAT_DEFAULT v4l2_h264dec_debug

enum
{
  PROP_0,
  PROP_LAST = PROP_0
};

static GstStaticPadTemplate sink_template =
GST_STATIC_PAD_TEMPLATE (GST_VIDEO_DECODER_SINK_NAME,
    GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264, "
        "stream-format=(string) byte-stream, alignment=(string) au")
    );

static GstStaticPadTemplate src_template =
GST_STATIC_PAD_TEMPLATE (GST_VIDEO_DECODER_SRC_NAME,
    GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{ NV12, YUY2 }")));

struct _GstV4l2CodecH264Dec
{
  GstH264Decoder parent;
  GstV4l2Decoder *decoder;
  GstVideoCodecState *output_state;
  GstVideoFormat out_format;
  gint width;
  gint height;
};

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (GstV4l2CodecH264Dec,
    gst_v4l2_codec_h264_dec, GST_TYPE_H264_DECODER,
    GST_DEBUG_CATEGORY_INIT (v4l2_h264dec_debug, "v4l2codecs-h264dec", 0,
        "V4L2 stateless h264 decoder"));
#define parent_class gst_v4l2_codec_h264_dec_parent_class

static gboolean
gst_v4l2_codec_h264_dec_open (GstVideoDecoder * decoder)
{
  return TRUE;
}

static gboolean
gst_v4l2_codec_h264_dec_close (GstVideoDecoder * decoder)
{
  return TRUE;
}

static gboolean
gst_v4l2_codec_h264_dec_stop (GstVideoDecoder * decoder)
{
  GstV4l2CodecH264Dec *self = GST_V4L2_CODEC_H264_DEC (decoder);

  if (self->output_state)
    gst_video_codec_state_unref (self->output_state);
  self->output_state = NULL;

  return GST_VIDEO_DECODER_CLASS (parent_class)->stop (decoder);
}

static gboolean
gst_v4l2_codec_h264_dec_negotiate (GstVideoDecoder * decoder)
{
  GstV4l2CodecH264Dec *self = GST_V4L2_CODEC_H264_DEC (decoder);
  GstH264Decoder *h264dec = GST_H264_DECODER (decoder);

  GST_DEBUG_OBJECT (self, "negotiate");

  if (self->output_state)
    gst_video_codec_state_unref (self->output_state);

  self->output_state =
      gst_video_decoder_set_output_state (GST_VIDEO_DECODER (self),
      self->out_format, self->width, self->height, h264dec->input_state);

  self->output_state->caps = gst_video_info_to_caps (&self->output_state->info);

  return GST_VIDEO_DECODER_CLASS (parent_class)->negotiate (decoder);
}

static gboolean
gst_v4l2_codec_h264_dec_decide_allocation (GstVideoDecoder * decoder,
    GstQuery * query)
{
  return GST_VIDEO_DECODER_CLASS (parent_class)->decide_allocation
      (decoder, query);
}

static gboolean
gst_v4l2_codec_h264_dec_new_sequence (GstH264Decoder * decoder,
    const GstH264SPS * sps, gint max_dpb_size)
{
  /* TODO check if we need to setup a new format */
  return TRUE;
}

static gboolean
gst_v4l2_codec_h264_dec_start_picture (GstH264Decoder * decoder,
    GstH264Picture * picture, GstH264Slice * slice, GstH264Dpb * dpb)
{
  return FALSE;
}

static GstFlowReturn
gst_v4l2_codec_h264_dec_output_picture (GstH264Decoder * decoder,
    GstH264Picture * picture)
{
  return GST_FLOW_ERROR;
}

static gboolean
gst_v4l2_codec_h264_dec_end_picture (GstH264Decoder * decoder,
    GstH264Picture * picture)
{
  return TRUE;
}

static gboolean
gst_v4l2_codec_h264_dec_decode_slice (GstH264Decoder * decoder,
    GstH264Picture * picture, GstH264Slice * slice)
{
  return TRUE;
}

static void
gst_v4l2_codec_h264_dec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstV4l2CodecH264Dec *self = GST_V4L2_CODEC_H264_DEC (object);
  GObject *dec = G_OBJECT (self->decoder);

  switch (prop_id) {
    default:
      gst_v4l2_decoder_set_property (dec, prop_id - PROP_LAST, value, pspec);
      break;
  }
}

static void
gst_v4l2_codec_h264_dec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstV4l2CodecH264Dec *self = GST_V4L2_CODEC_H264_DEC (object);
  GObject *dec = G_OBJECT (self->decoder);

  switch (prop_id) {
    default:
      gst_v4l2_decoder_get_property (dec, prop_id - PROP_LAST, value, pspec);
      break;
  }
}

static void
gst_v4l2_codec_h264_dec_init (GstV4l2CodecH264Dec * self)
{
}

static void
gst_v4l2_codec_h264_dec_subinit (GstV4l2CodecH264Dec * self,
    GstV4l2CodecH264DecClass * klass)
{
  self->decoder = gst_v4l2_decoder_new (klass->device);
}

static void
gst_v4l2_codec_h264_dec_dispose (GObject * object)
{
  GstV4l2CodecH264Dec *self = GST_V4L2_CODEC_H264_DEC (object);

  g_clear_object (&self->decoder);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_v4l2_codec_h264_dec_class_init (GstV4l2CodecH264DecClass * klass)
{
}

static void
gst_v4l2_codec_h264_dec_subclass_init (GstV4l2CodecH264DecClass * klass,
    GstV4l2CodecDevice * device)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstVideoDecoderClass *decoder_class = GST_VIDEO_DECODER_CLASS (klass);
  GstH264DecoderClass *h264decoder_class = GST_H264_DECODER_CLASS (klass);

  gobject_class->set_property = gst_v4l2_codec_h264_dec_set_property;
  gobject_class->get_property = gst_v4l2_codec_h264_dec_get_property;
  gobject_class->dispose = gst_v4l2_codec_h264_dec_dispose;

  gst_element_class_set_static_metadata (element_class,
      "V4L2 Stateless H.264 Video Decoder",
      "Codec/Decoder/Video/Hardware",
      "A V4L2 based H.264 video decoder",
      "Nicolas Dufresne <nicolas.dufresne@collabora.com>");

  gst_element_class_add_static_pad_template (element_class, &sink_template);
  gst_element_class_add_static_pad_template (element_class, &src_template);

  decoder_class->open = GST_DEBUG_FUNCPTR (gst_v4l2_codec_h264_dec_open);
  decoder_class->close = GST_DEBUG_FUNCPTR (gst_v4l2_codec_h264_dec_close);
  decoder_class->stop = GST_DEBUG_FUNCPTR (gst_v4l2_codec_h264_dec_stop);
  decoder_class->negotiate =
      GST_DEBUG_FUNCPTR (gst_v4l2_codec_h264_dec_negotiate);
  decoder_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_v4l2_codec_h264_dec_decide_allocation);

  h264decoder_class->new_sequence =
      GST_DEBUG_FUNCPTR (gst_v4l2_codec_h264_dec_new_sequence);
  h264decoder_class->output_picture =
      GST_DEBUG_FUNCPTR (gst_v4l2_codec_h264_dec_output_picture);
  h264decoder_class->start_picture =
      GST_DEBUG_FUNCPTR (gst_v4l2_codec_h264_dec_start_picture);
  h264decoder_class->decode_slice =
      GST_DEBUG_FUNCPTR (gst_v4l2_codec_h264_dec_decode_slice);
  h264decoder_class->end_picture =
      GST_DEBUG_FUNCPTR (gst_v4l2_codec_h264_dec_end_picture);

  klass->device = device;
  gst_v4l2_decoder_install_properties (gobject_class, PROP_LAST, device);
}

void
gst_v4l2_codec_h264_dec_register (GstPlugin * plugin,
    GstV4l2CodecDevice * device, guint rank)
{
  GTypeQuery type_query;
  GTypeInfo type_info = { 0, };
  GType subtype;
  gchar *type_name;

  g_type_query (GST_TYPE_V4L2_CODEC_H264_DEC, &type_query);
  memset (&type_info, 0, sizeof (type_info));
  type_info.class_size = type_query.class_size;
  type_info.instance_size = type_query.instance_size;
  type_info.class_init = (GClassInitFunc) gst_v4l2_codec_h264_dec_subclass_init;
  type_info.class_data = gst_mini_object_ref (GST_MINI_OBJECT (device));
  type_info.instance_init = (GInstanceInitFunc) gst_v4l2_codec_h264_dec_subinit;
  GST_MINI_OBJECT_FLAG_SET (device, GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);

  /* The first decoder to be registered should use a constant name, like
   * v4l2slh264dec, for any additional decoders, we create unique names. Decoder
   * names may change between boots, so this should help gain stable names for
   * the most common use cases. SL stands for state-less, we differentiate
   * with v4l2h264dec as this element may not have the same properties */
  type_name = g_strdup ("v4l2slh264dec");

  if (g_type_from_name (type_name) != 0) {
    gchar *basename = g_path_get_basename (device->video_device_path);
    g_free (type_name);
    type_name = g_strdup_printf ("v4l2sl%sh264dec", basename);
    g_free (basename);
  }

  subtype = g_type_register_static (GST_TYPE_V4L2_CODEC_H264_DEC, type_name,
      &type_info, 0);

  if (!gst_element_register (plugin, type_name, rank, subtype))
    GST_WARNING ("Failed to register plugin '%s'", type_name);

  g_free (type_name);
}
