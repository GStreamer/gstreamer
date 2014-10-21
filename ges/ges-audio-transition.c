/* GStreamer Editing Services
 * Copyright (C) 2010 Brandon Lewis <brandon.lewis@collabora.co.uk>
 *               2010 Nokia Corporation
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:gesaudiotransition
 * @short_description: implements audio crossfade transition
 */

#include "ges-internal.h"
#include "ges-track-element.h"
#include "ges-audio-transition.h"

#include <gst/controller/gstdirectcontrolbinding.h>

G_DEFINE_TYPE (GESAudioTransition, ges_audio_transition, GES_TYPE_TRANSITION);

struct _GESAudioTransitionPrivate
{
  /* these enable volume interpolation. Unlike video, both inputs are adjusted
   * simultaneously */
  GstControlSource *a_control_source;

  GstControlSource *b_control_source;

};

enum
{
  PROP_0,
};


#define fast_element_link(a,b) gst_element_link_pads_full((a),"src",(b),"sink",GST_PAD_LINK_CHECK_NOTHING)

static void
ges_audio_transition_duration_changed (GESTrackElement * self, guint64);

static GstElement *ges_audio_transition_create_element (GESTrackElement * self);

static void ges_audio_transition_dispose (GObject * object);

static void ges_audio_transition_finalize (GObject * object);

static void ges_audio_transition_get_property (GObject * object, guint
    property_id, GValue * value, GParamSpec * pspec);

static void ges_audio_transition_set_property (GObject * object, guint
    property_id, const GValue * value, GParamSpec * pspec);

static void
duration_changed_cb (GESTrackElement * self, GParamSpec * arg G_GNUC_UNUSED)
{
  ges_audio_transition_duration_changed (self,
      ges_timeline_element_get_duration (GES_TIMELINE_ELEMENT (self)));
}

static void
ges_audio_transition_class_init (GESAudioTransitionClass * klass)
{
  GObjectClass *object_class;
  GESTrackElementClass *toclass;

  g_type_class_add_private (klass, sizeof (GESAudioTransitionPrivate));

  object_class = G_OBJECT_CLASS (klass);
  toclass = GES_TRACK_ELEMENT_CLASS (klass);

  object_class->get_property = ges_audio_transition_get_property;
  object_class->set_property = ges_audio_transition_set_property;
  object_class->dispose = ges_audio_transition_dispose;
  object_class->finalize = ges_audio_transition_finalize;

  toclass->create_element = ges_audio_transition_create_element;

}

static void
ges_audio_transition_init (GESAudioTransition * self)
{

  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_AUDIO_TRANSITION, GESAudioTransitionPrivate);
}

static void
ges_audio_transition_dispose (GObject * object)
{
  GESAudioTransition *self;

  self = GES_AUDIO_TRANSITION (object);

  if (self->priv->a_control_source) {
    if (self->priv->a_control_source)
      gst_object_unref (self->priv->a_control_source);
    self->priv->a_control_source = NULL;
  }

  if (self->priv->b_control_source) {
    if (self->priv->b_control_source)
      gst_object_unref (self->priv->b_control_source);
    self->priv->b_control_source = NULL;
  }

  g_signal_handlers_disconnect_by_func (GES_TRACK_ELEMENT (self),
      duration_changed_cb, NULL);

  G_OBJECT_CLASS (ges_audio_transition_parent_class)->dispose (object);
}

static void
ges_audio_transition_finalize (GObject * object)
{
  G_OBJECT_CLASS (ges_audio_transition_parent_class)->finalize (object);
}

static void
ges_audio_transition_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_audio_transition_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static GObject *
link_element_to_mixer_with_volume (GstBin * bin, GstElement * element,
    GstElement * mixer)
{
  GstElement *volume = gst_element_factory_make ("volume", NULL);
  GstElement *resample = gst_element_factory_make ("audioresample", NULL);

  gst_bin_add (bin, volume);
  gst_bin_add (bin, resample);

  if (!fast_element_link (element, volume) ||
      !fast_element_link (volume, resample) ||
      !gst_element_link_pads_full (resample, "src", mixer, "sink_%u",
          GST_PAD_LINK_CHECK_NOTHING))
    GST_ERROR_OBJECT (bin, "Error linking volume to mixer");

  return G_OBJECT (volume);
}

static GstElement *
ges_audio_transition_create_element (GESTrackElement * track_element)
{
  GESAudioTransition *self;
  GstElement *topbin, *iconva, *iconvb, *oconv;
  GObject *atarget, *btarget = NULL;
  const gchar *propname = "volume";
  GstElement *mixer = NULL;
  GstPad *sinka_target, *sinkb_target, *src_target, *sinka, *sinkb, *src;
  guint64 duration;
  GstControlSource *acontrol_source, *bcontrol_source;

  self = GES_AUDIO_TRANSITION (track_element);

  GST_LOG ("creating an audio bin");

  topbin = gst_bin_new ("transition-bin");
  iconva = gst_element_factory_make ("audioconvert", "tr-aconv-a");
  iconvb = gst_element_factory_make ("audioconvert", "tr-aconv-b");
  oconv = gst_element_factory_make ("audioconvert", "tr-aconv-output");

  gst_bin_add_many (GST_BIN (topbin), iconva, iconvb, oconv, NULL);

  mixer = gst_element_factory_make ("audiomixer", NULL);
  gst_bin_add (GST_BIN (topbin), mixer);

  atarget = link_element_to_mixer_with_volume (GST_BIN (topbin), iconva, mixer);
  btarget = link_element_to_mixer_with_volume (GST_BIN (topbin), iconvb, mixer);

  g_assert (atarget && btarget);

  fast_element_link (mixer, oconv);

  sinka_target = gst_element_get_static_pad (iconva, "sink");
  sinkb_target = gst_element_get_static_pad (iconvb, "sink");
  src_target = gst_element_get_static_pad (oconv, "src");

  sinka = gst_ghost_pad_new ("sinka", sinka_target);
  sinkb = gst_ghost_pad_new ("sinkb", sinkb_target);
  src = gst_ghost_pad_new ("src", src_target);

  gst_element_add_pad (topbin, src);
  gst_element_add_pad (topbin, sinka);
  gst_element_add_pad (topbin, sinkb);

  /* set up interpolation */

  gst_object_unref (sinka_target);
  gst_object_unref (sinkb_target);
  gst_object_unref (src_target);

  acontrol_source = gst_interpolation_control_source_new ();
  g_object_set (acontrol_source, "mode", GST_INTERPOLATION_MODE_LINEAR, NULL);

  bcontrol_source = gst_interpolation_control_source_new ();
  g_object_set (bcontrol_source, "mode", GST_INTERPOLATION_MODE_LINEAR, NULL);

  self->priv->a_control_source = acontrol_source;
  self->priv->b_control_source = bcontrol_source;

  duration =
      ges_timeline_element_get_duration (GES_TIMELINE_ELEMENT (track_element));
  ges_audio_transition_duration_changed (track_element, duration);

  g_signal_connect (track_element, "notify::duration",
      G_CALLBACK (duration_changed_cb), NULL);

  gst_object_add_control_binding (GST_OBJECT (atarget),
      gst_direct_control_binding_new (GST_OBJECT (atarget), propname,
          acontrol_source));
  gst_object_add_control_binding (GST_OBJECT (btarget),
      gst_direct_control_binding_new (GST_OBJECT (btarget), propname,
          bcontrol_source));

  self->priv->a_control_source = acontrol_source;
  self->priv->b_control_source = bcontrol_source;

  return topbin;
}

static void
ges_audio_transition_duration_changed (GESTrackElement * track_element,
    guint64 duration)
{
  GESAudioTransition *self;
  GstElement *nleobj = ges_track_element_get_nleobject (track_element);
  GstTimedValueControlSource *ta, *tb;

  self = GES_AUDIO_TRANSITION (track_element);

  GST_INFO ("updating controller: nleobj (%p)", nleobj);

  if (G_UNLIKELY ((!self->priv->a_control_source ||
              !self->priv->b_control_source)))
    return;

  GST_INFO ("setting values on controller");
  ta = GST_TIMED_VALUE_CONTROL_SOURCE (self->priv->a_control_source);
  tb = GST_TIMED_VALUE_CONTROL_SOURCE (self->priv->b_control_source);

  gst_timed_value_control_source_unset_all (ta);
  gst_timed_value_control_source_unset_all (tb);
  /* The volume property goes from 0 to 10, so we want to interpolate between
   * 0 and 0.1 */
  gst_timed_value_control_source_set (ta, 0, 0.1);
  gst_timed_value_control_source_set (ta, duration, 0.0);

  gst_timed_value_control_source_set (tb, 0, 0.0);
  gst_timed_value_control_source_set (tb, duration, 0.1);

  GST_INFO ("done updating controller");
}

/**
 * ges_audio_transition_new:
 *
 * Creates a new #GESAudioTransition.
 *
 * Returns: The newly created #GESAudioTransition.
 */
GESAudioTransition *
ges_audio_transition_new (void)
{
  return g_object_new (GES_TYPE_AUDIO_TRANSITION, "track-type",
      GES_TRACK_TYPE_AUDIO, NULL);
}
