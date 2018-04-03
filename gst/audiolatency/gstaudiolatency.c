/* Audio latency measurement plugin
 * Copyright (C) 2018 Centricular Ltd.
 *   Author: Nirbheek Chauhan <nirbheek@centricular.com>
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
 * SECTION:element-audiolatency
 * @title: audiolatency
 *
 * Measures the audio latency between the source pad and the sink pad by
 * outputting period ticks on the source pad and measuring how long they take to
 * arrive on the sink pad.
 *
 * The ticks have a period of 1 second, so this element can only measure
 * latencies smaller than that.
 *
 * ## Example pipeline
 * |[
 * gst-launch-1.0 -v autoaudiosrc ! audiolatency print-latency=true ! autoaudiosink
 * ]| Continuously print the latency of the audio output and the audio capture
 *
 * In this case, you must ensure that the audio output is captured by the audio
 * source. The simplest way is to use a standard 3.5mm male-to-male audio cable
 * to connect line-out to line-in, or speaker-out to mic-in, etc.
 *
 * Capturing speaker output with a microphone should also work, as long as the
 * ambient noise level is low enough. You may have to adjust the microphone gain
 * to ensure that the volume is loud enough to be detected by the element, and
 * at the same time that it's not so loud that it picks up ambient noise.
 *
 * For programmatic use, instead of using 'print-stats', you should read the
 * 'last-latency' and 'average-latency' properties at most once a second, or
 * parse the "latency" element message, which contains the "last-latency" and
 * "average-latency" fields in the GstStructure.
 *
 * The average latency is a running average of the last 5 measurements.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstaudiolatency.h"

#define AUDIOLATENCY_CAPS "audio/x-raw, " \
    "format = (string) F32LE, " \
    "layout = (string) interleaved, " \
    "rate = (int) [ 1, MAX ], " \
    "channels = (int) [ 1, MAX ]"

GST_DEBUG_CATEGORY_STATIC (gst_audiolatency_debug);
#define GST_CAT_DEFAULT gst_audiolatency_debug

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (AUDIOLATENCY_CAPS)
    );

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (AUDIOLATENCY_CAPS)
    );

#define gst_audiolatency_parent_class parent_class
G_DEFINE_TYPE (GstAudioLatency, gst_audiolatency, GST_TYPE_BIN);

#define DEFAULT_PRINT_LATENCY   FALSE
enum
{
  PROP_0,
  PROP_PRINT_LATENCY,
  PROP_LAST_LATENCY,
  PROP_AVERAGE_LATENCY
};

static gint64 gst_audiolatency_get_latency (GstAudioLatency * self);
static gint64 gst_audiolatency_get_average_latency (GstAudioLatency * self);
static GstFlowReturn gst_audiolatency_sink_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buffer);
static GstPadProbeReturn gst_audiolatency_src_probe (GstPad * pad,
    GstPadProbeInfo * info, gpointer user_data);

static void
gst_audiolatency_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstAudioLatency *self = GST_AUDIOLATENCY (object);

  switch (prop_id) {
    case PROP_PRINT_LATENCY:
      g_value_set_boolean (value, self->print_latency);
      break;
    case PROP_LAST_LATENCY:
      g_value_set_int64 (value, gst_audiolatency_get_latency (self));
      break;
    case PROP_AVERAGE_LATENCY:
      g_value_set_int64 (value, gst_audiolatency_get_average_latency (self));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_audiolatency_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstAudioLatency *self = GST_AUDIOLATENCY (object);

  switch (prop_id) {
    case PROP_PRINT_LATENCY:
      self->print_latency = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_audiolatency_class_init (GstAudioLatencyClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;

  gobject_class->get_property = gst_audiolatency_get_property;
  gobject_class->set_property = gst_audiolatency_set_property;

  g_object_class_install_property (gobject_class, PROP_PRINT_LATENCY,
      g_param_spec_boolean ("print-latency", "Print latencies",
          "Print the measured latencies on stdout",
          DEFAULT_PRINT_LATENCY, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_LAST_LATENCY,
      g_param_spec_int64 ("last-latency", "Last measured latency",
          "The last latency that was measured, in microseconds", 0,
          G_USEC_PER_SEC, 0, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_AVERAGE_LATENCY,
      g_param_spec_int64 ("average-latency", "Running average latency",
          "The running average latency, in microseconds", 0,
          G_USEC_PER_SEC, 0, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_static_pad_template (gstelement_class, &src_template);
  gst_element_class_add_static_pad_template (gstelement_class, &sink_template);

  gst_element_class_set_static_metadata (gstelement_class, "AudioLatency",
      "Audio/Util",
      "Measures the audio latency between the source and the sink",
      "Nirbheek Chauhan <nirbheek@centricular.com>");
}

static void
gst_audiolatency_init (GstAudioLatency * self)
{
  GstPad *srcpad;
  GstPadTemplate *templ;

  self->send_pts = 0;
  self->recv_pts = 0;
  self->print_latency = DEFAULT_PRINT_LATENCY;

  /* Setup sinkpad */
  self->sinkpad = gst_pad_new_from_static_template (&sink_template, "sink");
  gst_pad_set_chain_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_audiolatency_sink_chain));
  gst_element_add_pad (GST_ELEMENT (self), self->sinkpad);

  /* Setup srcpad */
  self->audiosrc = gst_element_factory_make ("audiotestsrc", NULL);
  g_object_set (self->audiosrc, "wave", 8, "samplesperbuffer", 240, NULL);
  gst_bin_add (GST_BIN (self), self->audiosrc);

  templ = gst_static_pad_template_get (&src_template);
  srcpad = gst_element_get_static_pad (self->audiosrc, "src");
  gst_pad_add_probe (srcpad, GST_PAD_PROBE_TYPE_BUFFER,
      (GstPadProbeCallback) gst_audiolatency_src_probe, self, NULL);

  self->srcpad = gst_ghost_pad_new_from_template ("src", srcpad, templ);
  gst_element_add_pad (GST_ELEMENT (self), self->srcpad);
  gst_object_unref (srcpad);
  gst_object_unref (templ);

  GST_LOG_OBJECT (self, "Initialized audiolatency");
}

static gint64
gst_audiolatency_get_latency (GstAudioLatency * self)
{
  gint64 last_latency;
  gint last_latency_idx;

  GST_OBJECT_LOCK (self);
  /* Decrement index, with wrap-around */
  last_latency_idx = self->next_latency_idx - 1;
  if (last_latency_idx < 0)
    last_latency_idx = GST_AUDIOLATENCY_NUM_LATENCIES - 1;

  last_latency = self->latencies[last_latency_idx];
  GST_OBJECT_UNLOCK (self);

  return last_latency;
}

static gint64
gst_audiolatency_get_average_latency_unlocked (GstAudioLatency * self)
{
  int ii, n = 0;
  gint64 average = 0;

  for (ii = 0; ii < GST_AUDIOLATENCY_NUM_LATENCIES; ii++) {
    if (G_LIKELY (self->latencies[ii] > 0))
      n += 1;
    average += self->latencies[ii];
  }

  return average / MAX (n, 1);
}

static gint64
gst_audiolatency_get_average_latency (GstAudioLatency * self)
{
  gint64 average;

  GST_OBJECT_LOCK (self);
  average = gst_audiolatency_get_average_latency_unlocked (self);
  GST_OBJECT_UNLOCK (self);

  return average;
}

static void
gst_audiolatency_set_latency (GstAudioLatency * self, gint64 latency)
{
  gint64 avg_latency;

  GST_OBJECT_LOCK (self);
  self->latencies[self->next_latency_idx] = latency;

  /* Increment index, with wrap-around */
  self->next_latency_idx += 1;
  if (self->next_latency_idx > GST_AUDIOLATENCY_NUM_LATENCIES - 1)
    self->next_latency_idx = 0;

  avg_latency = gst_audiolatency_get_average_latency_unlocked (self);

  if (self->print_latency)
    g_print ("last latency: %" G_GINT64_FORMAT "ms, running average: %"
        G_GINT64_FORMAT "ms\n", latency / 1000, avg_latency / 1000);
  GST_OBJECT_UNLOCK (self);

  /* Post an element message about it */
  gst_element_post_message (GST_ELEMENT (self),
      gst_message_new_element (GST_OBJECT (self),
          gst_structure_new ("latency", "last-latency", G_TYPE_INT64, latency,
              "average-latency", G_TYPE_INT64, avg_latency, NULL)));
}

static gint64
buffer_has_wave (GstBuffer * buffer, GstPad * pad)
{
  const GstStructure *s;
  GstCaps *caps;
  GstMapInfo minfo;
  guint64 duration;
  gint64 offset;
  gint ii, channels, fsize;
  gfloat *fdata;
  gboolean ret;
  GstMemory *memory = NULL;

  switch (gst_buffer_n_memory (buffer)) {
    case 0:
      GST_WARNING_OBJECT (pad, "buffer %" GST_PTR_FORMAT "has no memory?",
          buffer);
      return -1;
    case 1:
      memory = gst_buffer_peek_memory (buffer, 0);
      ret = gst_memory_map (memory, &minfo, GST_MAP_READ);
      break;
    default:
      ret = gst_buffer_map (buffer, &minfo, GST_MAP_READ);
  }

  if (!ret) {
    GST_WARNING_OBJECT (pad, "failed to map buffer %" GST_PTR_FORMAT, buffer);
    return -1;
  }

  caps = gst_pad_get_current_caps (pad);
  s = gst_caps_get_structure (caps, 0);
  ret = gst_structure_get_int (s, "channels", &channels);
  gst_caps_unref (caps);
  if (!ret) {
    GST_WARNING_OBJECT (pad, "unknown number of channels, can't detect wave");
    return -1;
  }

  fdata = (gfloat *) minfo.data;
  fsize = minfo.size / sizeof (gfloat);

  offset = -1;
  duration = GST_BUFFER_DURATION (buffer);
  /* Read only one channel */
  for (ii = 1; ii < fsize; ii += channels) {
    if (ABS (fdata[ii]) > 0.7) {
      /* The waveform probably starts somewhere inside the buffer,
       * so get the offset in nanoseconds from the buffer pts */
      offset = gst_util_uint64_scale_int_round (duration, ii, fsize);
      break;
    }
  }

  if (memory)
    gst_memory_unmap (memory, &minfo);
  else
    gst_buffer_unmap (buffer, &minfo);

  /* Return offset in microseconds */
  return (offset > 0) ? offset / 1000 : -1;
}

static GstPadProbeReturn
gst_audiolatency_src_probe (GstPad * pad, GstPadProbeInfo * info,
    gpointer user_data)
{
  GstAudioLatency *self = user_data;
  GstBuffer *buffer;
  gint64 pts, offset;

  if (!(info->type & GST_PAD_PROBE_TYPE_BUFFER))
    goto out;

  if (GST_STATE (self) != GST_STATE_PLAYING)
    goto out;

  GST_TRACE ("audiotestsrc pushed out a buffer");

  pts = g_get_monotonic_time ();
  /* Ticks are once a second, so once we send something, we can skip
   * checking ~1sec of buffers till the next one. */
  if (self->send_pts > 0 && pts - self->send_pts <= 950 * 1000)
    goto out;

  /* Check if buffer contains a waveform */
  buffer = gst_pad_probe_info_get_buffer (info);
  offset = buffer_has_wave (buffer, pad);
  if (offset < 0)
    goto out;

  pts -= offset;
  {
    gint64 after = 0;
    if (self->send_pts > 0)
      after = (pts - self->send_pts) / 1000;
    GST_INFO ("send pts: %" G_GINT64_FORMAT "us (after %" G_GINT64_FORMAT
        "ms, offset %" G_GINT64_FORMAT "ms)", pts, after, offset / 1000);
  }

  self->send_pts = pts + offset;

out:
  return GST_PAD_PROBE_OK;
}

static GstFlowReturn
gst_audiolatency_sink_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer)
{
  GstAudioLatency *self = GST_AUDIOLATENCY (parent);
  gint64 latency, offset, pts;

  /* Ignore buffers till something gets sent out by us. Fixes a bug where we'd
   * start out by printing one garbage latency value on Windows. */
  if (self->send_pts == 0)
    goto out;

  GST_TRACE_OBJECT (pad, "Got buffer %p", buffer);

  pts = g_get_monotonic_time ();
  /* Ticks are once a second, so once we receive something, we can skip
   * checking ~1sec of buffers till the next one. This way we also don't count
   * the same tick twice for latency measurement. */
  if (self->recv_pts > 0 && pts - self->recv_pts <= 950 * 1000)
    goto out;

  offset = buffer_has_wave (buffer, pad);
  if (offset < 0)
    goto out;

  self->recv_pts = pts + offset;
  latency = (self->recv_pts - self->send_pts);
  gst_audiolatency_set_latency (self, latency);

  GST_INFO ("recv pts: %" G_GINT64_FORMAT "us, latency: %" G_GINT64_FORMAT "ms",
      self->recv_pts, latency / 1000);

out:
  gst_buffer_unref (buffer);
  return GST_FLOW_OK;
}

/* Element registration */
static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_audiolatency_debug, "audiolatency", 0,
      "audiolatency");

  return gst_element_register (plugin, "audiolatency", GST_RANK_PRIMARY,
      GST_TYPE_AUDIOLATENCY);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    audiolatency,
    "A plugin to measure audio latency",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
