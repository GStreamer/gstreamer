/* GStreamer Adaptive Multi-Rate Wide-Band (AMR-WB) plugin
 * Copyright (C) 2006 Edgard Lima <edgard.lima@indt.org.br>
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
 * SECTION:element-amrwbparse
 * @see_also: #GstAmrwbDec, #GstAmrwbEnc
 *
 * This is an AMR wideband parser.
 * 
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch filesrc location=abc.amr ! amrwbparse ! amrwbdec ! audioresample ! audioconvert ! alsasink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include "gstamrwbparse.h"

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/AMR-WB, "
        "rate = (int) 16000, " "channels = (int) 1")
    );

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-amr-wb-sh")
    );

GST_DEBUG_CATEGORY_STATIC (gst_amrwbparse_debug);
#define GST_CAT_DEFAULT gst_amrwbparse_debug

extern const UWord8 block_size[];

#define AMRWB_HEADER_SIZE 9
#define AMRWB_HEADER_STR "#!AMR-WB\n"

static void gst_amrwbparse_base_init (gpointer klass);
static void gst_amrwbparse_class_init (GstAmrwbParseClass * klass);
static void gst_amrwbparse_init (GstAmrwbParse * amrwbparse,
    GstAmrwbParseClass * klass);

static const GstQueryType *gst_amrwbparse_querytypes (GstPad * pad);
static gboolean gst_amrwbparse_query (GstPad * pad, GstQuery * query);

static gboolean gst_amrwbparse_sink_event (GstPad * pad, GstEvent * event);
static gboolean gst_amrwbparse_src_event (GstPad * pad, GstEvent * event);
static GstFlowReturn gst_amrwbparse_chain (GstPad * pad, GstBuffer * buffer);
static void gst_amrwbparse_loop (GstPad * pad);
static gboolean gst_amrwbparse_sink_activate (GstPad * sinkpad);
static gboolean gst_amrwbparse_sink_activate_pull (GstPad * sinkpad,
    gboolean active);
static gboolean gst_amrwbparse_sink_activate_push (GstPad * sinkpad,
    gboolean active);
static GstStateChangeReturn gst_amrwbparse_state_change (GstElement * element,
    GstStateChange transition);

static void gst_amrwbparse_finalize (GObject * object);

#define _do_init(bla) \
    GST_DEBUG_CATEGORY_INIT (gst_amrwbparse_debug, "amrwbparse", 0, "AMR-WB audio stream parser");

GST_BOILERPLATE_FULL (GstAmrwbParse, gst_amrwbparse, GstElement,
    GST_TYPE_ELEMENT, _do_init);

static void
gst_amrwbparse_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstElementDetails details = GST_ELEMENT_DETAILS ("AMR-WB audio stream parser",
      "Codec/Parser/Audio",
      "Adaptive Multi-Rate WideBand audio parser",
      "Renato Filho <renato.filho@indt.org.br>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));

  gst_element_class_set_details (element_class, &details);

}

static void
gst_amrwbparse_class_init (GstAmrwbParseClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  object_class->finalize = gst_amrwbparse_finalize;

  element_class->change_state = GST_DEBUG_FUNCPTR (gst_amrwbparse_state_change);
}

static void
gst_amrwbparse_init (GstAmrwbParse * amrwbparse, GstAmrwbParseClass * klass)
{
  /* create the sink pad */
  amrwbparse->sinkpad =
      gst_pad_new_from_static_template (&sink_template, "sink");
  gst_pad_set_chain_function (amrwbparse->sinkpad,
      GST_DEBUG_FUNCPTR (gst_amrwbparse_chain));
  gst_pad_set_event_function (amrwbparse->sinkpad,
      GST_DEBUG_FUNCPTR (gst_amrwbparse_sink_event));
  gst_pad_set_activate_function (amrwbparse->sinkpad,
      gst_amrwbparse_sink_activate);
  gst_pad_set_activatepull_function (amrwbparse->sinkpad,
      gst_amrwbparse_sink_activate_pull);
  gst_pad_set_activatepush_function (amrwbparse->sinkpad,
      gst_amrwbparse_sink_activate_push);
  gst_element_add_pad (GST_ELEMENT (amrwbparse), amrwbparse->sinkpad);

  /* create the src pad */
  amrwbparse->srcpad = gst_pad_new_from_static_template (&src_template, "src");
  gst_pad_set_event_function (amrwbparse->srcpad,
      GST_DEBUG_FUNCPTR (gst_amrwbparse_src_event));
  gst_pad_set_query_function (amrwbparse->srcpad,
      GST_DEBUG_FUNCPTR (gst_amrwbparse_query));
  gst_pad_set_query_type_function (amrwbparse->srcpad,
      GST_DEBUG_FUNCPTR (gst_amrwbparse_querytypes));
  gst_element_add_pad (GST_ELEMENT (amrwbparse), amrwbparse->srcpad);

  amrwbparse->adapter = gst_adapter_new ();

  /* init rest */
  amrwbparse->ts = 0;
}

static void
gst_amrwbparse_finalize (GObject * object)
{
  GstAmrwbParse *amrwbparse;

  amrwbparse = GST_AMRWBPARSE (object);

  gst_adapter_clear (amrwbparse->adapter);
  g_object_unref (amrwbparse->adapter);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}


static const GstQueryType *
gst_amrwbparse_querytypes (GstPad * pad)
{
  static const GstQueryType list[] = {
    GST_QUERY_POSITION,
    GST_QUERY_DURATION,
    0
  };

  return list;
}

static gboolean
gst_amrwbparse_query (GstPad * pad, GstQuery * query)
{
  GstAmrwbParse *amrwbparse;
  gboolean res = TRUE;

  amrwbparse = GST_AMRWBPARSE (gst_pad_get_parent (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstFormat format;
      gint64 cur;

      gst_query_parse_position (query, &format, NULL);

      if (format != GST_FORMAT_TIME) {
        res = FALSE;
        break;
      }

      cur = amrwbparse->ts;

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

      if (format != GST_FORMAT_TIME) {
        res = FALSE;
        break;
      }

      tot = -1;
      res = FALSE;

      peer = gst_pad_get_peer (amrwbparse->sinkpad);
      if (peer) {
        GstFormat pformat;
        gint64 ptot;

        pformat = GST_FORMAT_BYTES;
        res = gst_pad_query_duration (peer, &pformat, &ptot);
        if (res && amrwbparse->block) {
          tot = gst_util_uint64_scale_int (ptot, 20 * GST_MSECOND,
              amrwbparse->block);
        }
        gst_object_unref (GST_OBJECT (peer));
      }
      gst_query_set_duration (query, GST_FORMAT_TIME, tot);
      res = TRUE;
      break;
    }
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }

  gst_object_unref (amrwbparse);
  return res;
}


static gboolean
gst_amrwbparse_handle_pull_seek (GstAmrwbParse * amrwbparse, GstPad * pad,
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

  GST_DEBUG_OBJECT (amrwbparse, "Performing seek to %" GST_TIME_FORMAT,
      GST_TIME_ARGS (cur));

  /* For any format other than TIME, see if upstream handles
   * it directly or fail. For TIME, try upstream, but do it ourselves if
   * it fails upstream */
  if (format != GST_FORMAT_TIME) {
    return gst_pad_push_event (amrwbparse->sinkpad, event);
  } else {
    if (gst_pad_push_event (amrwbparse->sinkpad, event))
      return TRUE;
  }

  flush = flags & GST_SEEK_FLAG_FLUSH;

  /* send flush start */
  if (flush)
    gst_pad_push_event (amrwbparse->sinkpad, gst_event_new_flush_start ());
  /* we only handle FLUSH seeks at the moment */
  else
    return FALSE;

  /* grab streaming lock, this should eventually be possible, either
   * because the task is paused or our streaming thread stopped
   * because our peer is flushing. */
  GST_PAD_STREAM_LOCK (amrwbparse->sinkpad);

  /* Convert the TIME to the appropriate BYTE position at which to resume
   * decoding. */
  cur = cur / (20 * GST_MSECOND) * (20 * GST_MSECOND);
  if (cur != -1)
    byte_cur = amrwbparse->block * (cur / 20 / GST_MSECOND) + AMRWB_HEADER_SIZE;
  if (stop != -1)
    byte_stop =
        amrwbparse->block * (stop / 20 / GST_MSECOND) + AMRWB_HEADER_SIZE;
  amrwbparse->offset = byte_cur;
  amrwbparse->ts = cur;

  GST_DEBUG_OBJECT (amrwbparse, "Seeking to byte range %" G_GINT64_FORMAT
      " to %" G_GINT64_FORMAT, byte_cur, cur);

  /* and prepare to continue streaming */
  /* send flush stop, peer will accept data and events again. We
   * are not yet providing data as we still have the STREAM_LOCK. */
  gst_pad_push_event (amrwbparse->sinkpad, gst_event_new_flush_stop ());
  gst_pad_push_event (amrwbparse->srcpad, gst_event_new_new_segment (FALSE,
          rate, format, cur, -1, cur));

  /* and restart the task in case it got paused explicitely or by
   * the FLUSH_START event we pushed out. */
  gst_pad_start_task (amrwbparse->sinkpad,
      (GstTaskFunction) gst_amrwbparse_loop, amrwbparse->sinkpad);

  /* and release the lock again so we can continue streaming */
  GST_PAD_STREAM_UNLOCK (amrwbparse->sinkpad);

  return TRUE;
}

static gboolean
gst_amrwbparse_handle_push_seek (GstAmrwbParse * amrwbparse, GstPad * pad,
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

  GST_DEBUG_OBJECT (amrwbparse, "Performing seek to %" GST_TIME_FORMAT,
      GST_TIME_ARGS (cur));

  /* For any format other than TIME, see if upstream handles
   * it directly or fail. For TIME, try upstream, but do it ourselves if
   * it fails upstream */
  if (format != GST_FORMAT_TIME) {
    return gst_pad_push_event (amrwbparse->sinkpad, event);
  } else {
    if (gst_pad_push_event (amrwbparse->sinkpad, event))
      return TRUE;
  }

  /* Convert the TIME to the appropriate BYTE position at which to resume
   * decoding. */
  cur = cur / (20 * GST_MSECOND) * (20 * GST_MSECOND);
  if (cur != -1)
    byte_cur = amrwbparse->block * (cur / 20 / GST_MSECOND) + AMRWB_HEADER_SIZE;
  if (stop != -1)
    byte_stop =
        amrwbparse->block * (stop / 20 / GST_MSECOND) + AMRWB_HEADER_SIZE;
  amrwbparse->ts = cur;

  GST_DEBUG_OBJECT (amrwbparse, "Seeking to byte range %" G_GINT64_FORMAT
      " to %" G_GINT64_FORMAT, byte_cur, byte_stop);

  /* Send BYTE based seek upstream */
  event = gst_event_new_seek (rate, GST_FORMAT_BYTES, flags, cur_type,
      byte_cur, stop_type, byte_stop);

  return gst_pad_push_event (amrwbparse->sinkpad, event);
}

static gboolean
gst_amrwbparse_src_event (GstPad * pad, GstEvent * event)
{
  GstAmrwbParse *amrwbparse = GST_AMRWBPARSE (gst_pad_get_parent (pad));
  gboolean res;

  GST_DEBUG_OBJECT (amrwbparse, "handling event %d", GST_EVENT_TYPE (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
      if (amrwbparse->seek_handler)
        res = amrwbparse->seek_handler (amrwbparse, pad, event);
      else
        res = FALSE;
      break;
    default:
      res = gst_pad_push_event (amrwbparse->sinkpad, event);
      break;
  }
  gst_object_unref (amrwbparse);

  return res;
}


/*
 * Data reading.
 */
static gboolean
gst_amrwbparse_sink_event (GstPad * pad, GstEvent * event)
{
  GstAmrwbParse *amrwbparse;
  gboolean res;

  amrwbparse = GST_AMRWBPARSE (gst_pad_get_parent (pad));

  GST_LOG ("handling event %d", GST_EVENT_TYPE (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      res = gst_pad_push_event (amrwbparse->srcpad, event);
      break;
    case GST_EVENT_FLUSH_STOP:
      gst_adapter_clear (amrwbparse->adapter);
      gst_segment_init (&amrwbparse->segment, GST_FORMAT_TIME);
      res = gst_pad_push_event (amrwbparse->srcpad, event);
      break;
    case GST_EVENT_EOS:
      res = gst_pad_push_event (amrwbparse->srcpad, event);
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
      res = gst_pad_push_event (amrwbparse->srcpad, event);
      break;
  }
  gst_object_unref (amrwbparse);

  return res;
}

/* streaming mode */
static GstFlowReturn
gst_amrwbparse_chain (GstPad * pad, GstBuffer * buffer)
{
  GstAmrwbParse *amrwbparse;
  GstFlowReturn res = GST_FLOW_OK;
  gint mode;
  const guint8 *data;
  GstBuffer *out;
  GstClockTime timestamp;

  amrwbparse = GST_AMRWBPARSE (gst_pad_get_parent (pad));

  timestamp = GST_BUFFER_TIMESTAMP (buffer);
  if (GST_CLOCK_TIME_IS_VALID (timestamp)) {
    GST_DEBUG_OBJECT (amrwbparse, "Lock on timestamp %" GST_TIME_FORMAT,
        GST_TIME_ARGS (timestamp));
    amrwbparse->ts = timestamp;
  }

  gst_adapter_push (amrwbparse->adapter, buffer);

  /* init */
  if (amrwbparse->need_header) {
    GstEvent *segev;
    GstCaps *caps;

    if (gst_adapter_available (amrwbparse->adapter) < AMRWB_HEADER_SIZE)
      goto done;

    data = gst_adapter_peek (amrwbparse->adapter, AMRWB_HEADER_SIZE);
    if (memcmp (data, AMRWB_HEADER_STR, AMRWB_HEADER_SIZE) != 0)
      goto done;

    gst_adapter_flush (amrwbparse->adapter, AMRWB_HEADER_SIZE);

    amrwbparse->need_header = FALSE;

    caps = gst_caps_new_simple ("audio/AMR-WB",
        "rate", G_TYPE_INT, 16000, "channels", G_TYPE_INT, 1, NULL);
    gst_pad_set_caps (amrwbparse->srcpad, caps);
    gst_caps_unref (caps);

    GST_DEBUG_OBJECT (amrwbparse, "Sending first segment");
    segev = gst_event_new_new_segment_full (FALSE, 1.0, 1.0,
        GST_FORMAT_TIME, 0, -1, 0);

    gst_pad_push_event (amrwbparse->srcpad, segev);
  }

  while (TRUE) {
    if (gst_adapter_available (amrwbparse->adapter) < 1)
      break;

    data = gst_adapter_peek (amrwbparse->adapter, 1);

    /* get size */
    mode = (data[0] >> 3) & 0x0F;
    amrwbparse->block = block_size[mode] + 1;   /* add one for the mode */

    if (gst_adapter_available (amrwbparse->adapter) < amrwbparse->block)
      break;

    out = gst_buffer_new_and_alloc (amrwbparse->block);

    data = gst_adapter_peek (amrwbparse->adapter, amrwbparse->block);
    memcpy (GST_BUFFER_DATA (out), data, amrwbparse->block);

    /* timestamp, all constants that won't overflow */
    GST_BUFFER_DURATION (out) = GST_SECOND * L_FRAME16k / 16000;
    GST_BUFFER_TIMESTAMP (out) = amrwbparse->ts;
    if (GST_CLOCK_TIME_IS_VALID (amrwbparse->ts))
      amrwbparse->ts += GST_BUFFER_DURATION (out);

    gst_buffer_set_caps (out, GST_PAD_CAPS (amrwbparse->srcpad));

    GST_DEBUG_OBJECT (amrwbparse, "Pushing %d bytes of data",
        amrwbparse->block);

    res = gst_pad_push (amrwbparse->srcpad, out);

    gst_adapter_flush (amrwbparse->adapter, amrwbparse->block);
  }
done:

  gst_object_unref (amrwbparse);
  return res;
}

static gboolean
gst_amrwbparse_pull_header (GstAmrwbParse * amrwbparse)
{
  GstBuffer *buffer;
  GstFlowReturn ret;
  guint8 *data;
  gint size;

  ret = gst_pad_pull_range (amrwbparse->sinkpad, G_GUINT64_CONSTANT (0),
      AMRWB_HEADER_SIZE, &buffer);
  if (ret != GST_FLOW_OK)
    return FALSE;

  data = GST_BUFFER_DATA (buffer);
  size = GST_BUFFER_SIZE (buffer);

  if (size < AMRWB_HEADER_SIZE)
    goto not_enough;

  if (memcmp (data, AMRWB_HEADER_STR, AMRWB_HEADER_SIZE))
    goto no_header;

  gst_buffer_unref (buffer);

  amrwbparse->offset = AMRWB_HEADER_SIZE;
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
gst_amrwbparse_loop (GstPad * pad)
{
  GstAmrwbParse *amrwbparse;
  GstBuffer *buffer;
  guint8 *data;
  gint size;
  gint block, mode;
  GstFlowReturn ret = GST_FLOW_OK;

  amrwbparse = GST_AMRWBPARSE (GST_PAD_PARENT (pad));

  /* init */
  if (G_UNLIKELY (amrwbparse->need_header)) {
    GstCaps *caps;

    if (!gst_amrwbparse_pull_header (amrwbparse)) {
      GST_ELEMENT_ERROR (amrwbparse, STREAM, WRONG_TYPE, (NULL), (NULL));
      GST_LOG_OBJECT (amrwbparse, "could not read header");
      goto need_pause;
    }

    caps = gst_caps_new_simple ("audio/AMR-WB",
        "rate", G_TYPE_INT, 16000, "channels", G_TYPE_INT, 1, NULL);
    gst_pad_set_caps (amrwbparse->srcpad, caps);
    gst_caps_unref (caps);

    GST_DEBUG_OBJECT (amrwbparse, "Sending newsegment event");
    gst_pad_push_event (amrwbparse->srcpad,
        gst_event_new_new_segment_full (FALSE, 1.0, 1.0,
            GST_FORMAT_TIME, 0, -1, 0));

    amrwbparse->need_header = FALSE;
  }

  ret =
      gst_pad_pull_range (amrwbparse->sinkpad, amrwbparse->offset, 1, &buffer);

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
  block = block_size[mode];     /* add one for the mode */

  gst_buffer_unref (buffer);

  ret = gst_pad_pull_range (amrwbparse->sinkpad,
      amrwbparse->offset, block, &buffer);

  if (ret == GST_FLOW_UNEXPECTED)
    goto eos;
  else if (ret != GST_FLOW_OK)
    goto need_pause;

  amrwbparse->offset += block;

  /* output */
  GST_BUFFER_DURATION (buffer) = GST_SECOND * L_FRAME16k / 16000;
  GST_BUFFER_TIMESTAMP (buffer) = amrwbparse->ts;

  gst_buffer_set_caps (buffer,
      (GstCaps *) gst_pad_get_pad_template_caps (amrwbparse->srcpad));

  ret = gst_pad_push (amrwbparse->srcpad, buffer);

  if (ret != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (amrwbparse, "Flow: %s", gst_flow_get_name (ret));
    if (GST_FLOW_IS_FATAL (ret)) {
      GST_ELEMENT_ERROR (amrwbparse, STREAM, FAILED, (NULL),    /* _("Internal data flow error.")), */
          ("streaming task paused, reason: %s", gst_flow_get_name (ret)));
      gst_pad_push_event (pad, gst_event_new_eos ());
    }
    goto need_pause;
  }

  amrwbparse->ts += GST_BUFFER_DURATION (buffer);

  return;

need_pause:
  {
    GST_LOG_OBJECT (amrwbparse, "pausing task");
    gst_pad_pause_task (pad);
    return;
  }
eos:
  {
    GST_LOG_OBJECT (amrwbparse, "pausing task %d", ret);
    gst_pad_push_event (amrwbparse->srcpad, gst_event_new_eos ());
    gst_pad_pause_task (pad);
    return;
  }
}

static gboolean
gst_amrwbparse_sink_activate (GstPad * sinkpad)
{
  gboolean result = FALSE;
  GstAmrwbParse *amrwbparse;

  amrwbparse = GST_AMRWBPARSE (gst_pad_get_parent (sinkpad));

  if (gst_pad_check_pull_range (sinkpad)) {
    GST_DEBUG ("Trying to activate in pull mode");
    amrwbparse->seekable = TRUE;
    amrwbparse->ts = 0;
    result = gst_pad_activate_pull (sinkpad, TRUE);
  } else {
    GST_DEBUG ("Try to activate in push mode");
    amrwbparse->seekable = FALSE;
    result = gst_pad_activate_push (sinkpad, TRUE);
  }

  gst_object_unref (amrwbparse);
  return result;
}



static gboolean
gst_amrwbparse_sink_activate_push (GstPad * sinkpad, gboolean active)
{
  GstAmrwbParse *amrwbparse = GST_AMRWBPARSE (gst_pad_get_parent (sinkpad));

  if (active) {
    amrwbparse->seek_handler = gst_amrwbparse_handle_push_seek;
  } else {
    amrwbparse->seek_handler = NULL;
  }

  gst_object_unref (amrwbparse);
  return TRUE;
}

static gboolean
gst_amrwbparse_sink_activate_pull (GstPad * sinkpad, gboolean active)
{
  gboolean result;
  GstAmrwbParse *amrwbparse;

  amrwbparse = GST_AMRWBPARSE (gst_pad_get_parent (sinkpad));
  if (active) {
    amrwbparse->seek_handler = gst_amrwbparse_handle_pull_seek;
    result = gst_pad_start_task (sinkpad,
        (GstTaskFunction) gst_amrwbparse_loop, sinkpad);
  } else {
    amrwbparse->seek_handler = NULL;
    result = gst_pad_stop_task (sinkpad);
  }

  gst_object_unref (amrwbparse);
  return result;
}


static GstStateChangeReturn
gst_amrwbparse_state_change (GstElement * element, GstStateChange transition)
{
  GstAmrwbParse *amrwbparse;
  GstStateChangeReturn ret;

  amrwbparse = GST_AMRWBPARSE (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      amrwbparse->need_header = TRUE;
      amrwbparse->ts = -1;
      amrwbparse->block = 0;
      gst_segment_init (&amrwbparse->segment, GST_FORMAT_TIME);
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
