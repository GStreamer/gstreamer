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

    GstController *controller;
    controller = gst_object_control_properties (G_OBJECT (b_pad), "alpha",
        NULL);
    GstControlSource *control_source;
    control_source =
        GST_CONTROL_SOURCE (gst_interpolation_control_source_new ());
    gst_controller_set_control_source (controller, "alpha", control_source);

    self->controller = controller;
    self->control_source = control_source;

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
