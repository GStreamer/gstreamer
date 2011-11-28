/*
 * gstcmmldec.c - GStreamer annodex CMML decoder
 * Copyright (C) 2005 Alessandro Decina
 * 
 * Authors:
 *   Alessandro Decina <alessandro@nnva.org>
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
 * SECTION:element-cmmldec
 * @see_also: cmmlenc, oggdemux
 *
 * Cmmldec extracts a CMML document from a CMML bitstream.<ulink
 * url="http://www.annodex.net/TR/draft-pfeiffer-cmml-02.html">CMML</ulink> is
 * an XML markup language for time-continuous data maintained by the <ulink
 * url="http:/www.annodex.org/">Annodex Foundation</ulink>.
 *
 * <refsect2>
 * <title>Example pipeline</title>
 * |[
 * gst-launch -v filesrc location=annotated.ogg ! oggdemux ! cmmldec ! filesink location=annotations.cmml
 * ]|
 * </refsect2>
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <glib.h>

#include <gst/tag/tag.h>
#include "gstannodex.h"
#include "gstcmmltag.h"
#include "gstcmmldec.h"
#include "gstcmmlutils.h"

GST_DEBUG_CATEGORY_STATIC (cmmldec);
#define GST_CAT_DEFAULT cmmldec

#define CMML_IDENT_HEADER_SIZE 29

enum
{
  ARG_0,
  GST_CMML_DEC_WAIT_CLIP_END
};

enum
{
  LAST_SIGNAL
};

static GstStaticPadTemplate gst_cmml_dec_src_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("text/x-cmml, encoded = (boolean) false")
    );

static GstStaticPadTemplate gst_cmml_dec_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("text/x-cmml, encoded = (boolean) true")
    );

/* GstCmmlDec */
GST_BOILERPLATE (GstCmmlDec, gst_cmml_dec, GstElement, GST_TYPE_ELEMENT);
static void gst_cmml_dec_get_property (GObject * dec, guint property_id,
    GValue * value, GParamSpec * pspec);
static void gst_cmml_dec_set_property (GObject * dec, guint property_id,
    const GValue * value, GParamSpec * pspec);
static const GstQueryType *gst_cmml_dec_query_types (GstPad * pad);
static gboolean gst_cmml_dec_sink_query (GstPad * pad, GstQuery * query);
static gboolean gst_cmml_dec_sink_event (GstPad * pad, GstEvent * event);
static gboolean gst_cmml_dec_convert (GstPad * pad, GstFormat src_fmt,
    gint64 src_val, GstFormat * dest_fmt, gint64 * dest_val);
static GstStateChangeReturn gst_cmml_dec_change_state (GstElement * element,
    GstStateChange transition);
static GstFlowReturn gst_cmml_dec_chain (GstPad * pad, GstBuffer * buffer);

static GstCmmlPacketType gst_cmml_dec_parse_packet_type (GstCmmlDec * dec,
    GstBuffer * buffer);
static void gst_cmml_dec_parse_ident_header (GstCmmlDec * dec, GstBuffer * buf);
static void gst_cmml_dec_parse_first_header (GstCmmlDec * dec, GstBuffer * buf);
static void gst_cmml_dec_parse_preamble (GstCmmlDec * dec,
    guchar * preamble, guchar * cmml_root_element);
static void gst_cmml_dec_parse_xml (GstCmmlDec * dec,
    guchar * data, guint size);
static void gst_cmml_dec_parse_head (GstCmmlDec * dec, GstCmmlTagHead * head);
static void gst_cmml_dec_parse_clip (GstCmmlDec * dec, GstCmmlTagClip * clip);

static GstFlowReturn gst_cmml_dec_new_buffer (GstCmmlDec * dec,
    guchar * data, gint size, GstBuffer ** buffer);
static void gst_cmml_dec_push_clip (GstCmmlDec * dec, GstCmmlTagClip * clip);
static void gst_cmml_dec_send_clip_tag (GstCmmlDec * dec,
    GstCmmlTagClip * clip);

static void gst_cmml_dec_finalize (GObject * object);

static void
gst_cmml_dec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class,
      &gst_cmml_dec_sink_factory);
  gst_element_class_add_static_pad_template (element_class,
      &gst_cmml_dec_src_factory);
  gst_element_class_set_details_simple (element_class, "CMML stream decoder",
      "Codec/Decoder",
      "Decodes CMML streams", "Alessandro Decina <alessandro@nnva.org>");
}

static void
gst_cmml_dec_class_init (GstCmmlDecClass * dec_class)
{
  GObjectClass *klass = G_OBJECT_CLASS (dec_class);

  GST_ELEMENT_CLASS (klass)->change_state = gst_cmml_dec_change_state;

  klass->set_property = gst_cmml_dec_set_property;
  klass->get_property = gst_cmml_dec_get_property;
  klass->finalize = gst_cmml_dec_finalize;

  g_object_class_install_property (klass, GST_CMML_DEC_WAIT_CLIP_END,
      g_param_spec_boolean ("wait-clip-end-time",
          "Wait clip end time",
          "Send a tag for a clip when the clip ends, setting its end-time. "
          "Use when you need to know both clip's start-time and end-time.",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_cmml_dec_init (GstCmmlDec * dec, GstCmmlDecClass * klass)
{
  dec->sinkpad =
      gst_pad_new_from_static_template (&gst_cmml_dec_sink_factory, "sink");
  gst_pad_set_chain_function (dec->sinkpad, gst_cmml_dec_chain);
  gst_pad_set_query_type_function (dec->sinkpad, gst_cmml_dec_query_types);
  gst_pad_set_query_function (dec->sinkpad, gst_cmml_dec_sink_query);
  gst_pad_set_event_function (dec->sinkpad, gst_cmml_dec_sink_event);
  gst_element_add_pad (GST_ELEMENT (dec), dec->sinkpad);

  dec->srcpad =
      gst_pad_new_from_static_template (&gst_cmml_dec_src_factory, "src");
  gst_element_add_pad (GST_ELEMENT (dec), dec->srcpad);

  dec->wait_clip_end = FALSE;
}

static void
gst_cmml_dec_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstCmmlDec *dec = GST_CMML_DEC (object);

  switch (property_id) {
    case GST_CMML_DEC_WAIT_CLIP_END:
      g_value_set_boolean (value, dec->wait_clip_end);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
gst_cmml_dec_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstCmmlDec *dec = GST_CMML_DEC (object);

  switch (property_id) {
    case GST_CMML_DEC_WAIT_CLIP_END:
      dec->wait_clip_end = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (dec, property_id, pspec);
  }
}

static void
gst_cmml_dec_finalize (GObject * object)
{
  GstCmmlDec *dec = GST_CMML_DEC (object);

  if (dec->tracks) {
    gst_cmml_track_list_destroy (dec->tracks);
    dec->tracks = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstStateChangeReturn
gst_cmml_dec_change_state (GstElement * element, GstStateChange transition)
{
  GstCmmlDec *dec = GST_CMML_DEC (element);
  GstStateChangeReturn res;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      dec->parser = gst_cmml_parser_new (GST_CMML_PARSER_DECODE);
      dec->parser->user_data = dec;
      dec->parser->preamble_callback =
          (GstCmmlParserPreambleCallback) gst_cmml_dec_parse_preamble;
      dec->parser->head_callback =
          (GstCmmlParserHeadCallback) gst_cmml_dec_parse_head;
      dec->parser->clip_callback =
          (GstCmmlParserClipCallback) gst_cmml_dec_parse_clip;
      dec->major = -1;
      dec->minor = -1;
      dec->granulerate_n = -1;
      dec->granulerate_d = -1;
      dec->granuleshift = 0;
      dec->granulepos = 0;
      dec->flow_return = GST_FLOW_OK;
      dec->sent_root = FALSE;
      dec->tracks = gst_cmml_track_list_new ();
      break;
    default:
      break;
  }

  res = parent_class->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_cmml_parser_free (dec->parser);
      gst_cmml_track_list_destroy (dec->tracks);
      dec->tracks = NULL;
      break;
    default:
      break;
  }

  return res;
}

static const GstQueryType *
gst_cmml_dec_query_types (GstPad * pad)
{
  static const GstQueryType query_types[] = {
    GST_QUERY_CONVERT,
    0
  };

  return query_types;
}

static gboolean
gst_cmml_dec_sink_query (GstPad * pad, GstQuery * query)
{
  gboolean res = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      res = gst_cmml_dec_convert (pad, src_fmt, src_val, &dest_fmt, &dest_val);
      if (res)
        gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);
      break;
    }
    default:
      break;
  }

  return res;
}

static gboolean
gst_cmml_dec_convert (GstPad * pad,
    GstFormat src_fmt, gint64 src_val, GstFormat * dest_fmt, gint64 * dest_val)
{
  GstCmmlDec *dec = GST_CMML_DEC (GST_PAD_PARENT (pad));
  gboolean res = FALSE;

  switch (src_fmt) {
    case GST_FORMAT_DEFAULT:
      switch (*dest_fmt) {
        case GST_FORMAT_TIME:
        {
          *dest_val = gst_annodex_granule_to_time (src_val, dec->granulerate_n,
              dec->granulerate_d, dec->granuleshift);
          res = TRUE;
          break;
        }
        default:
          break;
      }
      break;
    default:
      break;
  }

  return res;
}

static gboolean
gst_cmml_dec_sink_event (GstPad * pad, GstEvent * event)
{
  GstCmmlDec *dec = GST_CMML_DEC (GST_PAD_PARENT (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
    {
      GstBuffer *buffer;
      GstCmmlTagClip *clip;
      GList *clips, *walk;

      GST_INFO_OBJECT (dec, "got EOS, flushing clips");

      /* since we output a clip when the next one in the same track is found, on
       * EOS we need to output the last clip (if any) of every track
       */
      clips = gst_cmml_track_list_get_clips (dec->tracks);
      for (walk = clips; walk; walk = g_list_next (walk)) {
        clip = GST_CMML_TAG_CLIP (walk->data);
        gst_cmml_dec_push_clip (dec, clip);
        if (dec->wait_clip_end) {
          clip->end_time = dec->timestamp;
          gst_cmml_dec_send_clip_tag (dec, clip);
        }
      }
      g_list_free (clips);

      /* send the cmml end tag */
      dec->flow_return = gst_cmml_dec_new_buffer (dec,
          (guchar *) "</cmml>", strlen ("</cmml>"), &buffer);

      if (dec->flow_return == GST_FLOW_OK)
        dec->flow_return = gst_pad_push (dec->srcpad, buffer);
      if (dec->flow_return == GST_FLOW_NOT_LINKED)
        dec->flow_return = GST_FLOW_OK; /* Ignore NOT_LINKED */

      break;
    }
    default:
      break;
  }

  return gst_pad_event_default (pad, event);
}

static GstFlowReturn
gst_cmml_dec_chain (GstPad * pad, GstBuffer * buffer)
{
  GstCmmlDec *dec = GST_CMML_DEC (GST_PAD_PARENT (pad));
  GstCmmlPacketType packet;

  if (GST_BUFFER_SIZE (buffer) == 0) {
    /* the EOS page could be empty */
    dec->flow_return = GST_FLOW_OK;
    goto done;
  }

  dec->granulepos = GST_BUFFER_OFFSET_END (buffer);
  dec->timestamp = gst_annodex_granule_to_time (dec->granulepos,
      dec->granulerate_n, dec->granulerate_d, dec->granuleshift);

  /* identify the packet type */
  packet = gst_cmml_dec_parse_packet_type (dec, buffer);

  /* handle the packet. the handler will set dec->flow_return */
  switch (packet) {
    case GST_CMML_PACKET_IDENT_HEADER:
      if (dec->sent_root == FALSE)
        /* don't parse the ident again in case of seeking to the beginning */
        gst_cmml_dec_parse_ident_header (dec, buffer);
      break;
    case GST_CMML_PACKET_FIRST_HEADER:
      if (dec->sent_root == FALSE)
        /* don't parse the xml preamble if it has already been parsed because it
         * would error out, so seeking to the beginning would fail */
        gst_cmml_dec_parse_first_header (dec, buffer);
      break;
    case GST_CMML_PACKET_SECOND_HEADER:
    case GST_CMML_PACKET_CLIP:
      gst_cmml_dec_parse_xml (dec,
          GST_BUFFER_DATA (buffer), GST_BUFFER_SIZE (buffer));
      break;
    case GST_CMML_PACKET_UNKNOWN:
    default:
      GST_ELEMENT_ERROR (dec, STREAM, DECODE, (NULL), ("unknown packet type"));
      dec->flow_return = GST_FLOW_ERROR;
  }

done:
  gst_buffer_unref (buffer);
  return dec->flow_return;
}

/* finds the packet type of the buffer
 */
static GstCmmlPacketType
gst_cmml_dec_parse_packet_type (GstCmmlDec * dec, GstBuffer * buffer)
{
  GstCmmlPacketType packet_type = GST_CMML_PACKET_UNKNOWN;
  gchar *data = (gchar *) GST_BUFFER_DATA (buffer);
  guint size = GST_BUFFER_SIZE (buffer);

  if (size >= 8 && !memcmp (data, "CMML\0\0\0\0", 8)) {
    packet_type = GST_CMML_PACKET_IDENT_HEADER;
  } else if (size >= 5) {
    if (!strncmp (data, "<?xml", 5))
      packet_type = GST_CMML_PACKET_FIRST_HEADER;
    else if (!strncmp (data, "<head", 5))
      packet_type = GST_CMML_PACKET_SECOND_HEADER;
    else if (!strncmp (data, "<clip", 5))
      packet_type = GST_CMML_PACKET_CLIP;
  }

  return packet_type;
}

/* creates a new buffer and sets caps and timestamp on it
 */
static GstFlowReturn
gst_cmml_dec_new_buffer (GstCmmlDec * dec,
    guchar * data, gint size, GstBuffer ** buffer)
{
  GstFlowReturn res;

  res = gst_pad_alloc_buffer (dec->srcpad, GST_BUFFER_OFFSET_NONE,
      size, gst_static_pad_template_get_caps (&gst_cmml_dec_src_factory),
      buffer);

  if (res == GST_FLOW_OK) {
    if (data)
      memcpy (GST_BUFFER_DATA (*buffer), data, size);
    GST_BUFFER_TIMESTAMP (*buffer) = dec->timestamp;
  } else if (res == GST_FLOW_NOT_LINKED) {
    GST_DEBUG_OBJECT (dec, "alloc function return NOT-LINKED, ignoring");
  } else {
    GST_WARNING_OBJECT (dec, "alloc function returned error %s",
        gst_flow_get_name (res));
  }

  return res;
}

/* parses the first CMML packet (the ident header)
 */
static void
gst_cmml_dec_parse_ident_header (GstCmmlDec * dec, GstBuffer * buffer)
{
  guint8 *data = GST_BUFFER_DATA (buffer);

  /* the ident header has a fixed length */
  if (GST_BUFFER_SIZE (buffer) != CMML_IDENT_HEADER_SIZE) {
    GST_ELEMENT_ERROR (dec, STREAM, DECODE,
        (NULL), ("wrong ident header size: %d", GST_BUFFER_SIZE (buffer)));
    dec->flow_return = GST_FLOW_ERROR;

    return;
  }

  data += 8;
  dec->major = GST_READ_UINT16_LE (data);
  data += 2;
  dec->minor = GST_READ_UINT16_LE (data);
  data += 2;
  dec->granulerate_n = GST_READ_UINT64_LE (data);
  data += 8;
  dec->granulerate_d = GST_READ_UINT64_LE (data);
  data += 8;
  dec->granuleshift = GST_READ_UINT8 (data);

  GST_INFO_OBJECT (dec, "bitstream initialized "
      "(major: %" G_GINT16_FORMAT " minor: %" G_GINT16_FORMAT
      " granulerate_n: %" G_GINT64_FORMAT " granulerate_d: %" G_GINT64_FORMAT
      " granuleshift: %d)",
      dec->major, dec->minor,
      dec->granulerate_n, dec->granulerate_d, dec->granuleshift);

  dec->flow_return = GST_FLOW_OK;
}

/* parses the first secondary header.
 * the first secondary header contains the xml version, the doctype and the
 * optional "cmml" processing instruction.
 */
static void
gst_cmml_dec_parse_first_header (GstCmmlDec * dec, GstBuffer * buffer)
{
  gst_cmml_dec_parse_xml (dec,
      GST_BUFFER_DATA (buffer), GST_BUFFER_SIZE (buffer));

  /* if there is a processing instruction, gst_cmml_dec_parse_preamble
   * will be triggered. Otherwise we need to call it manually.
   */
  if (dec->flow_return == GST_FLOW_OK && !dec->sent_root) {
    guchar *preamble = (guchar *) g_strndup ((gchar *) GST_BUFFER_DATA (buffer),
        GST_BUFFER_SIZE (buffer));

    gst_cmml_dec_parse_preamble (dec, preamble, (guchar *) "<cmml>");
    g_free (preamble);
  }
}

/* feeds data into the cmml parser.
 */
static void
gst_cmml_dec_parse_xml (GstCmmlDec * dec, guchar * data, guint size)
{
  GError *err = NULL;

  if (!gst_cmml_parser_parse_chunk (dec->parser, (gchar *) data, size, &err)) {
    GST_ELEMENT_ERROR (dec, STREAM, DECODE, (NULL), ("%s", err->message));
    g_error_free (err);
    dec->flow_return = GST_FLOW_ERROR;
  }
}

static void
gst_cmml_dec_parse_preamble (GstCmmlDec * dec, guchar * preamble,
    guchar * root_element)
{
  GstBuffer *buffer;
  guchar *encoded_preamble;

  encoded_preamble = (guchar *) g_strconcat ((gchar *) preamble,
      (gchar *) root_element, NULL);

  /* send the root element to the internal parser */
  gst_cmml_dec_parse_xml (dec, root_element, strlen ((gchar *) root_element));
  dec->sent_root = TRUE;

  /* push the root element */
  dec->flow_return = gst_cmml_dec_new_buffer (dec,
      encoded_preamble, strlen ((gchar *) encoded_preamble), &buffer);
  if (dec->flow_return == GST_FLOW_OK) {
    dec->flow_return = gst_pad_push (dec->srcpad, buffer);
  }

  if (dec->flow_return == GST_FLOW_OK) {
    GST_INFO_OBJECT (dec, "preamble parsed");
  }

  g_free (encoded_preamble);
  return;
}

/* outputs the cmml head element and send TITLE and CMML_HEAD tags.
 * This callback is registered with dec->parser. It is called when the
 * head element is parsed.
 */
static void
gst_cmml_dec_parse_head (GstCmmlDec * dec, GstCmmlTagHead * head)
{
  GstTagList *tags;
  GValue str_val = { 0 }, title_val = {
  0};
  guchar *head_str;
  GstBuffer *buffer;

  GST_DEBUG_OBJECT (dec, "found CMML head (title: %s base: %s)",
      head->title, head->base);

  /* create the GST_TAG_TITLE tag */
  g_value_init (&str_val, G_TYPE_STRING);
  g_value_init (&title_val, gst_tag_get_type (GST_TAG_TITLE));
  g_value_set_string (&str_val, (gchar *) head->title);
  g_value_transform (&str_val, &title_val);

  tags = gst_tag_list_new ();
  gst_tag_list_add_values (tags, GST_TAG_MERGE_APPEND,
      GST_TAG_TITLE, &title_val, NULL);
  gst_tag_list_add (tags, GST_TAG_MERGE_APPEND, GST_TAG_CMML_HEAD, head, NULL);
  gst_element_found_tags_for_pad (GST_ELEMENT (dec), dec->srcpad, tags);

  g_value_unset (&str_val);
  g_value_unset (&title_val);

  head_str = gst_cmml_parser_tag_head_to_string (dec->parser, head);

  dec->flow_return = gst_cmml_dec_new_buffer (dec,
      head_str, strlen ((gchar *) head_str), &buffer);
  g_free (head_str);
  if (dec->flow_return == GST_FLOW_OK)
    dec->flow_return = gst_pad_push (dec->srcpad, buffer);
  if (dec->flow_return == GST_FLOW_NOT_LINKED)
    dec->flow_return = GST_FLOW_OK;     /* Ignore NOT_LINKED */
}

/* send a TAG_MESSAGE event for a clip */
static void
gst_cmml_dec_send_clip_tag (GstCmmlDec * dec, GstCmmlTagClip * clip)
{
  GstTagList *tags;

  GST_DEBUG_OBJECT (dec, "sending clip tag %s", clip->id);

  tags = gst_tag_list_new ();
  gst_tag_list_add (tags, GST_TAG_MERGE_APPEND, GST_TAG_CMML_CLIP, clip, NULL);
  gst_element_found_tags_for_pad (GST_ELEMENT (dec), dec->srcpad, tags);
}

/* push the string representation of a clip */
static void
gst_cmml_dec_push_clip (GstCmmlDec * dec, GstCmmlTagClip * clip)
{
  GstBuffer *buffer;
  guchar *clip_str;

  GST_DEBUG_OBJECT (dec, "pushing clip %s", clip->id);

  clip_str = gst_cmml_parser_tag_clip_to_string (dec->parser, clip);
  dec->flow_return = gst_cmml_dec_new_buffer (dec,
      clip_str, strlen ((gchar *) clip_str), &buffer);
  if (dec->flow_return == GST_FLOW_OK)
    dec->flow_return = gst_pad_push (dec->srcpad, buffer);
  if (dec->flow_return == GST_FLOW_NOT_LINKED)
    dec->flow_return = GST_FLOW_OK;     /* Ignore NOT_LINKED */

  g_free (clip_str);
}

/* decode a clip tag
 * this callback is registered with dec->parser. It is called whenever a
 * clip is parsed.
 */
static void
gst_cmml_dec_parse_clip (GstCmmlDec * dec, GstCmmlTagClip * clip)
{
  GstCmmlTagClip *prev_clip;

  dec->flow_return = GST_FLOW_OK;

  if (clip->empty)
    GST_INFO_OBJECT (dec, "parsing empty clip");
  else
    GST_INFO_OBJECT (dec, "parsing clip (id: %s)", clip->id);

  clip->start_time = dec->timestamp;
  if (clip->start_time == GST_CLOCK_TIME_NONE) {
    GST_ELEMENT_ERROR (dec, STREAM, DECODE,
        (NULL), ("invalid clip start time"));

    dec->flow_return = GST_FLOW_ERROR;
    return;
  }

  /* get the last clip in the current track */
  prev_clip = gst_cmml_track_list_get_track_last_clip (dec->tracks,
      (gchar *) clip->track);
  if (prev_clip) {
    /* output the previous clip */
    if (clip->empty)
      /* the current clip marks the end of the previous one */
      prev_clip->end_time = clip->start_time;

    gst_cmml_dec_push_clip (dec, prev_clip);
  }

  if (dec->wait_clip_end) {
    /* now it's time to send the tag for the previous clip */
    if (prev_clip) {
      prev_clip->end_time = clip->start_time;
      gst_cmml_dec_send_clip_tag (dec, prev_clip);
    }
  } else if (!clip->empty) {
    /* send the tag for the current clip */
    gst_cmml_dec_send_clip_tag (dec, clip);
  }

  if (prev_clip)
    gst_cmml_track_list_del_clip (dec->tracks, prev_clip);

  if (!clip->empty)
    if (!gst_cmml_track_list_has_clip (dec->tracks, clip))
      gst_cmml_track_list_add_clip (dec->tracks, clip);
}

gboolean
gst_cmml_dec_plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "cmmldec", GST_RANK_PRIMARY,
          GST_TYPE_CMML_DEC))
    return FALSE;

  GST_DEBUG_CATEGORY_INIT (cmmldec, "cmmldec", 0,
      "annodex CMML decoding element");

  return TRUE;
}
