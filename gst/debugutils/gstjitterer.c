/*
 * GStreamer
 * Copyright (C) 2017 Vivia Nikolaidou <vivia@ahiru.eu>
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
 * SECTION:element-jitterer
 * @title: jitterer
 *
 * Adds jitter and/or drift to a buffer's PTS and/or DTS. Amplitude and
 * average of jitter and drift are configurable.
 *
 * ## Example launch line
 * |[
 * gst-launch-1.0 videotestsrc ! jitterer drift-average=100 drift-amplitude=10 ! autovideosink
 * ]|
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstjitterer.h"

#define GST_CAT_DEFAULT gst_jitterer_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

enum
{
  PROP_0,
  PROP_JITTER_AMPL,
  PROP_JITTER_AVG,
  PROP_DRIFT_AMPL,
  PROP_DRIFT_AVG,
  PROP_CHANGE_PTS,
  PROP_CHANGE_DTS
};

static void gst_jitterer_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_jitterer_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_jitterer_finalize (GObject * object);

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

#define parent_class gst_jitterer_parent_class
G_DEFINE_TYPE (GstJitterer, gst_jitterer, GST_TYPE_ELEMENT);

static GstFlowReturn gst_jitterer_sink_chain (GstPad * pad,
    GstObject * parent, GstBuffer * inbuf);
static gboolean gst_jitterer_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);

static void
gst_jitterer_class_init (GstJittererClass * klass)
{
  GstElementClass *gstelement_class;
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (gst_jitterer_debug, "jitterer", 0,
      "Add jitter and/or drift to buffers");

  gstelement_class = (GstElementClass *) klass;

  gst_element_class_set_static_metadata (gstelement_class,
      "Jitterer", "Generic", "Add jitter and/or drift to buffers",
      "Vivia Nikolaidou <vivia@ahiru.eu>");

  gst_element_class_add_static_pad_template (gstelement_class, &src_template);
  gst_element_class_add_static_pad_template (gstelement_class, &sink_template);

  /* define virtual function pointers */
  object_class->set_property = gst_jitterer_set_property;
  object_class->get_property = gst_jitterer_get_property;
  object_class->finalize = gst_jitterer_finalize;

  /* define properties */
  g_object_class_install_property (object_class, PROP_JITTER_AMPL,
      g_param_spec_uint64 ("jitter-amplitude", "Jitter amplitude",
          "Amplitude of the jitter to apply",
          0, G_MAXINT64 / 2, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_JITTER_AVG,
      g_param_spec_int64 ("jitter-average", "Jitter average",
          "Average of the jitter to apply",
          -G_MAXINT64 / 2, G_MAXINT64 / 2, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_DRIFT_AMPL,
      g_param_spec_uint64 ("drift-amplitude", "Drift amplitude",
          "Amplitude of the drift to apply",
          0, G_MAXINT64 / 2, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_DRIFT_AVG,
      g_param_spec_int64 ("drift-average", "Drift average",
          "Average of the drift to apply",
          -G_MAXINT64 / 2, G_MAXINT64 / 2, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_CHANGE_PTS,
      g_param_spec_boolean ("change-pts", "Change PTS",
          "Whether to change the PTS of incoming buffers",
          TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_CHANGE_DTS,
      g_param_spec_boolean ("change-dts", "Change DTS",
          "Whether to change the DTS of incoming buffers",
          TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

}

static void
gst_jitterer_init (GstJitterer * self)
{
  self->sinkpad = gst_pad_new_from_static_template (&sink_template, "sink");
  gst_pad_set_chain_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_jitterer_sink_chain));
  gst_pad_set_event_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_jitterer_sink_event));
  GST_PAD_SET_PROXY_ALLOCATION (self->sinkpad);
  GST_PAD_SET_PROXY_CAPS (self->sinkpad);
  GST_PAD_SET_PROXY_SCHEDULING (self->sinkpad);
  gst_element_add_pad (GST_ELEMENT (self), self->sinkpad);

  self->srcpad = gst_pad_new_from_static_template (&src_template, "src");
  gst_pad_use_fixed_caps (self->srcpad);
  GST_PAD_SET_PROXY_ALLOCATION (self->srcpad);
  GST_PAD_SET_PROXY_CAPS (self->srcpad);
  GST_PAD_SET_PROXY_SCHEDULING (self->srcpad);
  gst_element_add_pad (GST_ELEMENT (self), self->srcpad);

  self->jitter_ampl = 0;
  self->jitter_avg = 0;
  self->drift_ampl = 0;
  self->drift_avg = 0;
  self->change_pts = TRUE;
  self->change_dts = TRUE;
  self->prev_pts = GST_CLOCK_TIME_NONE;

  self->dts_drift_so_far = 0;
  self->pts_drift_so_far = 0;
  self->rand = g_rand_new ();
}

static void
gst_jitterer_finalize (GObject * object)
{
  GstJitterer *self = GST_JITTERER (object);

  g_rand_free (self->rand);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_jitterer_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstJitterer *self = GST_JITTERER (object);

  switch (prop_id) {
    case PROP_JITTER_AMPL:
      self->jitter_ampl = g_value_get_uint64 (value);
      break;
    case PROP_JITTER_AVG:
      self->jitter_avg = g_value_get_int64 (value);
      break;
    case PROP_DRIFT_AMPL:
      self->drift_ampl = g_value_get_uint64 (value);
      break;
    case PROP_DRIFT_AVG:
      self->drift_avg = g_value_get_int64 (value);
      break;
    case PROP_CHANGE_PTS:
      self->change_pts = g_value_get_boolean (value);
      break;
    case PROP_CHANGE_DTS:
      self->change_dts = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_jitterer_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstJitterer *self = GST_JITTERER (object);

  switch (prop_id) {
    case PROP_JITTER_AMPL:
      g_value_set_uint64 (value, self->jitter_ampl);
      break;
    case PROP_JITTER_AVG:
      g_value_set_int64 (value, self->jitter_avg);
      break;
    case PROP_DRIFT_AMPL:
      g_value_set_uint64 (value, self->drift_ampl);
      break;
    case PROP_DRIFT_AVG:
      g_value_set_int64 (value, self->drift_avg);
      break;
    case PROP_CHANGE_PTS:
      g_value_set_boolean (value, self->change_pts);
      break;
    case PROP_CHANGE_DTS:
      g_value_set_boolean (value, self->change_dts);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gint64
gst_jitterer_rand_uint64_range (GRand * rand, gint64 min, gint64 max)
{
  gint64 dist;

  g_assert (max > min);
  dist = max - min;

  if (G_LIKELY (dist < G_MAXINT32)) {
    return g_rand_int_range (rand, 0, dist) + min;
  } else {
    /* This code is based on g_rand_int_range source */
    gint64 ret = max + 1;
    /* maxvalue is set to the predecessor of the greatest
     * multiple of dist less or equal 2^64.
     */
    guint64 maxvalue;
    if (dist <= 0x8000000000000000u) {  /* 2^63 */
      /* maxvalue = 2^64 - 1 - (2^64 % dist) */
      guint64 leftover = (0x8000000000000000u % dist) * 2;
      if (leftover >= dist)
        leftover -= dist;
      maxvalue = 0xffffffffffffffffu - leftover;
    } else {
      maxvalue = dist - 1;
    }
    do {
      gint32 highrand, lowrand;

      highrand = g_rand_int (rand);
      lowrand = g_rand_int (rand);
      ret = ((((gint64) highrand) << 32) | lowrand);
    } while (ret > maxvalue);
    ret %= dist;
    return ret;
  }
}

static gboolean
gst_jitterer_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstJitterer *self = GST_JITTERER (parent);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEGMENT:
    case GST_EVENT_FLUSH_STOP:
      self->prev_pts = GST_CLOCK_TIME_NONE;
      break;
    default:
      break;
  }
  return gst_pad_event_default (pad, parent, event);
}

static GstFlowReturn
gst_jitterer_sink_chain (GstPad * pad, GstObject * parent, GstBuffer * inbuf)
{
  GstJitterer *self = GST_JITTERER (parent);

  if (self->jitter_ampl > 0) {
    gint64 min, max;
    gint64 jitter;

    min = self->jitter_avg - self->jitter_ampl / 2;
    max = self->jitter_avg + self->jitter_ampl / 2;

    if (self->change_pts && GST_BUFFER_PTS (inbuf) != GST_CLOCK_TIME_NONE) {
      jitter = gst_jitterer_rand_uint64_range (self->rand, min, max);
      if (GST_BUFFER_PTS (inbuf) + jitter > 0)
        GST_BUFFER_PTS (inbuf) = GST_BUFFER_PTS (inbuf) + jitter;
    }
    if (self->change_dts && GST_BUFFER_DTS (inbuf) != GST_CLOCK_TIME_NONE) {
      jitter = gst_jitterer_rand_uint64_range (self->rand, min, max);
      if (GST_BUFFER_DTS (inbuf) + jitter > 0)
        GST_BUFFER_DTS (inbuf) = GST_BUFFER_DTS (inbuf) + jitter;
    }
  } else {
    if (self->change_pts && GST_BUFFER_PTS (inbuf) != GST_CLOCK_TIME_NONE)
      GST_BUFFER_PTS (inbuf) = GST_BUFFER_PTS (inbuf) + self->jitter_avg;
    if (self->change_dts && GST_BUFFER_DTS (inbuf) != GST_CLOCK_TIME_NONE)
      GST_BUFFER_DTS (inbuf) = GST_BUFFER_DTS (inbuf) + self->jitter_avg;
  }

  if (self->prev_pts != GST_CLOCK_TIME_NONE) {
    GstClockTimeDiff drift_avg_per_frame;
    GstClockTime pts_diff = GST_BUFFER_PTS (inbuf) - self->prev_pts;

    if (self->drift_avg == 0) {
      drift_avg_per_frame = 0;
    } else if (self->drift_avg > 0) {
      drift_avg_per_frame =
          gst_util_uint64_scale (self->drift_avg, pts_diff, GST_SECOND);
    } else {
      drift_avg_per_frame =
          -gst_util_uint64_scale (-self->drift_avg, pts_diff, GST_SECOND);
    }

    if (self->drift_ampl > 0) {
      gint min, max;
      GstClockTime drift_ampl_per_frame;

      drift_ampl_per_frame =
          gst_util_uint64_scale (self->drift_ampl, pts_diff, GST_SECOND);

      min = drift_avg_per_frame - drift_ampl_per_frame / 2;
      max = drift_avg_per_frame + drift_ampl_per_frame / 2;

      if (self->change_pts && GST_BUFFER_PTS (inbuf) != GST_CLOCK_TIME_NONE) {
        /* FIXME: if (min == max) means the amplitude is too small, make a
         * probabilistic +1 or -1 instead */
        if (min == max)
          self->pts_drift_so_far += drift_avg_per_frame;
        else
          self->pts_drift_so_far +=
              gst_jitterer_rand_uint64_range (self->rand, min, max);
        if (GST_BUFFER_PTS (inbuf) + self->pts_drift_so_far > 0)
          GST_BUFFER_PTS (inbuf) =
              GST_BUFFER_PTS (inbuf) + self->pts_drift_so_far;
      }
      if (self->change_dts && GST_BUFFER_DTS (inbuf) != GST_CLOCK_TIME_NONE) {
        if (min == max)
          self->pts_drift_so_far += drift_avg_per_frame;
        else
          self->dts_drift_so_far +=
              gst_jitterer_rand_uint64_range (self->rand, min, max);
        if (GST_BUFFER_DTS (inbuf) + self->dts_drift_so_far > 0)
          GST_BUFFER_DTS (inbuf) =
              GST_BUFFER_DTS (inbuf) + self->dts_drift_so_far;
      }
    } else {
      if (self->change_pts && GST_BUFFER_PTS (inbuf) != GST_CLOCK_TIME_NONE) {
        self->pts_drift_so_far += drift_avg_per_frame;
        GST_BUFFER_PTS (inbuf) =
            GST_BUFFER_PTS (inbuf) + self->pts_drift_so_far;
      }
      if (self->change_dts && GST_BUFFER_DTS (inbuf) != GST_CLOCK_TIME_NONE) {
        self->dts_drift_so_far += drift_avg_per_frame;
        GST_BUFFER_DTS (inbuf) =
            GST_BUFFER_DTS (inbuf) + self->dts_drift_so_far;
      }
    }
  }
  self->prev_pts = GST_BUFFER_PTS (inbuf);

  return gst_pad_push (self->srcpad, inbuf);
}
