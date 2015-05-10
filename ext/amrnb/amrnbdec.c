/* GStreamer Adaptive Multi-Rate Narrow-Band (AMR-NB) plugin
 * Copyright (C) 2004 Ronald Bultje <rbultje@ronald.bitfreak.net>
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

/**
 * SECTION:element-amrnbdec
 * @see_also: #GstAmrnbEnc, #GstAmrParse
 *
 * AMR narrowband decoder based on the 
 * <ulink url="http://sourceforge.net/projects/opencore-amr">opencore codec implementation</ulink>.
 * 
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 filesrc location=abc.amr ! amrparse ! amrnbdec ! audioconvert ! audioresample ! autoaudiosink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "amrnbdec.h"

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/AMR, " "rate = (int) 8000, " "channels = (int) 1")
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw, format = (string) " GST_AUDIO_NE (S16) ", "
        "layout = (string) interleaved, "
        "rate = (int) 8000," "channels = (int) 1")
    );

GST_DEBUG_CATEGORY_STATIC (gst_amrnbdec_debug);
#define GST_CAT_DEFAULT gst_amrnbdec_debug

static const gint block_size_if1[16] = { 12, 13, 15, 17, 19, 20, 26, 31, 5,
  0, 0, 0, 0, 0, 0, 0
};

static const gint block_size_if2[16] = { 12, 13, 15, 17, 18, 20, 25, 30, 5,
  0, 0, 0, 0, 0, 0, 0
};

static GType
gst_amrnb_variant_get_type (void)
{
  static GType gst_amrnb_variant_type = 0;
  static const GEnumValue gst_amrnb_variant[] = {
    {GST_AMRNB_VARIANT_IF1, "IF1", "IF1"},
    {GST_AMRNB_VARIANT_IF2, "IF2", "IF2"},
    {0, NULL, NULL},
  };
  if (!gst_amrnb_variant_type) {
    gst_amrnb_variant_type =
        g_enum_register_static ("GstAmrnbVariant", gst_amrnb_variant);
  }
  return gst_amrnb_variant_type;
}

#define GST_AMRNB_VARIANT_TYPE (gst_amrnb_variant_get_type())

#define VARIANT_DEFAULT GST_AMRNB_VARIANT_IF1
enum
{
  PROP_0,
  PROP_VARIANT
};

static void gst_amrnbdec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_amrnbdec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_amrnbdec_start (GstAudioDecoder * dec);
static gboolean gst_amrnbdec_stop (GstAudioDecoder * dec);
static gboolean gst_amrnbdec_set_format (GstAudioDecoder * dec, GstCaps * caps);
static gboolean gst_amrnbdec_parse (GstAudioDecoder * dec, GstAdapter * adapter,
    gint * offset, gint * length);
static GstFlowReturn gst_amrnbdec_handle_frame (GstAudioDecoder * dec,
    GstBuffer * buffer);

#define gst_amrnbdec_parent_class parent_class
G_DEFINE_TYPE (GstAmrnbDec, gst_amrnbdec, GST_TYPE_AUDIO_DECODER);

static void
gst_amrnbdec_class_init (GstAmrnbDecClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstAudioDecoderClass *base_class = GST_AUDIO_DECODER_CLASS (klass);

  object_class->set_property = gst_amrnbdec_set_property;
  object_class->get_property = gst_amrnbdec_get_property;

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));

  gst_element_class_set_static_metadata (element_class, "AMR-NB audio decoder",
      "Codec/Decoder/Audio",
      "Adaptive Multi-Rate Narrow-Band audio decoder",
      "GStreamer maintainers <gstreamer-devel@lists.sourceforge.net>");

  base_class->start = GST_DEBUG_FUNCPTR (gst_amrnbdec_start);
  base_class->stop = GST_DEBUG_FUNCPTR (gst_amrnbdec_stop);
  base_class->set_format = GST_DEBUG_FUNCPTR (gst_amrnbdec_set_format);
  base_class->parse = GST_DEBUG_FUNCPTR (gst_amrnbdec_parse);
  base_class->handle_frame = GST_DEBUG_FUNCPTR (gst_amrnbdec_handle_frame);

  g_object_class_install_property (object_class, PROP_VARIANT,
      g_param_spec_enum ("variant", "Variant",
          "The decoder variant", GST_AMRNB_VARIANT_TYPE,
          VARIANT_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  GST_DEBUG_CATEGORY_INIT (gst_amrnbdec_debug, "amrnbdec", 0,
      "AMR-NB audio decoder");
}

static void
gst_amrnbdec_init (GstAmrnbDec * amrnbdec)
{
  gst_audio_decoder_set_needs_format (GST_AUDIO_DECODER (amrnbdec), TRUE);
}

static gboolean
gst_amrnbdec_start (GstAudioDecoder * dec)
{
  GstAmrnbDec *amrnbdec = GST_AMRNBDEC (dec);

  GST_DEBUG_OBJECT (dec, "start");
  if (!(amrnbdec->handle = Decoder_Interface_init ()))
    return FALSE;

  amrnbdec->rate = 0;
  amrnbdec->channels = 0;

  return TRUE;
}

static gboolean
gst_amrnbdec_stop (GstAudioDecoder * dec)
{
  GstAmrnbDec *amrnbdec = GST_AMRNBDEC (dec);

  GST_DEBUG_OBJECT (dec, "stop");
  Decoder_Interface_exit (amrnbdec->handle);

  return TRUE;
}

static void
gst_amrnbdec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAmrnbDec *self = GST_AMRNBDEC (object);

  switch (prop_id) {
    case PROP_VARIANT:
      self->variant = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  return;
}

static void
gst_amrnbdec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstAmrnbDec *self = GST_AMRNBDEC (object);

  switch (prop_id) {
    case PROP_VARIANT:
      g_value_set_enum (value, self->variant);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  return;
}

static gboolean
gst_amrnbdec_set_format (GstAudioDecoder * dec, GstCaps * caps)
{
  GstStructure *structure;
  GstAmrnbDec *amrnbdec;
  GstAudioInfo info;

  amrnbdec = GST_AMRNBDEC (dec);

  structure = gst_caps_get_structure (caps, 0);

  /* get channel count */
  gst_structure_get_int (structure, "channels", &amrnbdec->channels);
  gst_structure_get_int (structure, "rate", &amrnbdec->rate);

  /* create reverse caps */
  gst_audio_info_init (&info);
  gst_audio_info_set_format (&info,
      GST_AUDIO_FORMAT_S16, amrnbdec->rate, amrnbdec->channels, NULL);

  return gst_audio_decoder_set_output_format (dec, &info);
}

static GstFlowReturn
gst_amrnbdec_parse (GstAudioDecoder * dec, GstAdapter * adapter,
    gint * offset, gint * length)
{
  GstAmrnbDec *amrnbdec = GST_AMRNBDEC (dec);
  guint8 head[1];
  guint size;
  gboolean sync, eos;
  gint block, mode;

  size = gst_adapter_available (adapter);
  g_return_val_if_fail (size > 0, GST_FLOW_ERROR);

  gst_audio_decoder_get_parse_state (dec, &sync, &eos);

  /* need to peek data to get the size */
  if (size < 1)
    return GST_FLOW_ERROR;

  gst_adapter_copy (adapter, head, 0, 1);

  /* get size */
  switch (amrnbdec->variant) {
    case GST_AMRNB_VARIANT_IF1:
      mode = (head[0] >> 3) & 0x0F;
      block = block_size_if1[mode] + 1;
      break;
    case GST_AMRNB_VARIANT_IF2:
      mode = head[0] & 0x0F;
      block = block_size_if2[mode] + 1;
      break;
    default:
      g_assert_not_reached ();
      return GST_FLOW_ERROR;
      break;
  }

  GST_DEBUG_OBJECT (amrnbdec, "mode %d, block %d", mode, block);

  if (block > size)
    return GST_FLOW_EOS;

  *offset = 0;
  *length = block;

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_amrnbdec_handle_frame (GstAudioDecoder * dec, GstBuffer * buffer)
{
  GstAmrnbDec *amrnbdec;
  GstMapInfo inmap, outmap;
  GstBuffer *out;

  amrnbdec = GST_AMRNBDEC (dec);

  /* no fancy flushing */
  if (!buffer || !gst_buffer_get_size (buffer))
    return GST_FLOW_OK;

  gst_buffer_map (buffer, &inmap, GST_MAP_READ);

  /* get output */
  out = gst_buffer_new_and_alloc (160 * 2);
  /* decode */
  gst_buffer_map (out, &outmap, GST_MAP_WRITE);
  Decoder_Interface_Decode (amrnbdec->handle, inmap.data,
      (gint16 *) outmap.data, 0);
  gst_buffer_unmap (out, &outmap);

  gst_buffer_unmap (buffer, &inmap);

  return gst_audio_decoder_finish_frame (dec, out, 1);
}
