/* GStreamer Tuner interface implementation
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * gstv4l2tuner.c: tuner interface implementation for V4L2
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

#include "gstv4l2tuner.h"
#include "gstv4l2element.h"
#include "v4l2_calls.h"

static void gst_v4l2_tuner_channel_class_init (GstV4l2TunerChannelClass *
    klass);
static void gst_v4l2_tuner_channel_init (GstV4l2TunerChannel * channel);

static void gst_v4l2_tuner_norm_class_init (GstV4l2TunerNormClass * klass);
static void gst_v4l2_tuner_norm_init (GstV4l2TunerNorm * norm);

static const GList *gst_v4l2_tuner_list_channels (GstTuner * mixer);
static void gst_v4l2_tuner_set_channel (GstTuner * mixer,
    GstTunerChannel * channel);
static GstTunerChannel *gst_v4l2_tuner_get_channel (GstTuner * mixer);

static const GList *gst_v4l2_tuner_list_norms (GstTuner * mixer);
static void gst_v4l2_tuner_set_norm (GstTuner * mixer, GstTunerNorm * norm);
static GstTunerNorm *gst_v4l2_tuner_get_norm (GstTuner * mixer);

static void gst_v4l2_tuner_set_frequency (GstTuner * mixer,
    GstTunerChannel * channel, gulong frequency);
static gulong gst_v4l2_tuner_get_frequency (GstTuner * mixer,
    GstTunerChannel * channel);
static gint gst_v4l2_tuner_signal_strength (GstTuner * mixer,
    GstTunerChannel * channel);

static GstTunerNormClass *norm_parent_class = NULL;
static GstTunerChannelClass *channel_parent_class = NULL;

GType
gst_v4l2_tuner_channel_get_type (void)
{
  static GType gst_v4l2_tuner_channel_type = 0;

  if (!gst_v4l2_tuner_channel_type) {
    static const GTypeInfo v4l2_tuner_channel_info = {
      sizeof (GstV4l2TunerChannelClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_v4l2_tuner_channel_class_init,
      NULL,
      NULL,
      sizeof (GstV4l2TunerChannel),
      0,
      (GInstanceInitFunc) gst_v4l2_tuner_channel_init,
      NULL
    };

    gst_v4l2_tuner_channel_type =
	g_type_register_static (GST_TYPE_TUNER_CHANNEL,
	"GstV4l2TunerChannel", &v4l2_tuner_channel_info, 0);
  }

  return gst_v4l2_tuner_channel_type;
}

static void
gst_v4l2_tuner_channel_class_init (GstV4l2TunerChannelClass * klass)
{
  channel_parent_class = g_type_class_ref (GST_TYPE_TUNER_CHANNEL);
}

static void
gst_v4l2_tuner_channel_init (GstV4l2TunerChannel * channel)
{
  channel->index = 0;
  channel->tuner = 0;
  channel->audio = 0;
}

GType
gst_v4l2_tuner_norm_get_type (void)
{
  static GType gst_v4l2_tuner_norm_type = 0;

  if (!gst_v4l2_tuner_norm_type) {
    static const GTypeInfo v4l2_tuner_norm_info = {
      sizeof (GstV4l2TunerNormClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_v4l2_tuner_norm_class_init,
      NULL,
      NULL,
      sizeof (GstV4l2TunerNorm),
      0,
      (GInstanceInitFunc) gst_v4l2_tuner_norm_init,
      NULL
    };

    gst_v4l2_tuner_norm_type =
	g_type_register_static (GST_TYPE_TUNER_NORM,
	"GstV4l2TunerNorm", &v4l2_tuner_norm_info, 0);
  }

  return gst_v4l2_tuner_norm_type;
}

static void
gst_v4l2_tuner_norm_class_init (GstV4l2TunerNormClass * klass)
{
  norm_parent_class = g_type_class_ref (GST_TYPE_TUNER_NORM);
}

static void
gst_v4l2_tuner_norm_init (GstV4l2TunerNorm * norm)
{
  norm->index = 0;
}

void
gst_v4l2_tuner_interface_init (GstTunerClass * klass)
{
  /* default virtual functions */
  klass->list_channels = gst_v4l2_tuner_list_channels;
  klass->set_channel = gst_v4l2_tuner_set_channel;
  klass->get_channel = gst_v4l2_tuner_get_channel;

  klass->list_norms = gst_v4l2_tuner_list_norms;
  klass->set_norm = gst_v4l2_tuner_set_norm;
  klass->get_norm = gst_v4l2_tuner_get_norm;

  klass->set_frequency = gst_v4l2_tuner_set_frequency;
  klass->get_frequency = gst_v4l2_tuner_get_frequency;
  klass->signal_strength = gst_v4l2_tuner_signal_strength;
}

static gboolean
gst_v4l2_tuner_is_sink (GstV4l2Element * v4l2element)
{
  const GList *pads = gst_element_get_pad_list (GST_ELEMENT (v4l2element));
  GstPadDirection dir = GST_PAD_UNKNOWN;

  /* get direction */
  if (pads && g_list_length ((GList *) pads) == 1)
    dir = GST_PAD_DIRECTION (GST_PAD (pads->data));

  return (dir == GST_PAD_SINK);
}

static gboolean
gst_v4l2_tuner_contains_channel (GstV4l2Element * v4l2element,
    GstV4l2TunerChannel * v4l2channel)
{
  const GList *item;

  for (item = v4l2element->channels; item != NULL; item = item->next)
    if (item->data == v4l2channel)
      return TRUE;

  return FALSE;
}

static const GList *
gst_v4l2_tuner_list_channels (GstTuner * mixer)
{
  /* ... or output, if we're a sink... */
  return GST_V4L2ELEMENT (mixer)->channels;
}

static void
gst_v4l2_tuner_set_channel (GstTuner * mixer, GstTunerChannel * channel)
{
  GstV4l2Element *v4l2element = GST_V4L2ELEMENT (mixer);
  GstV4l2TunerChannel *v4l2channel = GST_V4L2_TUNER_CHANNEL (channel);

  /* assert that we're opened and that we're using a known item */
  g_return_if_fail (GST_V4L2_IS_OPEN (v4l2element));
  g_return_if_fail (gst_v4l2_tuner_contains_channel (v4l2element, v4l2channel));

  /* ... or output, if we're a sink... */
  if (gst_v4l2_tuner_is_sink (v4l2element) ?
      gst_v4l2_set_output (v4l2element, v4l2channel->index) :
      gst_v4l2_set_input (v4l2element, v4l2channel->index)) {
    gst_tuner_channel_changed (mixer, channel);
    g_object_notify (G_OBJECT (v4l2element), "channel");
  }
}

static GstTunerChannel *
gst_v4l2_tuner_get_channel (GstTuner * mixer)
{
  GstV4l2Element *v4l2element = GST_V4L2ELEMENT (mixer);
  GList *item;
  gint channel;

  /* assert that we're opened and that we're using a known item */
  g_return_val_if_fail (GST_V4L2_IS_OPEN (v4l2element), NULL);

  /* ... or output, if we're a sink... */
  if (gst_v4l2_tuner_is_sink (v4l2element))
    gst_v4l2_get_output (v4l2element, &channel);
  else
    gst_v4l2_get_input (v4l2element, &channel);

  for (item = v4l2element->channels; item != NULL; item = item->next) {
    if (channel == GST_V4L2_TUNER_CHANNEL (item->data)->index)
      return (GstTunerChannel *) item->data;
  }

  return NULL;
}

static gboolean
gst_v4l2_tuner_contains_norm (GstV4l2Element * v4l2element,
    GstV4l2TunerNorm * v4l2norm)
{
  const GList *item;

  for (item = v4l2element->norms; item != NULL; item = item->next)
    if (item->data == v4l2norm)
      return TRUE;

  return FALSE;
}

static const GList *
gst_v4l2_tuner_list_norms (GstTuner * mixer)
{
  return GST_V4L2ELEMENT (mixer)->norms;
}

static void
gst_v4l2_tuner_set_norm (GstTuner * mixer, GstTunerNorm * norm)
{
  GstV4l2Element *v4l2element = GST_V4L2ELEMENT (mixer);
  GstV4l2TunerNorm *v4l2norm = GST_V4L2_TUNER_NORM (norm);

  /* assert that we're opened and that we're using a known item */
  g_return_if_fail (GST_V4L2_IS_OPEN (v4l2element));
  g_return_if_fail (gst_v4l2_tuner_contains_norm (v4l2element, v4l2norm));

  if (gst_v4l2_set_norm (v4l2element, v4l2norm->index)) {
    gst_tuner_norm_changed (mixer, norm);
    g_object_notify (G_OBJECT (v4l2element), "norm");
  }
}

static GstTunerNorm *
gst_v4l2_tuner_get_norm (GstTuner * mixer)
{
  GstV4l2Element *v4l2element = GST_V4L2ELEMENT (mixer);
  GList *item;
  v4l2_std_id norm;

  /* assert that we're opened and that we're using a known item */
  g_return_val_if_fail (GST_V4L2_IS_OPEN (v4l2element), NULL);

  gst_v4l2_get_norm (v4l2element, &norm);

  for (item = v4l2element->norms; item != NULL; item = item->next) {
    if (norm == GST_V4L2_TUNER_NORM (item->data)->index)
      return (GstTunerNorm *) item->data;
  }

  return NULL;
}

static void
gst_v4l2_tuner_set_frequency (GstTuner * mixer,
    GstTunerChannel * channel, gulong frequency)
{
  GstV4l2Element *v4l2element = GST_V4L2ELEMENT (mixer);
  GstV4l2TunerChannel *v4l2channel = GST_V4L2_TUNER_CHANNEL (channel);
  gint chan;

  /* assert that we're opened and that we're using a known item */
  g_return_if_fail (GST_V4L2_IS_OPEN (v4l2element));
  g_return_if_fail (GST_TUNER_CHANNEL_HAS_FLAG (channel,
	  GST_TUNER_CHANNEL_FREQUENCY));
  g_return_if_fail (gst_v4l2_tuner_contains_channel (v4l2element, v4l2channel));

  gst_v4l2_get_input (v4l2element, &chan);
  if (chan == GST_V4L2_TUNER_CHANNEL (channel)->index &&
      GST_TUNER_CHANNEL_HAS_FLAG (channel, GST_TUNER_CHANNEL_FREQUENCY)) {
    if (gst_v4l2_set_frequency (v4l2element, v4l2channel->tuner, frequency)) {
      gst_tuner_frequency_changed (mixer, channel, frequency);
      g_object_notify (G_OBJECT (v4l2element), "frequency");
    }
  }
}

static gulong
gst_v4l2_tuner_get_frequency (GstTuner * mixer, GstTunerChannel * channel)
{
  GstV4l2Element *v4l2element = GST_V4L2ELEMENT (mixer);
  GstV4l2TunerChannel *v4l2channel = GST_V4L2_TUNER_CHANNEL (channel);
  gint chan;
  gulong frequency = 0;

  /* assert that we're opened and that we're using a known item */
  g_return_val_if_fail (GST_V4L2_IS_OPEN (v4l2element), 0);
  g_return_val_if_fail (GST_TUNER_CHANNEL_HAS_FLAG (channel,
	  GST_TUNER_CHANNEL_FREQUENCY), 0);
  g_return_val_if_fail (gst_v4l2_tuner_contains_channel (v4l2element,
	  v4l2channel), 0);

  gst_v4l2_get_input (v4l2element, &chan);
  if (chan == GST_V4L2_TUNER_CHANNEL (channel)->index &&
      GST_TUNER_CHANNEL_HAS_FLAG (channel, GST_TUNER_CHANNEL_FREQUENCY)) {
    gst_v4l2_get_frequency (v4l2element, v4l2channel->tuner, &frequency);
  }

  return frequency;
}

static gint
gst_v4l2_tuner_signal_strength (GstTuner * mixer, GstTunerChannel * channel)
{
  GstV4l2Element *v4l2element = GST_V4L2ELEMENT (mixer);
  GstV4l2TunerChannel *v4l2channel = GST_V4L2_TUNER_CHANNEL (channel);
  gint chan;
  gulong signal = 0;

  /* assert that we're opened and that we're using a known item */
  g_return_val_if_fail (GST_V4L2_IS_OPEN (v4l2element), 0);
  g_return_val_if_fail (GST_TUNER_CHANNEL_HAS_FLAG (channel,
	  GST_TUNER_CHANNEL_FREQUENCY), 0);
  g_return_val_if_fail (gst_v4l2_tuner_contains_channel (v4l2element,
	  v4l2channel), 0);

  gst_v4l2_get_input (v4l2element, &chan);
  if (chan == GST_V4L2_TUNER_CHANNEL (channel)->index &&
      GST_TUNER_CHANNEL_HAS_FLAG (channel, GST_TUNER_CHANNEL_FREQUENCY)) {
    gst_v4l2_signal_strength (v4l2element, v4l2channel->tuner, &signal);
  }

  return signal;
}
