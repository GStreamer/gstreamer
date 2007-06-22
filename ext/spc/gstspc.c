/* Copyright (C) 2004-2005 Michael Pyne <michael dot pyne at kdemail net>
 * Copyright (C) 2004-2006 Chris Lee <clee at kde org>
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

#include "gstspc.h"

#include <string.h>

static const GstElementDetails gst_spc_dec_details =
GST_ELEMENT_DETAILS ("OpenSPC SPC decoder",
    "Codec/Audio/Decoder",
    "Uses OpenSPC to emulate an SPC processor",
    "Chris Lee <clee@kde.org>");

static GstStaticPadTemplate sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-spc"));

static GstStaticPadTemplate src_factory =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "endianness = (int) BYTE_ORDER, "
        "signed = (boolean) TRUE, "
        "width = (int) 16, "
        "depth = (int) 16, " "rate = (int) 32000, " "channels = (int) 2"));

GST_BOILERPLATE (GstSpcDec, gst_spc_dec, GstElement, GST_TYPE_ELEMENT);

static GstFlowReturn gst_spc_dec_chain (GstPad * pad, GstBuffer * buffer);
static gboolean gst_spc_dec_sink_event (GstPad * pad, GstEvent * event);
static gboolean gst_spc_dec_src_event (GstPad * pad, GstEvent * event);
static gboolean gst_spc_dec_src_query (GstPad * pad, GstQuery * query);
static GstStateChangeReturn gst_spc_dec_change_state (GstElement * element,
    GstStateChange transition);
static void spc_play (GstPad * pad);
static gboolean spc_setup (GstSpcDec * spc);

static gboolean
spc_negotiate (GstSpcDec * spc)
{
  GstCaps *allowed, *caps;
  GstStructure *structure;
  gint width = 16, depth = 16;
  gboolean sign;
  int rate = 32000;
  int channels = 2;

  allowed = gst_pad_get_allowed_caps (spc->srcpad);
  if (!allowed) {
    GST_DEBUG_OBJECT (spc, "couldn't get allowed caps");
    return FALSE;
  }

  GST_DEBUG_OBJECT (spc, "allowed caps: %" GST_PTR_FORMAT, allowed);

  structure = gst_caps_get_structure (allowed, 0);
  gst_structure_get_int (structure, "width", &width);
  gst_structure_get_int (structure, "depth", &depth);

  if (width && depth && width != depth) {
    GST_DEBUG_OBJECT (spc, "width %d and depth %d are different", width, depth);
    gst_caps_unref (allowed);
    return FALSE;
  }

  gst_structure_get_boolean (structure, "signed", &sign);
  gst_structure_get_int (structure, "rate", &rate);
  gst_structure_get_int (structure, "channels", &channels);

  caps = gst_caps_new_simple ("audio/x-raw-int",
      "endianness", G_TYPE_INT, G_BYTE_ORDER,
      "signed", G_TYPE_BOOLEAN, TRUE,
      "width", G_TYPE_INT, width,
      "depth", G_TYPE_INT, depth,
      "rate", G_TYPE_INT, rate, "channels", G_TYPE_INT, channels, NULL);
  gst_pad_set_caps (spc->srcpad, caps);

  gst_caps_unref (caps);
  gst_caps_unref (allowed);

  return TRUE;
}

static void
gst_spc_dec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &gst_spc_dec_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
}

static void
gst_spc_dec_class_init (GstSpcDecClass * klass)
{
  GstElementClass *element_class = (GstElementClass *) klass;

  element_class->change_state = GST_DEBUG_FUNCPTR (gst_spc_dec_change_state);
}

static void
gst_spc_dec_init (GstSpcDec * spc, GstSpcDecClass * klass)
{
  spc->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_pad_set_query_function (spc->sinkpad, NULL);
  gst_pad_set_event_function (spc->sinkpad, gst_spc_dec_sink_event);
  gst_pad_set_chain_function (spc->sinkpad, gst_spc_dec_chain);
  gst_element_add_pad (GST_ELEMENT (spc), spc->sinkpad);

  spc->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  gst_pad_set_event_function (spc->srcpad, gst_spc_dec_src_event);
  gst_pad_set_query_function (spc->srcpad, gst_spc_dec_src_query);
  gst_pad_use_fixed_caps (spc->srcpad);
  gst_element_add_pad (GST_ELEMENT (spc), spc->srcpad);

  spc->buf = NULL;
  spc->initialized = FALSE;
}

static GstFlowReturn
gst_spc_dec_chain (GstPad * pad, GstBuffer * buffer)
{
  GstSpcDec *spc = GST_SPC_DEC (gst_pad_get_parent (pad));

  if (spc->buf) {
    spc->buf = gst_buffer_join (spc->buf, buffer);
  } else {
    spc->buf = buffer;
  }

  gst_object_unref (spc);

  return GST_FLOW_OK;
}

static gboolean
gst_spc_dec_sink_event (GstPad * pad, GstEvent * event)
{
  GstSpcDec *spc = GST_SPC_DEC (gst_pad_get_parent (pad));
  gboolean result;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      result = spc_setup (spc);
      break;
    case GST_EVENT_NEWSEGMENT:
      result = FALSE;
      break;
    default:
      result = FALSE;
      break;
  }

  gst_event_unref (event);
  gst_object_unref (spc);

  return result;
}

static gboolean
gst_spc_dec_src_event (GstPad * pad, GstEvent * event)
{
  GstSpcDec *spc = GST_SPC_DEC (gst_pad_get_parent (pad));
  gboolean result = FALSE;

  switch (GST_EVENT_TYPE (event)) {
    default:
      break;
  }

  gst_event_unref (event);
  gst_object_unref (spc);

  return result;
}

static gboolean
gst_spc_dec_src_query (GstPad * pad, GstQuery * query)
{
  GstSpcDec *spc = GST_SPC_DEC (gst_pad_get_parent (pad));
  gboolean result = TRUE;

  switch (GST_QUERY_TYPE (query)) {
    default:
      result = gst_pad_query_default (pad, query);
      break;
  }

  gst_object_unref (spc);

  return result;
}

static void
spc_play (GstPad * pad)
{
  GstSpcDec *spc = GST_SPC_DEC (gst_pad_get_parent (pad));
  GstFlowReturn flow_return;
  GstBuffer *out;

  out = gst_buffer_new_and_alloc (1600 * 4);
  gst_buffer_set_caps (out, GST_PAD_CAPS (pad));

  OSPC_Run (-1, (short *) GST_BUFFER_DATA (out), 1600 * 4);

  if ((flow_return = gst_pad_push (spc->srcpad, out)) != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (spc, "pausing task, reason %s",
        gst_flow_get_name (flow_return));

    gst_pad_pause_task (pad);

    if (GST_FLOW_IS_FATAL (flow_return) || flow_return == GST_FLOW_NOT_LINKED) {
      gst_pad_push_event (pad, gst_event_new_eos ());
    }
  }

  gst_object_unref (spc);

  return;
}

static gboolean
spc_setup (GstSpcDec * spc)
{
  guchar *data = GST_BUFFER_DATA (spc->buf);
  gboolean text_format = FALSE;

  if (!spc_negotiate (spc)) {
    return FALSE;
  }

  if (data[0x23] == 26) {
    GstEvent *tagevent;
    GstTagList *taglist = gst_tag_list_new ();

    if (data[0xA0] == '/')
      text_format = TRUE;

    gchar spctitle[0x21];
    gchar spcartist[0x21];
    gchar spcgame[0x21];

    strncpy (spctitle, (gchar *) & data[0x2E], 32);
    strncpy (spcartist, (gchar *) & data[(text_format ? 0xB1 : 0xB0)], 32);
    strncpy (spcgame, (gchar *) & data[0x4E], 32);

    spctitle[0x20] = '\0';
    spcartist[0x20] = '\0';
    spcgame[0x20] = '\0';

    gst_tag_list_add (taglist, GST_TAG_MERGE_APPEND,
        GST_TAG_TITLE, spctitle,
        GST_TAG_ARTIST, spcartist, GST_TAG_ALBUM, spcgame, NULL);

    tagevent = gst_event_new_tag (taglist);
    gst_element_found_tags_for_pad (GST_ELEMENT (spc), spc->srcpad, taglist);
  }

  if (OSPC_Init (GST_BUFFER_DATA (spc->buf), GST_BUFFER_SIZE (spc->buf)) != 0) {
    return FALSE;
  }

  gst_pad_push_event (spc->srcpad, gst_event_new_new_segment (FALSE, 1.0,
          GST_FORMAT_TIME, 0, -1, 0));

  gst_pad_start_task (spc->srcpad, (GstTaskFunction) spc_play, spc->srcpad);

  gst_buffer_unref (spc->buf);
  spc->buf = NULL;
  spc->initialized = TRUE;
  return spc->initialized;
}

static GstStateChangeReturn
gst_spc_dec_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn result;
  GstSpcDec *dec;

  dec = GST_SPC_DEC (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    default:
      break;
  }

  result = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (result == GST_STATE_CHANGE_FAILURE)
    return result;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (dec->buf) {
        gst_buffer_unref (dec->buf);
        dec->buf = NULL;
      }
      break;
    default:
      break;
  }

  return result;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "spcdec", GST_RANK_PRIMARY,
      GST_TYPE_SPC_DEC);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "spcdec",
    "OpenSPC Audio Decoder",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
