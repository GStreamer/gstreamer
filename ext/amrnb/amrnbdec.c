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
    GST_STATIC_CAPS ("audio/x-amr-nb, "
        "rate = (int) [ 1000, 96000 ], " "channels = (int) [ 1, 2 ]")
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "width = (int) 16, "
        "depth = (int) 16, "
        "signed = (boolean) TRUE, "
        "endianness = (int) BYTE_ORDER, "
        "rate = (int) [ 1000, 96000 ]," "channels = (int) [ 1, 2 ]")
    );

static void gst_amrnbdec_base_init (GstAmrnbDecClass * klass);
static void gst_amrnbdec_class_init (GstAmrnbDecClass * klass);
static void gst_amrnbdec_init (GstAmrnbDec * amrnbdec);

static void gst_amrnbdec_chain (GstPad * pad, GstData * data);
static GstPadLinkReturn gst_amrnbdec_link (GstPad * pad, const GstCaps * caps);
static GstElementStateReturn gst_amrnbdec_state_change (GstElement * element);

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
  gst_pad_set_link_function (amrnbdec->sinkpad, gst_amrnbdec_link);
  gst_pad_set_chain_function (amrnbdec->sinkpad, gst_amrnbdec_chain);
  gst_element_add_pad (GST_ELEMENT (amrnbdec), amrnbdec->sinkpad);

  /* create the src pad */
  amrnbdec->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&src_template),
      "src");
  gst_pad_use_explicit_caps (amrnbdec->srcpad);
  gst_element_add_pad (GST_ELEMENT (amrnbdec), amrnbdec->srcpad);

  /* init rest */
  amrnbdec->handle = NULL;
  amrnbdec->channels = 0;
  amrnbdec->rate = 0;
  amrnbdec->ts = 0;
}

static GstPadLinkReturn
gst_amrnbdec_link (GstPad * pad, const GstCaps * caps)
{
  GstStructure *structure = gst_caps_get_structure (caps, 0);
  GstAmrnbDec *amrnbdec = GST_AMRNBDEC (gst_pad_get_parent (pad));
  GstCaps *copy;

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
  if (!gst_pad_set_explicit_caps (amrnbdec->srcpad, copy))
    return GST_PAD_LINK_REFUSED;

  return GST_PAD_LINK_OK;
}

static void
gst_amrnbdec_chain (GstPad * pad, GstData * in_data)
{
  const gint block_size[16] = { 12, 13, 15, 17, 19, 20, 26, 31, 5,
    0, 0, 0, 0, 0, 0, 0
  };
  GstAmrnbDec *amrnbdec = GST_AMRNBDEC (GST_OBJECT_PARENT (pad));
  GstBuffer *buf = gst_buffer_copy_on_write (in_data), *out;
  guint8 *data = GST_BUFFER_DATA (buf);
  gint size = GST_BUFFER_SIZE (buf), block, mode;

  if (GST_BUFFER_TIMESTAMP_IS_VALID (buf))
    amrnbdec->ts = GST_BUFFER_TIMESTAMP (buf);

  while (size >= 1) {
    /* get size */
    mode = (data[0] >> 3) & 0x0F;
    block = block_size[mode] + 1;
    if (size < block)
      break;

    /* get output */
    out = gst_buffer_new_and_alloc (160 * 2);
    GST_BUFFER_DURATION (out) = GST_SECOND * 160 /
        (amrnbdec->rate * amrnbdec->channels);
    GST_BUFFER_TIMESTAMP (out) = amrnbdec->ts;
    amrnbdec->ts += GST_BUFFER_DURATION (out);

    /* decode */
    Decoder_Interface_Decode (amrnbdec->handle, data,
        (short *) GST_BUFFER_DATA (out), 0);
    data += block;
    size -= block;

    /* play */
    gst_pad_push (amrnbdec->srcpad, GST_DATA (out));
  }

  gst_buffer_unref (buf);
}

static GstElementStateReturn
gst_amrnbdec_state_change (GstElement * element)
{
  GstAmrnbDec *amrnbdec = GST_AMRNBDEC (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      if (!(amrnbdec->handle = Decoder_Interface_init ()))
        return GST_STATE_FAILURE;
      break;
    case GST_STATE_PAUSED_TO_READY:
      amrnbdec->ts = 0;
      break;
    case GST_STATE_READY_TO_NULL:
      Decoder_Interface_exit (amrnbdec->handle);
      break;
    default:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}
