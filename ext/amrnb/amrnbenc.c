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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:element-amrnbenc
 * @see_also: #GstAmrnbDec, #GstAmrnbParse
 *
 * AMR narrowband encoder based on the 
 * <ulink url="http://sourceforge.net/projects/opencore-amr">opencore codec implementation</ulink>.
 * 
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch filesrc location=abc.wav ! wavparse ! audioresample ! audioconvert ! amrnbenc ! filesink location=abc.amr
 * ]|
 * Please note that the above stream misses the header, that is needed to play
 * the stream.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "amrnbenc.h"

static GType
gst_amrnbenc_bandmode_get_type (void)
{
  static GType gst_amrnbenc_bandmode_type = 0;
  static const GEnumValue gst_amrnbenc_bandmode[] = {
    {MR475, "MR475", "MR475"},
    {MR515, "MR515", "MR515"},
    {MR59, "MR59", "MR59"},
    {MR67, "MR67", "MR67"},
    {MR74, "MR74", "MR74"},
    {MR795, "MR795", "MR795"},
    {MR102, "MR102", "MR102"},
    {MR122, "MR122", "MR122"},
    {MRDTX, "MRDTX", "MRDTX"},
    {0, NULL, NULL},
  };
  if (!gst_amrnbenc_bandmode_type) {
    gst_amrnbenc_bandmode_type =
        g_enum_register_static ("GstAmrnbEncBandMode", gst_amrnbenc_bandmode);
  }
  return gst_amrnbenc_bandmode_type;
}

#define GST_AMRNBENC_BANDMODE_TYPE (gst_amrnbenc_bandmode_get_type())

#define BANDMODE_DEFAULT MR122
enum
{
  PROP_0,
  PROP_BANDMODE
};

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw, format = (string) " GST_AUDIO_NE (S16) ", "
        "layout = (string) interleaved, "
        "rate = (int) 8000," "channels = (int) 1")
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/AMR, " "rate = (int) 8000, " "channels = (int) 1")
    );

GST_DEBUG_CATEGORY_STATIC (gst_amrnbenc_debug);
#define GST_CAT_DEFAULT gst_amrnbenc_debug

static gboolean gst_amrnbenc_start (GstAudioEncoder * enc);
static gboolean gst_amrnbenc_stop (GstAudioEncoder * enc);
static gboolean gst_amrnbenc_set_format (GstAudioEncoder * enc,
    GstAudioInfo * info);
static GstFlowReturn gst_amrnbenc_handle_frame (GstAudioEncoder * enc,
    GstBuffer * in_buf);

#define gst_amrnbenc_parent_class parent_class
G_DEFINE_TYPE (GstAmrnbEnc, gst_amrnbenc, GST_TYPE_AUDIO_ENCODER);

static void
gst_amrnbenc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAmrnbEnc *self = GST_AMRNBENC (object);

  switch (prop_id) {
    case PROP_BANDMODE:
      self->bandmode = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  return;
}

static void
gst_amrnbenc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstAmrnbEnc *self = GST_AMRNBENC (object);

  switch (prop_id) {
    case PROP_BANDMODE:
      g_value_set_enum (value, self->bandmode);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  return;
}

static void
gst_amrnbenc_class_init (GstAmrnbEncClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstAudioEncoderClass *base_class = GST_AUDIO_ENCODER_CLASS (klass);

  object_class->set_property = gst_amrnbenc_set_property;
  object_class->get_property = gst_amrnbenc_get_property;

  base_class->start = GST_DEBUG_FUNCPTR (gst_amrnbenc_start);
  base_class->stop = GST_DEBUG_FUNCPTR (gst_amrnbenc_stop);
  base_class->set_format = GST_DEBUG_FUNCPTR (gst_amrnbenc_set_format);
  base_class->handle_frame = GST_DEBUG_FUNCPTR (gst_amrnbenc_handle_frame);

  g_object_class_install_property (object_class, PROP_BANDMODE,
      g_param_spec_enum ("band-mode", "Band Mode",
          "Encoding Band Mode (Kbps)", GST_AMRNBENC_BANDMODE_TYPE,
          BANDMODE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));

  gst_element_class_set_static_metadata (element_class, "AMR-NB audio encoder",
      "Codec/Encoder/Audio",
      "Adaptive Multi-Rate Narrow-Band audio encoder",
      "Wim Taymans <wim.taymans@gmail.com>");

  GST_DEBUG_CATEGORY_INIT (gst_amrnbenc_debug, "amrnbenc", 0,
      "AMR-NB audio encoder");
}

static void
gst_amrnbenc_init (GstAmrnbEnc * amrnbenc)
{
}

static gboolean
gst_amrnbenc_start (GstAudioEncoder * enc)
{
  GstAmrnbEnc *amrnbenc = GST_AMRNBENC (enc);

  GST_DEBUG_OBJECT (amrnbenc, "start");

  if (!(amrnbenc->handle = Encoder_Interface_init (0)))
    return FALSE;

  return TRUE;
}

static gboolean
gst_amrnbenc_stop (GstAudioEncoder * enc)
{
  GstAmrnbEnc *amrnbenc = GST_AMRNBENC (enc);

  GST_DEBUG_OBJECT (amrnbenc, "stop");

  Encoder_Interface_exit (amrnbenc->handle);

  return TRUE;
}

static gboolean
gst_amrnbenc_set_format (GstAudioEncoder * enc, GstAudioInfo * info)
{
  GstAmrnbEnc *amrnbenc;
  GstCaps *copy;

  amrnbenc = GST_AMRNBENC (enc);

  /* parameters already parsed for us */
  amrnbenc->rate = GST_AUDIO_INFO_RATE (info);
  amrnbenc->channels = GST_AUDIO_INFO_CHANNELS (info);

  /* we do not really accept other input, but anyway ... */
  /* this is not wrong but will sound bad */
  if (amrnbenc->channels != 1) {
    g_warning ("amrnbdec is only optimized for mono channels");
  }
  if (amrnbenc->rate != 8000) {
    g_warning ("amrnbdec is only optimized for 8000 Hz samplerate");
  }

  /* create reverse caps */
  copy = gst_caps_new_simple ("audio/AMR",
      "channels", G_TYPE_INT, amrnbenc->channels,
      "rate", G_TYPE_INT, amrnbenc->rate, NULL);

  gst_audio_encoder_set_output_format (GST_AUDIO_ENCODER (amrnbenc), copy);
  gst_caps_unref (copy);

  /* report needs to base class: hand one frame at a time */
  gst_audio_encoder_set_frame_samples_min (enc, 160);
  gst_audio_encoder_set_frame_samples_max (enc, 160);
  gst_audio_encoder_set_frame_max (enc, 1);

  return TRUE;
}

static GstFlowReturn
gst_amrnbenc_handle_frame (GstAudioEncoder * enc, GstBuffer * buffer)
{
  GstAmrnbEnc *amrnbenc;
  GstFlowReturn ret;
  GstBuffer *out;
  GstMapInfo in_map, out_map;
  gsize out_size;

  amrnbenc = GST_AMRNBENC (enc);

  g_return_val_if_fail (amrnbenc->handle, GST_FLOW_FLUSHING);

  /* we don't deal with squeezing remnants, so simply discard those */
  if (G_UNLIKELY (buffer == NULL)) {
    GST_DEBUG_OBJECT (amrnbenc, "no data");
    return GST_FLOW_OK;
  }

  gst_buffer_map (buffer, &in_map, GST_MAP_READ);

  if (G_UNLIKELY (in_map.size < 320)) {
    gst_buffer_unmap (buffer, &in_map);
    GST_DEBUG_OBJECT (amrnbenc, "discarding trailing data of %" G_GSIZE_FORMAT
        " bytes", in_map.size);
    return gst_audio_encoder_finish_frame (enc, NULL, -1);
  }

  /* get output, max size is 32 */
  out = gst_buffer_new_and_alloc (32);
  /* AMR encoder actually writes into the source data buffers it gets */
  /* should be able to handle that with what we are given */

  gst_buffer_map (out, &out_map, GST_MAP_WRITE);
  /* encode */
  out_size =
      Encoder_Interface_Encode (amrnbenc->handle, amrnbenc->bandmode,
      (short *) in_map.data, out_map.data, 0);
  gst_buffer_unmap (out, &out_map);
  gst_buffer_resize (out, 0, out_size);
  gst_buffer_unmap (buffer, &in_map);

  GST_LOG_OBJECT (amrnbenc, "output data size %" G_GSIZE_FORMAT, out_size);

  if (out_size) {
    ret = gst_audio_encoder_finish_frame (enc, out, 160);
  } else {
    /* should not happen (without dtx or so at least) */
    GST_WARNING_OBJECT (amrnbenc, "no encoded data; discarding input");
    gst_buffer_unref (out);
    ret = gst_audio_encoder_finish_frame (enc, NULL, -1);
  }

  return ret;
}
