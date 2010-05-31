/* GStreamer Editing Services
 * Copyright (C) 2010 Brandon Lewis <brandon@collabora.co.uk>
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
 * SECTION:ges-track-transition
 * @short_description: Concrete, track-level implemenation of audio and video
 * transitinos.
 */

#include "ges-internal.h"
#include "ges-track-object.h"
#include "ges-track-transition.h"

G_DEFINE_TYPE (GESTrackTransition, ges_track_transition, GES_TYPE_TRACK_OBJECT);

static void
ges_track_transition_update_vcontroller (GESTrackTransition * self,
    GstElement * gnlobj)
{
  GST_LOG ("updating controller");

  if (!gnlobj)
    return;

  if (!(self->vcontroller))
    return;

  GST_LOG ("getting properties");
  guint64 duration;
  g_object_get (G_OBJECT (gnlobj), "duration", (guint64 *) & duration, NULL);

  GST_INFO ("duration: %d\n", duration);

  GValue start_value = { 0, };
  GValue end_value = { 0, };
  g_value_init (&start_value, G_TYPE_DOUBLE);
  g_value_init (&end_value, G_TYPE_DOUBLE);
  g_value_set_double (&start_value, self->vstart_value);
  g_value_set_double (&end_value, self->vend_value);

  GST_LOG ("setting values on controller");

  g_assert (GST_IS_CONTROLLER (self->vcontroller));
  g_assert (GST_IS_CONTROL_SOURCE (self->vcontrol_source));

  gst_interpolation_control_source_unset_all (self->vcontrol_source);
  gst_interpolation_control_source_set (self->vcontrol_source, 0, &start_value);
  gst_interpolation_control_source_set (self->vcontrol_source,
      duration, &end_value);

  GST_LOG ("done updating controller");
}

static void
ges_track_transition_update_acontroller (GESTrackTransition * self,
    GstElement * gnlobj)
{
  GST_LOG ("updating controller: gnlobj (%p) acontroller(%p) bcontroller(%p)",
      self->a_acontroller, self->a_bcontroller);

  if (!gnlobj)
    return;

  if (!(self->a_acontroller) || !(self->a_bcontroller))
    return;

  GST_LOG ("getting properties");
  guint64 duration;
  g_object_get (G_OBJECT (gnlobj), "duration", (guint64 *) & duration, NULL);

  GST_INFO ("duration: %lud\n", duration);

  GValue zero = { 0, };
  GValue one = { 0, };
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

static void
gnlobject_duration_cb (GstElement * gnlobject, GParamSpec * arg
    G_GNUC_UNUSED, GESTrackTransition * self)
{
  GESTrackType type = ((GESTrackObject *) self)->track->type;
  GST_LOG ("got duration changed signal");

  if (type == GES_TRACK_TYPE_VIDEO)
    ges_track_transition_update_vcontroller (self, gnlobject);
  else if (type == GES_TRACK_TYPE_AUDIO) {
    GST_LOG ("transition is an audio transition");
    ges_track_transition_update_acontroller (self, gnlobject);
  }
}

static void
ges_track_transition_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_track_transition_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_track_transition_dispose (GObject * object)
{
  GESTrackTransition *self = GES_TRACK_TRANSITION (object);
  if (self->vcontroller) {
    g_object_unref (self->vcontroller);
    self->vcontroller = NULL;
    /* is this referenec owned by someone other than us? */
    self->vcontrol_source = NULL;
  }

  if (self->a_acontroller) {
    g_object_unref (self->a_acontroller);
    self->a_acontroller = NULL;
    self->a_acontrol_source = NULL;
  }

  if (self->a_bcontroller) {
    g_object_unref (self->a_acontroller);
    self->a_bcontroller = NULL;
    self->a_bcontrol_source = NULL;
  }

  G_OBJECT_CLASS (ges_track_transition_parent_class)->dispose (object);
}

static void
ges_track_transition_finalize (GObject * object)
{
  G_OBJECT_CLASS (ges_track_transition_parent_class)->finalize (object);
}

static GObject *
link_element_to_mixer (GstElement * element, GstElement * mixer)
{
  GstPad *sinkpad = gst_element_get_request_pad (mixer, "sink_%d");
  GstPad *srcpad = gst_element_get_static_pad (element, "src");

  g_assert (sinkpad);
  g_assert (srcpad);

  gst_pad_link (srcpad, sinkpad);

  return G_OBJECT (sinkpad);
}

static GObject *
link_element_to_mixer_with_smpte (GstBin * bin, GstElement * element,
    GstElement * mixer, GEnumValue * type)
{
  GstElement *smptealpha = gst_element_factory_make ("smptealpha", NULL);
  g_object_set (G_OBJECT (smptealpha),
      "type", (gint) type->value, "invert", (gboolean) TRUE, NULL);
  gst_bin_add (bin, smptealpha);

  gst_element_link_many (element, smptealpha, mixer, NULL);

  return G_OBJECT (smptealpha);
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
create_video_bin (GESTrackTransition * self)
{
  GST_LOG ("creating a video bin");

  GstElement *topbin = gst_bin_new ("transition-bin");
  GstElement *iconva = gst_element_factory_make ("ffmpegcolorspace",
      "tr-csp-a");
  GstElement *iconvb = gst_element_factory_make ("ffmpegcolorspace",
      "tr-csp-b");
  GstElement *oconv = gst_element_factory_make ("ffmpegcolorspace",
      "tr-csp-output");

  gst_bin_add_many (GST_BIN (topbin), iconva, iconvb, oconv, NULL);

  GObject *target = NULL;
  gchar *propname = NULL;
  GstElement *mixer = NULL;

  mixer = gst_element_factory_make ("videomixer", NULL);
  g_object_set (G_OBJECT (mixer), "background", 1, NULL);
  gst_bin_add (GST_BIN (topbin), mixer);

  if (self->vtype) {
    link_element_to_mixer_with_smpte (GST_BIN (topbin), iconva, mixer,
        self->vtype);
    target = link_element_to_mixer_with_smpte (GST_BIN (topbin), iconvb,
        mixer, self->vtype);
    propname = "position";
    self->vstart_value = 1.0;
    self->vend_value = 0.0;
  } else {
    link_element_to_mixer (iconva, mixer);
    target = link_element_to_mixer (iconvb, mixer);
    propname = "alpha";
    self->vstart_value = 0.0;
    self->vend_value = 1.0;
  }

  gst_element_link (mixer, oconv);

  GstPad *sinka_target = gst_element_get_static_pad (iconva, "sink");
  GstPad *sinkb_target = gst_element_get_static_pad (iconvb, "sink");
  GstPad *src_target = gst_element_get_static_pad (oconv, "src");

  GstPad *sinka = gst_ghost_pad_new ("sinka", sinka_target);
  GstPad *sinkb = gst_ghost_pad_new ("sinkb", sinkb_target);
  GstPad *src = gst_ghost_pad_new ("src", src_target);

  gst_element_add_pad (topbin, src);
  gst_element_add_pad (topbin, sinka);
  gst_element_add_pad (topbin, sinkb);

  /* set up interpolation */

  g_object_set (target, propname, (gfloat) 0.0, NULL);

  GstController *controller;
  controller = gst_object_control_properties (target, propname, NULL);
  GstInterpolationControlSource *control_source;
  control_source = gst_interpolation_control_source_new ();
  gst_controller_set_control_source (controller,
      propname, GST_CONTROL_SOURCE (control_source));
  gst_interpolation_control_source_set_interpolation_mode (control_source,
      GST_INTERPOLATE_LINEAR);

  self->vcontroller = controller;
  self->vcontrol_source = control_source;

  GST_LOG ("controller created, updating");
  ges_track_transition_update_vcontroller (self,
      ((GESTrackObject *) self)->gnlobject);

  return topbin;
}

static GstElement *
create_audio_bin (GESTrackTransition * self)
{
  GST_LOG ("creating an audio bin");

  GstElement *topbin = gst_bin_new ("transition-bin");
  GstElement *iconva = gst_element_factory_make ("audioconvert",
      "tr-aconv-a");
  GstElement *iconvb = gst_element_factory_make ("audioconvert",
      "tr-aconv-b");
  GstElement *oconv = gst_element_factory_make ("audioconvert",
      "tr-aconv-output");

  gst_bin_add_many (GST_BIN (topbin), iconva, iconvb, oconv, NULL);

  GObject *atarget, *btarget = NULL;
  gchar *propname = "volume";
  GstElement *mixer = NULL;

  mixer = gst_element_factory_make ("adder", NULL);
  gst_bin_add (GST_BIN (topbin), mixer);

  atarget = link_element_to_mixer_with_volume (GST_BIN (topbin), iconva, mixer);
  btarget = link_element_to_mixer_with_volume (GST_BIN (topbin), iconvb, mixer);

  g_assert (atarget && btarget);

  gst_element_link (mixer, oconv);

  GstPad *sinka_target = gst_element_get_static_pad (iconva, "sink");
  GstPad *sinkb_target = gst_element_get_static_pad (iconvb, "sink");
  GstPad *src_target = gst_element_get_static_pad (oconv, "src");

  GstPad *sinka = gst_ghost_pad_new ("sinka", sinka_target);
  GstPad *sinkb = gst_ghost_pad_new ("sinkb", sinkb_target);
  GstPad *src = gst_ghost_pad_new ("src", src_target);

  gst_element_add_pad (topbin, src);
  gst_element_add_pad (topbin, sinka);
  gst_element_add_pad (topbin, sinkb);

  /* set up interpolation */

  //g_object_set(atarget, propname, (gdouble) 0, NULL);
  //g_object_set(btarget, propname, (gdouble) 0, NULL);

  GstController *acontroller, *bcontroller;

  acontroller = gst_object_control_properties (atarget, propname, NULL);
  bcontroller = gst_object_control_properties (btarget, propname, NULL);

  g_assert (acontroller && bcontroller);

  GstInterpolationControlSource *acontrol_source, *bcontrol_source;

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

  GST_LOG ("controllers created, updating");

  ges_track_transition_update_acontroller (self,
      ((GESTrackObject *) self)->gnlobject);

  return topbin;
}

static gboolean
ges_track_transition_create_gnl_object (GESTrackObject * object)
{
  GESTrackTransition *self = GES_TRACK_TRANSITION (object);

  static gint tnum = 0;

  gchar *name = g_strdup_printf ("transition-operation%d", tnum++);
  object->gnlobject = gst_element_factory_make ("gnloperation", name);
  g_free (name);

  g_object_set (object->gnlobject, "priority", 0, NULL);
  g_signal_connect (G_OBJECT (object->gnlobject), "notify::duration",
      G_CALLBACK (gnlobject_duration_cb), object);

  if ((object->track->type) == GES_TRACK_TYPE_VIDEO) {
    gst_bin_add (GST_BIN (object->gnlobject), create_video_bin (self));
    return TRUE;
  }

  else if ((object->track->type) == GES_TRACK_TYPE_AUDIO) {
    gst_bin_add (GST_BIN (object->gnlobject), create_audio_bin (self));
    return TRUE;
  }

  return FALSE;

}

static void
ges_track_transition_class_init (GESTrackTransitionClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GESTrackObjectClass *track_class = GES_TRACK_OBJECT_CLASS (klass);

  object_class->get_property = ges_track_transition_get_property;
  object_class->set_property = ges_track_transition_set_property;
  object_class->dispose = ges_track_transition_dispose;
  object_class->finalize = ges_track_transition_finalize;

  track_class->create_gnl_object = ges_track_transition_create_gnl_object;
}

static void
ges_track_transition_init (GESTrackTransition * self)
{
  self->vcontroller = NULL;
  self->vcontrol_source = NULL;
  self->vtype = NULL;
  self->vstart_value = 0.0;
  self->vend_value = 0.0;

  self->a_acontroller = NULL;
  self->a_acontrol_source = NULL;

  self->a_bcontroller = NULL;
  self->a_bcontrol_source = NULL;
}

GESTrackTransition *
ges_track_transition_new (GEnumValue * type)
{
  GESTrackTransition *ret = g_object_new (GES_TYPE_TRACK_TRANSITION, NULL);
  ret->vtype = type;

  return ret;
}
