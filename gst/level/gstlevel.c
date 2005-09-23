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
 * <refsect2>
 * <para>
 * Level analyses incoming audio buffers and, if the
 * <link linkend="GstLevel--message">message property</link> is #TRUE,
 * generates an application message named
 * <classname>&quot;level&quot;</classname>:
 * after each interval of time given by the
 * <link linkend="GstLevel--interval">interval property</link>.
 * The message's structure contains four fields:
 * <itemizedlist>
 * <listitem>
 *   <para>
 *   #GstClockTime
 *   <classname>&quot;endtime&quot;</classname>:
 *   the end time of the buffer that triggered the message
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
 *   <link linkend="GstLevel--peak-falloff">the peak fallof rate</link>.
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
 * </para>
 * <title>Example application</title>
 * <para>
 * <include xmlns="http://www.w3.org/2003/XInclude" href="element-level-example.xml" />
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <gst/gst.h>
#include <gst/audio/audio.h>
#include "gstlevel.h"
#include "math.h"

GST_DEBUG_CATEGORY (level_debug);
#define GST_CAT_DEFAULT level_debug

static GstElementDetails level_details = {
  "Level",
  "Filter/Analyzer/Audio",
  "RMS/Peak/Decaying Peak Level messager for audio/raw",
  "Thomas <thomas@apestaart.org>"
};

static GstStaticPadTemplate sink_template_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) [ 1, 8 ], "
        "endianness = (int) BYTE_ORDER, "
        "width = (int) { 8, 16 }, "
        "depth = (int) { 8, 16 }, " "signed = (boolean) true")
    );

static GstStaticPadTemplate src_template_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) [ 1, 8 ], "
        "endianness = (int) BYTE_ORDER, "
        "width = (int) { 8, 16 }, "
        "depth = (int) { 8, 16 }, " "signed = (boolean) true")
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

static gboolean gst_level_set_caps (GstBaseTransform * trans, GstCaps * in,
    GstCaps * out);
static GstFlowReturn gst_level_transform_ip (GstBaseTransform * trans,
    GstBuffer * in);


static void
gst_level_base_init (gpointer g_class)
{
  GstElementClass *element_class = g_class;

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template_factory));
  gst_element_class_set_details (element_class, &level_details);
}

static void
gst_level_class_init (GstLevelClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseTransformClass *trans_class;

  gobject_class = (GObjectClass *) klass;
  trans_class = (GstBaseTransformClass *) klass;

  gobject_class->set_property = gst_level_set_property;
  gobject_class->get_property = gst_level_get_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SIGNAL_LEVEL,
      g_param_spec_boolean ("message", "mesage",
          "Post a level message for each passed interval",
          TRUE, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SIGNAL_INTERVAL,
      g_param_spec_uint64 ("interval", "Interval",
          "Interval of time between message posts (in nanoseconds)",
          1, G_MAXUINT64, GST_SECOND / 10, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_PEAK_TTL,
      g_param_spec_uint64 ("peak_ttl", "Peak TTL",
          "Time To Live of decay peak before it falls back (in nanoseconds)",
          0, G_MAXUINT64, GST_SECOND / 10 * 3, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_PEAK_FALLOFF,
      g_param_spec_double ("peak_falloff", "Peak Falloff",
          "Decay rate of decay peak after TTL (in dB/sec)",
          0.0, G_MAXDOUBLE, 10.0, G_PARAM_READWRITE));

  GST_DEBUG_CATEGORY_INIT (level_debug, "level", 0, "Level calculation");

  trans_class->set_caps = gst_level_set_caps;
  trans_class->transform_ip = gst_level_transform_ip;
  trans_class->passthrough_on_same_caps = TRUE;
}

static void
gst_level_init (GstLevel * filter, GstLevelClass * g_class)
{
  filter->CS = NULL;
  filter->peak = NULL;
  filter->RMS_dB = NULL;

  filter->rate = 0;
  filter->width = 0;
  filter->channels = 0;

  filter->interval = GST_SECOND / 10;
  filter->decay_peak_ttl = GST_SECOND / 10 * 3;
  filter->decay_peak_falloff = 10.0;    /* dB falloff (/sec) */

  filter->message = TRUE;
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
      break;
    case PROP_PEAK_TTL:
      filter->decay_peak_ttl = g_value_get_uint64 (value);
      break;
    case PROP_PEAK_FALLOFF:
      filter->decay_peak_falloff = g_value_get_double (value);
      break;
    default:
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
  GstLevel *filter;
  GstStructure *structure;
  int i;

  filter = GST_LEVEL (trans);

  filter->num_frames = 0;

  structure = gst_caps_get_structure (in, 0);
  filter->rate = structure_get_int (structure, "rate");
  filter->width = structure_get_int (structure, "width");
  filter->channels = structure_get_int (structure, "channels");

  /* allocate channel variable arrays */
  g_free (filter->CS);
  g_free (filter->peak);
  g_free (filter->last_peak);
  g_free (filter->decay_peak);
  g_free (filter->decay_peak_age);
  g_free (filter->RMS_dB);
  filter->CS = g_new (double, filter->channels);
  filter->peak = g_new (double, filter->channels);
  filter->last_peak = g_new (double, filter->channels);
  filter->decay_peak = g_new (double, filter->channels);

  filter->decay_peak_age = g_new (GstClockTime, filter->channels);
  filter->RMS_dB = g_new (double, filter->channels);

  for (i = 0; i < filter->channels; ++i) {
    filter->CS[i] = filter->peak[i] = filter->last_peak[i] =
        filter->decay_peak[i] = filter->RMS_dB[i] = 0.0;
    filter->decay_peak_age[i] = 0LL;
  }

  return TRUE;
}

/* process one (interleaved) channel of incoming samples
 * calculate square sum of samples
 * normalize and average over number of samples
 * returns a normalized average power value as CS, as a double between 0 and 1
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

#define DEFINE_LEVEL_CALCULATOR(TYPE)                                         \
static void inline                                                            \
gst_level_calculate_##TYPE (TYPE * in, guint num, gint channels,              \
                            gint resolution, double *CS, double *peak)        \
{                                                                             \
  register int j;                                                             \
  double squaresum = 0.0;        /* square sum of the integer samples */      \
  register double square = 0.0;	 /* Square */                                 \
  register double PSS = 0.0;     /* Peak Square Sample */                     \
  gdouble normalizer;            /* divisor to get a [-1.0, 1.0] range */     \
                                                                              \
  *CS = 0.0;                     /* Cumulative Square for this block */       \
                                                                              \
  normalizer = (double) (1 << resolution);                                    \
                                                                              \
  for (j = 0; j < num; j += channels)                                         \
  {                                                                           \
    square = ((double) in[j]) * in[j];                                        \
    if (square > PSS) PSS = square;                                           \
    squaresum += square;                                                      \
  }                                                                           \
                                                                              \
  *CS = squaresum / (normalizer * normalizer);                                \
  *peak = PSS / (normalizer * normalizer);                                    \
}

DEFINE_LEVEL_CALCULATOR (gint16);
DEFINE_LEVEL_CALCULATOR (gint8);

static GstMessage *
gst_level_message_new (GstLevel * l, GstClockTime endtime)
{
  GstStructure *s;
  GValue v = { 0, };

  g_value_init (&v, GST_TYPE_LIST);

  s = gst_structure_new ("level", "endtime", GST_TYPE_CLOCK_TIME,
      endtime, NULL);
  /* will copy-by-value */
  gst_structure_set_value (s, "rms", &v);
  gst_structure_set_value (s, "peak", &v);
  gst_structure_set_value (s, "decay", &v);

  return gst_message_new_application (GST_OBJECT (l), s);
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
}

static GstFlowReturn
gst_level_transform_ip (GstBaseTransform * trans, GstBuffer * in)
{
  GstLevel *filter;
  gpointer in_data;
  double CS = 0.0;
  gint num_frames = 0;
  gint num_int_samples = 0;     /* number of interleaved samples
                                 * ie. total count for all channels combined */
  gint i;

  filter = GST_LEVEL (trans);

  for (i = 0; i < filter->channels; ++i)
    filter->peak[i] = filter->RMS_dB[i] = 0.0;

  in_data = GST_BUFFER_DATA (in);
  num_int_samples = GST_BUFFER_SIZE (in) / (filter->width / 8);

  g_return_val_if_fail (num_int_samples % filter->channels == 0,
      GST_FLOW_ERROR);

  num_frames = num_int_samples / filter->channels;

  for (i = 0; i < filter->channels; ++i) {
    CS = 0.0;
    switch (filter->width) {
      case 16:
        gst_level_calculate_gint16 (((gint16 *) in_data) + i, num_int_samples,
            filter->channels, filter->width - 1, &CS, &filter->peak[i]);
        break;
      case 8:
        gst_level_calculate_gint8 (((gint8 *) in_data) + i, num_int_samples,
            filter->channels, filter->width - 1, &CS, &filter->peak[i]);
        break;
    }
    GST_LOG_OBJECT (filter,
        "channel %d, cumulative sum %f, peak %f, over %d samples/%d channels",
        i, CS, filter->peak[i], num_int_samples, filter->channels);
    filter->CS[i] += CS;
  }

  filter->num_frames += num_frames;

  for (i = 0; i < filter->channels; ++i) {
    filter->decay_peak_age[i] +=
        GST_FRAMES_TO_CLOCK_TIME (num_frames, filter->rate);
    GST_LOG_OBJECT (filter, "filter peak info [%d]: peak %f, age %"
        GST_TIME_FORMAT, i,
        filter->last_peak[i], GST_TIME_ARGS (filter->decay_peak_age[i]));

    /* update running peak */
    if (filter->peak[i] > filter->last_peak[i])
      filter->last_peak[i] = filter->peak[i];

    /* update decay peak */
    if (filter->peak[i] >= filter->decay_peak[i]) {
      GST_LOG_OBJECT (filter, "new peak, %f", filter->peak[i]);
      filter->decay_peak[i] = filter->peak[i];
      filter->decay_peak_age[i] = 0LL;
    } else {
      /* make decay peak fall off if too old */
      if (filter->decay_peak_age[i] > filter->decay_peak_ttl) {
        double falloff_dB;
        double falloff;
        double length;          /* length of buffer in seconds */


        length = (double) num_frames / filter->rate;
        falloff_dB = filter->decay_peak_falloff * length;
        falloff = pow (10, falloff_dB / -20.0);

        GST_LOG_OBJECT (filter,
            "falloff: length %f, dB falloff %f, falloff factor %e",
            length, falloff_dB, falloff);
        filter->decay_peak[i] *= falloff;
        GST_LOG_OBJECT (filter,
            "peak is %" GST_TIME_FORMAT " old, decayed with factor %e to %f",
            GST_TIME_ARGS (filter->decay_peak_age[i]), falloff,
            filter->decay_peak[i]);
      } else {
        GST_LOG_OBJECT (filter, "peak not old enough, not decaying");
      }
    }
  }

  /* do we need to emit ? */

  if (filter->num_frames >=
      (gint) ((gdouble) filter->interval / GST_SECOND * filter->rate)) {
    if (filter->message) {
      GstMessage *m;
      GstClockTime endtime;
      double RMS;
      double RMSdB, lastdB, decaydB;

      /* FIXME: convert to a GstClockTime instead */
      endtime = GST_BUFFER_TIMESTAMP (in)
          + GST_FRAMES_TO_CLOCK_TIME (num_frames, filter->rate);

      m = gst_level_message_new (filter, endtime);

      for (i = 0; i < filter->channels; ++i) {
        RMS = sqrt (filter->CS[i] / filter->num_frames);
        GST_LOG_OBJECT (filter,
            "CS: %f, num_frames %d, channel %d, RMS %f",
            filter->CS[i], filter->num_frames, i, RMS);
        /* RMS values are calculated in amplitude, so 20 * log 10 */
        RMSdB = 20 * log10 (RMS);
        /* peak values are square sums, ie. power, so 10 * log 10 */
        lastdB = 10 * log10 (filter->last_peak[i]);
        decaydB = 10 * log10 (filter->decay_peak[i]);

        GST_LOG_OBJECT (filter,
            "time %" GST_TIME_FORMAT
            ", channel %d, RMS %f dB, peak %f dB, decay %f dB",
            GST_TIME_ARGS (endtime), i, RMSdB, lastdB, decaydB);

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
  return gst_element_register (plugin, "level", GST_RANK_NONE, GST_TYPE_LEVEL);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "level",
    "Audio level plugin",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE, GST_ORIGIN)
