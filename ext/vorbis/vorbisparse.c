/* GStreamer
 * Copyright (C) <2004> Thomas Vander Stichele <thomas at apestaart dot org>
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
#  include "config.h"
#endif

#include "vorbisparse.h"

GST_DEBUG_CATEGORY_EXTERN (vorbisparse_debug);
#define GST_CAT_DEFAULT vorbisparse_debug

static GstElementDetails vorbis_parse_details = {
  "VorbisParse",
  "Codec/Parser/Audio",
  "parse raw vorbis streams",
  "Thomas Vander Stichele <thomas at apestaart dot org>"
};

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0
};

static GstStaticPadTemplate vorbis_parse_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-vorbis")
    );

static GstStaticPadTemplate vorbis_parse_src_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-vorbis")
    );

GST_BOILERPLATE (GstVorbisParse, gst_vorbis_parse, GstElement,
    GST_TYPE_ELEMENT);

static void vorbis_parse_chain (GstPad * pad, GstData * data);
static GstElementStateReturn vorbis_parse_change_state (GstElement * element);

static void
gst_vorbis_parse_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&vorbis_parse_src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&vorbis_parse_sink_factory));
  gst_element_class_set_details (element_class, &vorbis_parse_details);
}

static void
gst_vorbis_parse_class_init (GstVorbisParseClass * klass)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gstelement_class->change_state = vorbis_parse_change_state;
}

static void
gst_vorbis_parse_init (GstVorbisParse * parse)
{
  parse->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&vorbis_parse_sink_factory), "sink");
  gst_pad_set_chain_function (parse->sinkpad, vorbis_parse_chain);
  gst_element_add_pad (GST_ELEMENT (parse), parse->sinkpad);

  parse->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&vorbis_parse_src_factory), "src");
  gst_element_add_pad (GST_ELEMENT (parse), parse->srcpad);
}
static void
vorbis_parse_set_header_on_caps (GstVorbisParse * parse, GstCaps * caps)
{
  GstBuffer *buf1, *buf2, *buf3;

  g_assert (parse);
  g_assert (parse->streamheader);
  g_assert (parse->streamheader->next);
  g_assert (parse->streamheader->next->next);
  buf1 = parse->streamheader->data;
  g_assert (buf1);
  buf2 = parse->streamheader->next->data;
  g_assert (buf2);
  buf3 = parse->streamheader->next->next->data;
  g_assert (buf3);

  GstStructure *structure = gst_caps_get_structure (caps, 0);
  GValue list = { 0 };
  GValue value = { 0 };

  /* mark buffers */
  GST_BUFFER_FLAG_SET (buf1, GST_BUFFER_IN_CAPS);
  GST_BUFFER_FLAG_SET (buf2, GST_BUFFER_IN_CAPS);
  GST_BUFFER_FLAG_SET (buf3, GST_BUFFER_IN_CAPS);

  /* put buffers in a fixed list */
  g_value_init (&list, GST_TYPE_FIXED_LIST);
  g_value_init (&value, GST_TYPE_BUFFER);
  g_value_set_boxed (&value, buf1);
  gst_value_list_append_value (&list, &value);
  g_value_unset (&value);
  g_value_init (&value, GST_TYPE_BUFFER);
  g_value_set_boxed (&value, buf2);
  gst_value_list_append_value (&list, &value);
  g_value_unset (&value);
  g_value_init (&value, GST_TYPE_BUFFER);
  g_value_set_boxed (&value, buf3);
  gst_value_list_append_value (&list, &value);
  gst_structure_set_value (structure, "streamheader", &list);
  g_value_unset (&value);
  g_value_unset (&list);
}

static void
vorbis_parse_chain (GstPad * pad, GstData * data)
{
  GstBuffer *buf;
  GstVorbisParse *parse;

  parse = GST_VORBIS_PARSE (gst_pad_get_parent (pad));
  g_assert (parse);

  buf = GST_BUFFER (data);
  parse->packetno++;

  /* if 1 <= packetno <= 3, it's streamheader,
   * so put it on the streamheader list and return */
  if (parse->packetno <= 3) {
    parse->streamheader = g_list_append (parse->streamheader, buf);
    return;
  }

  /* else, if we haven't sent streamheader buffers yet,
   * set caps again, and send out the streamheader buffers */
  if (!parse->streamheader_sent) {
    /* mark and put on caps */
    GstCaps *caps = gst_pad_get_caps (parse->srcpad);

    vorbis_parse_set_header_on_caps (parse, caps);

    /* negotiate with these caps */
    GST_DEBUG ("here are the caps: %" GST_PTR_FORMAT, caps);
    gst_pad_try_set_caps (parse->srcpad, caps);

    /* push out buffers */
    gst_pad_push (parse->srcpad, parse->streamheader->data);
    gst_pad_push (parse->srcpad, parse->streamheader->next->data);
    gst_pad_push (parse->srcpad, parse->streamheader->next->next->data);

    parse->streamheader_sent = TRUE;
  }
  /* just send on buffer by default */
  gst_pad_push (parse->srcpad, data);
}

static GstElementStateReturn
vorbis_parse_change_state (GstElement * element)
{
  GstVorbisParse *parse = GST_VORBIS_PARSE (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_PAUSED_TO_READY:
      parse->packetno = 0;
      break;
    default:
      break;
  }

  return parent_class->change_state (element);
}
