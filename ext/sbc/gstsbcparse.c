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
#include "gstsbcutil.h"

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
        "allocation = (string) { snr, loudness },"
        "bitpool = (int) [ 2, 64 ]"));

static gboolean
sbc_parse_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstSbcParse *parse;
  GstStructure *structure;
  gint rate, channels;

  parse = GST_SBC_PARSE (GST_PAD_PARENT (pad));

  structure = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (structure, "rate", &rate))
    return FALSE;

  if (!gst_structure_get_int (structure, "channels", &channels))
    return FALSE;

  if (!(parse->rate == 0 || rate == parse->rate))
    return FALSE;

  if (!(parse->channels == 0 || channels == parse->channels))
    return FALSE;

  parse->rate = rate;
  parse->channels = channels;

  return gst_sbc_util_fill_sbc_params (&parse->sbc, caps);
}

static GstCaps *
sbc_parse_src_getcaps (GstPad * pad)
{
  GstCaps *caps;
  const GstCaps *allowed_caps;
  GstStructure *structure;
  GValue *value;
  GstSbcParse *parse = GST_SBC_PARSE (GST_PAD_PARENT (pad));

  allowed_caps = gst_pad_get_allowed_caps (pad);
  if (allowed_caps == NULL)
    allowed_caps = gst_pad_get_pad_template_caps (pad);
  caps = gst_caps_copy (allowed_caps);

  value = g_new0 (GValue, 1);

  structure = gst_caps_get_structure (caps, 0);

  if (parse->rate != 0)
    gst_sbc_util_set_structure_int_param (structure, "rate",
        parse->rate, value);
  if (parse->channels != 0)
    gst_sbc_util_set_structure_int_param (structure, "channels",
        parse->channels, value);

  g_free (value);

  return caps;
}

static gboolean
sbc_parse_src_acceptcaps (GstPad * pad, GstCaps * caps)
{
  GstStructure *structure;
  GstSbcParse *parse;
  gint rate, channels;

  parse = GST_SBC_PARSE (GST_PAD_PARENT (pad));

  structure = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (structure, "rate", &rate))
    return FALSE;
  if (!gst_structure_get_int (structure, "channels", &channels))
    return FALSE;

  if ((parse->rate == 0 || parse->rate == rate)
      && (parse->channels == 0 || parse->channels == channels))
    return TRUE;

  return FALSE;
}

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
    GstBuffer *temp;
    temp = buffer;
    buffer = gst_buffer_span (parse->buffer, 0, buffer,
        GST_BUFFER_SIZE (parse->buffer)
        + GST_BUFFER_SIZE (buffer));
    gst_buffer_unref (parse->buffer);
    gst_buffer_unref (temp);
    parse->buffer = NULL;
  }

  data = GST_BUFFER_DATA (buffer);
  size = GST_BUFFER_SIZE (buffer);

  while (offset < size) {
    GstBuffer *output;
    GstCaps *temp;
    int consumed;

    consumed = sbc_parse (&parse->sbc, data + offset, size - offset);
    if (consumed <= 0)
      break;

    temp = GST_PAD_CAPS (parse->srcpad);

    res = gst_pad_alloc_buffer_and_set_caps (parse->srcpad,
        GST_BUFFER_OFFSET_NONE, consumed, temp, &output);

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
  gst_pad_set_setcaps_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (sbc_parse_sink_setcaps));
  gst_element_add_pad (GST_ELEMENT (self), self->sinkpad);

  self->srcpad =
      gst_pad_new_from_static_template (&sbc_parse_src_factory, "src");
  gst_pad_set_getcaps_function (self->srcpad,
      GST_DEBUG_FUNCPTR (sbc_parse_src_getcaps));
  gst_pad_set_acceptcaps_function (self->srcpad,
      GST_DEBUG_FUNCPTR (sbc_parse_src_acceptcaps));
  /* FIXME get encoding parameters on set caps */
  gst_element_add_pad (GST_ELEMENT (self), self->srcpad);
}

gboolean
gst_sbc_parse_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "sbcparse",
      GST_RANK_NONE, GST_TYPE_SBC_PARSE);
}
