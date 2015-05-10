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
 * SECTION:element-amrwbdec
 * @see_also: #GstAmrwbEnc
 *
 * AMR wideband decoder based on the 
 * <ulink url="http://sourceforge.net/projects/opencore-amr">opencore codec implementation</ulink>.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 filesrc location=abc.amr ! amrparse ! amrwbdec ! audioconvert ! audioresample ! autoaudiosink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/audio/audio.h>

#include "amrwbdec.h"

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/AMR-WB, "
        "rate = (int) 16000, " "channels = (int) 1")
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw, "
        "format = (string) " GST_AUDIO_NE (S16) ", "
        "layout = (string) interleaved, "
        "rate = (int) 16000, " "channels = (int) 1")
    );

GST_DEBUG_CATEGORY_STATIC (gst_amrwbdec_debug);
#define GST_CAT_DEFAULT gst_amrwbdec_debug

#define L_FRAME16k      320     /* Frame size at 16kHz  */

static const unsigned char block_size[16] =
    { 18, 24, 33, 37, 41, 47, 51, 59, 61,
  6, 0, 0, 0, 0, 1, 1
};

static gboolean gst_amrwbdec_start (GstAudioDecoder * dec);
static gboolean gst_amrwbdec_stop (GstAudioDecoder * dec);
static gboolean gst_amrwbdec_set_format (GstAudioDecoder * dec, GstCaps * caps);
static gboolean gst_amrwbdec_parse (GstAudioDecoder * dec, GstAdapter * adapter,
    gint * offset, gint * length);
static GstFlowReturn gst_amrwbdec_handle_frame (GstAudioDecoder * dec,
    GstBuffer * buffer);

#define gst_amrwbdec_parent_class parent_class
G_DEFINE_TYPE (GstAmrwbDec, gst_amrwbdec, GST_TYPE_AUDIO_DECODER);

static void
gst_amrwbdec_class_init (GstAmrwbDecClass * klass)
{
  GstAudioDecoderClass *base_class = GST_AUDIO_DECODER_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));

  gst_element_class_set_static_metadata (element_class, "AMR-WB audio decoder",
      "Codec/Decoder/Audio",
      "Adaptive Multi-Rate Wideband audio decoder",
      "Renato Araujo <renato.filho@indt.org.br>");

  base_class->start = GST_DEBUG_FUNCPTR (gst_amrwbdec_start);
  base_class->stop = GST_DEBUG_FUNCPTR (gst_amrwbdec_stop);
  base_class->set_format = GST_DEBUG_FUNCPTR (gst_amrwbdec_set_format);
  base_class->parse = GST_DEBUG_FUNCPTR (gst_amrwbdec_parse);
  base_class->handle_frame = GST_DEBUG_FUNCPTR (gst_amrwbdec_handle_frame);

  GST_DEBUG_CATEGORY_INIT (gst_amrwbdec_debug, "amrwbdec", 0,
      "AMR-WB audio decoder");
}

static void
gst_amrwbdec_init (GstAmrwbDec * amrwbdec)
{
  gst_audio_decoder_set_needs_format (GST_AUDIO_DECODER (amrwbdec), TRUE);
}

static gboolean
gst_amrwbdec_start (GstAudioDecoder * dec)
{
  GstAmrwbDec *amrwbdec = GST_AMRWBDEC (dec);

  GST_DEBUG_OBJECT (dec, "start");
  if (!(amrwbdec->handle = D_IF_init ()))
    return FALSE;

  amrwbdec->rate = 0;
  amrwbdec->channels = 0;

  return TRUE;
}

static gboolean
gst_amrwbdec_stop (GstAudioDecoder * dec)
{
  GstAmrwbDec *amrwbdec = GST_AMRWBDEC (dec);

  GST_DEBUG_OBJECT (dec, "stop");
  D_IF_exit (amrwbdec->handle);

  return TRUE;
}

static gboolean
gst_amrwbdec_set_format (GstAudioDecoder * dec, GstCaps * caps)
{
  GstStructure *structure;
  GstAmrwbDec *amrwbdec;
  GstAudioInfo info;

  amrwbdec = GST_AMRWBDEC (dec);

  structure = gst_caps_get_structure (caps, 0);

  /* get channel count */
  gst_structure_get_int (structure, "channels", &amrwbdec->channels);
  gst_structure_get_int (structure, "rate", &amrwbdec->rate);

  /* create reverse caps */
  gst_audio_info_init (&info);
  gst_audio_info_set_format (&info,
      GST_AUDIO_FORMAT_S16, amrwbdec->rate, amrwbdec->channels, NULL);

  gst_audio_decoder_set_output_format (dec, &info);

  return TRUE;
}

static GstFlowReturn
gst_amrwbdec_parse (GstAudioDecoder * dec, GstAdapter * adapter,
    gint * offset, gint * length)
{
  GstAmrwbDec *amrwbdec = GST_AMRWBDEC (dec);
  guint8 header[1];
  guint size;
  gboolean sync, eos;
  gint block, mode;

  size = gst_adapter_available (adapter);
  g_return_val_if_fail (size > 0, GST_FLOW_ERROR);

  gst_audio_decoder_get_parse_state (dec, &sync, &eos);

  /* need to peek data to get the size */
  if (size < 1)
    return GST_FLOW_ERROR;

  gst_adapter_copy (adapter, header, 0, 1);
  mode = (header[0] >> 3) & 0x0F;
  block = block_size[mode];

  GST_DEBUG_OBJECT (amrwbdec, "mode %d, block %d", mode, block);

  if (block) {
    if (block > size)
      return GST_FLOW_EOS;
    *offset = 0;
    *length = block;
  } else {
    /* no frame yet, skip one byte */
    GST_LOG_OBJECT (amrwbdec, "skipping byte");
    *offset = 1;
    return GST_FLOW_EOS;
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_amrwbdec_handle_frame (GstAudioDecoder * dec, GstBuffer * buffer)
{
  GstAmrwbDec *amrwbdec;
  GstBuffer *out;
  GstMapInfo inmap, outmap;

  amrwbdec = GST_AMRWBDEC (dec);

  /* no fancy flushing */
  if (!buffer || !gst_buffer_get_size (buffer))
    return GST_FLOW_OK;

  /* the library seems to write into the source data, hence the copy. */
  /* should be no problem */
  gst_buffer_map (buffer, &inmap, GST_MAP_READ);

  /* get output */
  out = gst_buffer_new_and_alloc (sizeof (gint16) * L_FRAME16k);
  gst_buffer_map (out, &outmap, GST_MAP_WRITE);

  /* decode */
  D_IF_decode (amrwbdec->handle, (unsigned char *) inmap.data,
      (short int *) outmap.data, _good_frame);

  gst_buffer_unmap (out, &outmap);
  gst_buffer_unmap (buffer, &inmap);

  /* send out */
  return gst_audio_decoder_finish_frame (dec, out, 1);
}
