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

/**
 * SECTION:element-amrnbparse
 * @see_also: #GstAmrnbDec, #GstAmrnbEnc
 *
 * AMR narrowband bitstream parser.
 * 
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch filesrc location=abc.amr ! amrnbparse ! amrnbdec ! audioresample ! audioconvert ! alsasink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include "amrnbparse.h"

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

GST_DEBUG_CATEGORY_STATIC (gst_amrnbparse_debug);
#define GST_CAT_DEFAULT gst_amrnbparse_debug

static const gint block_size[16] = { 12, 13, 15, 17, 19, 20, 26, 31, 5,
  0, 0, 0, 0, 0, 0, 0
};

/*static const GstFormat *gst_amrnbparse_formats (GstPad * pad);*/
static const GstQueryType *gst_amrnbparse_querytypes (GstPad * pad);
static gboolean gst_amrnbparse_query (GstPad * pad, GstQuery * query);

static gboolean gst_amrnbparse_sink_event (GstPad * pad, GstEvent * event);
static gboolean gst_amrnbparse_src_event (GstPad * pad, GstEvent * event);
static GstFlowReturn gst_amrnbparse_chain (GstPad * pad, GstBuffer * buffer);
static void gst_amrnbparse_loop (GstPad * pad);
static gboolean gst_amrnbparse_sink_activate (GstPad * sinkpad);
static gboolean gst_amrnbparse_sink_activate_pull (GstPad * sinkpad,
    gboolean active);
static gboolean gst_amrnbparse_sink_activate_push (GstPad * sinkpad,
    gboolean active);
static GstStateChangeReturn gst_amrnbparse_state_change (GstElement * element,
    GstStateChange transition);

static void gst_amrnbparse_finalize (GObject * object);

#define _do_init(bla) \
    GST_DEBUG_CATEGORY_INIT (gst_amrnbparse_debug, "amrnbparse", 0, "AMR-NB audio stream parser");

GST_BOILERPLATE_FULL (GstAmrnbParse, gst_amrnbparse, GstElement,
    GST_TYPE_ELEMENT, _do_init);

static void
gst_amrnbparse_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstElementDetails details = GST_ELEMENT_DETAILS ("AMR-NB audio stream parser",
      "Codec/Parser/Audio",
      "Adaptive Multi-Rate Narrow-Band audio parser",
      "Ronald Bultje <rbultje@ronald.bitfreak.net>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));

  gst_element_class_set_details (element_class, &details);
}

static void
gst_amrnbparse_class_init (GstAmrnbParseClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  object_class->finalize = gst_amrnbparse_finalize;

  element_class->change_state = GST_DEBUG_FUNCPTR (gst_amrnbparse_state_change);
}

static void
gst_amrnbparse_init (GstAmrnbParse * amrnbparse, GstAmrnbParseClass * klass)
{
  /* create the sink pad */
  amrnbparse->sinkpad =
      gst_pad_new_from_static_template (&sink_template, "sink");
  gst_pad_set_chain_function (amrnbparse->sinkpad,
      GST_DEBUG_FUNCPTR (gst_amrnbparse_chain));
  gst_pad_set_event_function (amrnbparse->sinkpad,
      GST_DEBUG_FUNCPTR (gst_amrnbparse_sink_event));
  gst_pad_set_activate_function (amrnbparse->sinkpad,
      gst_amrnbparse_sink_activate);
  gst_pad_set_activatepull_function (amrnbparse->sinkpad,
      gst_amrnbparse_sink_activate_pull);
  gst_pad_set_activatepush_function (amrnbparse->sinkpad,
      gst_amrnbparse_sink_activate_push);
  gst_element_add_pad (GST_ELEMENT (amrnbparse), amrnbparse->sinkpad);

  /* create the src pad */
  amrnbparse->srcpad = gst_pad_new_from_static_template (&src_template, "src");
  gst_pad_set_event_function (amrnbparse->srcpad,
      GST_DEBUG_FUNCPTR (gst_amrnbparse_src_event));
  gst_pad_set_query_function (amrnbparse->srcpad,
      GST_DEBUG_FUNCPTR (gst_amrnbparse_query));
  gst_pad_set_query_type_function (amrnbparse->srcpad,
      GST_DEBUG_FUNCPTR (gst_amrnbparse_querytypes));
  gst_pad_use_fixed_caps (amrnbparse->srcpad);
  gst_element_add_pad (GST_ELEMENT (amrnbparse), amrnbparse->srcpad);

  amrnbparse->adapter = gst_adapter_new ();

  /* init rest */
  amrnbparse->ts = 0;
}

static void
gst_amrnbparse_finalize (GObject * object)
{
  GstAmrnbParse *amrnbparse;

  amrnbparse = GST_AMRNBPARSE (object);

  gst_adapter_clear (amrnbparse->adapter);
  g_object_unref (amrnbparse->adapter);

  G_OBJECT_CLASS (parent_class)->finalize (object);
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
      gint64 cur;

      gst_query_parse_position (query, &format, NULL);

      if (format != GST_FORMAT_TIME)
        return FALSE;

      cur = amrnbparse->ts;

      gst_query_set_position (query, GST_FORMAT_TIME, cur);
      res = TRUE;
      break;
    }
    case GST_QUERY_DURATION:
    {
      GstFormat format;
      gint64 tot;
      GstPad *peer;

      gst_query_parse_duration (query, &format, NULL);

      if (format != GST_FORMAT_TIME)
        return FALSE;

      tot = -1;
      res = FALSE;

      peer = gst_pad_get_peer (amrnbparse->sinkpad);
      if (peer) {
        GstFormat pformat;
        gint64 ptot;

        pformat = GST_FORMAT_BYTES;
        res = gst_pad_query_duration (peer, &pformat, &ptot);
        if (res && amrnbparse->block) {
          tot =
              gst_util_uint64_scale_int (ptot, 20 * GST_MSECOND,
              amrnbparse->block);
        }
        gst_object_unref (GST_OBJECT (peer));
      }
      gst_query_set_duration (query, GST_FORMAT_TIME, tot);
      break;
    }
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }
  return res;
}


static gboolean
gst_amrnbparse_handle_pull_seek (GstAmrnbParse * amrnbparse, GstPad * pad,
    GstEvent * event)
{
  GstFormat format;
  gdouble rate;
  GstSeekFlags flags;
  GstSeekType cur_type, stop_type;
  gint64 cur, stop;
  gint64 byte_cur = -1, byte_stop = -1;
  gboolean flush;

  gst_event_parse_seek (event, &rate, &format, &flags, &cur_type, &cur,
      &stop_type, &stop);

  GST_DEBUG_OBJECT (amrnbparse, "Performing seek to %" GST_TIME_FORMAT,
      GST_TIME_ARGS (cur));

  /* For any format other than TIME, see if upstream handles
   * it directly or fail. For TIME, try upstream, but do it ourselves if
   * it fails upstream */
  if (format != GST_FORMAT_TIME) {
    return gst_pad_push_event (amrnbparse->sinkpad, event);
  } else {
    if (gst_pad_push_event (amrnbparse->sinkpad, event))
      return TRUE;
  }

  flush = flags & GST_SEEK_FLAG_FLUSH;

  /* send flush start */
  if (flush)
    gst_pad_push_event (amrnbparse->sinkpad, gst_event_new_flush_start ());
  /* we only handle FLUSH seeks at the moment */
  else
    return FALSE;

  /* grab streaming lock, this should eventually be possible, either
   * because the task is paused or our streaming thread stopped
   * because our peer is flushing. */
  GST_PAD_STREAM_LOCK (amrnbparse->sinkpad);

  /* Convert the TIME to the appropriate BYTE position at which to resume
   * decoding. */
  cur = cur / (20 * GST_MSECOND) * (20 * GST_MSECOND);
  if (cur != -1)
    byte_cur = amrnbparse->block * (cur / 20 / GST_MSECOND) + 6;
  if (stop != -1)
    byte_stop = amrnbparse->block * (stop / 20 / GST_MSECOND) + 6;
  amrnbparse->offset = byte_cur;
  amrnbparse->ts = cur;

  GST_DEBUG_OBJECT (amrnbparse, "Seeking to byte range %" G_GINT64_FORMAT
      " to %" G_GINT64_FORMAT, byte_cur, cur);

  /* and prepare to continue streaming */
  /* send flush stop, peer will accept data and events again. We
   * are not yet providing data as we still have the STREAM_LOCK. */
  gst_pad_push_event (amrnbparse->sinkpad, gst_event_new_flush_stop ());
  gst_pad_push_event (amrnbparse->srcpad, gst_event_new_new_segment (FALSE,
          rate, format, cur, -1, cur));

  /* and restart the task in case it got paused explicitely or by
   * the FLUSH_START event we pushed out. */
  gst_pad_start_task (amrnbparse->sinkpad,
      (GstTaskFunction) gst_amrnbparse_loop, amrnbparse->sinkpad);

  /* and release the lock again so we can continue streaming */
  GST_PAD_STREAM_UNLOCK (amrnbparse->sinkpad);

  return TRUE;
}

static gboolean
gst_amrnbparse_handle_push_seek (GstAmrnbParse * amrnbparse, GstPad * pad,
    GstEvent * event)
{
  GstFormat format;
  gdouble rate;
  GstSeekFlags flags;
  GstSeekType cur_type, stop_type;
  gint64 cur, stop;
  gint64 byte_cur = -1, byte_stop = -1;

  gst_event_parse_seek (event, &rate, &format, &flags, &cur_type, &cur,
      &stop_type, &stop);

  GST_DEBUG_OBJECT (amrnbparse, "Performing seek to %" GST_TIME_FORMAT,
      GST_TIME_ARGS (cur));

  /* For any format other than TIME, see if upstream handles
   * it directly or fail. For TIME, try upstream, but do it ourselves if
   * it fails upstream */
  if (format != GST_FORMAT_TIME) {
    return gst_pad_push_event (amrnbparse->sinkpad, event);
  } else {
    if (gst_pad_push_event (amrnbparse->sinkpad, event))
      return TRUE;
  }

  /* Convert the TIME to the appropriate BYTE position at which to resume
   * decoding. */
  cur = cur / (20 * GST_MSECOND) * (20 * GST_MSECOND);
  if (cur != -1)
    byte_cur = amrnbparse->block * (cur / 20 / GST_MSECOND) + 6;
  if (stop != -1)
    byte_stop = amrnbparse->block * (stop / 20 / GST_MSECOND) + 6;
  amrnbparse->ts = cur;

  GST_DEBUG_OBJECT (amrnbparse, "Seeking to byte range %" G_GINT64_FORMAT
      " to %" G_GINT64_FORMAT, byte_cur, byte_stop);

  /* Send BYTE based seek upstream */
  event = gst_event_new_seek (rate, GST_FORMAT_BYTES, flags, cur_type,
      byte_cur, stop_type, byte_stop);

  return gst_pad_push_event (amrnbparse->sinkpad, event);
}

static gboolean
gst_amrnbparse_src_event (GstPad * pad, GstEvent * event)
{
  GstAmrnbParse *amrnbparse = GST_AMRNBPARSE (gst_pad_get_parent (pad));
  gboolean res;

  GST_DEBUG_OBJECT (amrnbparse, "handling event %d", GST_EVENT_TYPE (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
      if (amrnbparse->seek_handler)
        res = amrnbparse->seek_handler (amrnbparse, pad, event);
      else
        res = FALSE;
      break;
    default:
      res = gst_pad_push_event (amrnbparse->sinkpad, event);
      break;
  }
  gst_object_unref (amrnbparse);

  return res;
}

/*
 * Data reading.
 */
static gboolean
gst_amrnbparse_sink_event (GstPad * pad, GstEvent * event)
{
  GstAmrnbParse *amrnbparse;
  gboolean res;

  amrnbparse = GST_AMRNBPARSE (gst_pad_get_parent (pad));

  GST_LOG ("handling event %d", GST_EVENT_TYPE (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      res = gst_pad_push_event (amrnbparse->srcpad, event);
      break;
    case GST_EVENT_FLUSH_STOP:
      gst_adapter_clear (amrnbparse->adapter);
      gst_segment_init (&amrnbparse->segment, GST_FORMAT_TIME);
      res = gst_pad_push_event (amrnbparse->srcpad, event);
      break;
    case GST_EVENT_EOS:
      res = gst_pad_push_event (amrnbparse->srcpad, event);
      break;
    case GST_EVENT_NEWSEGMENT:
    {
      /* eat for now, we send a newsegment at start with infinite
       * duration. */
      gst_event_unref (event);
      res = TRUE;
      break;
    }
    default:
      res = gst_pad_push_event (amrnbparse->srcpad, event);
      break;
  }
  gst_object_unref (amrnbparse);

  return res;
}

/* streaming mode */
static GstFlowReturn
gst_amrnbparse_chain (GstPad * pad, GstBuffer * buffer)
{
  GstAmrnbParse *amrnbparse;
  GstFlowReturn res;
  gint mode;
  const guint8 *data;
  GstBuffer *out;
  GstClockTime timestamp;

  amrnbparse = GST_AMRNBPARSE (GST_PAD_PARENT (pad));

  timestamp = GST_BUFFER_TIMESTAMP (buffer);
  if (GST_CLOCK_TIME_IS_VALID (timestamp)) {
    GST_DEBUG_OBJECT (amrnbparse, "Lock on timestamp %" GST_TIME_FORMAT,
        GST_TIME_ARGS (timestamp));
    amrnbparse->ts = timestamp;
  }

  gst_adapter_push (amrnbparse->adapter, buffer);

  res = GST_FLOW_OK;

  /* init */
  if (amrnbparse->need_header) {
    GstEvent *segev;
    GstCaps *caps;

    if (gst_adapter_available (amrnbparse->adapter) < 6)
      goto done;

    data = gst_adapter_peek (amrnbparse->adapter, 6);
    if (memcmp (data, "#!AMR\n", 6) != 0)
      goto done;

    gst_adapter_flush (amrnbparse->adapter, 6);

    amrnbparse->need_header = FALSE;

    caps = gst_caps_new_simple ("audio/AMR",
        "rate", G_TYPE_INT, 8000, "channels", G_TYPE_INT, 1, NULL);
    gst_pad_set_caps (amrnbparse->srcpad, caps);
    gst_caps_unref (caps);

    GST_DEBUG_OBJECT (amrnbparse, "Sending first segment");
    segev = gst_event_new_new_segment_full (FALSE, 1.0, 1.0,
        GST_FORMAT_TIME, 0, -1, 0);

    gst_pad_push_event (amrnbparse->srcpad, segev);
  }

  while (TRUE) {
    if (gst_adapter_available (amrnbparse->adapter) < 1)
      break;
    data = gst_adapter_peek (amrnbparse->adapter, 1);

    /* get size */
    mode = (data[0] >> 3) & 0x0F;
    amrnbparse->block = block_size[mode] + 1;   /* add one for the mode */

    if (gst_adapter_available (amrnbparse->adapter) < amrnbparse->block)
      break;

    out = gst_buffer_new_and_alloc (amrnbparse->block);

    data = gst_adapter_peek (amrnbparse->adapter, amrnbparse->block);
    memcpy (GST_BUFFER_DATA (out), data, amrnbparse->block);

    /* timestamp, all constants that won't overflow */
    GST_BUFFER_DURATION (out) = GST_SECOND * 160 / 8000;
    GST_BUFFER_TIMESTAMP (out) = amrnbparse->ts;
    if (GST_CLOCK_TIME_IS_VALID (amrnbparse->ts))
      amrnbparse->ts += GST_BUFFER_DURATION (out);

    gst_buffer_set_caps (out, GST_PAD_CAPS (amrnbparse->srcpad));

    GST_DEBUG_OBJECT (amrnbparse, "Pushing %d bytes of data",
        amrnbparse->block);
    res = gst_pad_push (amrnbparse->srcpad, out);

    gst_adapter_flush (amrnbparse->adapter, amrnbparse->block);
  }
done:

  return res;
}

static gboolean
gst_amrnbparse_pull_header (GstAmrnbParse * amrnbparse)
{
  GstBuffer *buffer;
  GstFlowReturn ret;
  guint8 *data;
  gint size;

  ret = gst_pad_pull_range (amrnbparse->sinkpad, 0, 6, &buffer);
  if (ret != GST_FLOW_OK)
    return FALSE;

  data = GST_BUFFER_DATA (buffer);
  size = GST_BUFFER_SIZE (buffer);
  if (size < 6)
    goto not_enough;

  if (memcmp (data, "#!AMR\n", 6))
    goto no_header;

  gst_buffer_unref (buffer);

  amrnbparse->offset = 6;
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
  gint mode;
  GstFlowReturn ret;

  amrnbparse = GST_AMRNBPARSE (GST_PAD_PARENT (pad));

  /* init */
  if (G_UNLIKELY (amrnbparse->need_header)) {
    GstCaps *caps;

    if (!gst_amrnbparse_pull_header (amrnbparse)) {
      GST_ELEMENT_ERROR (amrnbparse, STREAM, WRONG_TYPE, (NULL), (NULL));
      GST_LOG_OBJECT (amrnbparse, "could not read header");
      goto need_pause;
    }

    caps = gst_caps_new_simple ("audio/AMR",
        "rate", G_TYPE_INT, 8000, "channels", G_TYPE_INT, 1, NULL);
    gst_pad_set_caps (amrnbparse->srcpad, caps);
    gst_caps_unref (caps);

    GST_DEBUG_OBJECT (amrnbparse, "Sending newsegment event");
    gst_pad_push_event (amrnbparse->srcpad,
        gst_event_new_new_segment_full (FALSE, 1.0, 1.0,
            GST_FORMAT_TIME, 0, -1, 0));

    amrnbparse->need_header = FALSE;
  }

  ret =
      gst_pad_pull_range (amrnbparse->sinkpad, amrnbparse->offset, 1, &buffer);

  if (ret == GST_FLOW_UNEXPECTED)
    goto eos;
  else if (ret != GST_FLOW_OK)
    goto need_pause;

  data = GST_BUFFER_DATA (buffer);
  size = GST_BUFFER_SIZE (buffer);

  /* EOS */
  if (size < 1) {
    gst_buffer_unref (buffer);
    goto eos;
  }

  /* get size */
  mode = (data[0] >> 3) & 0x0F;
  amrnbparse->block = block_size[mode] + 1;     /* add one for the mode */

  gst_buffer_unref (buffer);

  ret =
      gst_pad_pull_range (amrnbparse->sinkpad, amrnbparse->offset,
      amrnbparse->block, &buffer);

  if (ret == GST_FLOW_UNEXPECTED)
    goto eos;
  else if (ret != GST_FLOW_OK)
    goto need_pause;

  if (GST_BUFFER_SIZE (buffer) < amrnbparse->block) {
    gst_buffer_unref (buffer);
    goto eos;
  }

  amrnbparse->offset += amrnbparse->block;

  /* output */
  buffer = gst_buffer_make_metadata_writable (buffer);
  GST_BUFFER_DURATION (buffer) = GST_SECOND * 160 / 8000;
  GST_BUFFER_TIMESTAMP (buffer) = amrnbparse->ts;

  gst_buffer_set_caps (buffer, GST_PAD_CAPS (amrnbparse->srcpad));

  GST_DEBUG_OBJECT (amrnbparse, "Pushing %2d bytes, ts=%" GST_TIME_FORMAT,
      amrnbparse->block, GST_TIME_ARGS (amrnbparse->ts));

  ret = gst_pad_push (amrnbparse->srcpad, buffer);

  if (G_UNLIKELY (ret != GST_FLOW_OK)) {
    GST_DEBUG_OBJECT (amrnbparse, "Flow: %s", gst_flow_get_name (ret));
    if (GST_FLOW_IS_FATAL (ret) || ret == GST_FLOW_NOT_LINKED) {
      if (ret == GST_FLOW_UNEXPECTED) {
        /* we don't do seeking yet, so no segment flag to check here either */
        if (0) {
          /* post segment_done message here one day when seeking works */
        } else {
          GST_LOG_OBJECT (amrnbparse, "Sending EOS at end of segment");
          gst_pad_push_event (amrnbparse->srcpad, gst_event_new_eos ());
        }
      } else {
        GST_ELEMENT_ERROR (amrnbparse, STREAM, FAILED, (NULL),
            ("streaming stopped, reason: %s", gst_flow_get_name (ret)));
        gst_pad_push_event (amrnbparse->srcpad, gst_event_new_eos ());
      }
    }
    goto need_pause;
  }

  amrnbparse->ts += GST_BUFFER_DURATION (buffer);

  return;

need_pause:
  {
    GST_LOG_OBJECT (amrnbparse, "pausing task");
    gst_pad_pause_task (pad);
    return;
  }
eos:
  {
    GST_LOG_OBJECT (amrnbparse, "pausing task (eos)");
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

  amrnbparse = GST_AMRNBPARSE (gst_pad_get_parent (sinkpad));

  if (gst_pad_check_pull_range (sinkpad)) {
    GST_DEBUG ("Trying to activate in pull mode");
    amrnbparse->seekable = TRUE;
    amrnbparse->ts = 0;
    result = gst_pad_activate_pull (sinkpad, TRUE);
  } else {
    GST_DEBUG ("Try to activate in push mode");
    amrnbparse->seekable = FALSE;
    result = gst_pad_activate_push (sinkpad, TRUE);
  }

  gst_object_unref (amrnbparse);
  return result;
}

static gboolean
gst_amrnbparse_sink_activate_push (GstPad * sinkpad, gboolean active)
{
  GstAmrnbParse *amrnbparse = GST_AMRNBPARSE (gst_pad_get_parent (sinkpad));

  if (active) {
    amrnbparse->seek_handler = gst_amrnbparse_handle_push_seek;
  } else {
    amrnbparse->seek_handler = NULL;
  }
  gst_object_unref (amrnbparse);

  return TRUE;
}

static gboolean
gst_amrnbparse_sink_activate_pull (GstPad * sinkpad, gboolean active)
{
  gboolean result;
  GstAmrnbParse *amrnbparse = GST_AMRNBPARSE (gst_pad_get_parent (sinkpad));

  if (active) {
    amrnbparse->seek_handler = gst_amrnbparse_handle_pull_seek;
    result = gst_pad_start_task (sinkpad,
        (GstTaskFunction) gst_amrnbparse_loop, sinkpad);
  } else {
    amrnbparse->seek_handler = NULL;
    result = gst_pad_stop_task (sinkpad);
  }

  return result;
}

static GstStateChangeReturn
gst_amrnbparse_state_change (GstElement * element, GstStateChange transition)
{
  GstAmrnbParse *amrnbparse;
  GstStateChangeReturn ret;

  amrnbparse = GST_AMRNBPARSE (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      amrnbparse->need_header = TRUE;
      amrnbparse->ts = -1;
      amrnbparse->block = 0;
      gst_segment_init (&amrnbparse->segment, GST_FORMAT_TIME);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}
