/*
 * gstskeldec.c - GStreamer annodex skeleton decoder
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

#include <string.h>
#include "gstskeldec.h"
#include "gstskeltag.h"
#include "gstannodex.h"

GST_DEBUG_CATEGORY (skeldec);
#define GST_CAT_DEFAULT skeldec

#define FISHEAD_SIZE 64
#define FISBONE_MIN_SIZE 52

enum
{
  GST_SKEL_TAG_FISHEAD_UTC_LEN = 19
};

enum
{
  LAST_SIGNAL
};

static GstElementDetails gst_skel_dec_details = {
  "skeldec: Annodex skeleton stream decoder",
  "Codec/Decoder",
  "Decodes ogg skeleton streams",
  "Alessandro Decina <alessandro@nnva.org>",
};

static GstStaticPadTemplate gst_skel_dec_src_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-ogg-skeleton, parsed=(boolean)TRUE")
    );

static GstStaticPadTemplate gst_skel_dec_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-ogg-skeleton, parsed=(boolean)FALSE")
    );

/* GstSkelDec prototypes */
GST_BOILERPLATE (GstSkelDec, gst_skel_dec, GstElement, GST_TYPE_ELEMENT);
static gboolean gst_skel_dec_sink_query (GstPad * pad, GstQuery * query);
static GstStateChangeReturn gst_skel_dec_change_state (GstElement * element,
    GstStateChange transition);
static GstFlowReturn gst_skel_dec_chain (GstPad * pad, GstBuffer * buffer);
static GstFlowReturn gst_skel_dec_parse_fishead (GstSkelDec * dec,
    GstBuffer * buffer);
static GstFlowReturn gst_skel_dec_parse_fisbone (GstSkelDec * dec,
    GstBuffer * buffer);

/* GstSkelDec code */
static void
gst_skel_dec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_skel_dec_sink_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_skel_dec_src_factory));
  gst_element_class_set_details (element_class, &gst_skel_dec_details);
}

static void
gst_skel_dec_class_init (GstSkelDecClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  element_class->change_state = gst_skel_dec_change_state;
}

static void
gst_skel_dec_init (GstSkelDec * dec, GstSkelDecClass * klass)
{
  dec->sinkpad =
      gst_pad_new_from_static_template (&gst_skel_dec_sink_factory, "sink");
  gst_pad_set_query_function (dec->sinkpad, gst_skel_dec_sink_query);
  gst_pad_set_chain_function (dec->sinkpad, gst_skel_dec_chain);
  gst_element_add_pad (GST_ELEMENT (dec), dec->sinkpad);

  dec->srcpad =
      gst_pad_new_from_static_template (&gst_skel_dec_src_factory, "src");
  gst_element_add_pad (GST_ELEMENT (dec), dec->srcpad);

}

static gboolean
gst_skel_dec_sink_query (GstPad * pad, GstQuery * query)
{
  gboolean res = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONVERT:
    {
      GstFormat src_format, dest_format;
      gint64 src_val, dest_val;

      gst_query_parse_convert (query, &src_format, &src_val,
          &dest_format, &dest_val);

      if (dest_format == GST_FORMAT_TIME) {
        dest_val = GST_CLOCK_TIME_NONE;
        res = TRUE;
      }

      if (res) {
        gst_query_set_convert (query, src_format, src_val,
            dest_format, dest_val);
      }

      break;
    }
    default:
      break;
  }

  return res;
}

static GstStateChangeReturn
gst_skel_dec_change_state (GstElement * element, GstStateChange transition)
{
  GstSkelDec *dec = GST_SKEL_DEC (element);
  GstStateChangeReturn res;

  /* handle the upward state changes */
  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      dec->major = 0;
      dec->minor = 0;
      break;
    default:
      break;
  }

  res = parent_class->change_state (element, transition);

  /* no need to handle downward state changes */

  return res;
}

static GstFlowReturn
gst_skel_dec_chain (GstPad * pad, GstBuffer * buffer)
{
  GstSkelDec *dec = GST_SKEL_DEC (GST_PAD_PARENT (pad));
  GstFlowReturn ret;

  gint size = GST_BUFFER_SIZE (buffer);
  guint8 *data = GST_BUFFER_DATA (buffer);

  if (GST_BUFFER_SIZE (buffer) == 0) {
    /* the skeleton EOS has no packet data */
    return GST_FLOW_OK;
  }

  if (size >= GST_SKEL_OGG_FISHEAD_SIZE && !memcmp (data, "fishead\0", 8)) {
    ret = gst_skel_dec_parse_fishead (dec, buffer);
  } else if (size >= 8 && !memcmp (data, "fisbone\0", 8)) {
    ret = gst_skel_dec_parse_fisbone (dec, buffer);
  } else {
    /* maybe it would be better to ignore the packet */
    GST_ELEMENT_ERROR (dec, STREAM, DECODE, (NULL), ("unknown packet type"));
    ret = GST_FLOW_UNEXPECTED;
  }

  return ret;
}

static GstFlowReturn
gst_skel_dec_parse_fishead (GstSkelDec * dec, GstBuffer * buffer)
{
  GstTagList *tags;
  guint8 *data = GST_BUFFER_DATA (buffer);
  GstSkelTagFishead *fishead;

  if (GST_BUFFER_SIZE (buffer) != FISHEAD_SIZE)
    goto wrong_size;

  fishead = g_object_new (GST_TYPE_SKEL_TAG_FISHEAD, NULL);

  data += 8;
  fishead->major = GST_READ_UINT16_LE (data);
  data += 2;
  fishead->minor = GST_READ_UINT16_LE (data);
  data += 2;
  fishead->prestime_n = (gint64) GST_READ_UINT64_LE (data);
  data += 8;
  fishead->prestime_d = (gint64) GST_READ_UINT64_LE (data);
  data += 8;
  fishead->basetime_n = (gint64) GST_READ_UINT64_LE (data);
  data += 8;
  fishead->basetime_d = (gint64) GST_READ_UINT64_LE (data);
  data += 8;
  fishead->utc = g_strndup ((gchar *) data, GST_SKEL_TAG_FISHEAD_UTC_LEN);

  GST_INFO_OBJECT (dec, "fishead parsed ("
      "major: %" G_GUINT16_FORMAT " minor: %" G_GUINT16_FORMAT
      " prestime_n: %" G_GINT64_FORMAT " prestime_d: %" G_GINT64_FORMAT
      " basetime_n: %" G_GINT64_FORMAT " basetime_d: %" G_GINT64_FORMAT
      " utc: %s)", fishead->major, fishead->minor, fishead->prestime_n,
      fishead->prestime_d, fishead->basetime_n, fishead->basetime_d,
      fishead->utc);

  /* send the TAG_MESSAGE */
  tags = gst_tag_list_new ();
  gst_tag_list_add (tags, GST_TAG_MERGE_APPEND,
      GST_TAG_SKELETON_FISHEAD, fishead, NULL);
  gst_element_found_tags_for_pad (GST_ELEMENT (dec), dec->srcpad, tags);

  g_object_unref (fishead);

  /* forward the fishead */
  gst_buffer_set_caps (buffer,
      gst_static_pad_template_get_caps (&gst_skel_dec_src_factory));
  return gst_pad_push (dec->srcpad, buffer);

wrong_size:
  GST_ELEMENT_ERROR (dec, STREAM, DECODE,
      (NULL), ("wrong fishead packet size: %d", GST_BUFFER_SIZE (buffer)));

  gst_buffer_unref (buffer);
  return GST_FLOW_ERROR;
}

static GstFlowReturn
gst_skel_dec_parse_fisbone (GstSkelDec * dec, GstBuffer * buffer)
{
  GstSkelTagFisbone *fisbone;
  guint8 *data;
  gchar *headers;
  gchar **tokens;
  const gchar *content_type;
  GstTagList *tags;
  GValue *val;

  if (GST_BUFFER_SIZE (buffer) < FISBONE_MIN_SIZE)
    goto wrong_size;

  data = GST_BUFFER_DATA (buffer);
  fisbone = g_object_new (GST_TYPE_SKEL_TAG_FISBONE, NULL);

  data += 8;
  fisbone->hdr_offset = GST_READ_UINT32_LE (data);
  data += 4;
  fisbone->serialno = GST_READ_UINT32_LE (data);
  data += 4;
  fisbone->hdr_num = GST_READ_UINT32_LE (data);
  data += 4;
  fisbone->granulerate_n = GST_READ_UINT64_LE (data);
  data += 8;
  fisbone->granulerate_d = GST_READ_UINT64_LE (data);
  data += 8;
  fisbone->start_granule = GST_READ_UINT64_LE (data);
  data += 8;
  fisbone->preroll = GST_READ_UINT32_LE (data);
  data += 4;
  fisbone->granuleshift = GST_READ_UINT8 (data);
  data += 1;
  data += 3;                    /* padding */

  /* 8 = strlen ("fishead\0") */
  headers = g_strndup ((gchar *) data,
      GST_BUFFER_SIZE (buffer) - 8 - fisbone->hdr_offset);
  fisbone->headers = gst_annodex_parse_headers (headers);
  g_free (headers);

  if (!fisbone->headers)
    goto bad_headers;

  /* check for the mandatory Content-Type header: it MUST be the first
   * header */
  if (fisbone->headers->n_values < 2 ||
      (val = g_value_array_get_nth (fisbone->headers, 0)) == NULL ||
      strcmp (g_value_get_string (val), "Content-Type"))
    goto no_content_type;

  /* get the Content-Type value */
  val = g_value_array_get_nth (fisbone->headers, 1);
  content_type = g_value_get_string (val);

  /* Content-Type must not be empty */
  if (*content_type == '\0')
    goto bad_content_type;

  /* split the content-type field into "content_type; encoding" */
  tokens = g_strsplit (content_type, ";", 2);
  fisbone->content_type = g_strdup (g_strstrip (tokens[0]));
  fisbone->encoding = tokens[1] == NULL ? NULL :
      g_strdup (g_strstrip (tokens[1]));
  g_strfreev (tokens);

  GST_INFO_OBJECT (dec, "fisbone parsed ("
      "serialno %" G_GUINT32_FORMAT " granulerate_n: %" G_GINT64_FORMAT
      " granulerate_d: %" G_GINT64_FORMAT " start_granule: %" G_GINT64_FORMAT
      " preroll: %" G_GUINT32_FORMAT " granuleshift: %d"
      " content-type: %s)",
      fisbone->serialno, fisbone->granulerate_n, fisbone->granulerate_d,
      fisbone->start_granule, fisbone->preroll, fisbone->granuleshift,
      fisbone->content_type);

  /* send the tag message */
  tags = gst_tag_list_new ();
  gst_tag_list_add (tags, GST_TAG_MERGE_APPEND,
      GST_TAG_SKELETON_FISBONE, fisbone, NULL);
  gst_element_found_tags_for_pad (GST_ELEMENT (dec), dec->srcpad, tags);

  g_object_unref (fisbone);

  gst_buffer_set_caps (buffer,
      gst_static_pad_template_get_caps (&gst_skel_dec_src_factory));
  return gst_pad_push (dec->srcpad, buffer);

wrong_size:
  GST_ELEMENT_ERROR (dec, STREAM, DECODE,
      (NULL), ("wrong fisbone size (%d)", GST_BUFFER_SIZE (buffer)));
  gst_buffer_unref (buffer);
  return GST_FLOW_ERROR;

bad_headers:
  GST_ELEMENT_ERROR (dec, STREAM, DECODE, (NULL), ("bad fisbone headers"));
  gst_buffer_unref (buffer);
  g_object_unref (fisbone);
  return GST_FLOW_ERROR;

no_content_type:
bad_content_type:
  GST_ELEMENT_ERROR (dec, STREAM, DECODE,
      (NULL), ("missing or bad fisbone content-type"));
  gst_buffer_unref (buffer);
  g_object_unref (fisbone);
  return GST_FLOW_ERROR;
}

gboolean
gst_skel_dec_plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "skeldec", GST_RANK_PRIMARY,
          gst_skel_dec_get_type ()))
    return FALSE;

  gst_tag_register (GST_TAG_SKELETON_FISHEAD, GST_TAG_FLAG_META,
      GST_TYPE_SKEL_TAG_FISHEAD, "skeleton-fishead",
      "annodex skeleton fishead tag", NULL);

  gst_tag_register (GST_TAG_SKELETON_FISBONE, GST_TAG_FLAG_META,
      GST_TYPE_SKEL_TAG_FISBONE, "skeleton-fisbone",
      "annodex skeleton fisbone tag", NULL);

  GST_DEBUG_CATEGORY_INIT (skeldec, "skeldec", 0,
      "annodex skeleton decoding element");

  return TRUE;
}
