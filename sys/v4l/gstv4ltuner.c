/* GStreamer
 *
 * gstv4ltuner.c: tuner interface implementation for V4L
 *
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
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
#include <gst/tuner/tuner.h>

#include "gstv4ltuner.h"
#include "gstv4lelement.h"
#include "v4l_calls.h"

static void gst_v4l_tuner_channel_class_init (GstV4lTunerChannelClass * klass);
static void gst_v4l_tuner_channel_init (GstV4lTunerChannel * channel);

static void gst_v4l_tuner_norm_class_init (GstV4lTunerNormClass * klass);
static void gst_v4l_tuner_norm_init (GstV4lTunerNorm * norm);

static const GList *gst_v4l_tuner_list_channels (GstTuner * tuner);
static void gst_v4l_tuner_set_channel (GstTuner * tuner,
    GstTunerChannel * channel);
static GstTunerChannel *gst_v4l_tuner_get_channel (GstTuner * tuner);

static const GList *gst_v4l_tuner_list_norms (GstTuner * tuner);
static void gst_v4l_tuner_set_norm (GstTuner * tuner, GstTunerNorm * norm);
static GstTunerNorm *gst_v4l_tuner_get_norm (GstTuner * tuner);

static void gst_v4l_tuner_set_frequency (GstTuner * tuner,
    GstTunerChannel * channel, gulong frequency);
static gulong gst_v4l_tuner_get_frequency (GstTuner * tuner,
    GstTunerChannel * channel);
static gint gst_v4l_tuner_signal_strength (GstTuner * tuner,
    GstTunerChannel * channel);

static GstTunerNormClass *norm_parent_class = NULL;
static GstTunerChannelClass *channel_parent_class = NULL;

GType
gst_v4l_tuner_channel_get_type (void)
{
  static GType gst_v4l_tuner_channel_type = 0;

  if (!gst_v4l_tuner_channel_type) {
    static const GTypeInfo v4l_tuner_channel_info = {
      sizeof (GstV4lTunerChannelClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_v4l_tuner_channel_class_init,
      NULL,
      NULL,
      sizeof (GstV4lTunerChannel),
      0,
      (GInstanceInitFunc) gst_v4l_tuner_channel_init,
      NULL
    };

    gst_v4l_tuner_channel_type =
        g_type_register_static (GST_TYPE_TUNER_CHANNEL,
        "GstV4lTunerChannel", &v4l_tuner_channel_info, 0);
  }

  return gst_v4l_tuner_channel_type;
}

static void
gst_v4l_tuner_channel_class_init (GstV4lTunerChannelClass * klass)
{
  channel_parent_class = g_type_class_ref (GST_TYPE_TUNER_CHANNEL);
}

static void
gst_v4l_tuner_channel_init (GstV4lTunerChannel * channel)
{
  channel->index = 0;
  channel->audio = 0;
  channel->tuner = 0;
}

GType
gst_v4l_tuner_norm_get_type (void)
{
  static GType gst_v4l_tuner_norm_type = 0;

  if (!gst_v4l_tuner_norm_type) {
    static const GTypeInfo v4l_tuner_norm_info = {
      sizeof (GstV4lTunerNormClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_v4l_tuner_norm_class_init,
      NULL,
      NULL,
      sizeof (GstV4lTunerNorm),
      0,
      (GInstanceInitFunc) gst_v4l_tuner_norm_init,
      NULL
    };

    gst_v4l_tuner_norm_type =
        g_type_register_static (GST_TYPE_TUNER_NORM,
        "GstV4lTunerNorm", &v4l_tuner_norm_info, 0);
  }

  return gst_v4l_tuner_norm_type;
}

static void
gst_v4l_tuner_norm_class_init (GstV4lTunerNormClass * klass)
{
  norm_parent_class = g_type_class_ref (GST_TYPE_TUNER_NORM);
}

static void
gst_v4l_tuner_norm_init (GstV4lTunerNorm * norm)
{
  norm->index = 0;
}

void
gst_v4l_tuner_interface_init (GstTunerClass * klass)
{
  /* default virtual functions */
  klass->list_channels = gst_v4l_tuner_list_channels;
  klass->set_channel = gst_v4l_tuner_set_channel;
  klass->get_channel = gst_v4l_tuner_get_channel;

  klass->list_norms = gst_v4l_tuner_list_norms;
  klass->set_norm = gst_v4l_tuner_set_norm;
  klass->get_norm = gst_v4l_tuner_get_norm;

  klass->set_frequency = gst_v4l_tuner_set_frequency;
  klass->get_frequency = gst_v4l_tuner_get_frequency;
  klass->signal_strength = gst_v4l_tuner_signal_strength;
}

static G_GNUC_UNUSED gboolean
gst_v4l_tuner_contains_channel (GstV4lElement * v4lelement,
    GstV4lTunerChannel * v4lchannel)
{
  const GList *item;

  for (item = v4lelement->channels; item != NULL; item = item->next)
    if (item->data == v4lchannel)
      return TRUE;

  return FALSE;
}

static const GList *
gst_v4l_tuner_list_channels (GstTuner * tuner)
{
  return GST_V4LELEMENT (tuner)->channels;
}

static void
gst_v4l_tuner_set_channel (GstTuner * tuner, GstTunerChannel * channel)
{
  GstV4lElement *v4lelement = GST_V4LELEMENT (tuner);
  GstV4lTunerChannel *v4lchannel = GST_V4L_TUNER_CHANNEL (channel);
  gint norm;

  /* assert that we're opened and that we're using a known item */
  g_return_if_fail (GST_V4L_IS_OPEN (v4lelement));
  g_return_if_fail (gst_v4l_tuner_contains_channel (v4lelement, v4lchannel));

  gst_v4l_get_chan_norm (v4lelement, NULL, &norm);
  gst_v4l_set_chan_norm (v4lelement, v4lchannel->index, norm);
}

static GstTunerChannel *
gst_v4l_tuner_get_channel (GstTuner * tuner)
{
  GstV4lElement *v4lelement = GST_V4LELEMENT (tuner);
  GList *item;
  gint channel;

  /* assert that we're opened */
  g_return_val_if_fail (GST_V4L_IS_OPEN (v4lelement), NULL);

  gst_v4l_get_chan_norm (v4lelement, &channel, NULL);

  for (item = v4lelement->channels; item != NULL; item = item->next) {
    if (channel == GST_V4L_TUNER_CHANNEL (item->data)->index)
      return GST_TUNER_CHANNEL (item->data);
  }

  return NULL;
}

static G_GNUC_UNUSED gboolean
gst_v4l_tuner_contains_norm (GstV4lElement * v4lelement,
    GstV4lTunerNorm * v4lnorm)
{
  const GList *item;

  for (item = v4lelement->norms; item != NULL; item = item->next)
    if (item->data == v4lnorm)
      return TRUE;

  return FALSE;
}

static const GList *
gst_v4l_tuner_list_norms (GstTuner * tuner)
{
  return GST_V4LELEMENT (tuner)->norms;
}

static void
gst_v4l_tuner_set_norm (GstTuner * tuner, GstTunerNorm * norm)
{
  GstV4lElement *v4lelement = GST_V4LELEMENT (tuner);
  GstV4lTunerNorm *v4lnorm = GST_V4L_TUNER_NORM (norm);
  gint channel;

  /* assert that we're opened and that we're using a known item */
  g_return_if_fail (GST_V4L_IS_OPEN (v4lelement));
  g_return_if_fail (gst_v4l_tuner_contains_norm (v4lelement, v4lnorm));

  gst_v4l_get_chan_norm (v4lelement, &channel, NULL);
  gst_v4l_set_chan_norm (v4lelement, channel, v4lnorm->index);
}

static GstTunerNorm *
gst_v4l_tuner_get_norm (GstTuner * tuner)
{
  GstV4lElement *v4lelement = GST_V4LELEMENT (tuner);
  GList *item;
  gint norm;

  /* assert that we're opened */
  g_return_val_if_fail (GST_V4L_IS_OPEN (v4lelement), NULL);

  gst_v4l_get_chan_norm (v4lelement, NULL, &norm);

  for (item = v4lelement->norms; item != NULL; item = item->next) {
    if (norm == GST_V4L_TUNER_NORM (item->data)->index)
      return GST_TUNER_NORM (item->data);
  }

  return NULL;
}

static void
gst_v4l_tuner_set_frequency (GstTuner * tuner,
    GstTunerChannel * channel, gulong frequency)
{
  GstV4lElement *v4lelement = GST_V4LELEMENT (tuner);
  GstV4lTunerChannel *v4lchannel = GST_V4L_TUNER_CHANNEL (channel);
  gint chan;

  /* assert that we're opened and that we're using a known item */
  g_return_if_fail (GST_V4L_IS_OPEN (v4lelement));
  g_return_if_fail (GST_TUNER_CHANNEL_HAS_FLAG (channel,
          GST_TUNER_CHANNEL_FREQUENCY));
  g_return_if_fail (gst_v4l_tuner_contains_channel (v4lelement, v4lchannel));

  gst_v4l_get_chan_norm (v4lelement, &chan, NULL);
  if (chan == GST_V4L_TUNER_CHANNEL (channel)->index) {
    gst_v4l_set_frequency (v4lelement, v4lchannel->tuner, frequency);
  }
}

static gulong
gst_v4l_tuner_get_frequency (GstTuner * tuner, GstTunerChannel * channel)
{
  GstV4lElement *v4lelement = GST_V4LELEMENT (tuner);
  GstV4lTunerChannel *v4lchannel = GST_V4L_TUNER_CHANNEL (channel);
  gint chan;
  gulong frequency = 0;

  /* assert that we're opened and that we're using a known item */
  g_return_val_if_fail (GST_V4L_IS_OPEN (v4lelement), 0);
  g_return_val_if_fail (GST_TUNER_CHANNEL_HAS_FLAG (channel,
          GST_TUNER_CHANNEL_FREQUENCY), 0);
  g_return_val_if_fail (gst_v4l_tuner_contains_channel (v4lelement,
          v4lchannel), 0);

  gst_v4l_get_chan_norm (v4lelement, &chan, NULL);
  if (chan == GST_V4L_TUNER_CHANNEL (channel)->index) {
    gst_v4l_get_frequency (v4lelement, v4lchannel->tuner, &frequency);
  }

  return frequency;
}

static gint
gst_v4l_tuner_signal_strength (GstTuner * tuner, GstTunerChannel * channel)
{
  GstV4lElement *v4lelement = GST_V4LELEMENT (tuner);
  GstV4lTunerChannel *v4lchannel = GST_V4L_TUNER_CHANNEL (channel);
  gint chan;
  gint signal = 0;

  /* assert that we're opened and that we're using a known item */
  g_return_val_if_fail (GST_V4L_IS_OPEN (v4lelement), 0);
  g_return_val_if_fail (GST_TUNER_CHANNEL_HAS_FLAG (channel,
          GST_TUNER_CHANNEL_FREQUENCY), 0);
  g_return_val_if_fail (gst_v4l_tuner_contains_channel (v4lelement,
          v4lchannel), 0);

  gst_v4l_get_chan_norm (v4lelement, &chan, NULL);
  if (chan == GST_V4L_TUNER_CHANNEL (channel)->index &&
      GST_TUNER_CHANNEL_HAS_FLAG (channel, GST_TUNER_CHANNEL_FREQUENCY)) {
    gst_v4l_get_signal (v4lelement, v4lchannel->tuner, &signal);
  }

  return signal;
}
