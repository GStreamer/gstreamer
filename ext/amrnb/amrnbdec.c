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

#include "amrnbdec.h"

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/AMR, " "rate = (int) 8000, " "channels = (int) 1")
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "width = (int) 16, "
        "depth = (int) 16, "
        "signed = (boolean) TRUE, "
        "endianness = (int) BYTE_ORDER, "
        "rate = (int) 8000," "channels = (int) 1")
    );

static const gint block_size[16] = { 12, 13, 15, 17, 19, 20, 26, 31, 5,
  0, 0, 0, 0, 0, 0, 0
};

static void gst_amrnbdec_base_init (GstAmrnbDecClass * klass);
static void gst_amrnbdec_class_init (GstAmrnbDecClass * klass);
static void gst_amrnbdec_init (GstAmrnbDec * amrnbdec);

static GstFlowReturn gst_amrnbdec_chain (GstPad * pad, GstBuffer * buffer);
static gboolean gst_amrnbdec_setcaps (GstPad * pad, GstCaps * caps);
static GstStateChangeReturn gst_amrnbdec_state_change (GstElement * element,
    GstStateChange transition);

static GstElementClass *parent_class = NULL;

GType
gst_amrnbdec_get_type (void)
{
  static GType amrnbdec_type = 0;

  if (!amrnbdec_type) {
    static const GTypeInfo amrnbdec_info = {
      sizeof (GstAmrnbDecClass),
      (GBaseInitFunc) gst_amrnbdec_base_init,
      NULL,
      (GClassInitFunc) gst_amrnbdec_class_init,
      NULL,
      NULL,
      sizeof (GstAmrnbDec),
      0,
      (GInstanceInitFunc) gst_amrnbdec_init,
    };

    amrnbdec_type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstAmrnbDec", &amrnbdec_info, 0);
  }

  return amrnbdec_type;
}

static void
gst_amrnbdec_base_init (GstAmrnbDecClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstElementDetails gst_amrnbdec_details = {
    "AMR-NB decoder",
    "Codec/Decoder/Audio",
    "Adaptive Multi-Rate Narrow-Band audio decoder",
    "Ronald Bultje <rbultje@ronald.bitfreak.net>"
  };

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));

  gst_element_class_set_details (element_class, &gst_amrnbdec_details);
}

static void
gst_amrnbdec_class_init (GstAmrnbDecClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  element_class->change_state = gst_amrnbdec_state_change;
}

static void
gst_amrnbdec_init (GstAmrnbDec * amrnbdec)
{
  /* create the sink pad */
  amrnbdec->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&sink_template),
      "sink");
  gst_pad_set_setcaps_function (amrnbdec->sinkpad, gst_amrnbdec_setcaps);
  gst_pad_set_chain_function (amrnbdec->sinkpad, gst_amrnbdec_chain);
  gst_element_add_pad (GST_ELEMENT (amrnbdec), amrnbdec->sinkpad);

  /* create the src pad */
  amrnbdec->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&src_template),
      "src");
  gst_pad_use_fixed_caps (amrnbdec->srcpad);
  gst_element_add_pad (GST_ELEMENT (amrnbdec), amrnbdec->srcpad);

  amrnbdec->adapter = gst_adapter_new ();

  /* init rest */
  amrnbdec->handle = NULL;
  amrnbdec->channels = 0;
  amrnbdec->rate = 0;
  amrnbdec->ts = 0;
}

static gboolean
gst_amrnbdec_setcaps (GstPad * pad, GstCaps * caps)
{
  GstStructure *structure;
  GstAmrnbDec *amrnbdec;
  GstCaps *copy;

  amrnbdec = GST_AMRNBDEC (GST_PAD_PARENT (pad));

  structure = gst_caps_get_structure (caps, 0);

  /* get channel count */
  gst_structure_get_int (structure, "channels", &amrnbdec->channels);
  gst_structure_get_int (structure, "rate", &amrnbdec->rate);

  /* create reverse caps */
  copy = gst_caps_new_simple ("audio/x-raw-int",
      "channels", G_TYPE_INT, amrnbdec->channels,
      "width", G_TYPE_INT, 16,
      "depth", G_TYPE_INT, 16,
      "endianness", G_TYPE_INT, G_BYTE_ORDER,
      "rate", G_TYPE_INT, amrnbdec->rate, "signed", G_TYPE_BOOLEAN, TRUE, NULL);

  gst_pad_set_caps (amrnbdec->srcpad, copy);
  gst_caps_unref (copy);

  return TRUE;
}

static GstFlowReturn
gst_amrnbdec_chain (GstPad * pad, GstBuffer * buffer)
{
  GstAmrnbDec *amrnbdec;
  GstFlowReturn ret;

  amrnbdec = GST_AMRNBDEC (GST_PAD_PARENT (pad));

  if (amrnbdec->rate == 0 || amrnbdec->channels == 0)
    goto not_negotiated;

  if (GST_BUFFER_TIMESTAMP_IS_VALID (buffer))
    amrnbdec->ts = GST_BUFFER_TIMESTAMP (buffer);

  gst_adapter_push (amrnbdec->adapter, buffer);

  ret = GST_FLOW_OK;

  while (TRUE) {
    GstBuffer *out;
    guint8 *data;
    gint block, mode;

    if (gst_adapter_available (amrnbdec->adapter) < 1)
      break;
    data = (guint8 *) gst_adapter_peek (amrnbdec->adapter, 1);

    /* get size */
    mode = (data[0] >> 3) & 0x0F;
    block = block_size[mode] + 1;

    if (gst_adapter_available (amrnbdec->adapter) < block)
      break;
    data = (guint8 *) gst_adapter_peek (amrnbdec->adapter, block);

    /* get output */
    out = gst_buffer_new_and_alloc (160 * 2);
    GST_BUFFER_DURATION (out) = GST_SECOND * 160 /
        (amrnbdec->rate * amrnbdec->channels);
    GST_BUFFER_TIMESTAMP (out) = amrnbdec->ts;
    amrnbdec->ts += GST_BUFFER_DURATION (out);
    gst_buffer_set_caps (out, gst_pad_get_caps (amrnbdec->srcpad));

    /* decode */
    Decoder_Interface_Decode (amrnbdec->handle, data,
        (short *) GST_BUFFER_DATA (out), 0);

    gst_adapter_flush (amrnbdec->adapter, block);

    /* play */
    ret = gst_pad_push (amrnbdec->srcpad, out);
  }

  return ret;

not_negotiated:
  {
    return GST_FLOW_NOT_NEGOTIATED;
  }
}

static GstStateChangeReturn
gst_amrnbdec_state_change (GstElement * element, GstStateChange transition)
{
  GstAmrnbDec *amrnbdec;
  GstStateChangeReturn ret;

  amrnbdec = GST_AMRNBDEC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!(amrnbdec->handle = Decoder_Interface_init ()))
        return GST_STATE_CHANGE_FAILURE;
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_adapter_clear (amrnbdec->adapter);
      amrnbdec->ts = 0;
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      Decoder_Interface_exit (amrnbdec->handle);
      break;
    default:
      break;
  }

  return ret;
}
