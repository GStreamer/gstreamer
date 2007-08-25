/*
 *
 *  BlueZ - Bluetooth protocol stack for Linux
 *
 *  Copyright (C) 2004-2007  Marcel Holtmann <marcel@holtmann.org>
 *
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include "gstsbcenc.h"

GST_DEBUG_CATEGORY_STATIC (sbc_enc_debug);
#define GST_CAT_DEFAULT sbc_enc_debug

GST_BOILERPLATE (GstSbcEnc, gst_sbc_enc, GstElement, GST_TYPE_ELEMENT);

static const GstElementDetails sbc_enc_details =
GST_ELEMENT_DETAILS ("Bluetooth SBC encoder",
    "Codec/Encoder/Audio",
    "Encode a SBC audio stream",
    "Marcel Holtmann <marcel@holtmann.org>");

static GstStaticPadTemplate sbc_enc_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "rate = (int) { 16000, 32000, 44100, 48000 }, "
        "channels = (int) [ 1, 2 ], "
        "endianness = (int) BYTE_ORDER, "
        "signed = (boolean) true, " "width = (int) 16, " "depth = (int) 16"));

static GstStaticPadTemplate sbc_enc_src_factory =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-sbc"));

static GstFlowReturn
sbc_enc_chain (GstPad * pad, GstBuffer * buffer)
{
  GstSbcEnc *enc = GST_SBC_ENC (gst_pad_get_parent (pad));
  GstFlowReturn res = GST_FLOW_OK;
  GstStructure *structure;
  gint rate, channels;
  guint size, offset = 0;
  guint8 *data;

  data = GST_BUFFER_DATA (buffer);
  size = GST_BUFFER_SIZE (buffer);

  structure = gst_caps_get_structure (GST_PAD_CAPS (pad), 0);
  gst_structure_get_int (structure, "rate", &rate);
  gst_structure_get_int (structure, "channels", &channels);

  enc->sbc.rate = rate;
  enc->sbc.channels = channels;
  enc->sbc.subbands = 8;
  enc->sbc.joint = 0;

  while (offset < size) {
    GstBuffer *output;
    GstCaps *caps;
    int consumed;

    consumed = sbc_encode (&enc->sbc, data + offset, size - offset);
    if (consumed <= 0)
      break;

    caps = GST_PAD_CAPS (enc->srcpad);

    res = gst_pad_alloc_buffer_and_set_caps (enc->srcpad,
        GST_BUFFER_OFFSET_NONE, enc->sbc.len, caps, &output);

    if (res != GST_FLOW_OK)
      goto done;

    memcpy (GST_BUFFER_DATA (output), enc->sbc.data, enc->sbc.len);

    res = gst_pad_push (enc->srcpad, output);
    if (res != GST_FLOW_OK)
      goto done;

    offset += consumed;
  }

  if (offset < size)
    res = GST_FLOW_ERROR;

done:
  gst_buffer_unref (buffer);
  gst_object_unref (enc);

  return res;
}

static GstStateChangeReturn
sbc_enc_change_state (GstElement * element, GstStateChange transition)
{
  GstSbcEnc *enc = GST_SBC_ENC (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      GST_DEBUG ("Setup subband codec");
      sbc_init (&enc->sbc, 0);
      break;

    case GST_STATE_CHANGE_PAUSED_TO_READY:
      GST_DEBUG ("Finish subband codec");
      sbc_finish (&enc->sbc);
      break;

    default:
      break;
  }

  return parent_class->change_state (element, transition);
}

static void
gst_sbc_enc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sbc_enc_sink_factory));

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sbc_enc_src_factory));

  gst_element_class_set_details (element_class, &sbc_enc_details);
}

static void
gst_sbc_enc_class_init (GstSbcEncClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  element_class->change_state = GST_DEBUG_FUNCPTR (sbc_enc_change_state);

  GST_DEBUG_CATEGORY_INIT (sbc_enc_debug, "sbcenc", 0, "SBC encoding element");
}

static void
gst_sbc_enc_init (GstSbcEnc * self, GstSbcEncClass * klass)
{
  self->sinkpad =
      gst_pad_new_from_static_template (&sbc_enc_sink_factory, "sink");
  gst_element_add_pad (GST_ELEMENT (self), self->sinkpad);

  self->srcpad = gst_pad_new_from_static_template (&sbc_enc_src_factory, "src");
  gst_pad_set_chain_function (self->sinkpad, GST_DEBUG_FUNCPTR (sbc_enc_chain));
  gst_element_add_pad (GST_ELEMENT (self), self->srcpad);
}
