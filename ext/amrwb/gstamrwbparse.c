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

static void gst_amrwbparse_base_init (gpointer klass);
static void gst_amrwbparse_class_init (GstAmrwbParseClass * klass);
static void gst_amrwbparse_init (GstAmrwbParse * amrwbparse,
    GstAmrwbParseClass * klass);

static const GstQueryType *gst_amrwbparse_querytypes (GstPad * pad);
static gboolean gst_amrwbparse_query (GstPad * pad, GstQuery * query);

static GstFlowReturn gst_amrwbparse_chain (GstPad * pad, GstBuffer * buffer);
static void gst_amrwbparse_loop (GstPad * pad);
static gboolean gst_amrwbparse_sink_activate (GstPad * sinkpad);
static gboolean gst_amrwbparse_sink_activate_pull (GstPad * sinkpad,
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

  gst_pad_set_activate_function (amrwbparse->sinkpad,
      gst_amrwbparse_sink_activate);
  gst_pad_set_activatepull_function (amrwbparse->sinkpad,
      gst_amrwbparse_sink_activate_pull);

  gst_element_add_pad (GST_ELEMENT (amrwbparse), amrwbparse->sinkpad);

  /* create the src pad */
  amrwbparse->srcpad = gst_pad_new_from_static_template (&src_template, "src");
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


/*
 * Data reading.
 */

/* streaming mode */
static GstFlowReturn
gst_amrwbparse_chain (GstPad * pad, GstBuffer * buffer)
{
  GstAmrwbParse *amrwbparse;
  GstFlowReturn res = GST_FLOW_OK;
  gint mode;
  const guint8 *data;
  GstBuffer *out;

  amrwbparse = GST_AMRWBPARSE (gst_pad_get_parent (pad));

  gst_adapter_push (amrwbparse->adapter, buffer);

  /* init */
  if (amrwbparse->need_header) {
    GstEvent *segev;
    GstCaps *caps;

    if (gst_adapter_available (amrwbparse->adapter) < 9)
      goto done;

    data = gst_adapter_peek (amrwbparse->adapter, 9);
    if (memcmp (data, "#!AMR-WB\n", 9) != 0)
      goto done;

    gst_adapter_flush (amrwbparse->adapter, 9);

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
  gboolean ret = TRUE;
  guint8 *data;
  gint size;
  const guint8 magic_number_size = 9;   /* sizeof("#!AMR-WB\n")-1 */

  if (GST_FLOW_OK != gst_pad_pull_range (amrwbparse->sinkpad,
          amrwbparse->offset, magic_number_size, &buffer)) {
    ret = FALSE;
    goto done;
  }

  data = GST_BUFFER_DATA (buffer);
  size = GST_BUFFER_SIZE (buffer);

  if (size < magic_number_size) {
    /* not enough */
    ret = FALSE;
    goto done;
  }

  if (memcmp (data, "#!AMR-WB\n", magic_number_size)) {
    /* no header */
    ret = FALSE;
    goto done;
  }

  amrwbparse->offset += magic_number_size;

done:

  gst_buffer_unref (buffer);
  return ret;

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

    if (!gst_amrwbparse_pull_header (amrwbparse)) {
      GST_ELEMENT_ERROR (amrwbparse, STREAM, WRONG_TYPE, (NULL), (NULL));
      GST_LOG_OBJECT (amrwbparse, "could not read header");
      goto need_pause;
    }

    GST_DEBUG_OBJECT (amrwbparse, "Sending newsegment event");
    gst_pad_push_event (amrwbparse->srcpad,
        gst_event_new_new_segment_full (FALSE, 1.0, 1.0,
            GST_FORMAT_TIME, 0, -1, 0));

    amrwbparse->need_header = FALSE;
  }

  ret = gst_pad_pull_range (amrwbparse->sinkpad,
      amrwbparse->offset, 1, &buffer);

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
  GstAmrwbParse *amrwbparse;

  amrwbparse = GST_AMRWBPARSE (GST_PAD_PARENT (sinkpad));
  if (gst_pad_check_pull_range (sinkpad)) {
    return gst_pad_activate_pull (sinkpad, TRUE);
  } else {
    amrwbparse->seekable = FALSE;
    return gst_pad_activate_push (sinkpad, TRUE);
  }
}


static gboolean
gst_amrwbparse_sink_activate_pull (GstPad * sinkpad, gboolean active)
{
  gboolean result;
  GstAmrwbParse *amrwbparse;

  amrwbparse = GST_AMRWBPARSE (GST_PAD_PARENT (sinkpad));
  if (active) {
    amrwbparse->need_header = TRUE;
    amrwbparse->seekable = TRUE;
    amrwbparse->ts = 0;
    /* if we have a scheduler we can start the task */
    result = gst_pad_start_task (sinkpad,
        (GstTaskFunction) gst_amrwbparse_loop, sinkpad);
  } else {
    result = gst_pad_stop_task (sinkpad);
  }

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
