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
    GST_STATIC_CAPS ("audio/AMR, " "rate = (int) 8000, " "channels = (int) 1")
    );

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-amr-nb-sh")
    );

static const gint block_size[16] = { 12, 13, 15, 17, 19, 20, 26, 31, 5,
  0, 0, 0, 0, 0, 0, 0
};

static void gst_amrnbparse_base_init (GstAmrnbParseClass * klass);
static void gst_amrnbparse_class_init (GstAmrnbParseClass * klass);
static void gst_amrnbparse_init (GstAmrnbParse * amrnbparse);

//static const GstFormat *gst_amrnbparse_formats (GstPad * pad);
static const GstQueryType *gst_amrnbparse_querytypes (GstPad * pad);
static gboolean gst_amrnbparse_query (GstPad * pad, GstQuery * query);

static gboolean gst_amrnbparse_event (GstPad * pad, GstEvent * event);
static GstFlowReturn gst_amrnbparse_chain (GstPad * pad, GstBuffer * buffer);
static void gst_amrnbparse_loop (GstPad * pad);
static gboolean gst_amrnbparse_sink_activate (GstPad * sinkpad);
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
  /* create the sink pad */
  amrnbparse->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&sink_template),
      "sink");
  gst_pad_set_chain_function (amrnbparse->sinkpad,
      GST_DEBUG_FUNCPTR (gst_amrnbparse_chain));
  gst_pad_set_event_function (amrnbparse->sinkpad,
      GST_DEBUG_FUNCPTR (gst_amrnbparse_event));
/*  gst_pad_set_loop_function (amrnbparse->sinkpad,
      GST_DEBUG_FUNCPTR (gst_amrnbparse_loop));
*/
  gst_pad_set_activate_function (amrnbparse->sinkpad,
      gst_amrnbparse_sink_activate);
  gst_element_add_pad (GST_ELEMENT (amrnbparse), amrnbparse->sinkpad);

  /* create the src pad */
  amrnbparse->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&src_template),
      "src");
  gst_pad_set_query_function (amrnbparse->srcpad,
      GST_DEBUG_FUNCPTR (gst_amrnbparse_query));
  gst_pad_set_query_type_function (amrnbparse->srcpad,
      GST_DEBUG_FUNCPTR (gst_amrnbparse_querytypes));
  gst_element_add_pad (GST_ELEMENT (amrnbparse), amrnbparse->srcpad);

  amrnbparse->adapter = gst_adapter_new ();

  /* init rest */
  amrnbparse->ts = 0;
}

/*
 * Position querying.
 */

#if 0
static const GstFormat *
gst_amrnbparse_formats (GstPad * pad)
{
  static const GstFormat list[] = {
    GST_FORMAT_TIME,
    0
  };

  return list;
}
#endif

static const GstQueryType *
gst_amrnbparse_querytypes (GstPad * pad)
{
  static const GstQueryType list[] = {
    GST_QUERY_POSITION,
    0
  };

  return list;
}

static gboolean
gst_amrnbparse_query (GstPad * pad, GstQuery * query)
{
  GstAmrnbParse *amrnbparse;
  gboolean res = TRUE;

  amrnbparse = GST_AMRNBPARSE (GST_PAD_PARENT (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstFormat format;
      gint64 cur, tot;
      GstPad *peer;

      gst_query_parse_position (query, &format, NULL, NULL);

      if (format != GST_FORMAT_TIME)
        return FALSE;

      tot = -1;

      peer = gst_pad_get_peer (amrnbparse->sinkpad);
      if (peer) {
        GstFormat pformat;
        gint64 pcur, ptot;

        pformat = GST_FORMAT_BYTES;
        res = gst_pad_query_position (peer, &pformat, &pcur, &ptot);
        gst_object_unref (GST_OBJECT (peer));
        if (res) {
          tot = amrnbparse->ts * ((gdouble) ptot / pcur);
        }
      }
      cur = amrnbparse->ts;

      gst_query_set_position (query, GST_FORMAT_TIME, cur, tot);
      res = TRUE;
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
gst_amrnbparse_event (GstPad * pad, GstEvent * event)
{
  GstAmrnbParse *amrnbparse;
  gboolean res;

  amrnbparse = GST_AMRNBPARSE (GST_PAD_PARENT (pad));

  GST_LOG ("handling event %d", GST_EVENT_TYPE (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
    default:
      break;
  }

  res = gst_pad_event_default (amrnbparse->sinkpad, event);

  return res;
}

/* streaming mode */
static GstFlowReturn
gst_amrnbparse_chain (GstPad * pad, GstBuffer * buffer)
{
  GstAmrnbParse *amrnbparse;
  GstFlowReturn res;
  gint block, mode;
  const guint8 *data;
  GstBuffer *out;

  amrnbparse = GST_AMRNBPARSE (GST_PAD_PARENT (pad));

  gst_adapter_push (amrnbparse->adapter, buffer);

  res = GST_FLOW_OK;

  /* init */
  if (amrnbparse->need_header) {

    if (gst_adapter_available (amrnbparse->adapter) < 6)
      goto done;

    data = gst_adapter_peek (amrnbparse->adapter, 6);
    if (memcmp (data, "#!AMR\n", 6) != 0)
      goto done;

    gst_adapter_flush (amrnbparse->adapter, 6);

    amrnbparse->need_header = FALSE;
  }

  while (TRUE) {
    if (gst_adapter_available (amrnbparse->adapter) < 1)
      break;
    data = gst_adapter_peek (amrnbparse->adapter, 1);

    /* get size */
    mode = (data[0] >> 3) & 0x0F;
    block = block_size[mode] + 1;       /* add one for the mode */

    if (gst_adapter_available (amrnbparse->adapter) < block)
      break;

    out = gst_buffer_new_and_alloc (block);

    data = gst_adapter_peek (amrnbparse->adapter, block);
    memcpy (GST_BUFFER_DATA (out), data, block);

    /* output */
    GST_BUFFER_DURATION (out) = GST_SECOND * 160 / 8000;
    GST_BUFFER_TIMESTAMP (out) = amrnbparse->ts;
    amrnbparse->ts += GST_BUFFER_DURATION (out);
    gst_buffer_set_caps (out,
        (GstCaps *) gst_pad_get_pad_template_caps (amrnbparse->srcpad));

    GST_DEBUG ("Pushing %d bytes of data", block);
    res = gst_pad_push (amrnbparse->srcpad, out);

    gst_adapter_flush (amrnbparse->adapter, block);
  }
done:

  return res;
}

static gboolean
gst_amrnbparse_read_header (GstAmrnbParse * amrnbparse)
{
  GstBuffer *buffer;
  GstFlowReturn ret;
  guint8 *data;
  gint size;

  ret =
      gst_pad_pull_range (amrnbparse->sinkpad, amrnbparse->offset, 6, &buffer);
  if (ret != GST_FLOW_OK)
    return FALSE;

  data = GST_BUFFER_DATA (buffer);
  size = GST_BUFFER_SIZE (buffer);
  if (size < 6)
    goto not_enough;

  if (memcmp (data, "#!AMR\n", 6))
    goto no_header;

  amrnbparse->offset += 6;

  return TRUE;

not_enough:
  {
    gst_buffer_unref (buffer);
    return FALSE;
  }
no_header:
  {
    gst_buffer_unref (buffer);
    return FALSE;
  }
}

/* random access mode, could just read a fixed size buffer and push it to
 * the chain function but we don't... */
static void
gst_amrnbparse_loop (GstPad * pad)
{
  GstAmrnbParse *amrnbparse;
  GstBuffer *buffer;
  guint8 *data;
  gint size;
  gint block, mode;
  GstFlowReturn ret;

  amrnbparse = GST_AMRNBPARSE (GST_PAD_PARENT (pad));

  /* init */
  if (amrnbparse->need_header) {
    gboolean got_header;

    got_header = gst_amrnbparse_read_header (amrnbparse);
    if (!got_header) {
      GST_LOG_OBJECT (amrnbparse, "could not read header");
      goto need_pause;
    }
    amrnbparse->need_header = FALSE;
  }

  ret =
      gst_pad_pull_range (amrnbparse->sinkpad, amrnbparse->offset, 1, &buffer);
  if (ret != GST_FLOW_OK)
    goto need_pause;

  data = GST_BUFFER_DATA (buffer);
  size = GST_BUFFER_SIZE (buffer);

  /* EOS */
  if (size < 1)
    goto eos;

  /* get size */
  mode = (data[0] >> 3) & 0x0F;
  block = block_size[mode] + 1; /* add one for the mode */

  gst_buffer_unref (buffer);

  ret =
      gst_pad_pull_range (amrnbparse->sinkpad, amrnbparse->offset, block,
      &buffer);
  if (ret != GST_FLOW_OK)
    goto need_pause;

  amrnbparse->offset += block;

  /* output */
  GST_BUFFER_DURATION (buffer) = GST_SECOND * 160 / 8000;
  GST_BUFFER_TIMESTAMP (buffer) = amrnbparse->ts;
  amrnbparse->ts += GST_BUFFER_DURATION (buffer);
  gst_buffer_set_caps (buffer,
      (GstCaps *) gst_pad_get_pad_template_caps (amrnbparse->srcpad));

  GST_DEBUG ("Pushing %d bytes of data", block);
  ret = gst_pad_push (amrnbparse->srcpad, buffer);
  if (ret != GST_FLOW_OK)
    goto need_pause;

  return;

need_pause:
  {
    GST_LOG_OBJECT (amrnbparse, "pausing task");
    gst_pad_pause_task (pad);
    return;
  }
eos:
  {
    GST_LOG_OBJECT (amrnbparse, "pausing task");
    gst_pad_push_event (amrnbparse->srcpad, gst_event_new_eos ());
    gst_pad_pause_task (pad);
    return;
  }
}

static gboolean
gst_amrnbparse_sink_activate (GstPad * sinkpad)
{
  gboolean result = FALSE;
  GstAmrnbParse *amrnbparse;
  GstActivateMode mode;

  amrnbparse = GST_AMRNBPARSE (GST_OBJECT_PARENT (sinkpad));

  GST_LOCK (sinkpad);
  mode = GST_PAD_ACTIVATE_MODE (sinkpad);
  GST_UNLOCK (sinkpad);

  switch (mode) {
    case GST_ACTIVATE_PUSH:
      amrnbparse->seekable = FALSE;
      result = TRUE;
      break;
    case GST_ACTIVATE_PULL:
      /*gst_pad_peer_set_active (sinkpad, mode); */

      amrnbparse->need_header = TRUE;
      amrnbparse->seekable = TRUE;
      amrnbparse->ts = 0;

      result = gst_pad_start_task (sinkpad,
          (GstTaskFunction) gst_amrnbparse_loop, sinkpad);
      break;
    case GST_ACTIVATE_NONE:
      /* step 1, unblock clock sync (if any) */

      /* step 2, make sure streaming finishes */
      result = gst_pad_stop_task (sinkpad);
      break;
  }
  return result;
}

static GstElementStateReturn
gst_amrnbparse_state_change (GstElement * element)
{
  GstAmrnbParse *amrnbparse;
  gint transition;
  GstElementStateReturn ret;

  amrnbparse = GST_AMRNBPARSE (element);
  transition = GST_STATE_TRANSITION (element);

  switch (transition) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element);

  switch (transition) {
    case GST_STATE_PAUSED_TO_READY:
      break;
    case GST_STATE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}
