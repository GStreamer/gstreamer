/* GStreamer Adaptive Multi-Rate Narrow-Band (AMR-NB) plugin
 * Copyright (C) 2004 Ronald Bultje <rbultje@ronald.bitfreak.net>
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

#include <string.h>

#include "amrnbparse.h"

GST_DEBUG_CATEGORY_STATIC (amrnbparse_debug);
#define GST_CAT_DEFAULT amrnbparse_debug

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-amr-nb, "
        "rate = (int) 8000, " "channels = (int) 1")
    );

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-amr-nb-sh")
    );

static void gst_amrnbparse_base_init (GstAmrnbParseClass * klass);
static void gst_amrnbparse_class_init (GstAmrnbParseClass * klass);
static void gst_amrnbparse_init (GstAmrnbParse * amrnbparse);

static const GstFormat *gst_amrnbparse_formats (GstPad * pad);
static const GstQueryType *gst_amrnbparse_querytypes (GstPad * pad);
static gboolean gst_amrnbparse_query (GstPad * pad, GstQueryType type,
    GstFormat * fmt, gint64 * value);
static void gst_amrnbparse_loop (GstElement * element);
static GstElementStateReturn gst_amrnbparse_state_change (GstElement * element);

static GstElementClass *parent_class = NULL;

GType
gst_amrnbparse_get_type (void)
{
  static GType amrnbparse_type = 0;

  if (!amrnbparse_type) {
    static const GTypeInfo amrnbparse_info = {
      sizeof (GstAmrnbParseClass),
      (GBaseInitFunc) gst_amrnbparse_base_init,
      NULL,
      (GClassInitFunc) gst_amrnbparse_class_init,
      NULL,
      NULL,
      sizeof (GstAmrnbParse),
      0,
      (GInstanceInitFunc) gst_amrnbparse_init,
    };

    amrnbparse_type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstAmrnbParse", &amrnbparse_info, 0);
  }

  return amrnbparse_type;
}

static void
gst_amrnbparse_base_init (GstAmrnbParseClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstElementDetails gst_amrnbparse_details = {
    "AMR-NB parser",
    "Codec/Parser/Audio",
    "Adaptive Multi-Rate Narrow-Band audio parser",
    "Ronald Bultje <rbultje@ronald.bitfreak.net>"
  };

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));

  gst_element_class_set_details (element_class, &gst_amrnbparse_details);
}

static void
gst_amrnbparse_class_init (GstAmrnbParseClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  element_class->change_state = gst_amrnbparse_state_change;

  GST_DEBUG_CATEGORY_INIT (amrnbparse_debug,
      "amrnbparse", 0, "AMR-NB stream parsing");
}

static void
gst_amrnbparse_init (GstAmrnbParse * amrnbparse)
{
  GST_FLAG_SET (amrnbparse, GST_ELEMENT_EVENT_AWARE);

  /* create the sink pad */
  amrnbparse->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&sink_template),
      "sink");
  gst_element_add_pad (GST_ELEMENT (amrnbparse), amrnbparse->sinkpad);

  /* create the src pad */
  amrnbparse->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&src_template),
      "src");
  gst_pad_set_query_function (amrnbparse->srcpad,
      GST_DEBUG_FUNCPTR (gst_amrnbparse_query));
  gst_pad_set_query_type_function (amrnbparse->srcpad,
      GST_DEBUG_FUNCPTR (gst_amrnbparse_querytypes));
  gst_pad_set_formats_function (amrnbparse->srcpad,
      GST_DEBUG_FUNCPTR (gst_amrnbparse_formats));
  gst_element_add_pad (GST_ELEMENT (amrnbparse), amrnbparse->srcpad);

  gst_element_set_loop_function (GST_ELEMENT (amrnbparse), gst_amrnbparse_loop);

  /* init rest */
  amrnbparse->ts = 0;
}

/*
 * Position querying.
 */

static const GstFormat *
gst_amrnbparse_formats (GstPad * pad)
{
  static const GstFormat list[] = {
    GST_FORMAT_TIME,
    0
  };

  return list;
}

static const GstQueryType *
gst_amrnbparse_querytypes (GstPad * pad)
{
  static const GstQueryType list[] = {
    GST_QUERY_POSITION,
    GST_QUERY_TOTAL,
    0
  };

  return list;
}

static gboolean
gst_amrnbparse_query (GstPad * pad, GstQueryType type,
    GstFormat * fmt, gint64 * value)
{
  GstAmrnbParse *amrnbparse = GST_AMRNBPARSE (gst_pad_get_parent (pad));
  gboolean res = TRUE;

  if (*fmt != GST_FORMAT_TIME)
    return FALSE;

  switch (type) {
    case GST_QUERY_POSITION:
      *value = amrnbparse->ts;
      break;
    case GST_QUERY_TOTAL:{
      gint64 pos = gst_bytestream_tell (amrnbparse->bs),
          tot = gst_bytestream_length (amrnbparse->bs);

      *value = amrnbparse->ts * ((gdouble) tot / pos);
      break;
    }
    default:
      res = FALSE;
      break;
  }

  return res;
}

/*
 * Data reading.
 */

static gboolean
gst_amrnbparse_handle_event (GstAmrnbParse * amrnbparse, GstEvent * event)
{
  gboolean res;

  if (!event) {
    GST_ELEMENT_ERROR (amrnbparse, RESOURCE, READ, (NULL), (NULL));
    return FALSE;
  }

  GST_LOG ("handling event %d", GST_EVENT_TYPE (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
    case GST_EVENT_INTERRUPT:
      res = FALSE;
      break;
    case GST_EVENT_DISCONTINUOUS:
    default:
      res = TRUE;
      break;
  }

  gst_pad_event_default (amrnbparse->sinkpad, event);

  return res;
}

static guint8 *
gst_amrnbparse_reserve (GstAmrnbParse * amrnbparse, gint size)
{
  gint read;
  guint8 *data;

  GST_DEBUG ("Trying to read %d bytes", size);
  do {
    if ((read = gst_bytestream_peek_bytes (amrnbparse->bs,
                &data, size)) != size) {
      GstEvent *event;
      guint32 avail;

      gst_bytestream_get_status (amrnbparse->bs, &avail, &event);
      if (!gst_amrnbparse_handle_event (amrnbparse, event))
        return NULL;
    }
  } while (read < size);

  GST_DEBUG ("Read %d bytes of data", read);

  return data;
}

static void
gst_amrnbparse_loop (GstElement * element)
{
  const gint block_size[16] = { 12, 13, 15, 17, 19, 20, 26, 31, 5,
    0, 0, 0, 0, 0, 0, 0
  };
  GstAmrnbParse *amrnbparse = GST_AMRNBPARSE (element);
  GstBuffer *buf;
  guint8 *data;
  gint block, mode, read;

  if (!(data = gst_amrnbparse_reserve (amrnbparse, 1)))
    return;

  /* init */
  if (amrnbparse->ts == 0 && data[0] == '#') {
    if (!(data = gst_amrnbparse_reserve (amrnbparse, 6)))
      return;
    if (!memcmp (data, "#!AMR", 5)) {
      GST_LOG ("Found header");
      gst_bytestream_flush_fast (amrnbparse->bs, 5);
      data += 5;
    }
  }

  /* get size */
  mode = (data[0] >> 3) & 0x0F;
  block = block_size[mode] + 1;
  if (!gst_amrnbparse_reserve (amrnbparse, block))
    return;
  read = gst_bytestream_read (amrnbparse->bs, &buf, block);
  g_assert (read == block);

  /* output */
  GST_BUFFER_DURATION (buf) = GST_SECOND * 160 / 8000;
  GST_BUFFER_TIMESTAMP (buf) = amrnbparse->ts;
  amrnbparse->ts += GST_BUFFER_DURATION (buf);

  GST_DEBUG ("Pushing %d bytes of data", block);
  gst_pad_push (amrnbparse->srcpad, GST_DATA (buf));
}

static GstElementStateReturn
gst_amrnbparse_state_change (GstElement * element)
{
  GstAmrnbParse *amrnbparse = GST_AMRNBPARSE (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      if (!(amrnbparse->bs = gst_bytestream_new (amrnbparse->sinkpad)))
        return GST_STATE_FAILURE;
      break;
    case GST_STATE_PAUSED_TO_READY:
      amrnbparse->ts = 0;
      break;
    case GST_STATE_READY_TO_NULL:
      gst_bytestream_destroy (amrnbparse->bs);
      break;
    default:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}
