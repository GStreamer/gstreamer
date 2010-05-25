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

#include "ges-internal.h"
#include "ges-track-object.h"
#include "ges-track-transition.h"

G_DEFINE_TYPE (GESTrackTransition, ges_track_transition, GES_TYPE_TRACK_OBJECT);

static void
ges_track_transition_update_controller (GESTrackTransition * self,
    GstElement * gnlobj)
{
  GST_LOG ("updating controller");

  if (!gnlobj)
    return;

  if (!(self->controller))
    return;

  GST_LOG ("getting properties");
  guint64 duration;
  g_object_get (G_OBJECT (gnlobj), "duration", (guint64 *) & duration, NULL);

  GST_INFO ("duration: %d\n", duration);

  GValue start_value = { 0, };
  GValue end_value = { 0, };
  g_value_init (&start_value, G_TYPE_DOUBLE);
  g_value_init (&end_value, G_TYPE_DOUBLE);
  g_value_set_double (&start_value, 0.0);
  g_value_set_double (&end_value, 1.0);

  GST_LOG ("setting values on controller");

  g_assert (GST_IS_CONTROLLER (self->controller));
  g_assert (GST_IS_CONTROL_SOURCE (self->control_source));

  gst_interpolation_control_source_unset_all (self->control_source);
  gst_interpolation_control_source_set (self->control_source, 0, &start_value);
  gst_interpolation_control_source_set (self->control_source,
      duration, &end_value);

  GST_LOG ("done updating controller");
}

static void
gnlobject_duration_cb (GstElement * gnlobject, GParamSpec * arg
    G_GNUC_UNUSED, GESTrackTransition * self)
{
  ges_track_transition_update_controller (self, gnlobject);
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
  if (self->controller) {
    g_object_unref (self->controller);
    self->controller = NULL;
    self->control_source = NULL;
  }

  G_OBJECT_CLASS (ges_track_transition_parent_class)->dispose (object);
}

static void
ges_track_transition_finalize (GObject * object)
{
  G_OBJECT_CLASS (ges_track_transition_parent_class)->dispose (object);
}

GstPad *
link_element_to_mixer (GstElement * element, GstElement * mixer)
{
  GstPad *sinkpad = gst_element_get_request_pad (mixer, "sink_%d");
  GstPad *srcpad = gst_element_get_static_pad (element, "src");

  g_assert (sinkpad);
  g_assert (srcpad);

  gst_pad_link (srcpad, sinkpad);

  return sinkpad;
}

static gboolean
ges_track_transition_create_gnl_object (GESTrackObject * object)
{
  GESTrackTransition *self = GES_TRACK_TRANSITION (object);

  object->gnlobject = gst_element_factory_make ("gnloperation",
      "transition-operation");
  g_object_set (object->gnlobject, "priority", 0, NULL);
  g_signal_connect (G_OBJECT (object->gnlobject), "notify::duration",
      G_CALLBACK (gnlobject_duration_cb), object);

  if ((object->track->type) == GES_TRACK_TYPE_VIDEO) {
    GstElement *topbin = gst_bin_new ("transition-bin");
    GstElement *iconva = gst_element_factory_make ("ffmpegcolorspace",
        "tr-csp-a");
    GstElement *iconvb = gst_element_factory_make ("ffmpegcolorspace",
        "tr-csp-b");
    GstElement *oconv = gst_element_factory_make ("ffmpegcolorspace",
        "tr-csp-output");
    GstElement *mixer = gst_element_factory_make ("videomixer", NULL);

    gst_bin_add_many (GST_BIN (topbin), iconva, iconvb, mixer, oconv, NULL);
    GstPad *a_pad = link_element_to_mixer (iconva, mixer);
    GstPad *b_pad = link_element_to_mixer (iconvb, mixer);
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

    gst_bin_add (GST_BIN (object->gnlobject), topbin);

    /* set up interpolation */

    g_object_set (G_OBJECT (b_pad), "alpha", (gfloat) 0.0, NULL);

    GstController *controller;
    controller = gst_object_control_properties (G_OBJECT (b_pad), "alpha",
        NULL);
    GstInterpolationControlSource *control_source;
    control_source = gst_interpolation_control_source_new ();
    gst_controller_set_control_source (controller,
        "alpha", GST_CONTROL_SOURCE (control_source));
    gst_interpolation_control_source_set_interpolation_mode (control_source,
        GST_INTERPOLATE_LINEAR);

    self->controller = controller;
    self->control_source = control_source;

    GST_LOG ("controller created, updating");
    ges_track_transition_update_controller (self, object->gnlobject);

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
  self->controller = NULL;
  self->control_source = NULL;
}

GESTrackTransition *
ges_track_transition_new (void)
{
  return g_object_new (GES_TYPE_TRACK_TRANSITION, NULL);
}
