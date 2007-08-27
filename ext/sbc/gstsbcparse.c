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

#include "gstsbcparse.h"

GST_DEBUG_CATEGORY_STATIC (sbc_parse_debug);
#define GST_CAT_DEFAULT sbc_parse_debug

GST_BOILERPLATE (GstSbcParse, gst_sbc_parse, GstElement, GST_TYPE_ELEMENT);

static const GstElementDetails sbc_parse_details =
GST_ELEMENT_DETAILS ("Bluetooth SBC parser",
    "Codec/Parser/Audio",
    "Parse a SBC audio stream",
    "Marcel Holtmann <marcel@holtmann.org>");

static GstStaticPadTemplate sbc_parse_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-sbc"));

static GstStaticPadTemplate sbc_parse_src_factory =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-sbc, "
        "rate = (int) { 16000, 32000, 44100, 48000 }, "
        "channels = (int) [ 1, 2 ], "
        "mode = (string) { mono, dual, stereo, joint }, "
        "blocks = (int) { 4, 8, 12, 16 }, "
        "subbands = (int) { 4, 8 }, "
        "allocation = (string) { snr, loudness }"));

static GstFlowReturn
sbc_parse_chain (GstPad * pad, GstBuffer * buffer)
{
  GstSbcParse *parse = GST_SBC_PARSE (gst_pad_get_parent (pad));
  GstFlowReturn res = GST_FLOW_OK;
  guint size, offset = 0;
  guint8 *data;
  GstClockTime timestamp;

  timestamp = GST_BUFFER_TIMESTAMP (buffer);

  if (parse->buffer) {
    GstBuffer *temp = buffer;
    buffer = gst_buffer_span (parse->buffer, 0, buffer,
        GST_BUFFER_SIZE (parse->buffer) + GST_BUFFER_SIZE (buffer));
    gst_buffer_unref (temp);
    gst_buffer_unref (parse->buffer);
    parse->buffer = NULL;
  }

  data = GST_BUFFER_DATA (buffer);
  size = GST_BUFFER_SIZE (buffer);

  while (offset < size) {
    GstBuffer *output;
    GstPadTemplate *template;
    GstCaps *caps, *temp;
    int consumed;

    consumed = sbc_parse (&parse->sbc, data + offset, size - offset);
    if (consumed <= 0)
      break;

    caps = gst_caps_new_simple ("audio/x-sbc",
        "rate", G_TYPE_INT, parse->sbc.rate,
        "channels", G_TYPE_INT, parse->sbc.channels, NULL);

    template = gst_static_pad_template_get (&sbc_parse_src_factory);

    temp = gst_caps_intersect (caps, gst_pad_template_get_caps (template));

    gst_caps_unref (caps);

    res = gst_pad_alloc_buffer_and_set_caps (parse->srcpad,
        GST_BUFFER_OFFSET_NONE, consumed, temp, &output);

    gst_caps_unref (temp);

    if (res != GST_FLOW_OK)
      goto done;

    memcpy (GST_BUFFER_DATA (output), data + offset, consumed);

    res = gst_pad_push (parse->srcpad, output);
    if (res != GST_FLOW_OK)
      goto done;

    offset += consumed;
  }

  if (offset < size)
    parse->buffer = gst_buffer_create_sub (buffer, offset, size - offset);

done:
  gst_buffer_unref (buffer);
  gst_object_unref (parse);

  return res;
}

static GstStateChangeReturn
sbc_parse_change_state (GstElement * element, GstStateChange transition)
{
  GstSbcParse *parse = GST_SBC_PARSE (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      GST_DEBUG ("Setup subband codec");
      if (parse->buffer) {
        gst_buffer_unref (parse->buffer);
        parse->buffer = NULL;
      }
      sbc_init (&parse->sbc, 0);
      break;

    case GST_STATE_CHANGE_PAUSED_TO_READY:
      GST_DEBUG ("Finish subband codec");
      if (parse->buffer) {
        gst_buffer_unref (parse->buffer);
        parse->buffer = NULL;
      }
      sbc_finish (&parse->sbc);
      break;

    default:
      break;
  }

  return parent_class->change_state (element, transition);
}

static void
gst_sbc_parse_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sbc_parse_sink_factory));

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sbc_parse_src_factory));

  gst_element_class_set_details (element_class, &sbc_parse_details);
}

static void
gst_sbc_parse_class_init (GstSbcParseClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  element_class->change_state = GST_DEBUG_FUNCPTR (sbc_parse_change_state);

  GST_DEBUG_CATEGORY_INIT (sbc_parse_debug, "sbcparse", 0,
      "SBC parsing element");
}

static void
gst_sbc_parse_init (GstSbcParse * self, GstSbcParseClass * klass)
{
  self->sinkpad =
      gst_pad_new_from_static_template (&sbc_parse_sink_factory, "sink");
  gst_pad_set_chain_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (sbc_parse_chain));
  gst_element_add_pad (GST_ELEMENT (self), self->sinkpad);

  self->srcpad =
      gst_pad_new_from_static_template (&sbc_parse_src_factory, "src");
  gst_element_add_pad (GST_ELEMENT (self), self->srcpad);
}
