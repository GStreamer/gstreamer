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
 * SECTION:element-amrwbdec
 * @see_also: #GstAmrwbEnc
 *
 * AMR wideband decoder based on the 
 * <ulink url="http://sourceforge.net/projects/opencore-amr">opencore codec implementation</ulink>.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch filesrc location=abc.amr ! amrparse ! amrwbdec ! audioresample ! audioconvert ! alsasink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

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
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "width = (int) 16, "
        "depth = (int) 16, "
        "signed = (boolean) TRUE, "
        "endianness = (int) BYTE_ORDER, "
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

#define _do_init(bla) \
    GST_DEBUG_CATEGORY_INIT (gst_amrwbdec_debug, "amrwbdec", 0, "AMR-WB audio decoder");

GST_BOILERPLATE_FULL (GstAmrwbDec, gst_amrwbdec, GstAudioDecoder,
    GST_TYPE_AUDIO_DECODER, _do_init);

static void
gst_amrwbdec_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class,
      &sink_template);
  gst_element_class_add_static_pad_template (element_class, &src_template);

  gst_element_class_set_details_simple (element_class, "AMR-WB audio decoder",
      "Codec/Decoder/Audio",
      "Adaptive Multi-Rate Wideband audio decoder",
      "Renato Araujo <renato.filho@indt.org.br>");
}

static void
gst_amrwbdec_class_init (GstAmrwbDecClass * klass)
{
  GstAudioDecoderClass *base_class = GST_AUDIO_DECODER_CLASS (klass);

  base_class->start = GST_DEBUG_FUNCPTR (gst_amrwbdec_start);
  base_class->stop = GST_DEBUG_FUNCPTR (gst_amrwbdec_stop);
  base_class->set_format = GST_DEBUG_FUNCPTR (gst_amrwbdec_set_format);
  base_class->parse = GST_DEBUG_FUNCPTR (gst_amrwbdec_parse);
  base_class->handle_frame = GST_DEBUG_FUNCPTR (gst_amrwbdec_handle_frame);
}

static void
gst_amrwbdec_init (GstAmrwbDec * amrwbdec, GstAmrwbDecClass * klass)
{
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
  GstCaps *copy;

  amrwbdec = GST_AMRWBDEC (dec);

  structure = gst_caps_get_structure (caps, 0);

  /* get channel count */
  gst_structure_get_int (structure, "channels", &amrwbdec->channels);
  gst_structure_get_int (structure, "rate", &amrwbdec->rate);

  /* create reverse caps */
  copy = gst_caps_new_simple ("audio/x-raw-int",
      "channels", G_TYPE_INT, amrwbdec->channels,
      "width", G_TYPE_INT, 16,
      "depth", G_TYPE_INT, 16,
      "endianness", G_TYPE_INT, G_BYTE_ORDER,
      "rate", G_TYPE_INT, amrwbdec->rate, "signed", G_TYPE_BOOLEAN, TRUE, NULL);

  gst_pad_set_caps (GST_AUDIO_DECODER_SRC_PAD (amrwbdec), copy);
  gst_caps_unref (copy);

  return TRUE;
}

static GstFlowReturn
gst_amrwbdec_parse (GstAudioDecoder * dec, GstAdapter * adapter,
    gint * offset, gint * length)
{
  GstAmrwbDec *amrwbdec = GST_AMRWBDEC (dec);
  const guint8 *data;
  guint size;
  gboolean sync, eos;
  gint block, mode;

  size = gst_adapter_available (adapter);
  g_return_val_if_fail (size > 0, GST_FLOW_ERROR);

  gst_audio_decoder_get_parse_state (dec, &sync, &eos);

  /* need to peek data to get the size */
  if (size < 1)
    return GST_FLOW_ERROR;

  data = gst_adapter_peek (adapter, 1);
  mode = (data[0] >> 3) & 0x0F;
  block = block_size[mode];

  GST_DEBUG_OBJECT (amrwbdec, "mode %d, block %d", mode, block);

  if (block) {
    if (block > size)
      return GST_FLOW_UNEXPECTED;
    *offset = 0;
    *length = block;
  } else {
    /* no frame yet, skip one byte */
    GST_LOG_OBJECT (amrwbdec, "skipping byte");
    *offset = 1;
    return GST_FLOW_UNEXPECTED;
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_amrwbdec_handle_frame (GstAudioDecoder * dec, GstBuffer * buffer)
{
  GstAmrwbDec *amrwbdec;
  GstBuffer *out;
  const guint8 *data;

  amrwbdec = GST_AMRWBDEC (dec);

  /* no fancy flushing */
  if (!buffer || !GST_BUFFER_SIZE (buffer))
    return GST_FLOW_OK;

  if (amrwbdec->rate == 0 || amrwbdec->channels == 0)
    goto not_negotiated;

  /* the library seems to write into the source data, hence the copy. */
  /* should be no problem */
  data = GST_BUFFER_DATA (buffer);

  /* get output */
  out = gst_buffer_new_and_alloc (sizeof (gint16) * L_FRAME16k);

  /* decode */
  D_IF_decode (amrwbdec->handle, (unsigned char *) data,
      (Word16 *) GST_BUFFER_DATA (out), _good_frame);

  /* send out */
  return gst_audio_decoder_finish_frame (dec, out, 1);

  /* ERRORS */
not_negotiated:
  {
    GST_ELEMENT_ERROR (amrwbdec, STREAM, TYPE_NOT_FOUND, (NULL),
        ("Decoder is not initialized"));
    return GST_FLOW_NOT_NEGOTIATED;
  }
}
