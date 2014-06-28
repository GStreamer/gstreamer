/* GStreamer
 * Copyright (C) <2004> Thomas Vander Stichele <thomas at apestaart dot org>
 * Copyright (C) 2006 Andy Wingo <wingo@pobox.com>
 * Copyright (C) 2008 Vincent Penquerc'h <ogg.k.ogg.k@googlemail.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:element-kateparse
 * @short_description: parses kate streams
 * @see_also: katedec, vorbisparse, oggdemux, theoraparse
 *
 * <refsect2>
 * <para>
 * The kateparse element will parse the header packets of the Kate
 * stream and put them as the streamheader in the caps. This is used in the
 * multifdsink case where you want to stream live kate streams to multiple
 * clients, each client has to receive the streamheaders first before they can
 * consume the kate packets.
 * </para>
 * <para>
 * This element also makes sure that the buffers that it pushes out are properly
 * timestamped and that their offset and offset_end are set. The buffers that
 * kateparse outputs have all of the metadata that oggmux expects to receive,
 * which allows you to (for example) remux an ogg/kate file.
 * </para>
 * <title>Example pipelines</title>
 * <para>
 * <programlisting>
 * gst-launch -v filesrc location=kate.ogg ! oggdemux ! kateparse ! fakesink
 * </programlisting>
 * This pipeline shows that the streamheader is set in the caps, and that each
 * buffer has the timestamp, duration, offset, and offset_end set.
 * </para>
 * <para>
 * <programlisting>
 * gst-launch filesrc location=kate.ogg ! oggdemux ! kateparse \
 *            ! oggmux ! filesink location=kate-remuxed.ogg
 * </programlisting>
 * This pipeline shows remuxing. kate-remuxed.ogg might not be exactly the same
 * as kate.ogg, but they should produce exactly the same decoded data.
 * </para>
 * </refsect2>
 *
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gstkate.h"
#include "gstkateutil.h"
#include "gstkateparse.h"

GST_DEBUG_CATEGORY_EXTERN (gst_kateparse_debug);
#define GST_CAT_DEFAULT gst_kateparse_debug

static GstStaticPadTemplate gst_kate_parse_sink_factory =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("subtitle/x-kate; application/x-kate")
    );

static GstStaticPadTemplate gst_kate_parse_src_factory =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("subtitle/x-kate; application/x-kate")
    );

#define gst_kate_parse_parent_class parent_class
G_DEFINE_TYPE (GstKateParse, gst_kate_parse, GST_TYPE_ELEMENT);

static GstFlowReturn gst_kate_parse_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer);
static GstStateChangeReturn gst_kate_parse_change_state (GstElement * element,
    GstStateChange transition);
static gboolean gst_kate_parse_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static gboolean gst_kate_parse_src_query (GstPad * pad, GstObject * parent,
    GstQuery * query);
#if 0
static gboolean gst_kate_parse_convert (GstPad * pad,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value);
#endif
static GstFlowReturn gst_kate_parse_parse_packet (GstKateParse * parse,
    GstBuffer * buf);

static void
gst_kate_parse_class_init (GstKateParseClass * klass)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gstelement_class->change_state = gst_kate_parse_change_state;

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_kate_parse_src_factory));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_kate_parse_sink_factory));

  gst_element_class_set_static_metadata (gstelement_class, "Kate stream parser",
      "Codec/Parser/Subtitle",
      "parse raw kate streams",
      "Vincent Penquerc'h <ogg.k.ogg.k at googlemail dot com>");

  klass->parse_packet = GST_DEBUG_FUNCPTR (gst_kate_parse_parse_packet);
}

static void
gst_kate_parse_init (GstKateParse * parse)
{
  parse->sinkpad =
      gst_pad_new_from_static_template (&gst_kate_parse_sink_factory, "sink");
  gst_pad_set_chain_function (parse->sinkpad,
      GST_DEBUG_FUNCPTR (gst_kate_parse_chain));
  gst_pad_set_event_function (parse->sinkpad,
      GST_DEBUG_FUNCPTR (gst_kate_parse_sink_event));
  gst_element_add_pad (GST_ELEMENT (parse), parse->sinkpad);

  parse->srcpad =
      gst_pad_new_from_static_template (&gst_kate_parse_src_factory, "src");
  gst_pad_set_query_function (parse->srcpad,
      GST_DEBUG_FUNCPTR (gst_kate_parse_src_query));
  gst_element_add_pad (GST_ELEMENT (parse), parse->srcpad);
}

static void
gst_kate_parse_drain_event_queue (GstKateParse * parse)
{
  while (parse->event_queue->length) {
    GstEvent *event;

    event = GST_EVENT_CAST (g_queue_pop_head (parse->event_queue));
    gst_pad_event_default (parse->sinkpad, NULL, event);
  }
}

static GstFlowReturn
gst_kate_parse_push_headers (GstKateParse * parse)
{
  /* mark and put on caps */
  GstCaps *caps;
  GstBuffer *outbuf;
  kate_packet packet;
  GList *headers, *outbuf_list = NULL;
  int ret;
  gboolean res;

  /* get the headers into the caps, passing them to kate as we go */
  caps =
      gst_kate_util_set_header_on_caps (&parse->element,
      gst_pad_get_current_caps (parse->sinkpad), parse->streamheader);

  if (G_UNLIKELY (!caps)) {
    GST_ELEMENT_ERROR (parse, STREAM, DECODE, (NULL),
        ("Failed to set headers on caps"));
    return GST_FLOW_ERROR;
  }

  GST_DEBUG_OBJECT (parse, "here are the caps: %" GST_PTR_FORMAT, caps);
  res = gst_pad_set_caps (parse->srcpad, caps);
  gst_caps_unref (caps);
  if (G_UNLIKELY (!res)) {
    GST_WARNING_OBJECT (parse->srcpad, "Failed to set caps on source pad");
    return GST_FLOW_NOT_NEGOTIATED;
  }

  headers = parse->streamheader;
  while (headers) {
    GstMapInfo info;

    outbuf = GST_BUFFER_CAST (headers->data);

    if (!gst_buffer_map (outbuf, &info, GST_MAP_READ)) {
      GST_WARNING_OBJECT (outbuf, "Failed to map buffer");
      continue;
    }

    kate_packet_wrap (&packet, info.size, info.data);
    ret = kate_decode_headerin (&parse->ki, &parse->kc, &packet);
    if (G_UNLIKELY (ret < 0)) {
      GST_WARNING_OBJECT (parse, "Failed to decode header: %s",
          gst_kate_util_get_error_message (ret));
    }
    gst_buffer_unmap (outbuf, &info);
    /* takes ownership of outbuf, which was previously in parse->streamheader */
    GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_HEADER);
    outbuf_list = g_list_append (outbuf_list, outbuf);
    headers = headers->next;
  }

  /* first process queued events */
  gst_kate_parse_drain_event_queue (parse);

  /* push out buffers, ignoring return value... */
  headers = outbuf_list;
  while (headers) {
    outbuf = GST_BUFFER_CAST (headers->data);
    gst_pad_push (parse->srcpad, outbuf);
    headers = headers->next;
  }

  g_list_free (outbuf_list);
  g_list_free (parse->streamheader);
  parse->streamheader = NULL;

  parse->streamheader_sent = TRUE;

  return GST_FLOW_OK;
}

static void
gst_kate_parse_clear_queue (GstKateParse * parse)
{
  GST_DEBUG_OBJECT (parse, "Clearing queue");
  while (parse->buffer_queue->length) {
    GstBuffer *buf;

    buf = GST_BUFFER_CAST (g_queue_pop_head (parse->buffer_queue));
    gst_buffer_unref (buf);
  }
  while (parse->event_queue->length) {
    GstEvent *event;

    event = GST_EVENT_CAST (g_queue_pop_head (parse->event_queue));
    gst_event_unref (event);
  }
}

static GstFlowReturn
gst_kate_parse_push_buffer (GstKateParse * parse, GstBuffer * buf,
    gint64 granulepos)
{
  GST_LOG_OBJECT (parse, "granulepos %16" G_GINT64_MODIFIER "x", granulepos);
  if (granulepos < 0) {
    /* packets coming not from Ogg won't have a granpos in the offset end,
       so we have to synthesize one here - only problem is we don't know
       the backlink - pretend there's none for now */
    GST_INFO_OBJECT (parse, "No granulepos on buffer, synthesizing one");
    granulepos =
        kate_duration_granule (&parse->ki,
        GST_BUFFER_TIMESTAMP (buf) /
        (double) GST_SECOND) << kate_granule_shift (&parse->ki);
  }
  GST_BUFFER_OFFSET (buf) =
      kate_granule_time (&parse->ki, granulepos) * GST_SECOND;
  GST_BUFFER_OFFSET_END (buf) = granulepos;
  GST_BUFFER_TIMESTAMP (buf) = GST_BUFFER_OFFSET (buf);

  return gst_pad_push (parse->srcpad, buf);
}

static GstFlowReturn
gst_kate_parse_drain_queue_prematurely (GstKateParse * parse)
{
  GstFlowReturn ret = GST_FLOW_OK;

  /* got an EOS event, make sure to push out any buffers that were in the queue
   * -- won't normally be the case, but this catches the
   * didn't-get-a-granulepos-on-the-last-packet case. Assuming a continuous
   * stream. */

  /* if we got EOS before any buffers came, go ahead and push the other events
   * first */
  gst_kate_parse_drain_event_queue (parse);

  while (!g_queue_is_empty (parse->buffer_queue)) {
    GstBuffer *buf;
    gint64 granpos;

    buf = GST_BUFFER_CAST (g_queue_pop_head (parse->buffer_queue));

    granpos = GST_BUFFER_OFFSET_END (buf);
    ret = gst_kate_parse_push_buffer (parse, buf, granpos);

    if (ret != GST_FLOW_OK)
      goto done;
  }

  g_assert (g_queue_is_empty (parse->buffer_queue));

done:
  return ret;
}

static GstFlowReturn
gst_kate_parse_drain_queue (GstKateParse * parse, gint64 granulepos)
{
  GstFlowReturn ret = GST_FLOW_OK;

  if (!g_queue_is_empty (parse->buffer_queue)) {
    GstBuffer *buf;
    buf = GST_BUFFER_CAST (g_queue_pop_head (parse->buffer_queue));
    ret = gst_kate_parse_push_buffer (parse, buf, granulepos);

    if (ret != GST_FLOW_OK)
      goto done;
  }
  g_assert (g_queue_is_empty (parse->buffer_queue));

done:
  return ret;
}

static GstFlowReturn
gst_kate_parse_queue_buffer (GstKateParse * parse, GstBuffer * buf)
{
  GstFlowReturn ret = GST_FLOW_OK;
  gint64 granpos;

  buf = gst_buffer_make_writable (buf);

  /* oggdemux stores the granule pos in the offset end */
  granpos = GST_BUFFER_OFFSET_END (buf);
  GST_LOG_OBJECT (parse, "granpos %16" G_GINT64_MODIFIER "x", granpos);
  g_queue_push_tail (parse->buffer_queue, buf);

#if 1
  /* if getting buffers from matroska, we won't have a granpos here... */
  //if (GST_BUFFER_OFFSET_END_IS_VALID (buf)) {
  ret = gst_kate_parse_drain_queue (parse, granpos);
  //}
#else
  if (granpos >= 0) {
    ret = gst_kate_parse_drain_queue (parse, granpos);
  } else {
    GST_ELEMENT_ERROR (parse, STREAM, DECODE, (NULL),
        ("Bad granulepos %" G_GINT64_FORMAT, granpos));
    ret = GST_FLOW_ERROR;
  }
#endif

  return ret;
}

static GstFlowReturn
gst_kate_parse_parse_packet (GstKateParse * parse, GstBuffer * buf)
{
  GstFlowReturn ret = GST_FLOW_OK;
  guint8 header[1];
  gsize size;

  g_assert (parse);

  parse->packetno++;

  size = gst_buffer_extract (buf, 0, header, 1);

  GST_LOG_OBJECT (parse, "Got packet %02x, %" G_GSIZE_FORMAT " bytes",
      size ? header[0] : -1, gst_buffer_get_size (buf));

  if (size > 0 && header[0] & 0x80) {
    GST_DEBUG_OBJECT (parse, "Found header %02x", header[0]);
    /* if 0x80 is set, it's streamheader,
     * so put it on the streamheader list and return */
    parse->streamheader = g_list_append (parse->streamheader, buf);
    ret = GST_FLOW_OK;
  } else {
    if (!parse->streamheader_sent) {
      GST_DEBUG_OBJECT (parse, "Found non header, pushing headers seen so far");
      ret = gst_kate_parse_push_headers (parse);
    }

    if (ret == GST_FLOW_OK) {
      ret = gst_kate_parse_queue_buffer (parse, buf);
    }
  }

  return ret;
}

static GstFlowReturn
gst_kate_parse_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  GstKateParseClass *klass;
  GstKateParse *parse;

  parse = GST_KATE_PARSE (parent);
  klass = GST_KATE_PARSE_CLASS (G_OBJECT_GET_CLASS (parse));

  g_assert (klass->parse_packet != NULL);

  if (G_UNLIKELY (!gst_pad_has_current_caps (pad)))
    return GST_FLOW_NOT_NEGOTIATED;

  return klass->parse_packet (parse, buffer);
}

static gboolean
gst_kate_parse_queue_event (GstKateParse * parse, GstEvent * event)
{
  GstFlowReturn ret = TRUE;

  g_queue_push_tail (parse->event_queue, event);

  return ret;
}

static gboolean
gst_kate_parse_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  gboolean ret;
  GstKateParse *parse;

  parse = GST_KATE_PARSE (parent);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
      gst_kate_parse_clear_queue (parse);
      ret = gst_pad_event_default (pad, parent, event);
      break;
    case GST_EVENT_EOS:
      if (!parse->streamheader_sent) {
        GST_DEBUG_OBJECT (parse, "Got EOS, pushing headers seen so far");
        ret = gst_kate_parse_push_headers (parse);
        if (ret != GST_FLOW_OK)
          break;
      }
      gst_kate_parse_drain_queue_prematurely (parse);
      ret = gst_pad_event_default (pad, parent, event);
      break;
    default:
      if (!parse->streamheader_sent && GST_EVENT_IS_SERIALIZED (event)
          && GST_EVENT_TYPE (event) > GST_EVENT_CAPS)
        ret = gst_kate_parse_queue_event (parse, event);
      else
        ret = gst_pad_event_default (pad, parent, event);
      break;
  }

  return ret;
}

#if 0
static gboolean
gst_kate_parse_convert (GstPad * pad,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = TRUE;
  GstKateParse *parse;

  parse = GST_KATE_PARSE (GST_PAD_PARENT (pad));

  /* fixme: assumes atomic access to lots of instance variables modified from
   * the streaming thread, including 64-bit variables */

  if (!parse->streamheader_sent)
    return FALSE;

  if (src_format == *dest_format) {
    *dest_value = src_value;
    return TRUE;
  }

  if (parse->sinkpad == pad &&
      (src_format == GST_FORMAT_BYTES || *dest_format == GST_FORMAT_BYTES))
    return FALSE;

  switch (src_format) {
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_DEFAULT:
      switch (*dest_format) {
        case GST_FORMAT_TIME:
          *dest_value = kate_granule_time (&parse->ki, src_value) * GST_SECOND;
          break;
        default:
          res = FALSE;
      }
      break;
    default:
      res = FALSE;
  }

  return res;
}
#endif

static gboolean
gst_kate_parse_src_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
#if 1
  // TODO
  GST_WARNING ("gst_kate_parse_src_query");
  return FALSE;
#else
  gint64 granulepos;
  GstKateParse *parse;
  gboolean res = FALSE;

  parse = GST_KATE_PARSE (parent);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstFormat format;
      gint64 value;

      granulepos = parse->prev_granulepos;

      gst_query_parse_position (query, &format, NULL);

      /* and convert to the final format */
      if (!(res =
              gst_kate_parse_convert (pad, GST_FORMAT_DEFAULT, granulepos,
                  &format, &value)))
        goto error;

      /* fixme: support segments
         value = (value - parse->segment_start) + parse->segment_time;
       */

      gst_query_set_position (query, format, value);

      GST_LOG_OBJECT (parse, "query %p: peer returned granulepos: %"
          G_GUINT64_FORMAT " - we return %" G_GUINT64_FORMAT " (format %u)",
          query, granulepos, value, format);

      break;
    }
    case GST_QUERY_DURATION:
    {
      /* fixme: not threadsafe */
      /* query peer for total length */
      if (!gst_pad_is_linked (parse->sinkpad)) {
        GST_WARNING_OBJECT (parse, "sink pad %" GST_PTR_FORMAT " is not linked",
            parse->sinkpad);
        goto error;
      }
      if (!(res = gst_pad_query (GST_PAD_PEER (parse->sinkpad), query)))
        goto error;
      break;
    }
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      if (!(res =
              gst_kate_parse_convert (pad, src_fmt, src_val, &dest_fmt,
                  &dest_val)))
        goto error;
      gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);
      break;
    }
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }
  return res;

error:
  {
    GST_WARNING_OBJECT (parse, "error handling query");
    return res;
  }
#endif
}

static void
gst_kate_parse_free_stream_headers (GstKateParse * parse)
{
  while (parse->streamheader != NULL) {
    gst_buffer_unref (GST_BUFFER (parse->streamheader->data));
    parse->streamheader = g_list_delete_link (parse->streamheader,
        parse->streamheader);
  }
}

static GstStateChangeReturn
gst_kate_parse_change_state (GstElement * element, GstStateChange transition)
{
  GstKateParse *parse = GST_KATE_PARSE (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      kate_info_init (&parse->ki);
      kate_comment_init (&parse->kc);
      parse->packetno = 0;
      parse->streamheader_sent = FALSE;
      parse->streamheader = NULL;
      parse->buffer_queue = g_queue_new ();
      parse->event_queue = g_queue_new ();
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      kate_info_clear (&parse->ki);
      kate_comment_clear (&parse->kc);

      gst_kate_parse_clear_queue (parse);
      g_queue_free (parse->buffer_queue);
      parse->buffer_queue = NULL;
      g_queue_free (parse->event_queue);
      parse->event_queue = NULL;
      gst_kate_parse_free_stream_headers (parse);
      break;

    default:
      break;
  }

  return ret;
}
