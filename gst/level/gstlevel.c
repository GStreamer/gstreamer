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

#include <gst/gst.h>
#include "gstlevel.h"
#include "math.h"

/* elementfactory information */
static GstElementDetails level_details = {
  "Level",
  "Filter/Audio/Analysis",
  "LGPL",
  "RMS/Peak/Decaying Peak Level signaller for audio/raw",
  VERSION,
  "Thomas <thomas@apestaart.org>",
  "(C) 2001, 2003, 2003",
};


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

static GstPadTemplate*
level_src_factory (void)
{
  static GstPadTemplate *template = NULL;

  if (!template) {
    template = gst_pad_template_new (
      "src",
      GST_PAD_SRC,
      GST_PAD_ALWAYS,
      gst_caps_new (
        "test_src",
        "audio/raw",
	gst_props_new (
          "channels", GST_PROPS_INT_RANGE (1, 2),
          "signed", GST_PROPS_BOOLEAN (TRUE),
	  NULL)),
      NULL);
  }
  return template;
}

static GstPadTemplate*
level_sink_factory (void)
{
  static GstPadTemplate *template = NULL;

  if (!template) {
    template = gst_pad_template_new (
      "sink",
      GST_PAD_SINK,
      GST_PAD_ALWAYS,
      gst_caps_new (
        "test_src",
        "audio/raw",
	gst_props_new (
          "channels", GST_PROPS_INT_RANGE (1, 2),
          "signed", GST_PROPS_BOOLEAN (TRUE),
	  NULL)),
      NULL);
  }
  return template;
}

static void		gst_level_class_init		(GstLevelClass *klass);
static void		gst_level_init			(GstLevel *filter);

static void		gst_level_set_property			(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void		gst_level_get_property			(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static void		gst_level_chain			(GstPad *pad, GstBuffer *buf);

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
      sizeof (GstLevelClass), NULL, NULL,
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
gst_level_connect (GstPad *pad, GstCaps *caps)
{
  GstLevel *filter;
  GstPad *otherpad;
  GstPadLinkReturn res;
  int i;

  filter = GST_LEVEL (gst_pad_get_parent (pad));
  g_return_val_if_fail (filter != NULL, GST_PAD_LINK_REFUSED);
  g_return_val_if_fail (GST_IS_LEVEL (filter), GST_PAD_LINK_REFUSED);
  otherpad = (pad == filter->srcpad ? filter->sinkpad : filter->srcpad);
	  
  if (GST_CAPS_IS_FIXED (caps)) 
  {
    /* yep, got them */
    res = gst_pad_try_set_caps (otherpad, caps);
    /* if ok, set filter */
    if (res == GST_PAD_LINK_OK)
    {
      filter->num_samples = 0;
      /* FIXME: error handling */
      if (! gst_caps_get_int (caps, "rate", &(filter->rate)))
        g_warning ("WARNING: level: Could not get rate from caps\n");
      if (!gst_caps_get_int (caps, "width", &(filter->width)))
        g_warning ("WARNING: level: Could not get width from caps\n");
      if (!gst_caps_get_int (caps, "channels", &(filter->channels)))
        g_warning ("WARNING: level: Could not get number of channels from caps\n");

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
      for (i = 0; i < filter->channels; ++i)
      {
        filter->CS[i] = filter->peak[i] = filter->last_peak[i] =
                        filter->decay_peak[i] = filter->decay_peak_age[i] = 
                        filter->MS[i] = filter->RMS_dB[i] = 0.0;
      }
    }
    return res;
  }
  return GST_PAD_LINK_DELAYED;
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
gst_level_chain (GstPad *pad, GstBuffer *buf)
{
  GstLevel *filter;
  gint16 *in_data;

  double CS = 0.0;
  gint num_samples = 0;
  gint i;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);


  g_print ("\nDEBUG: chain start\n");
  filter = GST_LEVEL (GST_OBJECT_PARENT (pad));
  g_return_if_fail (filter != NULL);
  g_return_if_fail (GST_IS_LEVEL (filter));

  for (i = 0; i < filter->channels; ++i)
    filter->CS[i] = filter->peak[i] = filter->MS[i] = filter->RMS_dB[i] = 0.0;
  
  in_data = (gint16 *) GST_BUFFER_DATA(buf);
  
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
    g_print ("DEBUG: CS %f, peak %f\n", CS, filter->peak[i]);
    filter->CS[i] += CS;

  }
  gst_pad_push (filter->srcpad, buf);

  filter->num_samples += num_samples;

  for (i = 0; i < filter->channels; ++i)
  {
    filter->decay_peak_age[i] += num_samples;
    g_print ("filter peak info [%d]: peak %f, age %f\n", i,
             filter->last_peak[i], filter->decay_peak_age[i]);
    /* update running peak */
    if (filter->peak[i] > filter->last_peak[i])
        filter->last_peak[i] = filter->peak[i];

    /* update decay peak */
    if (filter->peak[i] >= filter->decay_peak[i])
    {
       g_print ("new peak, %f\n", filter->peak[i]);
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

        g_print ("falloff: length %f, dB falloff %f, falloff factor %e\n",
                 length, falloff_dB, falloff);
        filter->decay_peak[i] *= falloff;
        g_print ("peak is %f samples old, decayed with factor %e to %f\n",
                 filter->decay_peak_age[i], falloff, filter->decay_peak[i]);
      }
    }
  }

  /* do we need to emit ? */
  
  if (filter->num_samples >= filter->interval * (gdouble) filter->rate)
  {
    if (filter->signal)
    {
      gdouble RMS, peak;
      for (i = 0; i < filter->channels; ++i)
      {
        RMS = sqrt (filter->CS[i] / (filter->num_samples / filter->channels));
        peak = filter->last_peak[i];

        g_signal_emit (G_OBJECT (filter), gst_filter_signals[SIGNAL_LEVEL], 0,
                       i, 20 * log10 (RMS), 20 * log10 (filter->last_peak[i]),
                       20 * log10 (filter->decay_peak[i]));
        /* we emitted, so reset cumulative and normal peak */
        filter->CS[i] = 0.0;
        filter->last_peak[i] = 0.0;
      }
    }
    filter->num_samples = 0;
  }
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
                  gstlevel_cclosure_marshal_VOID__INT_DOUBLE_DOUBLE_DOUBLE,
                  G_TYPE_NONE, 4,
                  G_TYPE_INT, G_TYPE_DOUBLE, G_TYPE_DOUBLE, G_TYPE_DOUBLE);
}

static void
gst_level_init (GstLevel *filter)
{
  filter->sinkpad = gst_pad_new_from_template (level_sink_factory (), "sink");
  gst_pad_set_link_function (filter->sinkpad, gst_level_connect);
  filter->srcpad = gst_pad_new_from_template (level_src_factory (), "src");
  gst_pad_set_link_function (filter->srcpad, gst_level_connect);

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
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;

  factory = gst_element_factory_new ("level", GST_TYPE_LEVEL,
                                     &level_details);
  g_return_val_if_fail (factory != NULL, FALSE);
  
  gst_element_factory_add_pad_template (factory, level_src_factory ());
  gst_element_factory_add_pad_template (factory, level_sink_factory ());

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "level",
  plugin_init
};
