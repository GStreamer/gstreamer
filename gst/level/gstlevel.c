/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) 2000,2001,2002,2003,2005
 *           Thomas Vander Stichele <thomas at apestaart dot org>
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
 * SECTION:element-level
 *
 * Level analyses incoming audio buffers and, if the #GstLevel:message property
 * is #TRUE, generates an element message named
 * <classname>&quot;level&quot;</classname>:
 * after each interval of time given by the #GstLevel:interval property.
 * The message's structure contains these fields:
 * <itemizedlist>
 * <listitem>
 *   <para>
 *   #GstClockTime
 *   <classname>&quot;timestamp&quot;</classname>:
 *   the timestamp of the buffer that triggered the message.
 *   </para>
 * </listitem>
 * <listitem>
 *   <para>
 *   #GstClockTime
 *   <classname>&quot;stream-time&quot;</classname>:
 *   the stream time of the buffer.
 *   </para>
 * </listitem>
 * <listitem>
 *   <para>
 *   #GstClockTime
 *   <classname>&quot;running-time&quot;</classname>:
 *   the running_time of the buffer.
 *   </para>
 * </listitem>
 * <listitem>
 *   <para>
 *   #GstClockTime
 *   <classname>&quot;duration&quot;</classname>:
 *   the duration of the buffer.
 *   </para>
 * </listitem>
 * <listitem>
 *   <para>
 *   #GstClockTime
 *   <classname>&quot;endtime&quot;</classname>:
 *   the end time of the buffer that triggered the message as stream time (this
 *   is deprecated, as it can be calculated from stream-time + duration)
 *   </para>
 * </listitem>
 * <listitem>
 *   <para>
 *   #GstValueList of #gdouble
 *   <classname>&quot;peak&quot;</classname>:
 *   the peak power level in dB for each channel
 *   </para>
 * </listitem>
 * <listitem>
 *   <para>
 *   #GstValueList of #gdouble
 *   <classname>&quot;decay&quot;</classname>:
 *   the decaying peak power level in dB for each channel
 *   the decaying peak level follows the peak level, but starts dropping
 *   if no new peak is reached after the time given by
 *   the <link linkend="GstLevel--peak-ttl">the time to live</link>.
 *   When the decaying peak level drops, it does so at the decay rate
 *   as specified by the
 *   <link linkend="GstLevel--peak-falloff">the peak falloff rate</link>.
 *   </para>
 * </listitem>
 * <listitem>
 *   <para>
 *   #GstValueList of #gdouble
 *   <classname>&quot;rms&quot;</classname>:
 *   the Root Mean Square (or average power) level in dB for each channel
 *   </para>
 * </listitem>
 * </itemizedlist>
 *
 * <refsect2>
 * <title>Example application</title>
 * |[
 * <xi:include xmlns:xi="http://www.w3.org/2003/XInclude" parse="text" href="../../../../tests/examples/level/level-example.c" />
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>
#include <math.h>
#include <gst/gst.h>
#include <gst/audio/audio.h>

#include "gstlevel.h"

GST_DEBUG_CATEGORY_STATIC (level_debug);
#define GST_CAT_DEFAULT level_debug

#define EPSILON 1e-35f

static GstStaticPadTemplate sink_template_factory =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) [ 1, MAX ], "
        "endianness = (int) BYTE_ORDER, "
        "width = (int) { 8, 16, 32 }, "
        "depth = (int) { 8, 16, 32 }, "
        "signed = (boolean) true; "
        "audio/x-raw-float, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) [ 1, MAX ], "
        "endianness = (int) BYTE_ORDER, " "width = (int) {32, 64} ")
    );

static GstStaticPadTemplate src_template_factory =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) [ 1, MAX ], "
        "endianness = (int) BYTE_ORDER, "
        "width = (int) { 8, 16, 32 }, "
        "depth = (int) { 8, 16, 32 }, "
        "signed = (boolean) true; "
        "audio/x-raw-float, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) [ 1, MAX ], "
        "endianness = (int) BYTE_ORDER, " "width = (int) {32, 64} ")
    );

enum
{
  PROP_0,
  PROP_SIGNAL_LEVEL,
  PROP_SIGNAL_INTERVAL,
  PROP_PEAK_TTL,
  PROP_PEAK_FALLOFF
};

GST_BOILERPLATE (GstLevel, gst_level, GstBaseTransform,
    GST_TYPE_BASE_TRANSFORM);

static void gst_level_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_level_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_level_finalize (GObject * obj);

static gboolean gst_level_set_caps (GstBaseTransform * trans, GstCaps * in,
    GstCaps * out);
static gboolean gst_level_start (GstBaseTransform * trans);
static GstFlowReturn gst_level_transform_ip (GstBaseTransform * trans,
    GstBuffer * in);


static void
gst_level_base_init (gpointer g_class)
{
  GstElementClass *element_class = g_class;

  gst_element_class_add_static_pad_template (element_class,
      &sink_template_factory);
  gst_element_class_add_static_pad_template (element_class,
      &src_template_factory);
  gst_element_class_set_details_simple (element_class, "Level",
      "Filter/Analyzer/Audio",
      "RMS/Peak/Decaying Peak Level messager for audio/raw",
      "Thomas Vander Stichele <thomas at apestaart dot org>");
}

static void
gst_level_class_init (GstLevelClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseTransformClass *trans_class = GST_BASE_TRANSFORM_CLASS (klass);

  gobject_class->set_property = gst_level_set_property;
  gobject_class->get_property = gst_level_get_property;
  gobject_class->finalize = gst_level_finalize;

  g_object_class_install_property (gobject_class, PROP_SIGNAL_LEVEL,
      g_param_spec_boolean ("message", "message",
          "Post a level message for each passed interval",
          TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_SIGNAL_INTERVAL,
      g_param_spec_uint64 ("interval", "Interval",
          "Interval of time between message posts (in nanoseconds)",
          1, G_MAXUINT64, GST_SECOND / 10,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PEAK_TTL,
      g_param_spec_uint64 ("peak-ttl", "Peak TTL",
          "Time To Live of decay peak before it falls back (in nanoseconds)",
          0, G_MAXUINT64, GST_SECOND / 10 * 3,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PEAK_FALLOFF,
      g_param_spec_double ("peak-falloff", "Peak Falloff",
          "Decay rate of decay peak after TTL (in dB/sec)",
          0.0, G_MAXDOUBLE, 10.0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  GST_DEBUG_CATEGORY_INIT (level_debug, "level", 0, "Level calculation");

  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_level_set_caps);
  trans_class->start = GST_DEBUG_FUNCPTR (gst_level_start);
  trans_class->transform_ip = GST_DEBUG_FUNCPTR (gst_level_transform_ip);
  trans_class->passthrough_on_same_caps = TRUE;
}

static void
gst_level_init (GstLevel * filter, GstLevelClass * g_class)
{
  filter->CS = NULL;
  filter->peak = NULL;

  filter->rate = 0;
  filter->width = 0;
  filter->channels = 0;

  filter->interval = GST_SECOND / 10;
  filter->decay_peak_ttl = GST_SECOND / 10 * 3;
  filter->decay_peak_falloff = 10.0;    /* dB falloff (/sec) */

  filter->message = TRUE;

  filter->process = NULL;

  gst_base_transform_set_gap_aware (GST_BASE_TRANSFORM (filter), TRUE);
}

static void
gst_level_finalize (GObject * obj)
{
  GstLevel *filter = GST_LEVEL (obj);

  g_free (filter->CS);
  g_free (filter->peak);
  g_free (filter->last_peak);
  g_free (filter->decay_peak);
  g_free (filter->decay_peak_base);
  g_free (filter->decay_peak_age);

  filter->CS = NULL;
  filter->peak = NULL;
  filter->last_peak = NULL;
  filter->decay_peak = NULL;
  filter->decay_peak_base = NULL;
  filter->decay_peak_age = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
gst_level_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstLevel *filter = GST_LEVEL (object);

  switch (prop_id) {
    case PROP_SIGNAL_LEVEL:
      filter->message = g_value_get_boolean (value);
      break;
    case PROP_SIGNAL_INTERVAL:
      filter->interval = g_value_get_uint64 (value);
      if (filter->rate) {
        filter->interval_frames =
            GST_CLOCK_TIME_TO_FRAMES (filter->interval, filter->rate);
      }
      break;
    case PROP_PEAK_TTL:
      filter->decay_peak_ttl =
          gst_guint64_to_gdouble (g_value_get_uint64 (value));
      break;
    case PROP_PEAK_FALLOFF:
      filter->decay_peak_falloff = g_value_get_double (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_level_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstLevel *filter = GST_LEVEL (object);

  switch (prop_id) {
    case PROP_SIGNAL_LEVEL:
      g_value_set_boolean (value, filter->message);
      break;
    case PROP_SIGNAL_INTERVAL:
      g_value_set_uint64 (value, filter->interval);
      break;
    case PROP_PEAK_TTL:
      g_value_set_uint64 (value, filter->decay_peak_ttl);
      break;
    case PROP_PEAK_FALLOFF:
      g_value_set_double (value, filter->decay_peak_falloff);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


/* process one (interleaved) channel of incoming samples
 * calculate square sum of samples
 * normalize and average over number of samples
 * returns a normalized cumulative square value, which can be averaged
 * to return the average power as a double between 0 and 1
 * also returns the normalized peak power (square of the highest amplitude)
 *
 * caller must assure num is a multiple of channels
 * samples for multiple channels are interleaved
 * input sample data enters in *in_data as 8 or 16 bit data
 * this filter only accepts signed audio data, so mid level is always 0
 *
 * for 16 bit, this code considers the non-existant 32768 value to be
 * full-scale; so 32767 will not map to 1.0
 */

#define DEFINE_INT_LEVEL_CALCULATOR(TYPE, RESOLUTION)                         \
static void inline                                                            \
gst_level_calculate_##TYPE (gpointer data, guint num, guint channels,         \
                            gdouble *NCS, gdouble *NPS)                       \
{                                                                             \
  TYPE * in = (TYPE *)data;                                                   \
  register guint j;                                                           \
  gdouble squaresum = 0.0;           /* square sum of the integer samples */  \
  register gdouble square = 0.0;     /* Square */                             \
  register gdouble peaksquare = 0.0; /* Peak Square Sample */                 \
  gdouble normalizer;               /* divisor to get a [-1.0, 1.0] range */  \
                                                                              \
  /* *NCS = 0.0; Normalized Cumulative Square */                              \
  /* *NPS = 0.0; Normalized Peask Square */                                   \
                                                                              \
  normalizer = (gdouble) (G_GINT64_CONSTANT(1) << (RESOLUTION * 2));          \
                                                                              \
  /* oil_squaresum_shifted_s16(&squaresum,in,num); */                         \
  for (j = 0; j < num; j += channels)                                         \
  {                                                                           \
    square = ((gdouble) in[j]) * in[j];                                       \
    if (square > peaksquare) peaksquare = square;                             \
    squaresum += square;                                                      \
  }                                                                           \
                                                                              \
  *NCS = squaresum / normalizer;                                              \
  *NPS = peaksquare / normalizer;                                             \
}

DEFINE_INT_LEVEL_CALCULATOR (gint32, 31);
DEFINE_INT_LEVEL_CALCULATOR (gint16, 15);
DEFINE_INT_LEVEL_CALCULATOR (gint8, 7);

#define DEFINE_FLOAT_LEVEL_CALCULATOR(TYPE)                                   \
static void inline                                                            \
gst_level_calculate_##TYPE (gpointer data, guint num, guint channels,         \
                            gdouble *NCS, gdouble *NPS)                       \
{                                                                             \
  TYPE * in = (TYPE *)data;                                                   \
  register guint j;                                                           \
  gdouble squaresum = 0.0;           /* square sum of the integer samples */  \
  register gdouble square = 0.0;     /* Square */                             \
  register gdouble peaksquare = 0.0; /* Peak Square Sample */                 \
                                                                              \
  /* *NCS = 0.0; Normalized Cumulative Square */                              \
  /* *NPS = 0.0; Normalized Peask Square */                                   \
                                                                              \
  /* oil_squaresum_f64(&squaresum,in,num); */                                 \
  for (j = 0; j < num; j += channels)                                         \
  {                                                                           \
    square = ((gdouble) in[j]) * in[j];                                       \
    if (square > peaksquare) peaksquare = square;                             \
    squaresum += square;                                                      \
  }                                                                           \
                                                                              \
  *NCS = squaresum;                                                           \
  *NPS = peaksquare;                                                          \
}

DEFINE_FLOAT_LEVEL_CALCULATOR (gfloat);
DEFINE_FLOAT_LEVEL_CALCULATOR (gdouble);

/* we would need stride to deinterleave also
static void inline
gst_level_calculate_gdouble (gpointer data, guint num, guint channels,
                            gdouble *NCS, gdouble *NPS)
{
  oil_squaresum_f64(NCS,(gdouble *)data,num);
  *NPS = 0.0;
}
*/


static gint
structure_get_int (GstStructure * structure, const gchar * field)
{
  gint ret;

  if (!gst_structure_get_int (structure, field, &ret))
    g_assert_not_reached ();

  return ret;
}

static gboolean
gst_level_set_caps (GstBaseTransform * trans, GstCaps * in, GstCaps * out)
{
  GstLevel *filter = GST_LEVEL (trans);
  const gchar *mimetype;
  GstStructure *structure;
  gint i;

  structure = gst_caps_get_structure (in, 0);
  filter->rate = structure_get_int (structure, "rate");
  filter->width = structure_get_int (structure, "width");
  filter->channels = structure_get_int (structure, "channels");
  mimetype = gst_structure_get_name (structure);

  /* FIXME: set calculator func depending on caps */
  filter->process = NULL;
  if (strcmp (mimetype, "audio/x-raw-int") == 0) {
    GST_DEBUG_OBJECT (filter, "use int: %u", filter->width);
    switch (filter->width) {
      case 8:
        filter->process = gst_level_calculate_gint8;
        break;
      case 16:
        filter->process = gst_level_calculate_gint16;
        break;
      case 32:
        filter->process = gst_level_calculate_gint32;
        break;
    }
  } else if (strcmp (mimetype, "audio/x-raw-float") == 0) {
    GST_DEBUG_OBJECT (filter, "use float, %u", filter->width);
    switch (filter->width) {
      case 32:
        filter->process = gst_level_calculate_gfloat;
        break;
      case 64:
        filter->process = gst_level_calculate_gdouble;
        break;
    }
  }

  /* allocate channel variable arrays */
  g_free (filter->CS);
  g_free (filter->peak);
  g_free (filter->last_peak);
  g_free (filter->decay_peak);
  g_free (filter->decay_peak_base);
  g_free (filter->decay_peak_age);
  filter->CS = g_new (gdouble, filter->channels);
  filter->peak = g_new (gdouble, filter->channels);
  filter->last_peak = g_new (gdouble, filter->channels);
  filter->decay_peak = g_new (gdouble, filter->channels);
  filter->decay_peak_base = g_new (gdouble, filter->channels);

  filter->decay_peak_age = g_new (GstClockTime, filter->channels);

  for (i = 0; i < filter->channels; ++i) {
    filter->CS[i] = filter->peak[i] = filter->last_peak[i] =
        filter->decay_peak[i] = filter->decay_peak_base[i] = 0.0;
    filter->decay_peak_age[i] = G_GUINT64_CONSTANT (0);
  }

  filter->interval_frames =
      GST_CLOCK_TIME_TO_FRAMES (filter->interval, filter->rate);

  return TRUE;
}

static gboolean
gst_level_start (GstBaseTransform * trans)
{
  GstLevel *filter = GST_LEVEL (trans);

  filter->num_frames = 0;

  return TRUE;
}

static GstMessage *
gst_level_message_new (GstLevel * level, GstClockTime timestamp,
    GstClockTime duration)
{
  GstBaseTransform *trans = GST_BASE_TRANSFORM_CAST (level);
  GstStructure *s;
  GValue v = { 0, };
  GstClockTime endtime, running_time, stream_time;

  g_value_init (&v, GST_TYPE_LIST);

  running_time = gst_segment_to_running_time (&trans->segment, GST_FORMAT_TIME,
      timestamp);
  stream_time = gst_segment_to_stream_time (&trans->segment, GST_FORMAT_TIME,
      timestamp);
  /* endtime is for backwards compatibility */
  endtime = stream_time + duration;

  s = gst_structure_new ("level",
      "endtime", GST_TYPE_CLOCK_TIME, endtime,
      "timestamp", G_TYPE_UINT64, timestamp,
      "stream-time", G_TYPE_UINT64, stream_time,
      "running-time", G_TYPE_UINT64, running_time,
      "duration", G_TYPE_UINT64, duration, NULL);
  /* will copy-by-value */
  gst_structure_set_value (s, "rms", &v);
  gst_structure_set_value (s, "peak", &v);
  gst_structure_set_value (s, "decay", &v);

  g_value_unset (&v);

  return gst_message_new_element (GST_OBJECT (level), s);
}

static void
gst_level_message_append_channel (GstMessage * m, gdouble rms, gdouble peak,
    gdouble decay)
{
  GstStructure *s;
  GValue v = { 0, };
  GValue *l;

  g_value_init (&v, G_TYPE_DOUBLE);

  s = (GstStructure *) gst_message_get_structure (m);

  l = (GValue *) gst_structure_get_value (s, "rms");
  g_value_set_double (&v, rms);
  gst_value_list_append_value (l, &v);  /* copies by value */

  l = (GValue *) gst_structure_get_value (s, "peak");
  g_value_set_double (&v, peak);
  gst_value_list_append_value (l, &v);  /* copies by value */

  l = (GValue *) gst_structure_get_value (s, "decay");
  g_value_set_double (&v, decay);
  gst_value_list_append_value (l, &v);  /* copies by value */

  g_value_unset (&v);
}

static GstFlowReturn
gst_level_transform_ip (GstBaseTransform * trans, GstBuffer * in)
{
  GstLevel *filter;
  guint8 *in_data;
  gdouble CS;
  guint i;
  guint num_frames = 0;
  guint num_int_samples = 0;    /* number of interleaved samples
                                 * ie. total count for all channels combined */
  GstClockTimeDiff falloff_time;

  filter = GST_LEVEL (trans);

  in_data = GST_BUFFER_DATA (in);
  num_int_samples = GST_BUFFER_SIZE (in) / (filter->width / 8);

  GST_LOG_OBJECT (filter, "analyzing %u sample frames at ts %" GST_TIME_FORMAT,
      num_int_samples, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (in)));

  g_return_val_if_fail (num_int_samples % filter->channels == 0,
      GST_FLOW_ERROR);

  num_frames = num_int_samples / filter->channels;

  for (i = 0; i < filter->channels; ++i) {
    if (!GST_BUFFER_FLAG_IS_SET (in, GST_BUFFER_FLAG_GAP)) {
      filter->process (in_data, num_int_samples, filter->channels, &CS,
          &filter->peak[i]);
      GST_LOG_OBJECT (filter,
          "channel %d, cumulative sum %f, peak %f, over %d samples/%d channels",
          i, CS, filter->peak[i], num_int_samples, filter->channels);
      filter->CS[i] += CS;
    } else {
      filter->peak[i] = 0.0;
    }
    in_data += (filter->width / 8);

    filter->decay_peak_age[i] +=
        GST_FRAMES_TO_CLOCK_TIME (num_frames, filter->rate);
    GST_LOG_OBJECT (filter, "filter peak info [%d]: decay peak %f, age %"
        GST_TIME_FORMAT, i,
        filter->decay_peak[i], GST_TIME_ARGS (filter->decay_peak_age[i]));

    /* update running peak */
    if (filter->peak[i] > filter->last_peak[i])
      filter->last_peak[i] = filter->peak[i];

    /* make decay peak fall off if too old */
    falloff_time =
        GST_CLOCK_DIFF (gst_gdouble_to_guint64 (filter->decay_peak_ttl),
        filter->decay_peak_age[i]);
    if (falloff_time > 0) {
      gdouble falloff_dB;
      gdouble falloff;
      gdouble length;           /* length of falloff time in seconds */

      length = (gdouble) falloff_time / (gdouble) GST_SECOND;
      falloff_dB = filter->decay_peak_falloff * length;
      falloff = pow (10, falloff_dB / -20.0);

      GST_LOG_OBJECT (filter,
          "falloff: current %f, base %f, interval %" GST_TIME_FORMAT
          ", dB falloff %f, factor %e",
          filter->decay_peak[i], filter->decay_peak_base[i],
          GST_TIME_ARGS (falloff_time), falloff_dB, falloff);
      filter->decay_peak[i] = filter->decay_peak_base[i] * falloff;
      GST_LOG_OBJECT (filter,
          "peak is %" GST_TIME_FORMAT " old, decayed with factor %e to %f",
          GST_TIME_ARGS (filter->decay_peak_age[i]), falloff,
          filter->decay_peak[i]);
    } else {
      GST_LOG_OBJECT (filter, "peak not old enough, not decaying");
    }

    /* if the peak of this run is higher, the decay peak gets reset */
    if (filter->peak[i] >= filter->decay_peak[i]) {
      GST_LOG_OBJECT (filter, "new peak, %f", filter->peak[i]);
      filter->decay_peak[i] = filter->peak[i];
      filter->decay_peak_base[i] = filter->peak[i];
      filter->decay_peak_age[i] = G_GINT64_CONSTANT (0);
    }
  }

  if (G_UNLIKELY (!filter->num_frames)) {
    /* remember start timestamp for message */
    filter->message_ts = GST_BUFFER_TIMESTAMP (in);
  }
  filter->num_frames += num_frames;

  /* do we need to message ? */
  if (filter->num_frames >= filter->interval_frames) {
    if (filter->message) {
      GstMessage *m;
      GstClockTime duration =
          GST_FRAMES_TO_CLOCK_TIME (filter->num_frames, filter->rate);

      m = gst_level_message_new (filter, filter->message_ts, duration);

      GST_LOG_OBJECT (filter,
          "message: ts %" GST_TIME_FORMAT ", num_frames %d",
          GST_TIME_ARGS (filter->message_ts), filter->num_frames);

      for (i = 0; i < filter->channels; ++i) {
        gdouble RMS;
        gdouble RMSdB, lastdB, decaydB;

        RMS = sqrt (filter->CS[i] / filter->num_frames);
        GST_LOG_OBJECT (filter,
            "message: channel %d, CS %f, num_frames %d, RMS %f",
            i, filter->CS[i], filter->num_frames, RMS);
        GST_LOG_OBJECT (filter,
            "message: last_peak: %f, decay_peak: %f",
            filter->last_peak[i], filter->decay_peak[i]);
        /* RMS values are calculated in amplitude, so 20 * log 10 */
        RMSdB = 20 * log10 (RMS + EPSILON);
        /* peak values are square sums, ie. power, so 10 * log 10 */
        lastdB = 10 * log10 (filter->last_peak[i] + EPSILON);
        decaydB = 10 * log10 (filter->decay_peak[i] + EPSILON);

        if (filter->decay_peak[i] < filter->last_peak[i]) {
          /* this can happen in certain cases, for example when
           * the last peak is between decay_peak and decay_peak_base */
          GST_DEBUG_OBJECT (filter,
              "message: decay peak dB %f smaller than last peak dB %f, copying",
              decaydB, lastdB);
          filter->decay_peak[i] = filter->last_peak[i];
        }
        GST_LOG_OBJECT (filter,
            "message: RMS %f dB, peak %f dB, decay %f dB",
            RMSdB, lastdB, decaydB);

        gst_level_message_append_channel (m, RMSdB, lastdB, decaydB);

        /* reset cumulative and normal peak */
        filter->CS[i] = 0.0;
        filter->last_peak[i] = 0.0;
      }

      gst_element_post_message (GST_ELEMENT (filter), m);
    }
    filter->num_frames = 0;
  }

  return GST_FLOW_OK;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  /*oil_init (); */

  return gst_element_register (plugin, "level", GST_RANK_NONE, GST_TYPE_LEVEL);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "level",
    "Audio level plugin",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
