/*
 * Farsight
 * GStreamer GSM encoder
 * Copyright (C) 2005 Philippe Khalaf <burger@speedy.org>
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


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>

#include "gstgsmdec.h"

GST_DEBUG_CATEGORY_STATIC (gsmdec_debug);
#define GST_CAT_DEFAULT (gsmdec_debug)

/* GSMDec signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  /* FILL ME */
  ARG_0
};

static gboolean gst_gsmdec_start (GstAudioDecoder * dec);
static gboolean gst_gsmdec_stop (GstAudioDecoder * dec);
static gboolean gst_gsmdec_set_format (GstAudioDecoder * dec, GstCaps * caps);
static GstFlowReturn gst_gsmdec_parse (GstAudioDecoder * dec,
    GstAdapter * adapter, gint * offset, gint * length);
static GstFlowReturn gst_gsmdec_handle_frame (GstAudioDecoder * dec,
    GstBuffer * in_buf);

/*static guint gst_gsmdec_signals[LAST_SIGNAL] = { 0 }; */

#define ENCODED_SAMPLES	160

static GstStaticPadTemplate gsmdec_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-gsm, rate = (int) 8000, channels = (int) 1; "
        "audio/ms-gsm, rate = (int) [1, MAX], channels = (int) 1")
    );

static GstStaticPadTemplate gsmdec_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "endianness = (int) BYTE_ORDER, "
        "signed = (boolean) true, "
        "width = (int) 16, "
        "depth = (int) 16, " "rate = (int) [1, MAX], " "channels = (int) 1")
    );

GST_BOILERPLATE (GstGSMDec, gst_gsmdec, GstAudioDecoder,
    GST_TYPE_AUDIO_DECODER);

static void
gst_gsmdec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class,
      &gsmdec_sink_template);
  gst_element_class_add_static_pad_template (element_class,
      &gsmdec_src_template);
  gst_element_class_set_details_simple (element_class, "GSM audio decoder",
      "Codec/Decoder/Audio",
      "Decodes GSM encoded audio", "Philippe Khalaf <burger@speedy.org>");
}

static void
gst_gsmdec_class_init (GstGSMDecClass * klass)
{
  GstAudioDecoderClass *base_class;

  base_class = (GstAudioDecoderClass *) klass;

  base_class->start = GST_DEBUG_FUNCPTR (gst_gsmdec_start);
  base_class->stop = GST_DEBUG_FUNCPTR (gst_gsmdec_stop);
  base_class->set_format = GST_DEBUG_FUNCPTR (gst_gsmdec_set_format);
  base_class->parse = GST_DEBUG_FUNCPTR (gst_gsmdec_parse);
  base_class->handle_frame = GST_DEBUG_FUNCPTR (gst_gsmdec_handle_frame);

  GST_DEBUG_CATEGORY_INIT (gsmdec_debug, "gsmdec", 0, "GSM Decoder");
}

static void
gst_gsmdec_init (GstGSMDec * gsmdec, GstGSMDecClass * klass)
{
}

static gboolean
gst_gsmdec_start (GstAudioDecoder * dec)
{
  GstGSMDec *gsmdec = GST_GSMDEC (dec);

  GST_DEBUG_OBJECT (dec, "start");

  gsmdec->state = gsm_create ();

  return TRUE;
}

static gboolean
gst_gsmdec_stop (GstAudioDecoder * dec)
{
  GstGSMDec *gsmdec = GST_GSMDEC (dec);

  GST_DEBUG_OBJECT (dec, "stop");

  gsm_destroy (gsmdec->state);

  return TRUE;
}

static gboolean
gst_gsmdec_set_format (GstAudioDecoder * dec, GstCaps * caps)
{
  GstGSMDec *gsmdec;
  GstCaps *srccaps;
  GstStructure *s;
  gboolean ret = FALSE;
  gint rate;

  gsmdec = GST_GSMDEC (dec);

  s = gst_caps_get_structure (caps, 0);
  if (s == NULL)
    goto wrong_caps;

  /* figure out if we deal with plain or MSGSM */
  if (gst_structure_has_name (s, "audio/x-gsm"))
    gsmdec->use_wav49 = 0;
  else if (gst_structure_has_name (s, "audio/ms-gsm"))
    gsmdec->use_wav49 = 1;
  else
    goto wrong_caps;

  gsmdec->needed = 33;

  if (!gst_structure_get_int (s, "rate", &rate)) {
    GST_WARNING_OBJECT (gsmdec, "missing sample rate parameter from sink caps");
    goto beach;
  }

  /* MSGSM needs different framing */
  gsm_option (gsmdec->state, GSM_OPT_WAV49, &gsmdec->use_wav49);

  /* Setting up src caps based on the input sample rate. */
  srccaps = gst_caps_new_simple ("audio/x-raw-int",
      "endianness", G_TYPE_INT, G_BYTE_ORDER,
      "signed", G_TYPE_BOOLEAN, TRUE,
      "width", G_TYPE_INT, 16,
      "depth", G_TYPE_INT, 16,
      "rate", G_TYPE_INT, rate, "channels", G_TYPE_INT, 1, NULL);

  ret = gst_pad_set_caps (GST_AUDIO_DECODER_SRC_PAD (dec), srccaps);
  gst_caps_unref (srccaps);

  return ret;

  /* ERRORS */
wrong_caps:

  GST_ERROR_OBJECT (gsmdec, "invalid caps received");

beach:

  return ret;
}

static GstFlowReturn
gst_gsmdec_parse (GstAudioDecoder * dec, GstAdapter * adapter,
    gint * offset, gint * length)
{
  GstGSMDec *gsmdec = GST_GSMDEC (dec);
  guint size;

  size = gst_adapter_available (adapter);
  g_return_val_if_fail (size > 0, GST_FLOW_ERROR);

  /* WAV49 requires alternating 33 and 32 bytes of input */
  if (gsmdec->use_wav49) {
    gsmdec->needed = (gsmdec->needed == 33 ? 32 : 33);
  }

  if (size < gsmdec->needed)
    return GST_FLOW_UNEXPECTED;

  *offset = 0;
  *length = gsmdec->needed;

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_gsmdec_handle_frame (GstAudioDecoder * dec, GstBuffer * buffer)
{
  GstGSMDec *gsmdec;
  gsm_byte *data;
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer *outbuf;

  /* no fancy draining */
  if (G_UNLIKELY (!buffer))
    return GST_FLOW_OK;

  gsmdec = GST_GSMDEC (dec);

  /* always the same amount of output samples */
  outbuf = gst_buffer_new_and_alloc (ENCODED_SAMPLES * sizeof (gsm_signal));

  /* now encode frame into the output buffer */
  data = (gsm_byte *) GST_BUFFER_DATA (buffer);
  if (gsm_decode (gsmdec->state, data,
          (gsm_signal *) GST_BUFFER_DATA (outbuf)) < 0) {
    /* invalid frame */
    GST_AUDIO_DECODER_ERROR (gsmdec, 1, STREAM, DECODE, (NULL),
        ("tried to decode an invalid frame"), ret);
    if (ret != GST_FLOW_OK)
      goto exit;
    gst_buffer_unref (outbuf);
    outbuf = NULL;
  }

  gst_audio_decoder_finish_frame (dec, outbuf, 1);

exit:
  return ret;
}
