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

/* elementfactory information */
static GstElementDetails level_details = {
  "Level",
  "Filter/Analyzer/Audio",
  "RMS/Peak/Decaying Peak Level signaller for audio/raw",
  "Thomas <thomas@apestaart.org>"
};

/* pad templates */

GST_PAD_TEMPLATE_FACTORY (sink_template_factory,
  "level_sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "level_sink",
    "audio/x-raw-int",
      "signed", GST_PROPS_BOOLEAN (TRUE),
      "width", GST_PROPS_LIST (
                 GST_PROPS_INT (8),
                 GST_PROPS_INT (16)
               ),
      "depth", GST_PROPS_LIST (
                 GST_PROPS_INT (8),
                 GST_PROPS_INT (16)
               ),
      "rate", GST_PROPS_INT_RANGE (1, G_MAXINT),
      "channels", GST_PROPS_INT_RANGE (1, 2)
  )
)

GST_PAD_TEMPLATE_FACTORY (src_template_factory,
  "level_src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "level_src",
    "audio/x-raw-int",
    "signed", GST_PROPS_BOOLEAN (TRUE),
    "width", GST_PROPS_LIST (
               GST_PROPS_INT (8),
               GST_PROPS_INT (16)
             ),
    "depth", GST_PROPS_LIST (
               GST_PROPS_INT (8),
               GST_PROPS_INT (16)
             ),
    "rate", GST_PROPS_INT_RANGE (1, G_MAXINT),
    "channels", GST_PROPS_INT_RANGE (1, 2)
  )
)

/* Filter signals and args */
enum {
  /* FILL ME */
  SIGNAL_LEVEL,
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_SIGNAL_LEVEL,
  ARG_SIGNAL_INTERVAL,
  ARG_PEAK_TTL,
  ARG_PEAK_FALLOFF
};

static void		gst_level_class_init		(GstLevelClass *klass);
static void		gst_level_base_init		(GstLevelClass *klass);
static void		gst_level_init			(GstLevel *filter);

static GstElementStateReturn gst_level_change_state     (GstElement *element);
static void		gst_level_set_property			(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void		gst_level_get_property			(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static void		gst_level_chain			(GstPad *pad, GstData *_data);

static GstElementClass *parent_class = NULL;
static guint gst_filter_signals[LAST_SIGNAL] = { 0 };

GType
gst_level_get_type (void) 
{
  static GType level_type = 0;

  if (!level_type) 
  {
    static const GTypeInfo level_info = 
    {
      sizeof (GstLevelClass),
      (GBaseInitFunc) gst_level_base_init, NULL,
      (GClassInitFunc) gst_level_class_init, NULL, NULL,
      sizeof (GstLevel), 0,
      (GInstanceInitFunc) gst_level_init
    };
    level_type = g_type_register_static (GST_TYPE_ELEMENT, "GstLevel", 
	                                 &level_info, 0);
  }
  return level_type;
}

static GstPadLinkReturn
gst_level_link (GstPad *pad, GstCaps *caps)
{
  GstLevel *filter;
  GstPad *otherpad;
  GstPadLinkReturn res;
  int i;

  filter = GST_LEVEL (gst_pad_get_parent (pad));
  g_return_val_if_fail (filter != NULL, GST_PAD_LINK_REFUSED);
  g_return_val_if_fail (GST_IS_LEVEL (filter), GST_PAD_LINK_REFUSED);
  otherpad = (pad == filter->srcpad ? filter->sinkpad : filter->srcpad);
	  
  if (!GST_CAPS_IS_FIXED (caps)) {
    return GST_PAD_LINK_DELAYED;
  }

  res = gst_pad_try_set_caps (otherpad, caps);
  /* if ok, set filter */
  if (res != GST_PAD_LINK_OK && res != GST_PAD_LINK_DONE) {
    return res;
  }

  filter->num_samples = 0;
  
  if (!gst_caps_get_int (caps, "rate", &(filter->rate)))
    return GST_PAD_LINK_REFUSED;
  if (!gst_caps_get_int (caps, "width", &(filter->width)))
    return GST_PAD_LINK_REFUSED;
  if (!gst_caps_get_int (caps, "channels", &(filter->channels)))
    return GST_PAD_LINK_REFUSED;

  /* allocate channel variable arrays */
  if (filter->CS) g_free (filter->CS);
  if (filter->peak) g_free (filter->peak);
  if (filter->last_peak) g_free (filter->last_peak);
  if (filter->decay_peak) g_free (filter->decay_peak);
  if (filter->decay_peak_age) g_free (filter->decay_peak_age);
  if (filter->MS) g_free (filter->MS);
  if (filter->RMS_dB) g_free (filter->RMS_dB);
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

  filter->inited = TRUE;

  return GST_PAD_LINK_OK;
}

static void inline
gst_level_fast_16bit_chain (gint16* in, guint num, gint channels, 
                            gint resolution, double *CS, double *peak)
#include "filter.func"

static void inline
gst_level_fast_8bit_chain (gint8* in, guint num, gint channels, 
                           gint resolution, double *CS, double *peak)
#include "filter.func"

static void
gst_level_chain (GstPad *pad, GstData *_data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstLevel *filter;
  gint16 *in_data;

  double CS = 0.0;
  gint num_samples = 0;
  gint i;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  filter = GST_LEVEL (GST_OBJECT_PARENT (pad));
  g_return_if_fail (filter != NULL);
  g_return_if_fail (GST_IS_LEVEL (filter));

  for (i = 0; i < filter->channels; ++i)
    filter->CS[i] = filter->peak[i] = filter->MS[i] = filter->RMS_dB[i] = 0.0;
  
  in_data = (gint16 *) GST_BUFFER_DATA (buf);
  
  num_samples = GST_BUFFER_SIZE (buf) / (filter->width / 8);
  if (num_samples % filter->channels != 0)
    g_warning ("WARNING: level: programming error, data not properly interleaved");

  for (i = 0; i < filter->channels; ++i)
  {
    switch (filter->width)
    {
      case 16:
	  gst_level_fast_16bit_chain (in_data + i, num_samples,
                                      filter->channels, filter->width - 1,
                                      &CS, &filter->peak[i]);
	  break;
      case 8:
	  gst_level_fast_8bit_chain (((gint8 *) in_data) + i, num_samples,
                                     filter->channels, filter->width - 1,
                                     &CS, &filter->peak[i]);
	  break;
    }
    /* g_print ("DEBUG: CS %f, peak %f\n", CS, filter->peak[i]); */
    filter->CS[i] += CS;

  }
  gst_pad_push (filter->srcpad, GST_DATA (buf));

  filter->num_samples += num_samples;

  for (i = 0; i < filter->channels; ++i)
  {
    filter->decay_peak_age[i] += num_samples;
    /* g_print ("filter peak info [%d]: peak %f, age %f\n", i, 
             filter->last_peak[i], filter->decay_peak_age[i]); */
    /* update running peak */
    if (filter->peak[i] > filter->last_peak[i])
        filter->last_peak[i] = filter->peak[i];

    /* update decay peak */
    if (filter->peak[i] >= filter->decay_peak[i])
    {
       /* g_print ("new peak, %f\n", filter->peak[i]); */
       filter->decay_peak[i] = filter->peak[i];
       filter->decay_peak_age[i] = 0;
    }
    else
    {
      /* make decay peak fall off if too old */
      if (filter->decay_peak_age[i] > filter->rate * filter->decay_peak_ttl)
      {
        double falloff_dB;
        double falloff;
        double length;		/* length of buffer in seconds */

 
        length = (double) num_samples / (filter->channels * filter->rate);
        falloff_dB = filter->decay_peak_falloff * length;
        falloff = pow (10, falloff_dB / -20.0);

        /* g_print ("falloff: length %f, dB falloff %f, falloff factor %e\n",
                 length, falloff_dB, falloff); */
        filter->decay_peak[i] *= falloff;
        /* g_print ("peak is %f samples old, decayed with factor %e to %f\n",
                 filter->decay_peak_age[i], falloff, filter->decay_peak[i]); */
      }
    }
  }

  /* do we need to emit ? */
  
  if (filter->num_samples >= filter->interval * (gdouble) filter->rate)
  {
    if (filter->signal)
    {
      gdouble RMS, peak, endtime;
      for (i = 0; i < filter->channels; ++i)
      {
        RMS = sqrt (filter->CS[i] / (filter->num_samples / filter->channels));
        peak = filter->last_peak[i];
        num_samples = GST_BUFFER_SIZE (buf) / (filter->width / 8);
        endtime = (double) GST_BUFFER_TIMESTAMP (buf) / GST_SECOND
                + (double) num_samples / (double) filter->rate;

        g_signal_emit (G_OBJECT (filter), gst_filter_signals[SIGNAL_LEVEL], 0,
                       endtime, i,
                       20 * log10 (RMS), 20 * log10 (filter->last_peak[i]),
                       20 * log10 (filter->decay_peak[i]));
        /* we emitted, so reset cumulative and normal peak */
        filter->CS[i] = 0.0;
        filter->last_peak[i] = 0.0;
      }
    }
    filter->num_samples = 0;
  }
}

static GstElementStateReturn gst_level_change_state (GstElement *element)
{
  GstLevel *filter = GST_LEVEL (element);

  switch(GST_STATE_TRANSITION(element)){
    case GST_STATE_PAUSED_TO_PLAYING:
      if (!filter->inited) return GST_STATE_FAILURE;
      break;
    default:
      break;
  }

  return GST_ELEMENT_CLASS(parent_class)->change_state(element);
}

static void
gst_level_set_property (GObject *object, guint prop_id, 
                        const GValue *value, GParamSpec *pspec)
{
  GstLevel *filter;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_LEVEL (object));
  filter = GST_LEVEL (object);

  switch (prop_id) {
    case ARG_SIGNAL_LEVEL:
      filter->signal = g_value_get_boolean (value);
      break;
    case ARG_SIGNAL_INTERVAL:
      filter->interval = g_value_get_double (value);
      break;
    case ARG_PEAK_TTL:
      filter->decay_peak_ttl = g_value_get_double (value);
      break;
    case ARG_PEAK_FALLOFF:
      filter->decay_peak_falloff = g_value_get_double (value);
      break;
    default:
      break;
  }
}

static void
gst_level_get_property (GObject *object, guint prop_id, 
                        GValue *value, GParamSpec *pspec)
{
  GstLevel *filter;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_LEVEL (object));
  filter = GST_LEVEL (object);

  switch (prop_id) {
    case ARG_SIGNAL_LEVEL:
      g_value_set_boolean (value, filter->signal);
      break;
      case ARG_SIGNAL_INTERVAL:
      g_value_set_double (value, filter->interval);
      break;
    case ARG_PEAK_TTL:
      g_value_set_double (value, filter->decay_peak_ttl);
      break;
    case ARG_PEAK_FALLOFF:
      g_value_set_double (value, filter->decay_peak_falloff);
      break;
   default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_level_base_init (GstLevelClass *klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
	GST_PAD_TEMPLATE_GET (sink_template_factory));
  gst_element_class_add_pad_template (element_class,
	GST_PAD_TEMPLATE_GET (src_template_factory));
  gst_element_class_set_details (element_class, &level_details);

  element_class->change_state = gst_level_change_state;
}

static void
gst_level_class_init (GstLevelClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*) klass;
  gstelement_class = (GstElementClass*) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_SIGNAL_LEVEL,
    g_param_spec_boolean ("signal", "Signal",
                          "Emit level signals for each interval",
                          TRUE, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_SIGNAL_INTERVAL,
    g_param_spec_double ("interval", "Interval",
                         "Interval between emissions (in seconds)",
                         0.01, 100.0, 0.1, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_PEAK_TTL,
    g_param_spec_double ("peak_ttl", "Peak TTL",
                         "Time To Live of decay peak before it falls back",
                         0, 100.0, 0.3, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_PEAK_FALLOFF,
    g_param_spec_double ("peak_falloff", "Peak Falloff",
                         "Decay rate of decay peak after TTL (in dB/sec)",
                         0.0, G_MAXDOUBLE, 10.0, G_PARAM_READWRITE));

  gobject_class->set_property = gst_level_set_property;
  gobject_class->get_property = gst_level_get_property;

  gst_filter_signals[SIGNAL_LEVEL] = 
    g_signal_new ("level", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (GstLevelClass, level), NULL, NULL,
                  gstlevel_cclosure_marshal_VOID__DOUBLE_INT_DOUBLE_DOUBLE_DOUBLE,
                  G_TYPE_NONE, 5,
                  G_TYPE_DOUBLE, G_TYPE_INT,
                  G_TYPE_DOUBLE, G_TYPE_DOUBLE, G_TYPE_DOUBLE);
}

static void
gst_level_init (GstLevel *filter)
{
  filter->sinkpad = gst_pad_new_from_template (GST_PAD_TEMPLATE_GET (sink_template_factory), "sink");
  gst_pad_set_link_function (filter->sinkpad, gst_level_link);
  filter->srcpad = gst_pad_new_from_template (GST_PAD_TEMPLATE_GET (src_template_factory), "src");
  gst_pad_set_link_function (filter->srcpad, gst_level_link);

  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);
  gst_pad_set_chain_function (filter->sinkpad, gst_level_chain);
  filter->srcpad = gst_pad_new ("src", GST_PAD_SRC);
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);

  filter->CS = NULL;
  filter->peak = NULL;
  filter->MS = NULL;
  filter->RMS_dB = NULL;

  filter->rate = 0;
  filter->width = 0;
  filter->channels = 0;

  filter->interval = 0.1;
  filter->decay_peak_ttl = 0.4;
  filter->decay_peak_falloff = 10.0;	/* dB falloff (/sec) */
}

static gboolean
plugin_init (GstPlugin *plugin)
{
  return gst_element_register (plugin, "level",
			       GST_RANK_NONE, GST_TYPE_LEVEL);
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "level",
  "Audio level plugin",
  plugin_init,
  VERSION,
  GST_LICENSE,
  GST_PACKAGE,
  GST_ORIGIN
)
