/* GStreamer
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <gst/gst.h>
#include <gst/audio/audio.h>
#include "gstcutter.h"
#include "math.h"

/* elementfactory information */
static GstElementDetails cutter_details = {
  "Cutter",
  "Filter/Editor/Audio",
  "Audio Cutter to split audio into non-silent bits",
  "Thomas <thomas@apestaart.org>",
};


/* Filter signals and args */
enum
{
  /* FILL ME */
  CUT_START,
  CUT_STOP,
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_THRESHOLD,
  ARG_THRESHOLD_DB,
  ARG_RUN_LENGTH,
  ARG_PRE_LENGTH,
  ARG_LEAKY
};

static GstStaticPadTemplate cutter_src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_AUDIO_INT_PAD_TEMPLATE_CAPS "; "
	GST_AUDIO_FLOAT_PAD_TEMPLATE_CAPS)
    );

static GstStaticPadTemplate cutter_sink_factory =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_AUDIO_INT_PAD_TEMPLATE_CAPS "; "
	GST_AUDIO_FLOAT_PAD_TEMPLATE_CAPS)
    );

static void gst_cutter_base_init (gpointer g_class);
static void gst_cutter_class_init (GstCutterClass * klass);
static void gst_cutter_init (GstCutter * filter);

static void gst_cutter_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_cutter_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_cutter_chain (GstPad * pad, GstData * _data);
static double inline gst_cutter_16bit_ms (gint16 * data, guint numsamples);
static double inline gst_cutter_8bit_ms (gint8 * data, guint numsamples);

void gst_cutter_get_caps (GstPad * pad, GstCutter * filter);

static GstElementClass *parent_class = NULL;
static guint gst_cutter_signals[LAST_SIGNAL] = { 0 };


GType
gst_cutter_get_type (void)
{
  static GType cutter_type = 0;

  if (!cutter_type) {
    static const GTypeInfo cutter_info = {
      sizeof (GstCutterClass),
      gst_cutter_base_init,
      NULL,
      (GClassInitFunc) gst_cutter_class_init, NULL, NULL,
      sizeof (GstCutter), 0,
      (GInstanceInitFunc) gst_cutter_init,
    };
    cutter_type = g_type_register_static (GST_TYPE_ELEMENT, "GstCutter",
	&cutter_info, 0);
  }
  return cutter_type;
}

static void
gst_cutter_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&cutter_src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&cutter_sink_factory));
  gst_element_class_set_details (element_class, &cutter_details);
}

static GstPadLinkReturn
gst_cutter_link (GstPad * pad, const GstCaps * caps)
{
  GstCutter *filter;
  GstPad *otherpad;

  filter = GST_CUTTER (gst_pad_get_parent (pad));
  g_return_val_if_fail (filter != NULL, GST_PAD_LINK_REFUSED);
  g_return_val_if_fail (GST_IS_CUTTER (filter), GST_PAD_LINK_REFUSED);
  otherpad = (pad == filter->srcpad ? filter->sinkpad : filter->srcpad);

  return gst_pad_try_set_caps (otherpad, caps);
}

static void
gst_cutter_class_init (GstCutterClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_THRESHOLD,
      g_param_spec_double ("threshold", "Threshold",
	  "Volume threshold before trigger",
	  -G_MAXDOUBLE, G_MAXDOUBLE, 0.0, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_THRESHOLD_DB,
      g_param_spec_double ("threshold_dB", "Threshold (dB)",
	  "Volume threshold before trigger (in dB)",
	  -G_MAXDOUBLE, G_MAXDOUBLE, 0.0, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_RUN_LENGTH,
      g_param_spec_double ("runlength", "Runlength",
	  "Length of drop below threshold before cut_stop (seconds)",
	  0.0, G_MAXDOUBLE, 0.0, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_PRE_LENGTH,
      g_param_spec_double ("prelength", "prelength",
	  "Length of pre-recording buffer (seconds)",
	  0.0, G_MAXDOUBLE, 0.0, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_LEAKY,
      g_param_spec_boolean ("leaky", "Leaky",
	  "do we leak buffers when below threshold ?",
	  FALSE, G_PARAM_READWRITE));
  gst_cutter_signals[CUT_START] =
      g_signal_new ("cut-start", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST,
      G_STRUCT_OFFSET (GstCutterClass, cut_start), NULL, NULL,
      g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
  gst_cutter_signals[CUT_STOP] =
      g_signal_new ("cut-stop", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST,
      G_STRUCT_OFFSET (GstCutterClass, cut_stop), NULL, NULL,
      g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);


  gobject_class->set_property = gst_cutter_set_property;
  gobject_class->get_property = gst_cutter_get_property;
}

static void
gst_cutter_init (GstCutter * filter)
{
  filter->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&cutter_sink_factory), "sink");
  filter->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&cutter_src_factory), "src");

  filter->threshold_level = 0.1;
  filter->threshold_length = 0.5;
  filter->silent_run_length = 0.0;
  filter->silent = TRUE;

  filter->pre_length = 0.2;
  filter->pre_run_length = 0.0;
  filter->pre_buffer = NULL;
  filter->leaky = FALSE;

  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);
  gst_pad_set_chain_function (filter->sinkpad, gst_cutter_chain);
  gst_pad_set_link_function (filter->sinkpad, gst_cutter_link);

  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);
  /*gst_pad_set_link_function (filter->srcpad, gst_cutter_link); */
}

static void
gst_cutter_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstCutter *filter;
  gint16 *in_data;
  double RMS = 0.0;		/* RMS of signal in buffer */
  double ms = 0.0;		/* mean square value of buffer */
  static gboolean silent_prev = FALSE;	/* previous value of silent */
  GstBuffer *prebuf;		/* pointer to a prebuffer element */

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  filter = GST_CUTTER (GST_OBJECT_PARENT (pad));
  g_return_if_fail (filter != NULL);
  g_return_if_fail (GST_IS_CUTTER (filter));

  if (gst_audio_is_buffer_framed (pad, buf) == FALSE)
    g_warning ("audio buffer is not framed !\n");

  if (!filter->have_caps)
    gst_cutter_get_caps (pad, filter);

  in_data = (gint16 *) GST_BUFFER_DATA (buf);
  GST_DEBUG ("length of prerec buffer: %.3f sec", filter->pre_run_length);

  /* calculate mean square value on buffer */
  switch (filter->width) {
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
  GST_DEBUG ("buffer stats: ms %f, RMS %f, audio length %f",
      ms, RMS, gst_audio_length (filter->srcpad, buf));
  if (RMS < filter->threshold_level)
    filter->silent_run_length += gst_audio_length (filter->srcpad, buf);
  else {
    filter->silent_run_length = 0.0;
    filter->silent = FALSE;
  }

  if (filter->silent_run_length > filter->threshold_length)
    /* it has been silent long enough, flag it */
    filter->silent = TRUE;

  /* has the silent status changed ? if so, send right signal
   * and, if from silent -> not silent, flush pre_record buffer
   */
  if (filter->silent != silent_prev) {
    if (filter->silent) {
/*      g_print ("DEBUG: cutter: cut to here, turning off out\n"); */
      g_signal_emit (G_OBJECT (filter), gst_cutter_signals[CUT_STOP], 0);
    } else {
      gint count = 0;

/*      g_print ("DEBUG: cutter: start from here, turning on out\n"); */
      /* first of all, flush current buffer */
      g_signal_emit (G_OBJECT (filter), gst_cutter_signals[CUT_START], 0);
      GST_DEBUG ("flushing buffer of length %.3f", filter->pre_run_length);
      while (filter->pre_buffer) {
	prebuf = (g_list_first (filter->pre_buffer))->data;
	filter->pre_buffer = g_list_remove (filter->pre_buffer, prebuf);
	gst_pad_push (filter->srcpad, GST_DATA (prebuf));
	++count;
      }
      GST_DEBUG ("flushed %d buffers", count);
      filter->pre_run_length = 0.0;
    }
  }
  /* now check if we have to send the new buffer to the internal buffer cache
   * or to the srcpad */
  if (filter->silent) {
    /* we ref it before putting it in the pre_buffer */
    /* FIXME: we shouldn't probably do this, because the buffer
     * arrives reffed already; the plugin should just push it
     * or unref it to make it disappear */
    /*
       gst_buffer_ref (buf);
     */
    filter->pre_buffer = g_list_append (filter->pre_buffer, buf);
    filter->pre_run_length += gst_audio_length (filter->srcpad, buf);
    while (filter->pre_run_length > filter->pre_length) {
      prebuf = (g_list_first (filter->pre_buffer))->data;
      g_assert (GST_IS_BUFFER (prebuf));
      filter->pre_buffer = g_list_remove (filter->pre_buffer, prebuf);
      filter->pre_run_length -= gst_audio_length (filter->srcpad, prebuf);
      /* only pass buffers if we don't leak */
      if (!filter->leaky)
	gst_pad_push (filter->srcpad, GST_DATA (prebuf));
      /* we unref it after getting it out of the pre_buffer */
      gst_buffer_unref (prebuf);
    }
  } else
    gst_pad_push (filter->srcpad, GST_DATA (buf));
}

static double inline
gst_cutter_16bit_ms (gint16 * data, guint num_samples)
#include "filter.func"
     static double inline gst_cutter_8bit_ms (gint8 * data, guint num_samples)
#include "filter.func"
     static void
	 gst_cutter_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstCutter *filter;

  g_return_if_fail (GST_IS_CUTTER (object));
  filter = GST_CUTTER (object);

  switch (prop_id) {
    case ARG_THRESHOLD:
      /* set the level */
      filter->threshold_level = g_value_get_double (value);
      GST_DEBUG ("DEBUG: set threshold level to %f", filter->threshold_level);
      break;
    case ARG_THRESHOLD_DB:
      /* set the level given in dB
       * value in dB = 20 * log (value)
       * values in dB < 0 result in values between 0 and 1
       */
      filter->threshold_level = pow (10, g_value_get_double (value) / 20);
      GST_DEBUG ("DEBUG: set threshold level to %f", filter->threshold_level);
      break;
    case ARG_RUN_LENGTH:
      /* set the minimum length of the silent run required */
      filter->threshold_length = g_value_get_double (value);
      break;
    case ARG_PRE_LENGTH:
      /* set the length of the pre-record block */
      filter->pre_length = g_value_get_double (value);
      break;
    case ARG_LEAKY:
      /* set if the pre-record buffer is leaky or not */
      filter->leaky = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_cutter_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstCutter *filter;

  g_return_if_fail (GST_IS_CUTTER (object));
  filter = GST_CUTTER (object);

  switch (prop_id) {
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
    case ARG_LEAKY:
      g_value_set_boolean (value, filter->leaky);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  /* load audio support library */
  if (!gst_library_load ("gstaudio"))
    return FALSE;

  if (!gst_element_register (plugin, "cutter", GST_RANK_NONE, GST_TYPE_CUTTER))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "cutter",
    "Audio Cutter to split audio into non-silent bits",
    plugin_init, VERSION, "LGPL", GST_PACKAGE, GST_ORIGIN)

     void gst_cutter_get_caps (GstPad * pad, GstCutter * filter)
{
  const GstCaps *caps = NULL;
  GstStructure *structure;

  caps = GST_PAD_CAPS (pad);
  /* FIXME : Please change this to a better warning method ! */
  g_assert (caps != NULL);
  if (caps == NULL)
    printf ("WARNING: get_caps: Could not get caps of pad !\n");
  structure = gst_caps_get_structure (caps, 0);
  gst_structure_get_int (structure, "width", &filter->width);
  filter->max_sample = gst_audio_highest_sample_value (pad);
  filter->have_caps = TRUE;
}
