/* Gnome-Streamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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
#include <gst/audio/audio.h>
#include "gstcutter.h"
#include "math.h"


static GstElementDetails cutter_details = {
  "Cutter",
  "Filter/Effect",
  "Audio Cutter to split audio into non-silent bits",
  VERSION,
  "Thomas <thomas@apestaart.org>",
  "(C) 2001",
};


/* Filter signals and args */
enum {
  /* FILL ME */
  CUT_START,
  CUT_STOP,
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_THRESHOLD,
  ARG_THRESHOLD_DB,
  ARG_RUN_LENGTH,
  ARG_PRE_LENGTH
};

GST_PADTEMPLATE_FACTORY (cutter_src_factory,
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "test_src",
    "audio/raw",
      "channels", GST_PROPS_INT_RANGE (1, 2)
  )
);

GST_PADTEMPLATE_FACTORY (cutter_sink_factory,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "test_src",
    "audio/raw",
      "channels", GST_PROPS_INT_RANGE (1, 2)
  )
);

static void		gst_cutter_class_init		(GstCutterClass *klass);
static void		gst_cutter_init			(GstCutter *filter);

static void		gst_cutter_set_property		(GObject *object, guint prop_id, 
							 const GValue *value, GParamSpec *pspec);
static void		gst_cutter_get_property		(GObject *object, guint prop_id, 
							 GValue *value, GParamSpec *pspec);

static void		gst_cutter_chain		(GstPad *pad, GstBuffer *buf);
static double inline	gst_cutter_16bit_ms 		(gint16* data, guint numsamples);
static double inline	gst_cutter_8bit_ms 		(gint8* data, guint numsamples);

void			gst_cutter_get_caps 		(GstPad *pad, GstCutter* filter);

static GstElementClass *parent_class = NULL;
static guint gst_cutter_signals[LAST_SIGNAL] = { 0 };


GType
gst_cutter_get_type(void) {
  static GType cutter_type = 0;

  if (!cutter_type) {
    static const GTypeInfo cutter_info = {
      sizeof(GstCutterClass),      NULL,      NULL,      (GClassInitFunc)gst_cutter_class_init,
      NULL,
      NULL,
      sizeof(GstCutter),
      0,
      (GInstanceInitFunc)gst_cutter_init,
    };
    cutter_type = g_type_register_static(GST_TYPE_ELEMENT, "GstCutter", &cutter_info, 0);
  }
  return cutter_type;
}

static void
gst_cutter_class_init (GstCutterClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*) klass;
  gstelement_class = (GstElementClass*) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_THRESHOLD,
    g_param_spec_double ("threshold", "threshold", "threshold",
                         -G_MAXDOUBLE, G_MAXDOUBLE, 0.0, G_PARAM_READWRITE)); // CHECKME
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_THRESHOLD_DB,
    g_param_spec_double ("threshold_dB", "threshold_dB", "threshold_dB",
                         -G_MAXDOUBLE, G_MAXDOUBLE, 0.0, G_PARAM_READWRITE)); // CHECKME
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_RUN_LENGTH,
    g_param_spec_double ("runlength", "runlength", "runlength",
                        -G_MAXDOUBLE, G_MAXDOUBLE, 0.0, G_PARAM_READWRITE)); // CHECKME

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_PRE_LENGTH,
    g_param_spec_double ("prelength", "prelength", "prelength",
                        -G_MAXDOUBLE, G_MAXDOUBLE, 0.0, G_PARAM_READWRITE)); // CHECKME
  gst_cutter_signals[CUT_START] = 
	g_signal_new ("cut_start", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_FIRST,
			G_STRUCT_OFFSET (GstCutterClass, cut_start), NULL, NULL,
			g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
  gst_cutter_signals[CUT_STOP] = 
	g_signal_new ("cut_stop", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_FIRST,
			G_STRUCT_OFFSET (GstCutterClass, cut_stop), NULL, NULL,
			g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);


  gobject_class->set_property = gst_cutter_set_property;
  gobject_class->get_property = gst_cutter_get_property;
}

static void
gst_cutter_init (GstCutter *filter)
{
  filter->sinkpad = gst_pad_new_from_template (cutter_sink_factory (),"sink");
  filter->srcpad = gst_pad_new_from_template (cutter_src_factory (),"src");

  filter->threshold_level = 0.1;
  filter->threshold_length = 0.5;
  filter->silent_run_length = 0.0;
  filter->silent = TRUE;

  filter->pre_length = 0.2;
  filter->pre_run_length = 0.0;
  filter->pre_buffer = NULL;

  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);
  gst_pad_set_chain_function (filter->sinkpad, gst_cutter_chain);
  filter->srcpad = gst_pad_new ("src", GST_PAD_SRC);
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);
}

static void
gst_cutter_chain (GstPad *pad, GstBuffer *buf)
{
  GstCutter *filter;
  gint16 *in_data;
  double RMS = 0.0;			/* RMS of signal in buffer */
  double ms = 0.0;			/* mean square value of buffer */
  static gboolean silent_prev = FALSE;  /* previous value of silent */
  GstBuffer* prebuf;                    /* pointer to a prebuffer element */
 
  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  filter = GST_CUTTER (GST_OBJECT_PARENT (pad));
  g_return_if_fail (filter != NULL);
  g_return_if_fail (GST_IS_CUTTER (filter));

  g_return_if_fail (gst_audio_is_buffer_framed (pad, buf) == TRUE);

  if (!filter->have_caps) gst_cutter_get_caps (pad, filter);

  in_data = (gint16 *) GST_BUFFER_DATA (buf);
  g_print ("DEBUG: cutter: length of prerec buffer: %.3f sec\n",
           filter->pre_run_length);

  /* calculate mean square value on buffer */
  switch (filter->width) 
  {
    case 16:
      ms = gst_cutter_16bit_ms (in_data, GST_BUFFER_SIZE (buf) / 2);
      break;
    case 8:
      ms = gst_cutter_8bit_ms ((gint8 *) in_data, GST_BUFFER_SIZE (buf));
      break;
    default:
      /* this shouldn't happen */
      g_print ("WARNING: no mean square function for width %d\n",
		filter->width);
      break;
  }

  silent_prev = filter->silent;

  RMS = sqrt (ms) / (double) filter->max_sample;
  /* if RMS below threshold, add buffer length to silent run length count 
   * if not, reset
   */
  //g_print ("DEBUG: cutter: ms %f, RMS %f\n", ms, RMS);
  if (RMS < filter->threshold_level)
    filter->silent_run_length += gst_audio_length (filter->srcpad, buf);
  else
  {  
    filter->silent_run_length = 0.0;
    filter->silent = FALSE;
  }

  if (filter->silent_run_length > filter->threshold_length)
    /* it has been silent long enough, flag it */
    filter->silent = TRUE;

  /* has the silent status changed ? if so, send right signal 
   * and, if from silent -> not silent, flush pre_record buffer 
   */
  if (filter->silent != silent_prev)
  {
    if (filter->silent)
    {
//      g_print ("DEBUG: cutter: cut to here, turning off out\n");
      gtk_signal_emit (G_OBJECT (filter), gst_cutter_signals[CUT_STOP]);
    }
    else
    {
//      g_print ("DEBUG: cutter: start from here, turning on out\n");
      /* first of all, flush current buffer */
      gtk_signal_emit (G_OBJECT (filter), gst_cutter_signals[CUT_START]);
      g_print ("DEBUG: cutter: flushing buffer ");
      while (filter->pre_buffer)
      {
        g_print (".");
        prebuf = (g_list_first (filter->pre_buffer))->data;
        filter->pre_buffer = g_list_remove (filter->pre_buffer, prebuf);
        gst_pad_push (filter->srcpad, prebuf);
        filter->pre_run_length = 0.0;
      }
      g_print ("\n");
    } 
  }
  /* now check if we have to add the new buffer to the cache or to the pad */
  if (filter->silent)
  {
      filter->pre_buffer = g_list_append (filter->pre_buffer, buf);
      filter->pre_run_length += gst_audio_length (filter->srcpad, buf);
      while (filter->pre_run_length > filter->pre_length)
      {
        prebuf = (g_list_first (filter->pre_buffer))->data;
        filter->pre_buffer = g_list_remove (filter->pre_buffer, prebuf);
        gst_pad_push (filter->srcpad, prebuf);
        filter->pre_run_length -= gst_audio_length (filter->srcpad, prebuf);
      }
  }
  else
    gst_pad_push (filter->srcpad, buf);
}

static double inline
gst_cutter_16bit_ms (gint16* data, guint num_samples)
#include "filter.func"

static double inline
gst_cutter_8bit_ms (gint8* data, guint num_samples)
#include "filter.func"

static void
gst_cutter_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstCutter *filter;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_CUTTER (object));
  filter = GST_CUTTER (object);

  switch (prop_id)
  {
    case ARG_THRESHOLD:
	/* set the level */
      filter->threshold_level = g_value_get_double (value);
      g_print ("DEBUG: cutter: set threshold level to %f\n",
		filter->threshold_level);
      break;
    case ARG_THRESHOLD_DB:
      /* set the level given in dB 
       * value in dB = 20 * log (value) 
       * values in dB < 0 result in values between 0 and 1
       */
      filter->threshold_level = pow (10, g_value_get_double (value) / 20);
      g_print ("DEBUG: cutter: set threshold level to %f\n",
		filter->threshold_level);
      break;
    case ARG_RUN_LENGTH:
      /* set the minimum length of the silent run required */
      filter->threshold_length = g_value_get_double (value);
      break;	
    case ARG_PRE_LENGTH:
      /* set the length of the pre-record block */
      filter->pre_length = g_value_get_double (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_cutter_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstCutter *filter;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_CUTTER(object));
  filter = GST_CUTTER (object);

  switch (prop_id)
  {
    case ARG_RUN_LENGTH:
     g_value_set_double (value, filter->threshold_length);
     break;
    case ARG_THRESHOLD:
     g_value_set_double (value, filter->threshold_level);
     break;
    case ARG_THRESHOLD_DB:
     g_value_set_double (value, 20 * log (filter->threshold_level));
     break;
    case ARG_PRE_LENGTH:
      g_value_set_double (value, filter->pre_length);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;

  factory = gst_elementfactory_new("cutter",GST_TYPE_CUTTER,
                                   &cutter_details);
  g_return_val_if_fail(factory != NULL, FALSE);
  
  gst_elementfactory_add_padtemplate (factory, GST_PADTEMPLATE_GET (cutter_src_factory));
  gst_elementfactory_add_padtemplate (factory, GST_PADTEMPLATE_GET (cutter_sink_factory));

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  /* load audio support library */
  if (!gst_library_load ("gstaudio"))
  {
    gst_info ("cutter: could not load support library: 'gstaudio'\n");
    return FALSE;
  }

  return TRUE;
}

GstPluginDesc plugin_desc = 
{
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "cutter",
  plugin_init
};

void
gst_cutter_get_caps (GstPad *pad, GstCutter* filter)
{
  GstCaps *caps = NULL;

  caps = GST_PAD_CAPS (pad);
    // FIXME : Please change this to a better warning method !
  if (caps == NULL)
    printf ("WARNING: cutter: get_caps: Could not get caps of pad !\n");
  filter->width = gst_caps_get_int (caps, "width");
  filter->max_sample = gst_audio_highest_sample_value (pad);
  filter->have_caps = TRUE;
}
