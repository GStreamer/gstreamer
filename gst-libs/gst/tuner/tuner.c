/* GStreamer Tuner
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * tuner.c: tuner design virtual class function wrappers
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

#include "tuner.h"
#include "tunermarshal.h"

enum {
  NORM_CHANGED,
  CHANNEL_CHANGED,
  FREQUENCY_CHANGED,
  SIGNAL_CHANGED,
  LAST_SIGNAL
};

static void 	gst_tuner_class_init	(GstTunerClass *klass);

static guint gst_tuner_signals[LAST_SIGNAL] = { 0 };

GType
gst_tuner_get_type (void)
{
  static GType gst_tuner_type = 0;

  if (!gst_tuner_type) {
    static const GTypeInfo gst_tuner_info = {
      sizeof (GstTunerClass),
      (GBaseInitFunc) gst_tuner_class_init,
      NULL,
      NULL,
      NULL,
      NULL,
      0,
      0,
      NULL,
    };

    gst_tuner_type = g_type_register_static (G_TYPE_INTERFACE,
					     "GstTuner",
					     &gst_tuner_info, 0);
    g_type_interface_add_prerequisite (gst_tuner_type,
				       GST_TYPE_IMPLEMENTS_INTERFACE);
  }

  return gst_tuner_type;
}

static void
gst_tuner_class_init (GstTunerClass *klass)
{
  static gboolean initialized = FALSE;

  if (!initialized) {
    gst_tuner_signals[NORM_CHANGED] =
      g_signal_new ("norm_changed",
		    GST_TYPE_TUNER, G_SIGNAL_RUN_LAST,
		    G_STRUCT_OFFSET (GstTunerClass, norm_changed),
		    NULL, NULL,
		    g_cclosure_marshal_VOID__OBJECT, G_TYPE_NONE, 1,
		    GST_TYPE_TUNER_NORM);
    gst_tuner_signals[CHANNEL_CHANGED] =
      g_signal_new ("channel_changed",
		    GST_TYPE_TUNER, G_SIGNAL_RUN_LAST,
		    G_STRUCT_OFFSET (GstTunerClass, channel_changed),
		    NULL, NULL,
		    g_cclosure_marshal_VOID__OBJECT, G_TYPE_NONE, 1,
		    GST_TYPE_TUNER_CHANNEL);
    gst_tuner_signals[NORM_CHANGED] =
      g_signal_new ("norm_changed",
		    GST_TYPE_TUNER, G_SIGNAL_RUN_LAST,
		    G_STRUCT_OFFSET (GstTunerClass, frequency_changed),
		    NULL, NULL,
		    gst_tuner_marshal_VOID__OBJECT_ULONG, G_TYPE_NONE, 2,
		    GST_TYPE_TUNER_CHANNEL, G_TYPE_ULONG);
    gst_tuner_signals[NORM_CHANGED] =
      g_signal_new ("norm_changed",
		    GST_TYPE_TUNER, G_SIGNAL_RUN_LAST,
		    G_STRUCT_OFFSET (GstTunerClass, signal_changed),
		    NULL, NULL,
		    gst_tuner_marshal_VOID__OBJECT_INT, G_TYPE_NONE, 2,
		    GST_TYPE_TUNER_CHANNEL, G_TYPE_INT);
      
    initialized = TRUE;
  }

  /* default virtual functions */
  klass->list_channels = NULL;
  klass->set_channel = NULL;
  klass->get_channel = NULL;

  klass->list_norms = NULL;
  klass->set_norm = NULL;
  klass->get_norm = NULL;

  klass->set_frequency = NULL;
  klass->get_frequency = NULL;
  klass->signal_strength = NULL;
}

const GList *
gst_tuner_list_channels (GstTuner *tuner)
{
  GstTunerClass *klass = GST_TUNER_GET_CLASS (tuner);

  if (klass->list_channels) {
    return klass->list_channels (tuner);
  }

  return NULL;
}

void
gst_tuner_set_channel (GstTuner        *tuner,
		       GstTunerChannel *channel)
{
  GstTunerClass *klass = GST_TUNER_GET_CLASS (tuner);

  if (klass->set_channel) {
    klass->set_channel (tuner, channel);
  }
}

const GstTunerChannel *
gst_tuner_get_channel (GstTuner *tuner)
{
  GstTunerClass *klass = GST_TUNER_GET_CLASS (tuner);

  if (klass->get_channel) {
    return klass->get_channel (tuner);
  }

  return NULL;
}

const GList *
gst_tuner_list_norms (GstTuner *tuner)
{
  GstTunerClass *klass = GST_TUNER_GET_CLASS (tuner);

  if (klass->list_norms) {
    return klass->list_norms (tuner);
  }

  return NULL;
}

void
gst_tuner_set_norm (GstTuner     *tuner,
		    GstTunerNorm *norm)
{
  GstTunerClass *klass = GST_TUNER_GET_CLASS (tuner);

  if (klass->set_norm) {
    klass->set_norm (tuner, norm);
  }
}

const GstTunerNorm *
gst_tuner_get_norm (GstTuner *tuner)
{
  GstTunerClass *klass = GST_TUNER_GET_CLASS (tuner);

  if (klass->get_norm) {
    return klass->get_norm (tuner);
  }

  return NULL;
}

void
gst_tuner_set_frequency (GstTuner        *tuner,
			 GstTunerChannel *channel,
			 gulong           frequency)
{
  GstTunerClass *klass = GST_TUNER_GET_CLASS (tuner);

  g_return_if_fail (GST_TUNER_CHANNEL_HAS_FLAG (channel,
					GST_TUNER_CHANNEL_FREQUENCY));

  if (klass->set_frequency) {
    klass->set_frequency (tuner, channel, frequency);
  }
}

gulong
gst_tuner_get_frequency (GstTuner        *tuner,
			 GstTunerChannel *channel)
{
  GstTunerClass *klass = GST_TUNER_GET_CLASS (tuner);

  g_return_val_if_fail (GST_TUNER_CHANNEL_HAS_FLAG (channel,
					GST_TUNER_CHANNEL_FREQUENCY), 0);

  if (klass->get_frequency) {
    return klass->get_frequency (tuner, channel);
  }

  return 0;
}

gint
gst_tuner_signal_strength (GstTuner        *tuner,
			   GstTunerChannel *channel)
{
  GstTunerClass *klass = GST_TUNER_GET_CLASS (tuner);

  g_return_val_if_fail (GST_TUNER_CHANNEL_HAS_FLAG (channel,
					GST_TUNER_CHANNEL_FREQUENCY), 0);

  if (klass->signal_strength) {
    return klass->signal_strength (tuner, channel);
  }

  return 0;
}

void
gst_tuner_channel_changed (GstTuner        *tuner,
			   GstTunerChannel *channel)
{
  g_signal_emit (G_OBJECT (tuner),
		 gst_tuner_signals[CHANNEL_CHANGED], 0,
		 channel);
}

void
gst_tuner_norm_changed (GstTuner        *tuner,
			GstTunerNorm    *norm)
{
  g_signal_emit (G_OBJECT (tuner),
		 gst_tuner_signals[NORM_CHANGED], 0,
		 norm);
}

void
gst_tuner_frequency_changed (GstTuner        *tuner,
			     GstTunerChannel *channel,
			     gulong           frequency)
{
  g_signal_emit (G_OBJECT (tuner),
		 gst_tuner_signals[FREQUENCY_CHANGED], 0,
		 channel, frequency);

  g_signal_emit_by_name (G_OBJECT (channel),
			 "frequency_changed",
			 frequency);
}

void
gst_tuner_signal_changed (GstTuner        *tuner,
			  GstTunerChannel *channel,
			  gint             signal)
{
  g_signal_emit (G_OBJECT (tuner),
		 gst_tuner_signals[SIGNAL_CHANGED], 0,
		 channel, signal);

  g_signal_emit_by_name (G_OBJECT (channel),
			 "signal_changed",
			 signal);
}
