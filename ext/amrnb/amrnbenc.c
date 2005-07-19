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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "amrnbenc.h"

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "width = (int) 16, "
        "depth = (int) 16, "
        "signed = (boolean) TRUE, "
        "endianness = (int) BYTE_ORDER, "
        "rate = (int) 8000," "channels = (int) 1")
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-amr-nb, "
        "rate = (int) 8000, " "channels = (int) 1")
    );

static void gst_amrnbenc_base_init (GstAmrnbEncClass * klass);
static void gst_amrnbenc_class_init (GstAmrnbEncClass * klass);
static void gst_amrnbenc_init (GstAmrnbEnc * amrnbenc);
static void gst_amrnbenc_finalize (GObject * object);

static GstFlowReturn gst_amrnbenc_chain (GstPad * pad, GstBuffer * buffer);
static gboolean gst_amrnbenc_setcaps (GstPad * pad, GstCaps * caps);
static GstElementStateReturn gst_amrnbenc_state_change (GstElement * element);

static GstElementClass *parent_class = NULL;

GType
gst_amrnbenc_get_type (void)
{
  static GType amrnbenc_type = 0;

  if (!amrnbenc_type) {
    static const GTypeInfo amrnbenc_info = {
      sizeof (GstAmrnbEncClass),
      (GBaseInitFunc) gst_amrnbenc_base_init,
      NULL,
      (GClassInitFunc) gst_amrnbenc_class_init,
      NULL,
      NULL,
      sizeof (GstAmrnbEnc),
      0,
      (GInstanceInitFunc) gst_amrnbenc_init,
    };

    amrnbenc_type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstAmrnbEnc", &amrnbenc_info, 0);
  }

  return amrnbenc_type;
}

static void
gst_amrnbenc_base_init (GstAmrnbEncClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstElementDetails gst_amrnbenc_details = {
    "AMR-NB decoder",
    "Codec/Decoder/Audio",
    "Adaptive Multi-Rate Narrow-Band audio encoder",
    "Ronald Bultje <rbultje@ronald.bitfreak.net>, "
        "Wim Taymans <wim@fluendo.com>"
  };

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));

  gst_element_class_set_details (element_class, &gst_amrnbenc_details);
}

static void
gst_amrnbenc_class_init (GstAmrnbEncClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  object_class->finalize = gst_amrnbenc_finalize;

  element_class->change_state = gst_amrnbenc_state_change;
}

static void
gst_amrnbenc_init (GstAmrnbEnc * amrnbenc)
{
  /* create the sink pad */
  amrnbenc->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&sink_template),
      "sink");
  gst_pad_set_setcaps_function (amrnbenc->sinkpad, gst_amrnbenc_setcaps);
  gst_pad_set_chain_function (amrnbenc->sinkpad, gst_amrnbenc_chain);
  gst_element_add_pad (GST_ELEMENT (amrnbenc), amrnbenc->sinkpad);

  /* create the src pad */
  amrnbenc->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&src_template),
      "src");
  gst_pad_use_fixed_caps (amrnbenc->srcpad);
  gst_element_add_pad (GST_ELEMENT (amrnbenc), amrnbenc->srcpad);

  amrnbenc->adapter = gst_adapter_new ();

  /* init rest */
  amrnbenc->handle = NULL;
  amrnbenc->channels = 0;
  amrnbenc->rate = 0;
  amrnbenc->ts = 0;
}

static void
gst_amrnbenc_finalize (GObject * object)
{
  GstAmrnbEnc *amrnbenc;

  amrnbenc = GST_AMRNBENC (object);

  g_object_unref (G_OBJECT (amrnbenc->adapter));
  amrnbenc->adapter = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_amrnbenc_setcaps (GstPad * pad, GstCaps * caps)
{
  GstStructure *structure;
  GstAmrnbEnc *amrnbenc;
  GstCaps *copy;

  amrnbenc = GST_AMRNBENC (GST_PAD_PARENT (pad));

  structure = gst_caps_get_structure (caps, 0);

  /* get channel count */
  gst_structure_get_int (structure, "channels", &amrnbenc->channels);
  gst_structure_get_int (structure, "rate", &amrnbenc->rate);

  /* this is not wrong but will sound bad */
  if (amrnbenc->channels != 1) {
    g_warning ("amrnbdec is only optimized for mono channels");
  }
  if (amrnbenc->rate != 8000) {
    g_warning ("amrnbdec is only optimized for 8000 Hz samplerate");
  }

  /* create reverse caps */
  copy = gst_caps_new_simple ("audio/x-amr-nb",
      "channels", G_TYPE_INT, amrnbenc->channels,
      "rate", G_TYPE_INT, amrnbenc->rate, NULL);

  gst_pad_set_caps (amrnbenc->srcpad, copy);
  gst_caps_unref (copy);

  return TRUE;
}

static GstFlowReturn
gst_amrnbenc_chain (GstPad * pad, GstBuffer * buffer)
{
  GstAmrnbEnc *amrnbenc;
  GstFlowReturn ret;

  amrnbenc = GST_AMRNBENC (GST_PAD_PARENT (pad));

  if (amrnbenc->rate == 0 || amrnbenc->channels == 0)
    goto not_negotiated;

  if (GST_BUFFER_TIMESTAMP_IS_VALID (buffer))
    amrnbenc->ts = GST_BUFFER_TIMESTAMP (buffer);

  ret = GST_FLOW_OK;
  gst_adapter_push (amrnbenc->adapter, buffer);

  /* Collect samples until we have enough for an output frame */
  while (gst_adapter_available (amrnbenc->adapter) >= 320) {
    GstBuffer *out;
    guint8 *data;
    gint outsize;

    /* get output,  max size is 32 */
    out = gst_buffer_new_and_alloc (32);
    GST_BUFFER_DURATION (out) = GST_SECOND * 160 /
        (amrnbenc->rate * amrnbenc->channels);
    GST_BUFFER_TIMESTAMP (out) = amrnbenc->ts;
    amrnbenc->ts += GST_BUFFER_DURATION (out);
    gst_buffer_set_caps (out, gst_pad_get_caps (amrnbenc->srcpad));

    data = (guint8 *) gst_adapter_peek (amrnbenc->adapter, 320);

    /* decode */
    outsize = Encoder_Interface_Encode (amrnbenc->handle, MR122, (short *) data,
        (guint8 *) GST_BUFFER_DATA (out), 0);

    gst_adapter_flush (amrnbenc->adapter, 320);

    GST_BUFFER_SIZE (out) = outsize;

    /* play */
    ret = gst_pad_push (amrnbenc->srcpad, out);
  }

  return ret;

not_negotiated:
  {
    return GST_FLOW_NOT_NEGOTIATED;
  }
}

static GstElementStateReturn
gst_amrnbenc_state_change (GstElement * element)
{
  GstAmrnbEnc *amrnbenc;
  GstElementStateReturn ret;
  gint transition;

  amrnbenc = GST_AMRNBENC (element);
  transition = GST_STATE_TRANSITION (element);

  switch (transition) {
    case GST_STATE_NULL_TO_READY:
      if (!(amrnbenc->handle = Encoder_Interface_init (0)))
        return GST_STATE_FAILURE;
      break;
    case GST_STATE_READY_TO_PAUSED:
      amrnbenc->ts = 0;
      gst_adapter_clear (amrnbenc->adapter);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element);

  switch (transition) {
    case GST_STATE_READY_TO_NULL:
      Encoder_Interface_exit (amrnbenc->handle);
      break;
    default:
      break;
  }

  return ret;
}
