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

#include "gstsbcdec.h"

GST_DEBUG_CATEGORY_STATIC (sbc_dec_debug);
#define GST_CAT_DEFAULT sbc_dec_debug

GST_BOILERPLATE (GstSbcDec, gst_sbc_dec, GstElement, GST_TYPE_ELEMENT);

static const GstElementDetails sbc_dec_details =
GST_ELEMENT_DETAILS ("Bluetooth SBC decoder",
    "Codec/Decoder/Audio",
    "Decode a SBC audio stream",
    "Marcel Holtmann <marcel@holtmann.org>");

static GstStaticPadTemplate sbc_dec_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-sbc"));

static GstStaticPadTemplate sbc_dec_src_factory =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "rate = (int) { 16000, 32000, 44100, 48000 }, "
        "channels = (int) [ 1, 2 ], "
        "endianness = (int) LITTLE_ENDIAN, "
        "signed = (boolean) true, " "width = (int) 16, " "depth = (int) 16"));

static GstFlowReturn
sbc_dec_chain (GstPad * pad, GstBuffer * buffer)
{
  GstSbcDec *dec = GST_SBC_DEC (gst_pad_get_parent (pad));
  GstFlowReturn res = GST_FLOW_OK;
  guint size, codesize, offset = 0;
  guint8 *data;
  GstClockTime timestamp;

  codesize = sbc_get_codesize (&dec->sbc);
  timestamp = GST_BUFFER_TIMESTAMP (buffer);

  if (dec->buffer) {
    GstBuffer *temp = buffer;
    buffer = gst_buffer_span (dec->buffer, 0, buffer,
        GST_BUFFER_SIZE (dec->buffer) + GST_BUFFER_SIZE (buffer));
    gst_buffer_unref (temp);
    gst_buffer_unref (dec->buffer);
    dec->buffer = NULL;
  }

  data = GST_BUFFER_DATA (buffer);
  size = GST_BUFFER_SIZE (buffer);

  while (offset < size) {
    GstBuffer *output;
    GstPadTemplate *template;
    GstCaps *caps, *temp;
    int consumed;

    caps = gst_caps_new_simple ("audio/x-raw-int",
        "rate", G_TYPE_INT, dec->sbc.rate,
        "channels", G_TYPE_INT, dec->sbc.channels, NULL);

    template = gst_static_pad_template_get (&sbc_dec_src_factory);

    temp = gst_caps_intersect (caps, gst_pad_template_get_caps (template));

    gst_caps_unref (caps);

    res = gst_pad_alloc_buffer_and_set_caps (dec->srcpad,
        GST_BUFFER_OFFSET_NONE, codesize, temp, &output);

    gst_caps_unref (temp);

    if (res != GST_FLOW_OK)
      goto done;

    consumed = sbc_decode (&dec->sbc, data + offset, size - offset,
        GST_BUFFER_DATA (output), codesize, NULL);
    if (consumed <= 0)
      break;

    GST_BUFFER_TIMESTAMP (output) = GST_BUFFER_TIMESTAMP (buffer);

    res = gst_pad_push (dec->srcpad, output);
    if (res != GST_FLOW_OK)
      goto done;

    offset += consumed;
  }

  if (offset < size)
    dec->buffer = gst_buffer_create_sub (buffer, offset, size - offset);

done:
  gst_buffer_unref (buffer);
  gst_object_unref (dec);

  return res;
}

static GstStateChangeReturn
sbc_dec_change_state (GstElement * element, GstStateChange transition)
{
  GstSbcDec *dec = GST_SBC_DEC (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      GST_DEBUG ("Setup subband codec");
      if (dec->buffer) {
        gst_buffer_unref (dec->buffer);
        dec->buffer = NULL;
      }
      sbc_init (&dec->sbc, 0);
      break;

    case GST_STATE_CHANGE_PAUSED_TO_READY:
      GST_DEBUG ("Finish subband codec");
      if (dec->buffer) {
        gst_buffer_unref (dec->buffer);
        dec->buffer = NULL;
      }
      sbc_finish (&dec->sbc);
      break;

    default:
      break;
  }

  return parent_class->change_state (element, transition);
}

static void
gst_sbc_dec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sbc_dec_sink_factory));

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sbc_dec_src_factory));

  gst_element_class_set_details (element_class, &sbc_dec_details);
}

static void
gst_sbc_dec_class_init (GstSbcDecClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  element_class->change_state = GST_DEBUG_FUNCPTR (sbc_dec_change_state);

  GST_DEBUG_CATEGORY_INIT (sbc_dec_debug, "sbcdec", 0, "SBC decoding element");
}

static void
gst_sbc_dec_init (GstSbcDec * self, GstSbcDecClass * klass)
{
  self->sinkpad =
      gst_pad_new_from_static_template (&sbc_dec_sink_factory, "sink");
  gst_pad_set_chain_function (self->sinkpad, GST_DEBUG_FUNCPTR (sbc_dec_chain));
  gst_element_add_pad (GST_ELEMENT (self), self->sinkpad);

  self->srcpad = gst_pad_new_from_static_template (&sbc_dec_src_factory, "src");
  gst_element_add_pad (GST_ELEMENT (self), self->srcpad);
}
