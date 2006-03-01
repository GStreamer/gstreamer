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

/**
 * SECTION:element-vorbisparse
 * @short_description: parses vorbis streams 
 * @see_also: vorbisdec, oggdemux
 *
 * <refsect2>
 * <para>
 * The vorbisparse element will parse the header packets of the Vorbis
 * stream and put them as the streamheader in the caps. This is used in the
 * multifdsink case where you want to stream live vorbis streams to multiple
 * clients, each client has to receive the streamheaders first before they can
 * consume the vorbis packets.
 * </para>
 * <title>Example pipelines</title>
 * <para>
 * <programlisting>
 * gst-launch -v filesrc location=sine.ogg ! oggdemux ! vorbisparse ! fakesink
 * </programlisting>
 * This pipeline shows that the streamheader is set in the caps.
 * </para>
 * </refsect2>
 *
 * Last reviewed on 2006-03-01 (0.10.4)
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

static GstFlowReturn vorbis_parse_chain (GstPad * pad, GstBuffer * buffer);
static GstStateChangeReturn vorbis_parse_change_state (GstElement * element,
    GstStateChange transition);

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
gst_vorbis_parse_init (GstVorbisParse * parse, GstVorbisParseClass * g_class)
{
  parse->sinkpad =
      gst_pad_new_from_static_template (&vorbis_parse_sink_factory, "sink");
  gst_pad_set_chain_function (parse->sinkpad, vorbis_parse_chain);
  gst_element_add_pad (GST_ELEMENT (parse), parse->sinkpad);

  parse->srcpad =
      gst_pad_new_from_static_template (&vorbis_parse_src_factory, "src");
  gst_element_add_pad (GST_ELEMENT (parse), parse->srcpad);
}

static void
vorbis_parse_set_header_on_caps (GstVorbisParse * parse, GstCaps * caps)
{
  GstBuffer *buf1, *buf2, *buf3;
  GstStructure *structure;
  GValue array = { 0 };
  GValue value = { 0 };

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

  structure = gst_caps_get_structure (caps, 0);

  /* mark buffers */
  GST_BUFFER_FLAG_SET (buf1, GST_BUFFER_FLAG_IN_CAPS);
  GST_BUFFER_FLAG_SET (buf2, GST_BUFFER_FLAG_IN_CAPS);
  GST_BUFFER_FLAG_SET (buf3, GST_BUFFER_FLAG_IN_CAPS);

  /* put buffers in a fixed list */
  g_value_init (&array, GST_TYPE_ARRAY);
  g_value_init (&value, GST_TYPE_BUFFER);
  gst_value_set_buffer (&value, buf1);
  gst_value_array_append_value (&array, &value);
  g_value_unset (&value);
  g_value_init (&value, GST_TYPE_BUFFER);
  gst_value_set_buffer (&value, buf2);
  gst_value_array_append_value (&array, &value);
  g_value_unset (&value);
  g_value_init (&value, GST_TYPE_BUFFER);
  gst_value_set_buffer (&value, buf3);
  gst_value_array_append_value (&array, &value);
  gst_structure_set_value (structure, "streamheader", &array);
  g_value_unset (&value);
  g_value_unset (&array);
}

static GstFlowReturn
vorbis_parse_chain (GstPad * pad, GstBuffer * buffer)
{
  GstFlowReturn ret;
  GstBuffer *buf;
  GstVorbisParse *parse;

  parse = GST_VORBIS_PARSE (GST_PAD_PARENT (pad));

  buf = GST_BUFFER (buffer);
  parse->packetno++;

  /* if 1 <= packetno <= 3, it's streamheader,
   * so put it on the streamheader list and return */
  if (parse->packetno <= 3) {
    parse->streamheader = g_list_append (parse->streamheader, buf);
    return GST_FLOW_OK;
  }

  /* else, if we haven't sent streamheader buffers yet,
   * set caps again, and send out the streamheader buffers */
  if (!parse->streamheader_sent) {
    /* mark and put on caps */
    GstCaps *padcaps, *caps;
    GstBuffer *outbuf;

    padcaps = gst_pad_get_caps (parse->srcpad);
    caps = gst_caps_make_writable (padcaps);
    gst_caps_unref (padcaps);

    vorbis_parse_set_header_on_caps (parse, caps);

    /* negotiate with these caps */
    GST_DEBUG_OBJECT (parse, "here are the caps: %" GST_PTR_FORMAT, caps);
    gst_pad_set_caps (parse->srcpad, caps);
    gst_caps_unref (caps);

    /* push out buffers, ignoring return value... */
    outbuf = GST_BUFFER_CAST (parse->streamheader->data);
    gst_buffer_set_caps (outbuf, GST_PAD_CAPS (parse->srcpad));
    gst_pad_push (parse->srcpad, outbuf);
    outbuf = GST_BUFFER_CAST (parse->streamheader->next->data);
    gst_buffer_set_caps (outbuf, GST_PAD_CAPS (parse->srcpad));
    gst_pad_push (parse->srcpad, outbuf);
    outbuf = GST_BUFFER_CAST (parse->streamheader->next->next->data);
    gst_buffer_set_caps (outbuf, GST_PAD_CAPS (parse->srcpad));
    gst_pad_push (parse->srcpad, outbuf);

    g_list_free (parse->streamheader);
    parse->streamheader = NULL;

    parse->streamheader_sent = TRUE;
  }
  /* just send on buffer by default */
  gst_buffer_set_caps (buf, GST_PAD_CAPS (parse->srcpad));
  ret = gst_pad_push (parse->srcpad, buf);

  return ret;
}

static GstStateChangeReturn
vorbis_parse_change_state (GstElement * element, GstStateChange transition)
{
  GstVorbisParse *parse = GST_VORBIS_PARSE (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      parse->packetno = 0;
      parse->streamheader_sent = FALSE;
      break;
    default:
      break;
  }
  ret = parent_class->change_state (element, transition);

  return ret;
}
