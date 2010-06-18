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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:ges-track-audio-transition
 * @short_description: implements audio crossfade transitino
 */

#include "ges-internal.h"
#include "ges-track-object.h"
#include "ges-track-audio-transition.h"

G_DEFINE_TYPE (GESTrackAudioTransition, ges_track_audio_transition,
    GES_TYPE_TRACK_TRANSITION);

enum
{
  PROP_0,
};


static void
ges_track_audio_transition_duration_changed (GESTrackTransition * self,
    GstElement * gnlobj);

static GstElement *ges_track_audio_transition_create_element (GESTrackTransition
    * self, GESTrack * unused);

static void ges_track_audio_transition_dispose (GObject * object);

static void ges_track_audio_transition_finalize (GObject * object);

static void ges_track_audio_transition_get_property (GObject * object, guint
    property_id, GValue * value, GParamSpec * pspec);

static void ges_track_audio_transition_set_property (GObject * object, guint
    property_id, const GValue * value, GParamSpec * pspec);

static void
ges_track_audio_transition_class_init (GESTrackAudioTransitionClass * klass)
{
  GObjectClass *object_class;
  GESTrackTransitionClass *pclass;

  object_class = G_OBJECT_CLASS (klass);
  pclass = GES_TRACK_TRANSITION_CLASS (klass);

  object_class->get_property = ges_track_audio_transition_get_property;
  object_class->set_property = ges_track_audio_transition_set_property;
  object_class->dispose = ges_track_audio_transition_dispose;
  object_class->finalize = ges_track_audio_transition_finalize;

  pclass->create_element = ges_track_audio_transition_create_element;
  pclass->duration_changed = ges_track_audio_transition_duration_changed;

}

static void
ges_track_audio_transition_init (GESTrackAudioTransition * self)
{
}

static void
ges_track_audio_transition_dispose (GObject * object)
{
  G_OBJECT_CLASS (ges_track_audio_transition_parent_class)->dispose (object);
}

static void
ges_track_audio_transition_finalize (GObject * object)
{
  G_OBJECT_CLASS (ges_track_audio_transition_parent_class)->finalize (object);
}

static void
ges_track_audio_transition_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_track_audio_transition_set_property (GObject * object,
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
  gst_bin_add (bin, volume);

  gst_element_link_many (element, volume, mixer, NULL);

  return G_OBJECT (volume);
}

static GstElement *
ges_track_audio_transition_create_element (GESTrackTransition * self,
    GESTrack * unded)
{
  GstElement *topbin, *iconva, *iconvb, *oconv;
  GObject *atarget, *btarget = NULL;
  const gchar *propname = "volume";
  GstElement *mixer = NULL;
  GstPad *sinka_target, *sinkb_target, *src_target, *sinka, *sinkb, *src;
  GstController *acontroller, *bcontroller;
  GstInterpolationControlSource *acontrol_source, *bcontrol_source;


  GST_LOG ("creating an audio bin");

  topbin = gst_bin_new ("transition-bin");
  iconva = gst_element_factory_make ("audioconvert", "tr-aconv-a");
  iconvb = gst_element_factory_make ("audioconvert", "tr-aconv-b");
  oconv = gst_element_factory_make ("audioconvert", "tr-aconv-output");

  gst_bin_add_many (GST_BIN (topbin), iconva, iconvb, oconv, NULL);

  mixer = gst_element_factory_make ("adder", NULL);
  gst_bin_add (GST_BIN (topbin), mixer);

  atarget = link_element_to_mixer_with_volume (GST_BIN (topbin), iconva, mixer);
  btarget = link_element_to_mixer_with_volume (GST_BIN (topbin), iconvb, mixer);

  g_assert (atarget && btarget);

  gst_element_link (mixer, oconv);

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


  //g_object_set(atarget, propname, (gdouble) 0, NULL);
  //g_object_set(btarget, propname, (gdouble) 0, NULL);

  acontroller = gst_object_control_properties (atarget, propname, NULL);
  bcontroller = gst_object_control_properties (btarget, propname, NULL);

  g_assert (acontroller && bcontroller);

  acontrol_source = gst_interpolation_control_source_new ();
  gst_controller_set_control_source (acontroller,
      propname, GST_CONTROL_SOURCE (acontrol_source));
  gst_interpolation_control_source_set_interpolation_mode (acontrol_source,
      GST_INTERPOLATE_LINEAR);

  bcontrol_source = gst_interpolation_control_source_new ();
  gst_controller_set_control_source (bcontroller,
      propname, GST_CONTROL_SOURCE (bcontrol_source));
  gst_interpolation_control_source_set_interpolation_mode (bcontrol_source,
      GST_INTERPOLATE_LINEAR);

  self->a_acontroller = acontroller;
  self->a_bcontroller = bcontroller;
  self->a_acontrol_source = acontrol_source;
  self->a_bcontrol_source = bcontrol_source;

  return topbin;
}

static void
ges_track_audio_transition_duration_changed (GESTrackTransition * self,
    GstElement * gnlobj)
{
  guint64 duration;
  GValue zero = { 0, };
  GValue one = { 0, };

  GST_LOG ("updating controller: gnlobj (%p) acontroller(%p) bcontroller(%p)",
      gnlobj, self->a_acontroller, self->a_bcontroller);

  if (!gnlobj)
    return;

  if (!(self->a_acontroller) || !(self->a_bcontroller))
    return;

  GST_LOG ("getting properties");
  g_object_get (G_OBJECT (gnlobj), "duration", (guint64 *) & duration, NULL);

  GST_INFO ("duration: %lud\n", duration);
  g_value_init (&zero, G_TYPE_DOUBLE);
  g_value_init (&one, G_TYPE_DOUBLE);
  g_value_set_double (&zero, 0.0);
  g_value_set_double (&one, 1.0);

  GST_LOG ("setting values on controller");

  g_assert (GST_IS_CONTROLLER (self->a_acontroller));
  g_assert (GST_IS_CONTROL_SOURCE (self->a_acontrol_source));

  g_assert (GST_IS_CONTROLLER (self->a_bcontroller));
  g_assert (GST_IS_CONTROL_SOURCE (self->a_bcontrol_source));

  gst_interpolation_control_source_unset_all (self->a_acontrol_source);
  gst_interpolation_control_source_set (self->a_acontrol_source, 0, &one);
  gst_interpolation_control_source_set (self->a_acontrol_source,
      duration, &zero);

  gst_interpolation_control_source_unset_all (self->a_bcontrol_source);
  gst_interpolation_control_source_set (self->a_bcontrol_source, 0, &zero);
  gst_interpolation_control_source_set (self->a_bcontrol_source,
      duration, &one);

  GST_LOG ("done updating controller");
}

GESTrackAudioTransition *
ges_track_audio_transition_new (void)
{
  return g_object_new (GES_TYPE_TRACK_AUDIO_TRANSITION, NULL);
}
