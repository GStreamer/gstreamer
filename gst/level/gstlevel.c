/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *
 * gstlevel.c: signals RMS, peak and decaying peak levels
 * Copyright (C) 2000,2001,2002,2003
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <gst/gst.h>
#include "gstlevel.h"
#include "math.h"

GST_DEBUG_CATEGORY (level_debug);
#define GST_CAT_DEFAULT level_debug

static GstElementDetails level_details = {
  "Level",
  "Filter/Analyzer/Audio",
  "RMS/Peak/Decaying Peak Level signaller for audio/raw",
  "Thomas <thomas@apestaart.org>"
};

static GstStaticPadTemplate sink_template_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) [ 1, 2 ], "
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
        "channels = (int) [ 1, 2 ], "
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
static GstFlowReturn gst_level_transform (GstBaseTransform * trans,
    GstBuffer * in, GstBuffer * out);


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
      g_param_spec_boolean ("signal", "Signal",
          "Emit level signals for each interval", TRUE, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SIGNAL_INTERVAL,
      g_param_spec_double ("interval", "Interval",
          "Interval between emissions (in seconds)",
          0.01, 100.0, 0.1, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_PEAK_TTL,
      g_param_spec_double ("peak_ttl", "Peak TTL",
          "Time To Live of decay peak before it falls back",
          0, 100.0, 0.3, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_PEAK_FALLOFF,
      g_param_spec_double ("peak_falloff", "Peak Falloff",
          "Decay rate of decay peak after TTL (in dB/sec)",
          0.0, G_MAXDOUBLE, 10.0, G_PARAM_READWRITE));

  GST_DEBUG_CATEGORY_INIT (level_debug, "level", 0, "Level calculation");

  trans_class->set_caps = gst_level_set_caps;
  trans_class->transform = gst_level_transform;
}

static void
gst_level_init (GstLevel * filter)
{
  filter->CS = NULL;
  filter->peak = NULL;
  filter->MS = NULL;
  filter->RMS_dB = NULL;

  filter->rate = 0;
  filter->width = 0;
  filter->channels = 0;

  filter->interval = 0.1;
  filter->decay_peak_ttl = 0.4;
  filter->decay_peak_falloff = 10.0;    /* dB falloff (/sec) */
}

static void
gst_level_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstLevel *filter = GST_LEVEL (object);

  switch (prop_id) {
    case PROP_SIGNAL_LEVEL:
      filter->signal = g_value_get_boolean (value);
      break;
    case PROP_SIGNAL_INTERVAL:
      filter->interval = g_value_get_double (value);
      break;
    case PROP_PEAK_TTL:
      filter->decay_peak_ttl = g_value_get_double (value);
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
      g_value_set_boolean (value, filter->signal);
      break;
    case PROP_SIGNAL_INTERVAL:
      g_value_set_double (value, filter->interval);
      break;
    case PROP_PEAK_TTL:
      g_value_set_double (value, filter->decay_peak_ttl);
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

  filter->num_samples = 0;

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
  g_free (filter->MS);
  g_free (filter->RMS_dB);
  filter->CS = g_new (double, filter->channels);
  filter->peak = g_new (double, filter->channels);
  filter->last_peak = g_new (double, filter->channels);
  filter->decay_peak = g_new (double, filter->channels);
  filter->decay_peak_age = g_new (double, filter->channels);
  filter->MS = g_new (double, filter->channels);
  filter->RMS_dB = g_new (double, filter->channels);

  for (i = 0; i < filter->channels; ++i) {
    filter->CS[i] = filter->peak[i] = filter->last_peak[i] =
        filter->decay_peak[i] = filter->decay_peak_age[i] =
        filter->MS[i] = filter->RMS_dB[i] = 0.0;
  }

  return TRUE;
}

#if 0
#define DEBUG(str,...) g_print (str, ...)
#else
#define DEBUG(str,...)          /*nop */
#endif

/* process one (interleaved) channel of incoming samples
 * calculate square sum of samples
 * normalize and return normalized Cumulative Square
 * caller must assure num is a multiple of channels
 * this filter only accepts signed audio data, so mid level is always 0
 */
#define DEFINE_LEVEL_CALCULATOR(TYPE)                                           \
static void inline                                                              \
gst_level_calculate_##TYPE (TYPE * in, guint num, gint channels,                \
                            gint resolution, double *CS, double *peak)          \
{                                                                               \
  register int j;                                                               \
  double squaresum = 0.0;	/* square sum of the integer samples */         \
  register double square = 0.0;		/* Square */                            \
  register double PSS = 0.0;		/* Peak Square Sample */                \
  gdouble normalizer;                                                           \
                                                                                \
  *CS = 0.0;      /* Cumulative Square for this block */                        \
                                                                                \
  normalizer = (double) (1 << resolution);                                      \
                                                                                \
  /*                                                                            \
   * process data here                                                          \
   * input sample data enters in *in_data as 8 or 16 bit data                   \
   * samples for left and right channel are interleaved                         \
   * returns the Mean Square of the samples as a double between 0 and 1         \
   */                                                                           \
                                                                                \
  for (j = 0; j < num; j += channels)                                           \
  {                                                                             \
    DEBUG ("ch %d -> smp %d\n", j, in[j]);                                      \
    square = (double) (in[j] * in[j]);                                          \
    if (square > PSS) PSS = square;                                             \
    squaresum += square;                                                        \
  }                                                                             \
  *peak = PSS / ((double) normalizer * (double) normalizer);                    \
                                                                                \
  /* return normalized cumulative square */                                     \
  *CS = squaresum / ((double) normalizer * (double) normalizer);                \
}

DEFINE_LEVEL_CALCULATOR (gint16);
DEFINE_LEVEL_CALCULATOR (gint8);

static GstMessage *
gst_level_message_new (GstLevel * l, gdouble endtime)
{
  GstStructure *s;
  GValue v = { 0, };

  g_value_init (&v, GST_TYPE_LIST);

  s = gst_structure_new ("level", "endtime", G_TYPE_DOUBLE, endtime, NULL);
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
gst_level_transform (GstBaseTransform * trans, GstBuffer * in, GstBuffer * out)
{
  GstLevel *filter;
  gpointer in_data;
  double CS = 0.0;
  gint num_samples = 0;
  gint i;

  filter = GST_LEVEL (trans);

  for (i = 0; i < filter->channels; ++i)
    filter->CS[i] = filter->peak[i] = filter->MS[i] = filter->RMS_dB[i] = 0.0;

  in_data = GST_BUFFER_DATA (in);
  num_samples = GST_BUFFER_SIZE (in) / (filter->width / 8);

  g_return_val_if_fail (num_samples % filter->channels == 0, GST_FLOW_ERROR);

  for (i = 0; i < filter->channels; ++i) {
    switch (filter->width) {
      case 16:
        gst_level_calculate_gint16 (in_data + i, num_samples,
            filter->channels, filter->width - 1, &CS, &filter->peak[i]);
        break;
      case 8:
        gst_level_calculate_gint8 (((gint8 *) in_data) + i, num_samples,
            filter->channels, filter->width - 1, &CS, &filter->peak[i]);
        break;
    }
    GST_LOG_OBJECT (filter, "channel %d, cumulative sum %f, peak %f", i, CS,
        filter->peak[i]);
    filter->CS[i] += CS;

  }

  filter->num_samples += num_samples;

  for (i = 0; i < filter->channels; ++i) {
    filter->decay_peak_age[i] += num_samples;
    DEBUG ("filter peak info [%d]: peak %f, age %f\n", i,
        filter->last_peak[i], filter->decay_peak_age[i]);
    /* update running peak */
    if (filter->peak[i] > filter->last_peak[i])
      filter->last_peak[i] = filter->peak[i];

    /* update decay peak */
    if (filter->peak[i] >= filter->decay_peak[i]) {
      DEBUG ("new peak, %f\n", filter->peak[i]);
      filter->decay_peak[i] = filter->peak[i];
      filter->decay_peak_age[i] = 0;
    } else {
      /* make decay peak fall off if too old */
      if (filter->decay_peak_age[i] > filter->rate * filter->decay_peak_ttl) {
        double falloff_dB;
        double falloff;
        double length;          /* length of buffer in seconds */


        length = (double) num_samples / (filter->channels * filter->rate);
        falloff_dB = filter->decay_peak_falloff * length;
        falloff = pow (10, falloff_dB / -20.0);

        DEBUG ("falloff: length %f, dB falloff %f, falloff factor %e\n",
            length, falloff_dB, falloff);
        filter->decay_peak[i] *= falloff;
        DEBUG ("peak is %f samples old, decayed with factor %e to %f\n",
            filter->decay_peak_age[i], falloff, filter->decay_peak[i]);
      }
    }
  }

  /* do we need to emit ? */

  if (filter->num_samples >= filter->interval * (gdouble) filter->rate) {
    if (filter->signal) {
      GstMessage *m;
      double endtime, RMS;

      endtime = (double) GST_BUFFER_TIMESTAMP (in) / GST_SECOND
          + (double) num_samples / (double) filter->rate;

      m = gst_level_message_new (filter, endtime);

      for (i = 0; i < filter->channels; ++i) {
        RMS = sqrt (filter->CS[i] / (filter->num_samples / filter->channels));

        gst_level_message_append_channel (m, 20 * log10 (RMS),
            20 * log10 (filter->last_peak[i]),
            20 * log10 (filter->decay_peak[i]));

        /* reset cumulative and normal peak */
        filter->CS[i] = 0.0;
        filter->last_peak[i] = 0.0;
      }

      gst_element_post_message (GST_ELEMENT (filter), m);
    }
    filter->num_samples = 0;
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
