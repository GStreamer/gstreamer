/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2001 Thomas <thomas@apestaart.org>
 *               2005,2006 Wim Taymans <wim@fluendo.com>
 *                    2013 Sebastian Dröge <sebastian@centricular.com>
 *                    2014 Collabora
 *                             Olivier Crete <olivier.crete@collabora.com>
 *
 * gstaudioaggregator.c:
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
 * SECTION: gstaudioaggregator
 * @title: GstAudioAggregator
 * @short_description: Base class that manages a set of audio input pads
 * with the purpose of aggregating or mixing their raw audio input buffers
 * @see_also: #GstAggregator, #GstAudioMixer
 *
 * Subclasses must use (a subclass of) #GstAudioAggregatorPad for both
 * their source and sink pads,
 * gst_element_class_add_static_pad_template_with_gtype() is a convenient
 * helper.
 *
 * #GstAudioAggregator can perform conversion on the data arriving
 * on its sink pads, based on the format expected downstream: in order
 * to enable that behaviour, the GType of the sink pads must either be
 * a (subclass of) #GstAudioAggregatorConvertPad to use the default
 * #GstAudioConverter implementation, or a subclass of #GstAudioAggregatorPad
 * implementing #GstAudioAggregatorPad.convert_buffer.
 *
 * To allow for the output caps to change, the mechanism is the same as
 * above, with the GType of the source pad.
 *
 * See #GstAudioMixer for an example.
 *
 * When conversion is enabled, #GstAudioAggregator will accept
 * any type of raw audio caps and perform conversion
 * on the data arriving on its sink pads, with whatever downstream
 * expects as the target format.
 *
 * In case downstream caps are not fully fixated, it will use
 * the first configured sink pad to finish fixating its source pad
 * caps.
 *
 * A notable exception for now is the sample rate, sink pads must
 * have the same sample rate as either the downstream requirement,
 * or the first configured pad, or a combination of both (when
 * downstream specifies a range or a set of acceptable rates).
 */


#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gstaudioaggregator.h"

#include <string.h>

GST_DEBUG_CATEGORY_STATIC (audio_aggregator_debug);
#define GST_CAT_DEFAULT audio_aggregator_debug

struct _GstAudioAggregatorPadPrivate
{
  /* All members are protected by the pad object lock */

  GstBuffer *buffer;            /* current buffer we're mixing, for
                                   comparison with a new input buffer from
                                   aggregator to see if we need to update our
                                   cached values. */

  guint position, size;         /* position in the input buffer and size of the
                                   input buffer in number of samples */

  GstBuffer *input_buffer;

  guint64 output_offset;        /* Sample offset in output segment relative to
                                   pad.segment.start that position refers to
                                   in the current buffer. */

  guint64 next_offset;          /* Next expected sample offset relative to
                                   pad.segment.start */

  /* Last time we noticed a discont */
  GstClockTime discont_time;

  /* A new unhandled segment event has been received */
  gboolean new_segment;
};


/*****************************************
 * GstAudioAggregatorPad implementation  *
 *****************************************/
G_DEFINE_TYPE (GstAudioAggregatorPad, gst_audio_aggregator_pad,
    GST_TYPE_AGGREGATOR_PAD);

enum
{
  PROP_PAD_0,
  PROP_PAD_CONVERTER_CONFIG,
};

static GstFlowReturn
gst_audio_aggregator_pad_flush_pad (GstAggregatorPad * aggpad,
    GstAggregator * aggregator);

static void
gst_audio_aggregator_pad_finalize (GObject * object)
{
  GstAudioAggregatorPad *pad = (GstAudioAggregatorPad *) object;

  gst_buffer_replace (&pad->priv->buffer, NULL);
  gst_buffer_replace (&pad->priv->input_buffer, NULL);

  G_OBJECT_CLASS (gst_audio_aggregator_pad_parent_class)->finalize (object);
}

static void
gst_audio_aggregator_pad_class_init (GstAudioAggregatorPadClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstAggregatorPadClass *aggpadclass = (GstAggregatorPadClass *) klass;

  g_type_class_add_private (klass, sizeof (GstAudioAggregatorPadPrivate));

  gobject_class->finalize = gst_audio_aggregator_pad_finalize;
  aggpadclass->flush = GST_DEBUG_FUNCPTR (gst_audio_aggregator_pad_flush_pad);
}

static void
gst_audio_aggregator_pad_init (GstAudioAggregatorPad * pad)
{
  pad->priv =
      G_TYPE_INSTANCE_GET_PRIVATE (pad, GST_TYPE_AUDIO_AGGREGATOR_PAD,
      GstAudioAggregatorPadPrivate);

  gst_audio_info_init (&pad->info);

  pad->priv->buffer = NULL;
  pad->priv->input_buffer = NULL;
  pad->priv->position = 0;
  pad->priv->size = 0;
  pad->priv->output_offset = -1;
  pad->priv->next_offset = -1;
  pad->priv->discont_time = GST_CLOCK_TIME_NONE;
}


static GstFlowReturn
gst_audio_aggregator_pad_flush_pad (GstAggregatorPad * aggpad,
    GstAggregator * aggregator)
{
  GstAudioAggregatorPad *pad = GST_AUDIO_AGGREGATOR_PAD (aggpad);

  GST_OBJECT_LOCK (aggpad);
  pad->priv->position = pad->priv->size = 0;
  pad->priv->output_offset = pad->priv->next_offset = -1;
  pad->priv->discont_time = GST_CLOCK_TIME_NONE;
  gst_buffer_replace (&pad->priv->buffer, NULL);
  gst_buffer_replace (&pad->priv->input_buffer, NULL);
  GST_OBJECT_UNLOCK (aggpad);

  return GST_FLOW_OK;
}

struct _GstAudioAggregatorConvertPadPrivate
{
  /* All members are protected by the pad object lock */
  GstAudioConverter *converter;
  GstStructure *converter_config;
  gboolean converter_config_changed;
};


G_DEFINE_TYPE (GstAudioAggregatorConvertPad, gst_audio_aggregator_convert_pad,
    GST_TYPE_AUDIO_AGGREGATOR_PAD);

static void
gst_audio_aggregator_convert_pad_update_converter (GstAudioAggregatorConvertPad
    * aaggcpad, GstAudioInfo * in_info, GstAudioInfo * out_info)
{
  if (!aaggcpad->priv->converter_config_changed)
    return;

  if (aaggcpad->priv->converter) {
    gst_audio_converter_free (aaggcpad->priv->converter);
    aaggcpad->priv->converter = NULL;
  }

  if (gst_audio_info_is_equal (in_info, out_info) ||
      in_info->finfo->format == GST_AUDIO_FORMAT_UNKNOWN) {
    if (aaggcpad->priv->converter) {
      gst_audio_converter_free (aaggcpad->priv->converter);
      aaggcpad->priv->converter = NULL;
    }
  } else {
    /* If we haven't received caps yet, this pad should not have
     * a buffer to convert anyway */
    aaggcpad->priv->converter =
        gst_audio_converter_new (GST_AUDIO_CONVERTER_FLAG_NONE,
        in_info, out_info,
        aaggcpad->priv->converter_config ? gst_structure_copy (aaggcpad->
            priv->converter_config) : NULL);
  }

  aaggcpad->priv->converter_config_changed = FALSE;
}

static void
gst_audio_aggregator_pad_update_conversion_info (GstAudioAggregatorPad *
    aaggpad)
{
  GST_AUDIO_AGGREGATOR_CONVERT_PAD (aaggpad)->priv->converter_config_changed =
      TRUE;
}

static GstBuffer *
gst_audio_aggregator_convert_pad_convert_buffer (GstAudioAggregatorPad *
    aaggpad, GstAudioInfo * in_info, GstAudioInfo * out_info,
    GstBuffer * input_buffer)
{
  GstBuffer *res;
  GstAudioAggregatorConvertPad *aaggcpad =
      GST_AUDIO_AGGREGATOR_CONVERT_PAD (aaggpad);

  gst_audio_aggregator_convert_pad_update_converter (aaggcpad, in_info,
      out_info);

  if (aaggcpad->priv->converter) {
    gint insize = gst_buffer_get_size (input_buffer);
    gsize insamples = insize / in_info->bpf;
    gsize outsamples =
        gst_audio_converter_get_out_frames (aaggcpad->priv->converter,
        insamples);
    gint outsize = outsamples * out_info->bpf;
    GstMapInfo inmap, outmap;

    res = gst_buffer_new_allocate (NULL, outsize, NULL);

    /* We create a perfectly similar buffer, except obviously for
     * its converted contents */
    gst_buffer_copy_into (res, input_buffer,
        GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS |
        GST_BUFFER_COPY_META, 0, -1);

    gst_buffer_map (input_buffer, &inmap, GST_MAP_READ);
    gst_buffer_map (res, &outmap, GST_MAP_WRITE);

    gst_audio_converter_samples (aaggcpad->priv->converter,
        GST_AUDIO_CONVERTER_FLAG_NONE,
        (gpointer *) & inmap.data, insamples,
        (gpointer *) & outmap.data, outsamples);

    gst_buffer_unmap (input_buffer, &inmap);
    gst_buffer_unmap (res, &outmap);
  } else {
    res = gst_buffer_ref (input_buffer);
  }

  return res;
}

static void
gst_audio_aggregator_convert_pad_finalize (GObject * object)
{
  GstAudioAggregatorConvertPad *pad = (GstAudioAggregatorConvertPad *) object;

  if (pad->priv->converter)
    gst_audio_converter_free (pad->priv->converter);

  if (pad->priv->converter_config)
    gst_structure_free (pad->priv->converter_config);

  G_OBJECT_CLASS (gst_audio_aggregator_convert_pad_parent_class)->finalize
      (object);
}

static void
gst_audio_aggregator_convert_pad_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstAudioAggregatorConvertPad *pad = GST_AUDIO_AGGREGATOR_CONVERT_PAD (object);

  switch (prop_id) {
    case PROP_PAD_CONVERTER_CONFIG:
      GST_OBJECT_LOCK (pad);
      if (pad->priv->converter_config)
        g_value_set_boxed (value, pad->priv->converter_config);
      GST_OBJECT_UNLOCK (pad);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_audio_aggregator_convert_pad_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAudioAggregatorConvertPad *pad = GST_AUDIO_AGGREGATOR_CONVERT_PAD (object);

  switch (prop_id) {
    case PROP_PAD_CONVERTER_CONFIG:
      GST_OBJECT_LOCK (pad);
      if (pad->priv->converter_config)
        gst_structure_free (pad->priv->converter_config);
      pad->priv->converter_config = g_value_dup_boxed (value);
      pad->priv->converter_config_changed = TRUE;
      GST_OBJECT_UNLOCK (pad);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_audio_aggregator_convert_pad_class_init (GstAudioAggregatorConvertPadClass *
    klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstAudioAggregatorPadClass *aaggpad_class =
      (GstAudioAggregatorPadClass *) klass;
  g_type_class_add_private (klass,
      sizeof (GstAudioAggregatorConvertPadPrivate));

  gobject_class->set_property = gst_audio_aggregator_convert_pad_set_property;
  gobject_class->get_property = gst_audio_aggregator_convert_pad_get_property;

  g_object_class_install_property (gobject_class, PROP_PAD_CONVERTER_CONFIG,
      g_param_spec_boxed ("converter-config", "Converter configuration",
          "A GstStructure describing the configuration that should be used "
          "when converting this pad's audio buffers",
          GST_TYPE_STRUCTURE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  aaggpad_class->convert_buffer =
      gst_audio_aggregator_convert_pad_convert_buffer;

  aaggpad_class->update_conversion_info =
      gst_audio_aggregator_pad_update_conversion_info;

  gobject_class->finalize = gst_audio_aggregator_convert_pad_finalize;
}

static void
gst_audio_aggregator_convert_pad_init (GstAudioAggregatorConvertPad * pad)
{
  pad->priv =
      G_TYPE_INSTANCE_GET_PRIVATE (pad, GST_TYPE_AUDIO_AGGREGATOR_CONVERT_PAD,
      GstAudioAggregatorConvertPadPrivate);
}

/**************************************
 * GstAudioAggregator implementation  *
 **************************************/

struct _GstAudioAggregatorPrivate
{
  GMutex mutex;

  /* All three properties are unprotected, can't be modified while streaming */
  /* Size in frames that is output per buffer */
  GstClockTime output_buffer_duration;
  GstClockTime alignment_threshold;
  GstClockTime discont_wait;

  /* Protected by srcpad stream clock */
  /* Output buffer starting at offset containing blocksize frames (calculated
   * from output_buffer_duration) */
  GstBuffer *current_buffer;

  /* counters to keep track of timestamps */
  /* Readable with object lock, writable with both aag lock and object lock */

  /* Sample offset starting from 0 at aggregator.segment.start */
  gint64 offset;
};

#define GST_AUDIO_AGGREGATOR_LOCK(self)   g_mutex_lock (&(self)->priv->mutex);
#define GST_AUDIO_AGGREGATOR_UNLOCK(self) g_mutex_unlock (&(self)->priv->mutex);

static void gst_audio_aggregator_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_audio_aggregator_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_audio_aggregator_dispose (GObject * object);

static gboolean gst_audio_aggregator_src_event (GstAggregator * agg,
    GstEvent * event);
static gboolean gst_audio_aggregator_sink_event (GstAggregator * agg,
    GstAggregatorPad * aggpad, GstEvent * event);
static gboolean gst_audio_aggregator_src_query (GstAggregator * agg,
    GstQuery * query);
static gboolean
gst_audio_aggregator_sink_query (GstAggregator * agg, GstAggregatorPad * aggpad,
    GstQuery * query);
static gboolean gst_audio_aggregator_start (GstAggregator * agg);
static gboolean gst_audio_aggregator_stop (GstAggregator * agg);
static GstFlowReturn gst_audio_aggregator_flush (GstAggregator * agg);

static GstBuffer *gst_audio_aggregator_create_output_buffer (GstAudioAggregator
    * aagg, guint num_frames);
static GstBuffer *gst_audio_aggregator_do_clip (GstAggregator * agg,
    GstAggregatorPad * bpad, GstBuffer * buffer);
static GstFlowReturn gst_audio_aggregator_aggregate (GstAggregator * agg,
    gboolean timeout);
static gboolean sync_pad_values (GstElement * aagg, GstPad * pad, gpointer ud);
static gboolean gst_audio_aggregator_negotiated_src_caps (GstAggregator * agg,
    GstCaps * caps);
static GstFlowReturn
gst_audio_aggregator_update_src_caps (GstAggregator * agg,
    GstCaps * caps, GstCaps ** ret);
static GstCaps *gst_audio_aggregator_fixate_src_caps (GstAggregator * agg,
    GstCaps * caps);

#define DEFAULT_OUTPUT_BUFFER_DURATION (10 * GST_MSECOND)
#define DEFAULT_ALIGNMENT_THRESHOLD   (40 * GST_MSECOND)
#define DEFAULT_DISCONT_WAIT (1 * GST_SECOND)

enum
{
  PROP_0,
  PROP_OUTPUT_BUFFER_DURATION,
  PROP_ALIGNMENT_THRESHOLD,
  PROP_DISCONT_WAIT,
};

G_DEFINE_ABSTRACT_TYPE (GstAudioAggregator, gst_audio_aggregator,
    GST_TYPE_AGGREGATOR);

static GstClockTime
gst_audio_aggregator_get_next_time (GstAggregator * agg)
{
  GstClockTime next_time;
  GstSegment *segment = &GST_AGGREGATOR_PAD (agg->srcpad)->segment;

  GST_OBJECT_LOCK (agg);
  if (segment->position == -1 || segment->position < segment->start)
    next_time = segment->start;
  else
    next_time = segment->position;

  if (segment->stop != -1 && next_time > segment->stop)
    next_time = segment->stop;

  next_time = gst_segment_to_running_time (segment, GST_FORMAT_TIME, next_time);
  GST_OBJECT_UNLOCK (agg);

  return next_time;
}

static GstBuffer *
gst_audio_aggregator_convert_buffer (GstAudioAggregator * aagg, GstPad * pad,
    GstAudioInfo * in_info, GstAudioInfo * out_info, GstBuffer * buffer)
{
  GstAudioAggregatorPadClass *klass = GST_AUDIO_AGGREGATOR_PAD_GET_CLASS (pad);
  GstAudioAggregatorPad *aaggpad = GST_AUDIO_AGGREGATOR_PAD (pad);

  g_assert (klass->convert_buffer);

  return klass->convert_buffer (aaggpad, in_info, out_info, buffer);
}

static void
gst_audio_aggregator_class_init (GstAudioAggregatorClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstAggregatorClass *gstaggregator_class = (GstAggregatorClass *) klass;

  g_type_class_add_private (klass, sizeof (GstAudioAggregatorPrivate));

  gobject_class->set_property = gst_audio_aggregator_set_property;
  gobject_class->get_property = gst_audio_aggregator_get_property;
  gobject_class->dispose = gst_audio_aggregator_dispose;

  gstaggregator_class->src_event =
      GST_DEBUG_FUNCPTR (gst_audio_aggregator_src_event);
  gstaggregator_class->sink_event =
      GST_DEBUG_FUNCPTR (gst_audio_aggregator_sink_event);
  gstaggregator_class->src_query =
      GST_DEBUG_FUNCPTR (gst_audio_aggregator_src_query);
  gstaggregator_class->sink_query = gst_audio_aggregator_sink_query;
  gstaggregator_class->start = gst_audio_aggregator_start;
  gstaggregator_class->stop = gst_audio_aggregator_stop;
  gstaggregator_class->flush = gst_audio_aggregator_flush;
  gstaggregator_class->aggregate =
      GST_DEBUG_FUNCPTR (gst_audio_aggregator_aggregate);
  gstaggregator_class->clip = GST_DEBUG_FUNCPTR (gst_audio_aggregator_do_clip);
  gstaggregator_class->get_next_time = gst_audio_aggregator_get_next_time;
  gstaggregator_class->update_src_caps =
      GST_DEBUG_FUNCPTR (gst_audio_aggregator_update_src_caps);
  gstaggregator_class->fixate_src_caps = gst_audio_aggregator_fixate_src_caps;
  gstaggregator_class->negotiated_src_caps =
      gst_audio_aggregator_negotiated_src_caps;

  klass->create_output_buffer = gst_audio_aggregator_create_output_buffer;

  GST_DEBUG_CATEGORY_INIT (audio_aggregator_debug, "audioaggregator",
      GST_DEBUG_FG_MAGENTA, "GstAudioAggregator");

  g_object_class_install_property (gobject_class, PROP_OUTPUT_BUFFER_DURATION,
      g_param_spec_uint64 ("output-buffer-duration", "Output Buffer Duration",
          "Output block size in nanoseconds", 1,
          G_MAXUINT64, DEFAULT_OUTPUT_BUFFER_DURATION,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ALIGNMENT_THRESHOLD,
      g_param_spec_uint64 ("alignment-threshold", "Alignment Threshold",
          "Timestamp alignment threshold in nanoseconds", 0,
          G_MAXUINT64 - 1, DEFAULT_ALIGNMENT_THRESHOLD,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_DISCONT_WAIT,
      g_param_spec_uint64 ("discont-wait", "Discont Wait",
          "Window of time in nanoseconds to wait before "
          "creating a discontinuity", 0,
          G_MAXUINT64 - 1, DEFAULT_DISCONT_WAIT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_audio_aggregator_init (GstAudioAggregator * aagg)
{
  aagg->priv =
      G_TYPE_INSTANCE_GET_PRIVATE (aagg, GST_TYPE_AUDIO_AGGREGATOR,
      GstAudioAggregatorPrivate);

  g_mutex_init (&aagg->priv->mutex);

  aagg->priv->output_buffer_duration = DEFAULT_OUTPUT_BUFFER_DURATION;
  aagg->priv->alignment_threshold = DEFAULT_ALIGNMENT_THRESHOLD;
  aagg->priv->discont_wait = DEFAULT_DISCONT_WAIT;

  aagg->current_caps = NULL;

  gst_aggregator_set_latency (GST_AGGREGATOR (aagg),
      aagg->priv->output_buffer_duration, aagg->priv->output_buffer_duration);
}

static void
gst_audio_aggregator_dispose (GObject * object)
{
  GstAudioAggregator *aagg = GST_AUDIO_AGGREGATOR (object);

  gst_caps_replace (&aagg->current_caps, NULL);

  g_mutex_clear (&aagg->priv->mutex);

  G_OBJECT_CLASS (gst_audio_aggregator_parent_class)->dispose (object);
}

static void
gst_audio_aggregator_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAudioAggregator *aagg = GST_AUDIO_AGGREGATOR (object);

  switch (prop_id) {
    case PROP_OUTPUT_BUFFER_DURATION:
      aagg->priv->output_buffer_duration = g_value_get_uint64 (value);
      gst_aggregator_set_latency (GST_AGGREGATOR (aagg),
          aagg->priv->output_buffer_duration,
          aagg->priv->output_buffer_duration);
      break;
    case PROP_ALIGNMENT_THRESHOLD:
      aagg->priv->alignment_threshold = g_value_get_uint64 (value);
      break;
    case PROP_DISCONT_WAIT:
      aagg->priv->discont_wait = g_value_get_uint64 (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_audio_aggregator_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstAudioAggregator *aagg = GST_AUDIO_AGGREGATOR (object);

  switch (prop_id) {
    case PROP_OUTPUT_BUFFER_DURATION:
      g_value_set_uint64 (value, aagg->priv->output_buffer_duration);
      break;
    case PROP_ALIGNMENT_THRESHOLD:
      g_value_set_uint64 (value, aagg->priv->alignment_threshold);
      break;
    case PROP_DISCONT_WAIT:
      g_value_set_uint64 (value, aagg->priv->discont_wait);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* Caps negotiation */

/* Unref after usage */
static GstAudioAggregatorPad *
gst_audio_aggregator_get_first_configured_pad (GstAggregator * agg)
{
  GstAudioAggregatorPad *res = NULL;
  GList *l;

  GST_OBJECT_LOCK (agg);
  for (l = GST_ELEMENT (agg)->sinkpads; l; l = l->next) {
    GstAudioAggregatorPad *aaggpad = l->data;

    if (GST_AUDIO_INFO_FORMAT (&aaggpad->info) != GST_AUDIO_FORMAT_UNKNOWN) {
      res = gst_object_ref (aaggpad);
      break;
    }
  }
  GST_OBJECT_UNLOCK (agg);

  return res;
}

static GstCaps *
gst_audio_aggregator_sink_getcaps (GstPad * pad, GstAggregator * agg,
    GstCaps * filter)
{
  GstAudioAggregatorPad *first_configured_pad =
      gst_audio_aggregator_get_first_configured_pad (agg);
  GstCaps *sink_template_caps = gst_pad_get_pad_template_caps (pad);
  GstCaps *downstream_caps = gst_pad_get_allowed_caps (agg->srcpad);
  GstCaps *sink_caps;
  GstStructure *s, *s2;
  gint downstream_rate;

  sink_template_caps = gst_caps_make_writable (sink_template_caps);
  s = gst_caps_get_structure (sink_template_caps, 0);

  if (downstream_caps && !gst_caps_is_empty (downstream_caps))
    s2 = gst_caps_get_structure (downstream_caps, 0);
  else
    s2 = NULL;

  if (s2 && gst_structure_get_int (s2, "rate", &downstream_rate)) {
    gst_structure_fixate_field_nearest_int (s, "rate", downstream_rate);
  } else if (first_configured_pad) {
    gst_structure_fixate_field_nearest_int (s, "rate",
        first_configured_pad->info.rate);
  }

  if (first_configured_pad)
    gst_object_unref (first_configured_pad);

  sink_caps = filter ? gst_caps_intersect (sink_template_caps,
      filter) : gst_caps_ref (sink_template_caps);

  GST_INFO_OBJECT (pad, "Getting caps with filter %" GST_PTR_FORMAT, filter);
  GST_DEBUG_OBJECT (pad, "sink template caps : %" GST_PTR_FORMAT,
      sink_template_caps);
  GST_DEBUG_OBJECT (pad, "downstream caps %" GST_PTR_FORMAT, downstream_caps);
  GST_INFO_OBJECT (pad, "returned sink caps : %" GST_PTR_FORMAT, sink_caps);

  gst_caps_unref (sink_template_caps);

  if (downstream_caps)
    gst_caps_unref (downstream_caps);

  return sink_caps;
}

static gboolean
gst_audio_aggregator_sink_setcaps (GstAudioAggregatorPad * aaggpad,
    GstAggregator * agg, GstCaps * caps)
{
  GstAudioAggregatorPad *first_configured_pad =
      gst_audio_aggregator_get_first_configured_pad (agg);
  GstCaps *downstream_caps = gst_pad_get_allowed_caps (agg->srcpad);
  GstAudioInfo info;
  gboolean ret = TRUE;
  gint downstream_rate;
  GstStructure *s;

  if (!downstream_caps || gst_caps_is_empty (downstream_caps)) {
    ret = FALSE;
    goto done;
  }

  gst_audio_info_from_caps (&info, caps);
  s = gst_caps_get_structure (downstream_caps, 0);

  /* TODO: handle different rates on sinkpads, a bit complex
   * because offsets will have to be updated, and audio resampling
   * has a latency to take into account
   */
  if ((gst_structure_get_int (s, "rate", &downstream_rate)
          && info.rate != downstream_rate) || (first_configured_pad
          && info.rate != first_configured_pad->info.rate)) {
    gst_pad_push_event (GST_PAD (aaggpad), gst_event_new_reconfigure ());
    ret = FALSE;
  } else {
    GstAudioAggregatorPadClass *klass =
        GST_AUDIO_AGGREGATOR_PAD_GET_CLASS (aaggpad);
    GST_OBJECT_LOCK (aaggpad);
    gst_audio_info_from_caps (&aaggpad->info, caps);
    if (klass->update_conversion_info)
      klass->update_conversion_info (aaggpad);
    GST_OBJECT_UNLOCK (aaggpad);
  }

done:
  if (first_configured_pad)
    gst_object_unref (first_configured_pad);

  if (downstream_caps)
    gst_caps_unref (downstream_caps);

  return ret;
}

static GstFlowReturn
gst_audio_aggregator_update_src_caps (GstAggregator * agg,
    GstCaps * caps, GstCaps ** ret)
{
  GstCaps *src_template_caps = gst_pad_get_pad_template_caps (agg->srcpad);
  GstCaps *downstream_caps =
      gst_pad_peer_query_caps (agg->srcpad, src_template_caps);

  gst_caps_unref (src_template_caps);

  *ret = gst_caps_intersect (caps, downstream_caps);

  GST_INFO ("Updated src caps to %" GST_PTR_FORMAT, *ret);

  if (downstream_caps)
    gst_caps_unref (downstream_caps);

  return GST_FLOW_OK;
}

/* At that point if the caps are not fixed, this means downstream
 * didn't have fully specified requirements, we'll just go ahead
 * and fixate raw audio fields using our first configured pad, we don't for
 * now need a more complicated heuristic
 */
static GstCaps *
gst_audio_aggregator_fixate_src_caps (GstAggregator * agg, GstCaps * caps)
{
  GstAudioAggregatorPad *first_configured_pad;

  if (!GST_AUDIO_AGGREGATOR_PAD_GET_CLASS (agg->srcpad)->convert_buffer)
    return
        GST_AGGREGATOR_CLASS
        (gst_audio_aggregator_parent_class)->fixate_src_caps (agg, caps);

  first_configured_pad = gst_audio_aggregator_get_first_configured_pad (agg);

  if (first_configured_pad) {
    GstStructure *s, *s2;
    GstCaps *first_configured_caps =
        gst_audio_info_to_caps (&first_configured_pad->info);
    gint first_configured_rate, first_configured_channels;

    caps = gst_caps_make_writable (caps);
    s = gst_caps_get_structure (caps, 0);
    s2 = gst_caps_get_structure (first_configured_caps, 0);

    gst_structure_get_int (s2, "rate", &first_configured_rate);
    gst_structure_get_int (s2, "channels", &first_configured_channels);

    gst_structure_fixate_field_string (s, "format",
        gst_structure_get_string (s2, "format"));
    gst_structure_fixate_field_string (s, "layout",
        gst_structure_get_string (s2, "layout"));
    gst_structure_fixate_field_nearest_int (s, "rate", first_configured_rate);
    gst_structure_fixate_field_nearest_int (s, "channels",
        first_configured_channels);

    gst_caps_unref (first_configured_caps);
    gst_object_unref (first_configured_pad);
  }

  if (!gst_caps_is_fixed (caps))
    caps = gst_caps_fixate (caps);

  GST_INFO_OBJECT (agg, "Fixated src caps to %" GST_PTR_FORMAT, caps);

  return caps;
}

/* Must be called with OBJECT_LOCK taken */
static void
gst_audio_aggregator_update_converters (GstAudioAggregator * aagg,
    GstAudioInfo * new_info)
{
  GList *l;

  for (l = GST_ELEMENT (aagg)->sinkpads; l; l = l->next) {
    GstAudioAggregatorPad *aaggpad = l->data;
    GstAudioAggregatorPadClass *klass =
        GST_AUDIO_AGGREGATOR_PAD_GET_CLASS (aaggpad);

    if (klass->update_conversion_info)
      klass->update_conversion_info (aaggpad);

    /* If we currently were mixing a buffer, we need to convert it to the new
     * format */
    if (aaggpad->priv->buffer) {
      GstBuffer *new_converted_buffer =
          gst_audio_aggregator_convert_buffer (aagg, GST_PAD (aaggpad),
          &aaggpad->info, new_info, aaggpad->priv->input_buffer);
      gst_buffer_replace (&aaggpad->priv->buffer, new_converted_buffer);
    }
  }
}

/* We now have our final output caps, we can create the required converters */
static gboolean
gst_audio_aggregator_negotiated_src_caps (GstAggregator * agg, GstCaps * caps)
{
  GstAudioAggregator *aagg = GST_AUDIO_AGGREGATOR (agg);
  GstAudioInfo info;
  GstAudioAggregatorPad *srcpad = GST_AUDIO_AGGREGATOR_PAD (agg->srcpad);

  GST_INFO_OBJECT (agg, "src caps negotiated %" GST_PTR_FORMAT, caps);

  if (!gst_audio_info_from_caps (&info, caps)) {
    GST_WARNING_OBJECT (aagg, "Rejecting invalid caps: %" GST_PTR_FORMAT, caps);
    return FALSE;
  }

  GST_AUDIO_AGGREGATOR_LOCK (aagg);
  GST_OBJECT_LOCK (aagg);

  if (GST_AUDIO_AGGREGATOR_PAD_GET_CLASS (agg->srcpad)->convert_buffer) {
    gst_audio_aggregator_update_converters (aagg, &info);

    if (aagg->priv->current_buffer
        && !gst_audio_info_is_equal (&srcpad->info, &info)) {
      GstBuffer *converted;
      GstAudioAggregatorPadClass *klass =
          GST_AUDIO_AGGREGATOR_PAD_GET_CLASS (agg->srcpad);

      if (klass->update_conversion_info)
        klass->update_conversion_info (GST_AUDIO_AGGREGATOR_PAD (agg->srcpad));

      converted =
          gst_audio_aggregator_convert_buffer (aagg, agg->srcpad, &srcpad->info,
          &info, aagg->priv->current_buffer);
      gst_buffer_unref (aagg->priv->current_buffer);
      aagg->priv->current_buffer = converted;
    }
  }

  if (!gst_audio_info_is_equal (&info, &srcpad->info)) {
    GST_INFO_OBJECT (aagg, "setting caps to %" GST_PTR_FORMAT, caps);
    gst_caps_replace (&aagg->current_caps, caps);

    memcpy (&srcpad->info, &info, sizeof (info));
  }

  GST_OBJECT_UNLOCK (aagg);
  GST_AUDIO_AGGREGATOR_UNLOCK (aagg);

  return
      GST_AGGREGATOR_CLASS
      (gst_audio_aggregator_parent_class)->negotiated_src_caps (agg, caps);
}

/* event handling */

static gboolean
gst_audio_aggregator_src_event (GstAggregator * agg, GstEvent * event)
{
  gboolean result;

  GstAudioAggregator *aagg = GST_AUDIO_AGGREGATOR (agg);
  GST_DEBUG_OBJECT (agg->srcpad, "Got %s event on src pad",
      GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_QOS:
      /* QoS might be tricky */
      gst_event_unref (event);
      return FALSE;
    case GST_EVENT_NAVIGATION:
      /* navigation is rather pointless. */
      gst_event_unref (event);
      return FALSE;
      break;
    case GST_EVENT_SEEK:
    {
      GstSeekFlags flags;
      gdouble rate;
      GstSeekType start_type, stop_type;
      gint64 start, stop;
      GstFormat seek_format, dest_format;

      /* parse the seek parameters */
      gst_event_parse_seek (event, &rate, &seek_format, &flags, &start_type,
          &start, &stop_type, &stop);

      /* Check the seeking parameters before linking up */
      if ((start_type != GST_SEEK_TYPE_NONE)
          && (start_type != GST_SEEK_TYPE_SET)) {
        result = FALSE;
        GST_DEBUG_OBJECT (aagg,
            "seeking failed, unhandled seek type for start: %d", start_type);
        goto done;
      }
      if ((stop_type != GST_SEEK_TYPE_NONE) && (stop_type != GST_SEEK_TYPE_SET)) {
        result = FALSE;
        GST_DEBUG_OBJECT (aagg,
            "seeking failed, unhandled seek type for end: %d", stop_type);
        goto done;
      }

      GST_OBJECT_LOCK (agg);
      dest_format = GST_AGGREGATOR_PAD (agg->srcpad)->segment.format;
      GST_OBJECT_UNLOCK (agg);
      if (seek_format != dest_format) {
        result = FALSE;
        GST_DEBUG_OBJECT (aagg,
            "seeking failed, unhandled seek format: %s",
            gst_format_get_name (seek_format));
        goto done;
      }
    }
      break;
    default:
      break;
  }

  return
      GST_AGGREGATOR_CLASS (gst_audio_aggregator_parent_class)->src_event (agg,
      event);

done:
  return result;
}


static gboolean
gst_audio_aggregator_sink_event (GstAggregator * agg,
    GstAggregatorPad * aggpad, GstEvent * event)
{
  GstAudioAggregatorPad *aaggpad = GST_AUDIO_AGGREGATOR_PAD (aggpad);
  gboolean res = TRUE;

  GST_DEBUG_OBJECT (aggpad, "Got %s event on sink pad",
      GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEGMENT:
    {
      const GstSegment *segment;
      gst_event_parse_segment (event, &segment);

      if (segment->format != GST_FORMAT_TIME) {
        GST_ERROR_OBJECT (agg, "Segment of type %s are not supported,"
            " only TIME segments are supported",
            gst_format_get_name (segment->format));
        gst_event_unref (event);
        event = NULL;
        res = FALSE;
        break;
      }

      GST_OBJECT_LOCK (agg);
      if (segment->rate != GST_AGGREGATOR_PAD (agg->srcpad)->segment.rate) {
        GST_ERROR_OBJECT (aggpad,
            "Got segment event with wrong rate %lf, expected %lf",
            segment->rate, GST_AGGREGATOR_PAD (agg->srcpad)->segment.rate);
        res = FALSE;
        gst_event_unref (event);
        event = NULL;
      } else if (segment->rate < 0.0) {
        GST_ERROR_OBJECT (aggpad, "Negative rates not supported yet");
        res = FALSE;
        gst_event_unref (event);
        event = NULL;
      } else {
        GstAudioAggregatorPad *pad = GST_AUDIO_AGGREGATOR_PAD (aggpad);

        GST_OBJECT_LOCK (pad);
        pad->priv->new_segment = TRUE;
        GST_OBJECT_UNLOCK (pad);
      }
      GST_OBJECT_UNLOCK (agg);

      break;
    }
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      GST_INFO_OBJECT (aggpad, "Got caps %" GST_PTR_FORMAT, caps);
      res = gst_audio_aggregator_sink_setcaps (aaggpad, agg, caps);
      gst_event_unref (event);
      event = NULL;
      break;
    }
    default:
      break;
  }

  if (event != NULL)
    return
        GST_AGGREGATOR_CLASS (gst_audio_aggregator_parent_class)->sink_event
        (agg, aggpad, event);

  return res;
}

static gboolean
gst_audio_aggregator_sink_query (GstAggregator * agg, GstAggregatorPad * aggpad,
    GstQuery * query)
{
  gboolean res = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *filter, *caps;

      gst_query_parse_caps (query, &filter);
      caps = gst_audio_aggregator_sink_getcaps (GST_PAD (aggpad), agg, filter);
      gst_query_set_caps_result (query, caps);
      gst_caps_unref (caps);
      res = TRUE;
      break;
    }
    default:
      res =
          GST_AGGREGATOR_CLASS (gst_audio_aggregator_parent_class)->sink_query
          (agg, aggpad, query);
      break;
  }

  return res;
}


/* FIXME, the duration query should reflect how long you will produce
 * data, that is the amount of stream time until you will emit EOS.
 *
 * For synchronized mixing this is always the max of all the durations
 * of upstream since we emit EOS when all of them finished.
 *
 * We don't do synchronized mixing so this really depends on where the
 * streams where punched in and what their relative offsets are against
 * eachother which we can get from the first timestamps we see.
 *
 * When we add a new stream (or remove a stream) the duration might
 * also become invalid again and we need to post a new DURATION
 * message to notify this fact to the parent.
 * For now we take the max of all the upstream elements so the simple
 * cases work at least somewhat.
 */
static gboolean
gst_audio_aggregator_query_duration (GstAudioAggregator * aagg,
    GstQuery * query)
{
  gint64 max;
  gboolean res;
  GstFormat format;
  GstIterator *it;
  gboolean done;
  GValue item = { 0, };

  /* parse format */
  gst_query_parse_duration (query, &format, NULL);

  max = -1;
  res = TRUE;
  done = FALSE;

  it = gst_element_iterate_sink_pads (GST_ELEMENT_CAST (aagg));
  while (!done) {
    GstIteratorResult ires;

    ires = gst_iterator_next (it, &item);
    switch (ires) {
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
      case GST_ITERATOR_OK:
      {
        GstPad *pad = g_value_get_object (&item);
        gint64 duration;

        /* ask sink peer for duration */
        res &= gst_pad_peer_query_duration (pad, format, &duration);
        /* take max from all valid return values */
        if (res) {
          /* valid unknown length, stop searching */
          if (duration == -1) {
            max = duration;
            done = TRUE;
          }
          /* else see if bigger than current max */
          else if (duration > max)
            max = duration;
        }
        g_value_reset (&item);
        break;
      }
      case GST_ITERATOR_RESYNC:
        max = -1;
        res = TRUE;
        gst_iterator_resync (it);
        break;
      default:
        res = FALSE;
        done = TRUE;
        break;
    }
  }
  g_value_unset (&item);
  gst_iterator_free (it);

  if (res) {
    /* and store the max */
    GST_DEBUG_OBJECT (aagg, "Total duration in format %s: %"
        GST_TIME_FORMAT, gst_format_get_name (format), GST_TIME_ARGS (max));
    gst_query_set_duration (query, format, max);
  }

  return res;
}


static gboolean
gst_audio_aggregator_src_query (GstAggregator * agg, GstQuery * query)
{
  GstAudioAggregator *aagg = GST_AUDIO_AGGREGATOR (agg);
  GstAudioAggregatorPad *srcpad = GST_AUDIO_AGGREGATOR_PAD (agg->srcpad);
  gboolean res = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_DURATION:
      res = gst_audio_aggregator_query_duration (aagg, query);
      break;
    case GST_QUERY_POSITION:
    {
      GstFormat format;

      gst_query_parse_position (query, &format, NULL);

      GST_OBJECT_LOCK (aagg);

      switch (format) {
        case GST_FORMAT_TIME:
          gst_query_set_position (query, format,
              gst_segment_to_stream_time (&GST_AGGREGATOR_PAD (agg->
                      srcpad)->segment, GST_FORMAT_TIME,
                  GST_AGGREGATOR_PAD (agg->srcpad)->segment.position));
          res = TRUE;
          break;
        case GST_FORMAT_BYTES:
          if (GST_AUDIO_INFO_BPF (&srcpad->info)) {
            gst_query_set_position (query, format, aagg->priv->offset *
                GST_AUDIO_INFO_BPF (&srcpad->info));
            res = TRUE;
          }
          break;
        case GST_FORMAT_DEFAULT:
          gst_query_set_position (query, format, aagg->priv->offset);
          res = TRUE;
          break;
        default:
          break;
      }

      GST_OBJECT_UNLOCK (aagg);

      break;
    }
    default:
      res =
          GST_AGGREGATOR_CLASS (gst_audio_aggregator_parent_class)->src_query
          (agg, query);
      break;
  }

  return res;
}


void
gst_audio_aggregator_set_sink_caps (GstAudioAggregator * aagg,
    GstAudioAggregatorPad * pad, GstCaps * caps)
{
#ifndef G_DISABLE_ASSERT
  gboolean valid;

  GST_OBJECT_LOCK (pad);
  valid = gst_audio_info_from_caps (&pad->info, caps);
  g_assert (valid);
  GST_OBJECT_UNLOCK (pad);
#else
  GST_OBJECT_LOCK (pad);
  (void) gst_audio_info_from_caps (&pad->info, caps);
  GST_OBJECT_UNLOCK (pad);
#endif
}

/* Must hold object lock and aagg lock to call */

static void
gst_audio_aggregator_reset (GstAudioAggregator * aagg)
{
  GstAggregator *agg = GST_AGGREGATOR (aagg);

  GST_AUDIO_AGGREGATOR_LOCK (aagg);
  GST_OBJECT_LOCK (aagg);
  GST_AGGREGATOR_PAD (agg->srcpad)->segment.position = -1;
  aagg->priv->offset = -1;
  gst_audio_info_init (&GST_AUDIO_AGGREGATOR_PAD (agg->srcpad)->info);
  gst_caps_replace (&aagg->current_caps, NULL);
  gst_buffer_replace (&aagg->priv->current_buffer, NULL);
  GST_OBJECT_UNLOCK (aagg);
  GST_AUDIO_AGGREGATOR_UNLOCK (aagg);
}

static gboolean
gst_audio_aggregator_start (GstAggregator * agg)
{
  GstAudioAggregator *aagg = GST_AUDIO_AGGREGATOR (agg);

  gst_audio_aggregator_reset (aagg);

  return TRUE;
}

static gboolean
gst_audio_aggregator_stop (GstAggregator * agg)
{
  GstAudioAggregator *aagg = GST_AUDIO_AGGREGATOR (agg);

  gst_audio_aggregator_reset (aagg);

  return TRUE;
}

static GstFlowReturn
gst_audio_aggregator_flush (GstAggregator * agg)
{
  GstAudioAggregator *aagg = GST_AUDIO_AGGREGATOR (agg);

  GST_AUDIO_AGGREGATOR_LOCK (aagg);
  GST_OBJECT_LOCK (aagg);
  GST_AGGREGATOR_PAD (agg->srcpad)->segment.position = -1;
  aagg->priv->offset = -1;
  gst_buffer_replace (&aagg->priv->current_buffer, NULL);
  GST_OBJECT_UNLOCK (aagg);
  GST_AUDIO_AGGREGATOR_UNLOCK (aagg);

  return GST_FLOW_OK;
}

static GstBuffer *
gst_audio_aggregator_do_clip (GstAggregator * agg,
    GstAggregatorPad * bpad, GstBuffer * buffer)
{
  GstAudioAggregatorPad *pad = GST_AUDIO_AGGREGATOR_PAD (bpad);
  gint rate, bpf;

  rate = GST_AUDIO_INFO_RATE (&pad->info);
  bpf = GST_AUDIO_INFO_BPF (&pad->info);

  GST_OBJECT_LOCK (bpad);
  buffer = gst_audio_buffer_clip (buffer, &bpad->segment, rate, bpf);
  GST_OBJECT_UNLOCK (bpad);

  return buffer;
}

/* Called with the object lock for both the element and pad held,
 * as well as the aagg lock
 *
 * Replace the current buffer with input and update GstAudioAggregatorPadPrivate
 * values.
 */
static gboolean
gst_audio_aggregator_fill_buffer (GstAudioAggregator * aagg,
    GstAudioAggregatorPad * pad)
{
  GstClockTime start_time, end_time;
  gboolean discont = FALSE;
  guint64 start_offset, end_offset;
  gint rate, bpf;

  GstAggregator *agg = GST_AGGREGATOR (aagg);
  GstAggregatorPad *aggpad = GST_AGGREGATOR_PAD (pad);
  GstAudioAggregatorPad *srcpad = GST_AUDIO_AGGREGATOR_PAD (agg->srcpad);

  if (GST_AUDIO_AGGREGATOR_PAD_GET_CLASS (pad)->convert_buffer) {
    rate = GST_AUDIO_INFO_RATE (&srcpad->info);
    bpf = GST_AUDIO_INFO_BPF (&srcpad->info);
  } else {
    rate = GST_AUDIO_INFO_RATE (&pad->info);
    bpf = GST_AUDIO_INFO_BPF (&pad->info);
  }

  pad->priv->position = 0;
  pad->priv->size = gst_buffer_get_size (pad->priv->buffer) / bpf;

  if (pad->priv->size == 0) {
    if (!GST_BUFFER_DURATION_IS_VALID (pad->priv->buffer) ||
        !GST_BUFFER_FLAG_IS_SET (pad->priv->buffer, GST_BUFFER_FLAG_GAP)) {
      GST_WARNING_OBJECT (pad, "Dropping 0-sized buffer missing either a"
          " duration or a GAP flag: %" GST_PTR_FORMAT, pad->priv->buffer);
      return FALSE;
    }

    pad->priv->size =
        gst_util_uint64_scale (GST_BUFFER_DURATION (pad->priv->buffer), rate,
        GST_SECOND);
  }

  if (!GST_BUFFER_PTS_IS_VALID (pad->priv->buffer)) {
    if (pad->priv->output_offset == -1)
      pad->priv->output_offset = aagg->priv->offset;
    if (pad->priv->next_offset == -1)
      pad->priv->next_offset = pad->priv->size;
    else
      pad->priv->next_offset += pad->priv->size;
    goto done;
  }

  start_time = GST_BUFFER_PTS (pad->priv->buffer);
  end_time =
      start_time + gst_util_uint64_scale_ceil (pad->priv->size, GST_SECOND,
      rate);

  /* Clipping should've ensured this */
  g_assert (start_time >= aggpad->segment.start);

  start_offset =
      gst_util_uint64_scale (start_time - aggpad->segment.start, rate,
      GST_SECOND);
  end_offset = start_offset + pad->priv->size;

  if (GST_BUFFER_IS_DISCONT (pad->priv->buffer)
      || GST_BUFFER_FLAG_IS_SET (pad->priv->buffer, GST_BUFFER_FLAG_RESYNC)
      || pad->priv->new_segment || pad->priv->next_offset == -1) {
    discont = TRUE;
    pad->priv->new_segment = FALSE;
  } else {
    guint64 diff, max_sample_diff;

    /* Check discont, based on audiobasesink */
    if (start_offset <= pad->priv->next_offset)
      diff = pad->priv->next_offset - start_offset;
    else
      diff = start_offset - pad->priv->next_offset;

    max_sample_diff =
        gst_util_uint64_scale_int (aagg->priv->alignment_threshold, rate,
        GST_SECOND);

    /* Discont! */
    if (G_UNLIKELY (diff >= max_sample_diff)) {
      if (aagg->priv->discont_wait > 0) {
        if (pad->priv->discont_time == GST_CLOCK_TIME_NONE) {
          pad->priv->discont_time = start_time;
        } else if (start_time - pad->priv->discont_time >=
            aagg->priv->discont_wait) {
          discont = TRUE;
          pad->priv->discont_time = GST_CLOCK_TIME_NONE;
        }
      } else {
        discont = TRUE;
      }
    } else if (G_UNLIKELY (pad->priv->discont_time != GST_CLOCK_TIME_NONE)) {
      /* we have had a discont, but are now back on track! */
      pad->priv->discont_time = GST_CLOCK_TIME_NONE;
    }
  }

  if (discont) {
    /* Have discont, need resync */
    if (pad->priv->next_offset != -1)
      GST_DEBUG_OBJECT (pad, "Have discont. Expected %"
          G_GUINT64_FORMAT ", got %" G_GUINT64_FORMAT,
          pad->priv->next_offset, start_offset);
    pad->priv->output_offset = -1;
    pad->priv->next_offset = end_offset;
  } else {
    pad->priv->next_offset += pad->priv->size;
  }

  if (pad->priv->output_offset == -1) {
    GstClockTime start_running_time;
    GstClockTime end_running_time;
    GstClockTime segment_pos;
    guint64 start_output_offset = -1;
    guint64 end_output_offset = -1;
    GstSegment *agg_segment = &GST_AGGREGATOR_PAD (agg->srcpad)->segment;

    start_running_time =
        gst_segment_to_running_time (&aggpad->segment,
        GST_FORMAT_TIME, start_time);
    end_running_time =
        gst_segment_to_running_time (&aggpad->segment,
        GST_FORMAT_TIME, end_time);

    /* Convert to position in the output segment */
    segment_pos =
        gst_segment_position_from_running_time (agg_segment, GST_FORMAT_TIME,
        start_running_time);
    if (GST_CLOCK_TIME_IS_VALID (segment_pos))
      start_output_offset =
          gst_util_uint64_scale (segment_pos - agg_segment->start, rate,
          GST_SECOND);

    segment_pos =
        gst_segment_position_from_running_time (agg_segment, GST_FORMAT_TIME,
        end_running_time);
    if (GST_CLOCK_TIME_IS_VALID (segment_pos))
      end_output_offset =
          gst_util_uint64_scale (segment_pos - agg_segment->start, rate,
          GST_SECOND);

    if (start_output_offset == -1 && end_output_offset == -1) {
      /* Outside output segment, drop */
      pad->priv->position = 0;
      pad->priv->size = 0;
      pad->priv->output_offset = -1;
      GST_DEBUG_OBJECT (pad, "Buffer outside output segment");
      return FALSE;
    }

    /* Calculate end_output_offset if it was outside the output segment */
    if (end_output_offset == -1)
      end_output_offset = start_output_offset + pad->priv->size;

    if (end_output_offset < aagg->priv->offset) {
      pad->priv->position = 0;
      pad->priv->size = 0;
      pad->priv->output_offset = -1;
      GST_DEBUG_OBJECT (pad,
          "Buffer before segment or current position: %" G_GUINT64_FORMAT " < %"
          G_GINT64_FORMAT, end_output_offset, aagg->priv->offset);
      return FALSE;
    }

    if (start_output_offset == -1 || start_output_offset < aagg->priv->offset) {
      guint diff;

      if (start_output_offset == -1 && end_output_offset < pad->priv->size) {
        diff = pad->priv->size - end_output_offset + aagg->priv->offset;
      } else if (start_output_offset == -1) {
        start_output_offset = end_output_offset - pad->priv->size;

        if (start_output_offset < aagg->priv->offset)
          diff = aagg->priv->offset - start_output_offset;
        else
          diff = 0;
      } else {
        diff = aagg->priv->offset - start_output_offset;
      }

      pad->priv->position += diff;
      if (pad->priv->position >= pad->priv->size) {
        /* Empty buffer, drop */
        pad->priv->position = 0;
        pad->priv->size = 0;
        pad->priv->output_offset = -1;
        GST_DEBUG_OBJECT (pad,
            "Buffer before segment or current position: %" G_GUINT64_FORMAT
            " < %" G_GINT64_FORMAT, end_output_offset, aagg->priv->offset);
        return FALSE;
      }
    }

    if (start_output_offset == -1 || start_output_offset < aagg->priv->offset)
      pad->priv->output_offset = aagg->priv->offset;
    else
      pad->priv->output_offset = start_output_offset;

    GST_DEBUG_OBJECT (pad,
        "Buffer resynced: Pad offset %" G_GUINT64_FORMAT
        ", current audio aggregator offset %" G_GINT64_FORMAT,
        pad->priv->output_offset, aagg->priv->offset);
  }

done:

  GST_LOG_OBJECT (pad,
      "Queued new buffer at offset %" G_GUINT64_FORMAT,
      pad->priv->output_offset);

  return TRUE;
}

/* Called with pad object lock held */

static gboolean
gst_audio_aggregator_mix_buffer (GstAudioAggregator * aagg,
    GstAudioAggregatorPad * pad, GstBuffer * inbuf, GstBuffer * outbuf,
    guint blocksize)
{
  guint overlap;
  guint out_start;
  gboolean filled;
  guint in_offset;
  gboolean pad_changed = FALSE;

  /* Overlap => mix */
  if (aagg->priv->offset < pad->priv->output_offset)
    out_start = pad->priv->output_offset - aagg->priv->offset;
  else
    out_start = 0;

  overlap = pad->priv->size - pad->priv->position;
  if (overlap > blocksize - out_start)
    overlap = blocksize - out_start;

  if (GST_BUFFER_FLAG_IS_SET (inbuf, GST_BUFFER_FLAG_GAP)) {
    /* skip gap buffer */
    GST_LOG_OBJECT (pad, "skipping GAP buffer");
    pad->priv->output_offset += pad->priv->size - pad->priv->position;
    pad->priv->position = pad->priv->size;

    gst_buffer_replace (&pad->priv->buffer, NULL);
    gst_buffer_replace (&pad->priv->input_buffer, NULL);
    return FALSE;
  }

  gst_buffer_ref (inbuf);
  in_offset = pad->priv->position;
  GST_OBJECT_UNLOCK (pad);
  GST_OBJECT_UNLOCK (aagg);

  filled = GST_AUDIO_AGGREGATOR_GET_CLASS (aagg)->aggregate_one_buffer (aagg,
      pad, inbuf, in_offset, outbuf, out_start, overlap);

  GST_OBJECT_LOCK (aagg);
  GST_OBJECT_LOCK (pad);

  pad_changed = (inbuf != pad->priv->buffer);
  gst_buffer_unref (inbuf);

  if (filled)
    GST_BUFFER_FLAG_UNSET (outbuf, GST_BUFFER_FLAG_GAP);

  if (pad_changed)
    return FALSE;

  pad->priv->position += overlap;
  pad->priv->output_offset += overlap;

  if (pad->priv->position == pad->priv->size) {
    /* Buffer done, drop it */
    gst_buffer_replace (&pad->priv->buffer, NULL);
    gst_buffer_replace (&pad->priv->input_buffer, NULL);
    GST_LOG_OBJECT (pad, "Finished mixing buffer, waiting for next");
    return FALSE;
  }

  return TRUE;
}

static GstBuffer *
gst_audio_aggregator_create_output_buffer (GstAudioAggregator * aagg,
    guint num_frames)
{
  GstAllocator *allocator;
  GstAllocationParams params;
  GstBuffer *outbuf;
  GstMapInfo outmap;
  GstAggregator *agg = GST_AGGREGATOR (aagg);
  GstAudioAggregatorPad *srcpad = GST_AUDIO_AGGREGATOR_PAD (agg->srcpad);

  gst_aggregator_get_allocator (GST_AGGREGATOR (aagg), &allocator, &params);

  GST_DEBUG ("Creating output buffer with size %d",
      num_frames * GST_AUDIO_INFO_BPF (&srcpad->info));

  outbuf = gst_buffer_new_allocate (allocator, num_frames *
      GST_AUDIO_INFO_BPF (&srcpad->info), &params);

  if (allocator)
    gst_object_unref (allocator);

  gst_buffer_map (outbuf, &outmap, GST_MAP_WRITE);
  gst_audio_format_fill_silence (srcpad->info.finfo, outmap.data, outmap.size);
  gst_buffer_unmap (outbuf, &outmap);

  return outbuf;
}

static gboolean
sync_pad_values (GstElement * aagg, GstPad * pad, gpointer user_data)
{
  GstAudioAggregatorPad *aapad = GST_AUDIO_AGGREGATOR_PAD (pad);
  GstAggregatorPad *bpad = GST_AGGREGATOR_PAD_CAST (pad);
  GstClockTime timestamp, stream_time;

  if (aapad->priv->buffer == NULL)
    return TRUE;

  timestamp = GST_BUFFER_PTS (aapad->priv->buffer);
  GST_OBJECT_LOCK (bpad);
  stream_time = gst_segment_to_stream_time (&bpad->segment, GST_FORMAT_TIME,
      timestamp);
  GST_OBJECT_UNLOCK (bpad);

  /* sync object properties on stream time */
  /* TODO: Ideally we would want to do that on every sample */
  if (GST_CLOCK_TIME_IS_VALID (stream_time))
    gst_object_sync_values (GST_OBJECT_CAST (pad), stream_time);

  return TRUE;
}

static GstFlowReturn
gst_audio_aggregator_aggregate (GstAggregator * agg, gboolean timeout)
{
  /* Calculate the current output offset/timestamp and offset_end/timestamp_end.
   * Allocate a silence buffer for this and store it.
   *
   * For all pads:
   * 1) Once per input buffer (cached)
   *   1) Check discont (flag and timestamp with tolerance)
   *   2) If discont or new, resync. That means:
   *     1) Drop all start data of the buffer that comes before
   *        the current position/offset.
   *     2) Calculate the offset (output segment!) that the first
   *        frame of the input buffer corresponds to. Base this on
   *        the running time.
   *
   * 2) If the current pad's offset/offset_end overlaps with the output
   *    offset/offset_end, mix it at the appropiate position in the output
   *    buffer and advance the pad's position. Remember if this pad needs
   *    a new buffer to advance behind the output offset_end.
   *
   * If we had no pad with a buffer, go EOS.
   *
   * If we had at least one pad that did not advance behind output
   * offset_end, let aggregate be called again for the current
   * output offset/offset_end.
   */
  GstElement *element;
  GstAudioAggregator *aagg;
  GList *iter;
  GstFlowReturn ret;
  GstBuffer *outbuf = NULL;
  gint64 next_offset;
  gint64 next_timestamp;
  gint rate, bpf;
  gboolean dropped = FALSE;
  gboolean is_eos = TRUE;
  gboolean is_done = TRUE;
  guint blocksize;
  GstAudioAggregatorPad *srcpad = GST_AUDIO_AGGREGATOR_PAD (agg->srcpad);
  GstSegment *agg_segment = &GST_AGGREGATOR_PAD (agg->srcpad)->segment;

  element = GST_ELEMENT (agg);
  aagg = GST_AUDIO_AGGREGATOR (agg);

  /* Sync pad properties to the stream time */
  gst_element_foreach_sink_pad (element, sync_pad_values, NULL);

  GST_AUDIO_AGGREGATOR_LOCK (aagg);
  GST_OBJECT_LOCK (agg);

  /* Update position from the segment start/stop if needed */
  if (agg_segment->position == -1) {
    if (agg_segment->rate > 0.0)
      agg_segment->position = agg_segment->start;
    else
      agg_segment->position = agg_segment->stop;
  }

  if (G_UNLIKELY (srcpad->info.finfo->format == GST_AUDIO_FORMAT_UNKNOWN)) {
    if (timeout) {
      GST_DEBUG_OBJECT (aagg,
          "Got timeout before receiving any caps, don't output anything");

      /* Advance position */
      if (agg_segment->rate > 0.0)
        agg_segment->position += aagg->priv->output_buffer_duration;
      else if (agg_segment->position > aagg->priv->output_buffer_duration)
        agg_segment->position -= aagg->priv->output_buffer_duration;
      else
        agg_segment->position = 0;

      GST_OBJECT_UNLOCK (agg);
      GST_AUDIO_AGGREGATOR_UNLOCK (aagg);
      return GST_AGGREGATOR_FLOW_NEED_DATA;
    } else {
      GST_OBJECT_UNLOCK (agg);
      goto not_negotiated;
    }
  }

  rate = GST_AUDIO_INFO_RATE (&srcpad->info);
  bpf = GST_AUDIO_INFO_BPF (&srcpad->info);

  if (aagg->priv->offset == -1) {
    aagg->priv->offset =
        gst_util_uint64_scale (agg_segment->position - agg_segment->start, rate,
        GST_SECOND);
    GST_DEBUG_OBJECT (aagg, "Starting at offset %" G_GINT64_FORMAT,
        aagg->priv->offset);
  }

  blocksize = gst_util_uint64_scale (aagg->priv->output_buffer_duration,
      rate, GST_SECOND);
  blocksize = MAX (1, blocksize);

  /* FIXME: Reverse mixing does not work at all yet */
  if (agg_segment->rate > 0.0) {
    next_offset = aagg->priv->offset + blocksize;
  } else {
    next_offset = aagg->priv->offset - blocksize;
  }

  /* Use the sample counter, which will never accumulate rounding errors */
  next_timestamp =
      agg_segment->start + gst_util_uint64_scale (next_offset, GST_SECOND,
      rate);

  if (aagg->priv->current_buffer == NULL) {
    GST_OBJECT_UNLOCK (agg);
    aagg->priv->current_buffer =
        GST_AUDIO_AGGREGATOR_GET_CLASS (aagg)->create_output_buffer (aagg,
        blocksize);
    /* Be careful, some things could have changed ? */
    GST_OBJECT_LOCK (agg);
    GST_BUFFER_FLAG_SET (aagg->priv->current_buffer, GST_BUFFER_FLAG_GAP);
  }
  outbuf = aagg->priv->current_buffer;

  GST_LOG_OBJECT (agg,
      "Starting to mix %u samples for offset %" G_GINT64_FORMAT
      " with timestamp %" GST_TIME_FORMAT, blocksize,
      aagg->priv->offset, GST_TIME_ARGS (agg_segment->position));

  for (iter = element->sinkpads; iter; iter = iter->next) {
    GstAudioAggregatorPad *pad = (GstAudioAggregatorPad *) iter->data;
    GstAggregatorPad *aggpad = (GstAggregatorPad *) iter->data;
    gboolean pad_eos = gst_aggregator_pad_is_eos (aggpad);

    if (!pad_eos)
      is_eos = FALSE;

    pad->priv->input_buffer = gst_aggregator_pad_peek_buffer (aggpad);

    GST_OBJECT_LOCK (pad);
    if (!pad->priv->input_buffer) {
      if (timeout) {
        if (pad->priv->output_offset < next_offset) {
          gint64 diff = next_offset - pad->priv->output_offset;
          GST_DEBUG_OBJECT (pad, "Timeout, missing %" G_GINT64_FORMAT
              " frames (%" GST_TIME_FORMAT ")", diff,
              GST_TIME_ARGS (gst_util_uint64_scale (diff, GST_SECOND,
                      GST_AUDIO_INFO_RATE (&srcpad->info))));
        }
      } else if (!pad_eos) {
        is_done = FALSE;
      }
      GST_OBJECT_UNLOCK (pad);
      continue;
    }

    /* New buffer? */
    if (!pad->priv->buffer) {
      if (GST_AUDIO_AGGREGATOR_PAD_GET_CLASS (pad)->convert_buffer)
        pad->priv->buffer =
            gst_audio_aggregator_convert_buffer
            (aagg, GST_PAD (pad), &pad->info, &srcpad->info,
            pad->priv->input_buffer);
      else
        pad->priv->buffer = gst_buffer_ref (pad->priv->input_buffer);

      if (!gst_audio_aggregator_fill_buffer (aagg, pad)) {
        gst_buffer_replace (&pad->priv->buffer, NULL);
        gst_buffer_replace (&pad->priv->input_buffer, NULL);
        pad->priv->buffer = NULL;
        dropped = TRUE;
        GST_OBJECT_UNLOCK (pad);

        gst_aggregator_pad_drop_buffer (aggpad);
        continue;
      }
    } else {
      gst_buffer_unref (pad->priv->input_buffer);
    }

    if (!pad->priv->buffer && !dropped && pad_eos) {
      GST_DEBUG_OBJECT (aggpad, "Pad is in EOS state");
      GST_OBJECT_UNLOCK (pad);
      continue;
    }

    g_assert (pad->priv->buffer);

    /* This pad is lagging behind, we need to update the offset
     * and maybe drop the current buffer */
    if (pad->priv->output_offset < aagg->priv->offset) {
      gint64 diff = aagg->priv->offset - pad->priv->output_offset;
      gint64 odiff = diff;

      if (pad->priv->position + diff > pad->priv->size)
        diff = pad->priv->size - pad->priv->position;
      pad->priv->position += diff;
      pad->priv->output_offset += diff;

      if (pad->priv->position == pad->priv->size) {
        GST_DEBUG_OBJECT (pad, "Buffer was late by %" GST_TIME_FORMAT
            ", dropping %" GST_PTR_FORMAT,
            GST_TIME_ARGS (gst_util_uint64_scale (odiff, GST_SECOND,
                    GST_AUDIO_INFO_RATE (&srcpad->info))), pad->priv->buffer);
        /* Buffer done, drop it */
        gst_buffer_replace (&pad->priv->buffer, NULL);
        gst_buffer_replace (&pad->priv->input_buffer, NULL);
        dropped = TRUE;
        GST_OBJECT_UNLOCK (pad);
        gst_aggregator_pad_drop_buffer (aggpad);
        continue;
      }
    }

    g_assert (pad->priv->buffer);

    if (pad->priv->output_offset >= aagg->priv->offset
        && pad->priv->output_offset < aagg->priv->offset + blocksize) {
      gboolean drop_buf;

      GST_LOG_OBJECT (aggpad, "Mixing buffer for current offset");
      drop_buf = !gst_audio_aggregator_mix_buffer (aagg, pad, pad->priv->buffer,
          outbuf, blocksize);
      if (pad->priv->output_offset >= next_offset) {
        GST_LOG_OBJECT (pad,
            "Pad is at or after current offset: %" G_GUINT64_FORMAT " >= %"
            G_GINT64_FORMAT, pad->priv->output_offset, next_offset);
      } else {
        is_done = FALSE;
      }
      if (drop_buf) {
        GST_OBJECT_UNLOCK (pad);
        gst_aggregator_pad_drop_buffer (aggpad);
        continue;
      }
    }

    GST_OBJECT_UNLOCK (pad);
  }
  GST_OBJECT_UNLOCK (agg);

  if (dropped) {
    /* We dropped a buffer, retry */
    GST_LOG_OBJECT (aagg, "A pad dropped a buffer, wait for the next one");
    GST_AUDIO_AGGREGATOR_UNLOCK (aagg);
    return GST_AGGREGATOR_FLOW_NEED_DATA;
  }

  if (!is_done && !is_eos) {
    /* Get more buffers */
    GST_LOG_OBJECT (aagg,
        "We're not done yet for the current offset, waiting for more data");
    GST_AUDIO_AGGREGATOR_UNLOCK (aagg);
    return GST_AGGREGATOR_FLOW_NEED_DATA;
  }

  if (is_eos) {
    gint64 max_offset = 0;

    GST_DEBUG_OBJECT (aagg, "We're EOS");

    GST_OBJECT_LOCK (agg);
    for (iter = GST_ELEMENT (agg)->sinkpads; iter; iter = iter->next) {
      GstAudioAggregatorPad *pad = GST_AUDIO_AGGREGATOR_PAD (iter->data);

      max_offset = MAX ((gint64) max_offset, (gint64) pad->priv->output_offset);
    }
    GST_OBJECT_UNLOCK (agg);

    /* This means EOS or nothing mixed in at all */
    if (aagg->priv->offset == max_offset) {
      gst_buffer_replace (&aagg->priv->current_buffer, NULL);
      GST_AUDIO_AGGREGATOR_UNLOCK (aagg);
      return GST_FLOW_EOS;
    }

    if (max_offset <= next_offset) {
      GST_DEBUG_OBJECT (aagg,
          "Last buffer is incomplete: %" G_GUINT64_FORMAT " <= %"
          G_GINT64_FORMAT, max_offset, next_offset);
      next_offset = max_offset;
      next_timestamp =
          agg_segment->start + gst_util_uint64_scale (next_offset, GST_SECOND,
          rate);

      if (next_offset > aagg->priv->offset)
        gst_buffer_resize (outbuf, 0, (next_offset - aagg->priv->offset) * bpf);
    }
  }

  /* set timestamps on the output buffer */
  GST_OBJECT_LOCK (agg);
  if (agg_segment->rate > 0.0) {
    GST_BUFFER_PTS (outbuf) = agg_segment->position;
    GST_BUFFER_OFFSET (outbuf) = aagg->priv->offset;
    GST_BUFFER_OFFSET_END (outbuf) = next_offset;
    GST_BUFFER_DURATION (outbuf) = next_timestamp - agg_segment->position;
  } else {
    GST_BUFFER_PTS (outbuf) = next_timestamp;
    GST_BUFFER_OFFSET (outbuf) = next_offset;
    GST_BUFFER_OFFSET_END (outbuf) = aagg->priv->offset;
    GST_BUFFER_DURATION (outbuf) = agg_segment->position - next_timestamp;
  }

  GST_OBJECT_UNLOCK (agg);

  /* send it out */
  GST_LOG_OBJECT (aagg,
      "pushing outbuf %p, timestamp %" GST_TIME_FORMAT " offset %"
      G_GINT64_FORMAT, outbuf, GST_TIME_ARGS (GST_BUFFER_PTS (outbuf)),
      GST_BUFFER_OFFSET (outbuf));

  GST_AUDIO_AGGREGATOR_UNLOCK (aagg);

  ret = gst_aggregator_finish_buffer (agg, outbuf);
  aagg->priv->current_buffer = NULL;

  GST_LOG_OBJECT (aagg, "pushed outbuf, result = %s", gst_flow_get_name (ret));

  GST_AUDIO_AGGREGATOR_LOCK (aagg);
  GST_OBJECT_LOCK (agg);
  aagg->priv->offset = next_offset;
  agg_segment->position = next_timestamp;

  /* If there was a timeout and there was a gap in data in out of the streams,
   * then it's a very good time to for a resync with the timestamps.
   */
  if (timeout) {
    for (iter = element->sinkpads; iter; iter = iter->next) {
      GstAudioAggregatorPad *pad = GST_AUDIO_AGGREGATOR_PAD (iter->data);

      GST_OBJECT_LOCK (pad);
      if (pad->priv->output_offset < aagg->priv->offset)
        pad->priv->output_offset = -1;
      GST_OBJECT_UNLOCK (pad);
    }
  }
  GST_OBJECT_UNLOCK (agg);
  GST_AUDIO_AGGREGATOR_UNLOCK (aagg);

  return ret;
  /* ERRORS */
not_negotiated:
  {
    GST_AUDIO_AGGREGATOR_UNLOCK (aagg);
    GST_ELEMENT_ERROR (aagg, STREAM, FORMAT, (NULL),
        ("Unknown data received, not negotiated"));
    return GST_FLOW_NOT_NEGOTIATED;
  }
}
