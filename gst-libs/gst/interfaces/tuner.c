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

static void 	gst_tuner_class_init	(GstTunerClass *klass);

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
				       GST_TYPE_INTERFACE);
  }

  return gst_tuner_type;
}

static void
gst_tuner_class_init (GstTunerClass *klass)
{
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
