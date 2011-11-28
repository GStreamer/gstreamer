/* gstmonoscope.c: implementation of monoscope drawing element
 * Copyright (C) <2002> Richard Boulton <richard@tartarus.org>
 * Copyright (C) <2006> Tim-Philipp Müller <tim centricular net>
 * Copyright (C) <2006> Wim Taymans <wim at fluendo dot com>
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
 * SECTION:element-monoscope
 * @see_also: goom
 *
 * Monoscope is an audio visualisation element. It creates a coloured
 * curve of the audio signal like on an oscilloscope.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v audiotestsrc ! audioconvert ! monoscope ! ffmpegcolorspace ! ximagesink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/video/video.h>
#include <gst/audio/audio.h>
#include <string.h>
#include "gstmonoscope.h"
#include "monoscope.h"

GST_DEBUG_CATEGORY_STATIC (monoscope_debug);
#define GST_CAT_DEFAULT monoscope_debug

#if G_BYTE_ORDER == G_BIG_ENDIAN
static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-rgb, "
        "bpp = (int) 32, "
        "depth = (int) 24, "
        "endianness = (int) BIG_ENDIAN, "
        "red_mask = (int) " GST_VIDEO_BYTE2_MASK_32 ", "
        "green_mask = (int) " GST_VIDEO_BYTE3_MASK_32 ", "
        "blue_mask = (int) " GST_VIDEO_BYTE4_MASK_32 ", "
        "width = (int)256, "
        "height = (int)128, " "framerate = " GST_VIDEO_FPS_RANGE)
    );
#else
static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-rgb, "
        "bpp = (int) 32, "
        "depth = (int) 24, "
        "endianness = (int) BIG_ENDIAN, "
        "red_mask = (int) " GST_VIDEO_BYTE3_MASK_32 ", "
        "green_mask = (int) " GST_VIDEO_BYTE2_MASK_32 ", "
        "blue_mask = (int) " GST_VIDEO_BYTE1_MASK_32 ", "
        "width = (int)256, "
        "height = (int)128, " "framerate = " GST_VIDEO_FPS_RANGE)
    );
#endif

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_AUDIO_INT_STANDARD_PAD_TEMPLATE_CAPS)
    );


GST_BOILERPLATE (GstMonoscope, gst_monoscope, GstElement, GST_TYPE_ELEMENT);

static void gst_monoscope_finalize (GObject * object);
static GstFlowReturn gst_monoscope_chain (GstPad * pad, GstBuffer * buf);
static gboolean gst_monoscope_src_setcaps (GstPad * pad, GstCaps * caps);
static gboolean gst_monoscope_sink_setcaps (GstPad * pad, GstCaps * caps);
static void gst_monoscope_reset (GstMonoscope * monoscope);
static gboolean gst_monoscope_sink_event (GstPad * pad, GstEvent * event);
static gboolean gst_monoscope_src_event (GstPad * pad, GstEvent * event);
static GstStateChangeReturn gst_monoscope_change_state (GstElement * element,
    GstStateChange transition);

static void
gst_monoscope_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class, &src_template);
  gst_element_class_add_static_pad_template (element_class,
      &sink_template);
  gst_element_class_set_details_simple (element_class, "Monoscope",
      "Visualization",
      "Displays a highly stabilised waveform of audio input",
      "Richard Boulton <richard@tartarus.org>");
}

static void
gst_monoscope_class_init (GstMonoscopeClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->finalize = gst_monoscope_finalize;

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_monoscope_change_state);
}

static void
gst_monoscope_init (GstMonoscope * monoscope, GstMonoscopeClass * klass)
{
  monoscope->sinkpad =
      gst_pad_new_from_static_template (&sink_template, "sink");
  gst_pad_set_chain_function (monoscope->sinkpad,
      GST_DEBUG_FUNCPTR (gst_monoscope_chain));
  gst_pad_set_event_function (monoscope->sinkpad,
      GST_DEBUG_FUNCPTR (gst_monoscope_sink_event));
  gst_pad_set_setcaps_function (monoscope->sinkpad,
      GST_DEBUG_FUNCPTR (gst_monoscope_sink_setcaps));
  gst_element_add_pad (GST_ELEMENT (monoscope), monoscope->sinkpad);

  monoscope->srcpad = gst_pad_new_from_static_template (&src_template, "src");
  gst_pad_set_setcaps_function (monoscope->srcpad,
      GST_DEBUG_FUNCPTR (gst_monoscope_src_setcaps));
  gst_pad_set_event_function (monoscope->srcpad,
      GST_DEBUG_FUNCPTR (gst_monoscope_src_event));
  gst_element_add_pad (GST_ELEMENT (monoscope), monoscope->srcpad);

  monoscope->adapter = gst_adapter_new ();
  monoscope->next_ts = GST_CLOCK_TIME_NONE;
  monoscope->bps = sizeof (gint16);

  /* reset the initial video state */
  monoscope->width = 256;
  monoscope->height = 128;
  monoscope->fps_num = 25;      /* desired frame rate */
  monoscope->fps_denom = 1;
  monoscope->visstate = NULL;

  /* reset the initial audio state */
  monoscope->rate = GST_AUDIO_DEF_RATE;
}

static void
gst_monoscope_finalize (GObject * object)
{
  GstMonoscope *monoscope = GST_MONOSCOPE (object);

  if (monoscope->visstate)
    monoscope_close (monoscope->visstate);

  g_object_unref (monoscope->adapter);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_monoscope_reset (GstMonoscope * monoscope)
{
  monoscope->next_ts = GST_CLOCK_TIME_NONE;

  gst_adapter_clear (monoscope->adapter);
  gst_segment_init (&monoscope->segment, GST_FORMAT_UNDEFINED);

  GST_OBJECT_LOCK (monoscope);
  monoscope->proportion = 1.0;
  monoscope->earliest_time = -1;
  GST_OBJECT_UNLOCK (monoscope);
}

static gboolean
gst_monoscope_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstMonoscope *monoscope = GST_MONOSCOPE (GST_PAD_PARENT (pad));
  GstStructure *structure;

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_get_int (structure, "rate", &monoscope->rate);

  GST_DEBUG_OBJECT (monoscope, "sample rate = %d", monoscope->rate);
  return TRUE;
}

static gboolean
gst_monoscope_src_setcaps (GstPad * pad, GstCaps * caps)
{
  GstMonoscope *monoscope = GST_MONOSCOPE (GST_PAD_PARENT (pad));
  GstStructure *structure;

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_get_int (structure, "width", &monoscope->width);
  gst_structure_get_int (structure, "height", &monoscope->height);
  gst_structure_get_fraction (structure, "framerate", &monoscope->fps_num,
      &monoscope->fps_denom);

  monoscope->outsize = monoscope->width * monoscope->height * 4;
  monoscope->frame_duration = gst_util_uint64_scale_int (GST_SECOND,
      monoscope->fps_denom, monoscope->fps_num);
  monoscope->spf =
      gst_util_uint64_scale_int (monoscope->rate, monoscope->fps_denom,
      monoscope->fps_num);

  GST_DEBUG_OBJECT (monoscope, "dimension %dx%d, framerate %d/%d, spf %d",
      monoscope->width, monoscope->height, monoscope->fps_num,
      monoscope->fps_denom, monoscope->spf);

  if (monoscope->visstate) {
    monoscope_close (monoscope->visstate);
    monoscope->visstate = NULL;
  }

  monoscope->visstate = monoscope_init (monoscope->width, monoscope->height);

  return (monoscope->visstate != NULL);
}

static gboolean
gst_monoscope_src_negotiate (GstMonoscope * monoscope)
{
  GstCaps *othercaps, *target;
  GstStructure *structure;
  const GstCaps *templ;

  templ = gst_pad_get_pad_template_caps (monoscope->srcpad);

  GST_DEBUG_OBJECT (monoscope, "performing negotiation");

  /* see what the peer can do */
  othercaps = gst_pad_peer_get_caps (monoscope->srcpad);
  if (othercaps) {
    target = gst_caps_intersect (othercaps, templ);
    gst_caps_unref (othercaps);

    if (gst_caps_is_empty (target))
      goto no_format;

    gst_caps_truncate (target);
  } else {
    target = gst_caps_ref ((GstCaps *) templ);
  }

  structure = gst_caps_get_structure (target, 0);
  gst_structure_fixate_field_nearest_int (structure, "width", 320);
  gst_structure_fixate_field_nearest_int (structure, "height", 240);
  gst_structure_fixate_field_nearest_fraction (structure, "framerate", 25, 1);

  gst_pad_set_caps (monoscope->srcpad, target);
  gst_caps_unref (target);

  return TRUE;

no_format:
  {
    gst_caps_unref (target);
    return FALSE;
  }
}

static GstFlowReturn
get_buffer (GstMonoscope * monoscope, GstBuffer ** outbuf)
{
  GstFlowReturn ret;

  if (GST_PAD_CAPS (monoscope->srcpad) == NULL) {
    if (!gst_monoscope_src_negotiate (monoscope))
      return GST_FLOW_NOT_NEGOTIATED;
  }

  GST_LOG_OBJECT (monoscope, "allocating output buffer of size %d with caps %"
      GST_PTR_FORMAT, monoscope->outsize, GST_PAD_CAPS (monoscope->srcpad));

  ret =
      gst_pad_alloc_buffer_and_set_caps (monoscope->srcpad,
      GST_BUFFER_OFFSET_NONE, monoscope->outsize,
      GST_PAD_CAPS (monoscope->srcpad), outbuf);

  if (ret != GST_FLOW_OK)
    return ret;

  if (*outbuf == NULL)
    return GST_FLOW_ERROR;

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_monoscope_chain (GstPad * pad, GstBuffer * inbuf)
{
  GstFlowReturn flow_ret = GST_FLOW_OK;
  GstMonoscope *monoscope;

  monoscope = GST_MONOSCOPE (GST_PAD_PARENT (pad));

  /* don't try to combine samples from discont buffer */
  if (GST_BUFFER_FLAG_IS_SET (inbuf, GST_BUFFER_FLAG_DISCONT)) {
    gst_adapter_clear (monoscope->adapter);
    monoscope->next_ts = GST_CLOCK_TIME_NONE;
  }

  /* Match timestamps from the incoming audio */
  if (GST_BUFFER_TIMESTAMP (inbuf) != GST_CLOCK_TIME_NONE)
    monoscope->next_ts = GST_BUFFER_TIMESTAMP (inbuf);

  GST_LOG_OBJECT (monoscope, "in buffer has %d samples, ts=%" GST_TIME_FORMAT,
      GST_BUFFER_SIZE (inbuf) / monoscope->bps,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (inbuf)));

  gst_adapter_push (monoscope->adapter, inbuf);
  inbuf = NULL;

  /* Collect samples until we have enough for an output frame */
  while (flow_ret == GST_FLOW_OK) {
    gint16 *samples;
    GstBuffer *outbuf = NULL;
    guint32 *pixels, avail, bytesperframe;

    avail = gst_adapter_available (monoscope->adapter);
    GST_LOG_OBJECT (monoscope, "bytes avail now %u", avail);

    /* do negotiation if not done yet, so ->spf etc. is set */
    if (GST_PAD_CAPS (monoscope->srcpad) == NULL) {
      flow_ret = get_buffer (monoscope, &outbuf);
      if (flow_ret != GST_FLOW_OK)
        goto out;
      gst_buffer_unref (outbuf);
      outbuf = NULL;
    }

    bytesperframe = monoscope->spf * monoscope->bps;
    if (avail < bytesperframe)
      break;

    /* FIXME: something is wrong with QoS, we are skipping way too much
     * stuff even with very low CPU loads */
#if 0
    if (monoscope->next_ts != -1) {
      gboolean need_skip;
      gint64 qostime;

      qostime = gst_segment_to_running_time (&monoscope->segment,
          GST_FORMAT_TIME, monoscope->next_ts);

      GST_OBJECT_LOCK (monoscope);
      /* check for QoS, don't compute buffers that are known to be late */
      need_skip =
          GST_CLOCK_TIME_IS_VALID (monoscope->earliest_time) &&
          qostime <= monoscope->earliest_time;
      GST_OBJECT_UNLOCK (monoscope);

      if (need_skip) {
        GST_WARNING_OBJECT (monoscope,
            "QoS: skip ts: %" GST_TIME_FORMAT ", earliest: %" GST_TIME_FORMAT,
            GST_TIME_ARGS (qostime), GST_TIME_ARGS (monoscope->earliest_time));
        goto skip;
      }
    }
#endif

    samples = (gint16 *) gst_adapter_peek (monoscope->adapter, bytesperframe);

    if (monoscope->spf < 512) {
      gint16 in_data[512], i;

      for (i = 0; i < 512; ++i) {
        gdouble off;

        off = ((gdouble) i * (gdouble) monoscope->spf) / 512.0;
        in_data[i] = samples[MIN ((guint) off, monoscope->spf)];
      }
      pixels = monoscope_update (monoscope->visstate, in_data);
    } else {
      /* not really correct, but looks much prettier */
      pixels = monoscope_update (monoscope->visstate, samples);
    }

    flow_ret = get_buffer (monoscope, &outbuf);
    if (flow_ret != GST_FLOW_OK)
      goto out;

    memcpy (GST_BUFFER_DATA (outbuf), pixels, monoscope->outsize);

    GST_BUFFER_TIMESTAMP (outbuf) = monoscope->next_ts;
    GST_BUFFER_DURATION (outbuf) = monoscope->frame_duration;

    flow_ret = gst_pad_push (monoscope->srcpad, outbuf);

#if 0
  skip:
#endif

    if (GST_CLOCK_TIME_IS_VALID (monoscope->next_ts))
      monoscope->next_ts += monoscope->frame_duration;

    gst_adapter_flush (monoscope->adapter, bytesperframe);
  }

out:

  return flow_ret;
}

static gboolean
gst_monoscope_sink_event (GstPad * pad, GstEvent * event)
{
  GstMonoscope *monoscope;
  gboolean res;

  monoscope = GST_MONOSCOPE (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      res = gst_pad_push_event (monoscope->srcpad, event);
      break;
    case GST_EVENT_FLUSH_STOP:
      gst_monoscope_reset (monoscope);
      res = gst_pad_push_event (monoscope->srcpad, event);
      break;
    case GST_EVENT_NEWSEGMENT:
    {
      GstFormat format;
      gdouble rate, arate;
      gint64 start, stop, time;
      gboolean update;

      /* the newsegment values are used to clip the input samples
       * and to convert the incomming timestamps to running time so
       * we can do QoS */
      gst_event_parse_new_segment_full (event, &update, &rate, &arate, &format,
          &start, &stop, &time);

      /* now configure the values */
      gst_segment_set_newsegment_full (&monoscope->segment, update,
          rate, arate, format, start, stop, time);

      res = gst_pad_push_event (monoscope->srcpad, event);
      break;
    }
    default:
      res = gst_pad_push_event (monoscope->srcpad, event);
      break;
  }

  gst_object_unref (monoscope);

  return res;
}

static gboolean
gst_monoscope_src_event (GstPad * pad, GstEvent * event)
{
  GstMonoscope *monoscope;
  gboolean res;

  monoscope = GST_MONOSCOPE (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_QOS:{
      gdouble proportion;
      GstClockTimeDiff diff;
      GstClockTime timestamp;

      gst_event_parse_qos (event, &proportion, &diff, &timestamp);

      /* save stuff for the _chain() function */
      GST_OBJECT_LOCK (monoscope);
      monoscope->proportion = proportion;
      if (diff >= 0)
        /* we're late, this is a good estimate for next displayable
         * frame (see part-qos.txt) */
        monoscope->earliest_time =
            timestamp + 2 * diff + monoscope->frame_duration;
      else
        monoscope->earliest_time = timestamp + diff;
      GST_OBJECT_UNLOCK (monoscope);

      res = gst_pad_push_event (monoscope->sinkpad, event);
      break;
    }
    default:
      res = gst_pad_push_event (monoscope->sinkpad, event);
      break;
  }
  gst_object_unref (monoscope);

  return res;
}

static GstStateChangeReturn
gst_monoscope_change_state (GstElement * element, GstStateChange transition)
{
  GstMonoscope *monoscope = GST_MONOSCOPE (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_monoscope_reset (monoscope);
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

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (monoscope_debug, "monoscope", 0,
      "monoscope element");

  return gst_element_register (plugin, "monoscope",
      GST_RANK_NONE, GST_TYPE_MONOSCOPE);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "monoscope",
    "Monoscope visualization",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
