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
 * SECTION:ges-track-video-transition
 * @short_description: implements video crossfade transitino
 */

#include "ges-internal.h"
#include "ges-track-object.h"
#include "ges-timeline-transition.h"
#include "ges-track-video-transition.h"

G_DEFINE_TYPE (GESTrackVideoTransition, ges_track_video_transition,
    GES_TYPE_TRACK_TRANSITION);

enum
{
  PROP_0,
};

static GObject *link_element_to_mixer (GstElement * element,
    GstElement * mixer);

static GObject *link_element_to_mixer_with_smpte (GstBin * bin,
    GstElement * element, GstElement * mixer, gint type,
    GstElement ** smpteref);

static void
ges_track_video_transition_duration_changed (GESTrackTransition * self,
    GstElement * gnlobj);

static GstElement *ges_track_video_transition_create_element (GESTrackTransition
    * self);

static void ges_track_video_transition_dispose (GObject * object);

static void ges_track_video_transition_finalize (GObject * object);

static void ges_track_video_transition_get_property (GObject * object, guint
    property_id, GValue * value, GParamSpec * pspec);

static void ges_track_video_transition_set_property (GObject * object, guint
    property_id, const GValue * value, GParamSpec * pspec);

static void
ges_track_video_transition_class_init (GESTrackVideoTransitionClass * klass)
{
  GObjectClass *object_class;
  GESTrackTransitionClass *pclass;

  object_class = G_OBJECT_CLASS (klass);
  pclass = GES_TRACK_TRANSITION_CLASS (klass);

  object_class->get_property = ges_track_video_transition_get_property;
  object_class->set_property = ges_track_video_transition_set_property;
  object_class->dispose = ges_track_video_transition_dispose;
  object_class->finalize = ges_track_video_transition_finalize;

  pclass->create_element = ges_track_video_transition_create_element;
  pclass->duration_changed = ges_track_video_transition_duration_changed;
}

static void
ges_track_video_transition_init (GESTrackVideoTransition * self)
{
  self->controller = NULL;
  self->control_source = NULL;
  self->smpte = NULL;
  self->mixer = NULL;
  self->sinka = NULL;
  self->sinkb = NULL;
  self->type = 0;
  self->start_value = 0.0;
  self->end_value = 0.0;
}

static void
ges_track_video_transition_dispose (GObject * object)
{
  GESTrackVideoTransition *self = GES_TRACK_VIDEO_TRANSITION (object);

  GST_DEBUG ("disposing");
  GST_LOG ("mixer: %p smpte: %p sinka: %p sinkb: %p",
      self->mixer, self->smpte, self->sinka, self->sinkb);

  if (self->controller) {
    g_object_unref (self->controller);
    self->controller = NULL;
    if (self->control_source)
      gst_object_unref (self->control_source);
    self->control_source = NULL;
  }

  if (self->sinka && self->sinkb) {
    GST_DEBUG ("releasing request pads for mixer");
    gst_element_release_request_pad (self->mixer, self->sinka);
    gst_element_release_request_pad (self->mixer, self->sinkb);
    gst_object_unref (self->sinka);
    gst_object_unref (self->sinkb);
    self->sinka = NULL;
    self->sinkb = NULL;
  }

  if (self->mixer) {
    GST_LOG ("unrefing mixer");
    gst_object_unref (self->mixer);
    self->mixer = NULL;
  }

  G_OBJECT_CLASS (ges_track_video_transition_parent_class)->dispose (object);
}

static void
ges_track_video_transition_finalize (GObject * object)
{
  G_OBJECT_CLASS (ges_track_video_transition_parent_class)->finalize (object);
}

static void
ges_track_video_transition_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_track_video_transition_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static GstElement *
ges_track_video_transition_create_element (GESTrackTransition * object)
{
  GstElement *topbin, *iconva, *iconvb, *oconv;
  GObject *target = NULL;
  const gchar *propname = NULL;
  GstElement *mixer = NULL;
  GstPad *sinka_target, *sinkb_target, *src_target, *sinka, *sinkb, *src;
  GstController *controller;
  GstInterpolationControlSource *control_source;
  GESTrackVideoTransition *self;

  self = GES_TRACK_VIDEO_TRANSITION (object);

  GST_LOG ("creating a video bin");

  topbin = gst_bin_new ("transition-bin");
  iconva = gst_element_factory_make ("ffmpegcolorspace", "tr-csp-a");
  iconvb = gst_element_factory_make ("ffmpegcolorspace", "tr-csp-b");
  oconv = gst_element_factory_make ("ffmpegcolorspace", "tr-csp-output");

  gst_bin_add_many (GST_BIN (topbin), iconva, iconvb, oconv, NULL);
  mixer = gst_element_factory_make ("videomixer", NULL);
  g_object_set (G_OBJECT (mixer), "background", 1, NULL);
  gst_bin_add (GST_BIN (topbin), mixer);

  if (self->type != VTYPE_CROSSFADE) {
    link_element_to_mixer_with_smpte (GST_BIN (topbin), iconva, mixer,
        self->type, NULL);
    target = link_element_to_mixer_with_smpte (GST_BIN (topbin), iconvb,
        mixer, self->type, &self->smpte);
    propname = "position";
    self->start_value = 1.0;
    self->end_value = 0.0;
  } else {
    self->sinka = (GstPad *) link_element_to_mixer (iconva, mixer);
    self->sinkb = (GstPad *) link_element_to_mixer (iconvb, mixer);
    target = (GObject *) self->sinkb;
    self->mixer = gst_object_ref (mixer);
    propname = "alpha";
    self->start_value = 0.0;
    self->end_value = 1.0;
  }

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

  gst_object_unref (sinka_target);
  gst_object_unref (sinkb_target);
  gst_object_unref (src_target);

  /* set up interpolation */

  g_object_set (target, propname, (gfloat) 0.0, NULL);

  controller = gst_object_control_properties (target, propname, NULL);

  control_source = gst_interpolation_control_source_new ();
  gst_controller_set_control_source (controller,
      propname, GST_CONTROL_SOURCE (control_source));
  gst_interpolation_control_source_set_interpolation_mode (control_source,
      GST_INTERPOLATE_LINEAR);

  self->controller = controller;
  self->control_source = control_source;

  return topbin;
}

static GObject *
link_element_to_mixer (GstElement * element, GstElement * mixer)
{
  GstPad *sinkpad = gst_element_get_request_pad (mixer, "sink_%d");
  GstPad *srcpad = gst_element_get_static_pad (element, "src");

  g_assert (sinkpad);
  g_assert (srcpad);

  gst_pad_link (srcpad, sinkpad);
  gst_object_unref (srcpad);

  return G_OBJECT (sinkpad);
}

static GObject *
link_element_to_mixer_with_smpte (GstBin * bin, GstElement * element,
    GstElement * mixer, gint type, GstElement ** smpteref)
{
  GstElement *smptealpha = gst_element_factory_make ("smptealpha", NULL);
  g_object_set (G_OBJECT (smptealpha),
      "type", (gint) type, "invert", (gboolean) TRUE, NULL);
  gst_bin_add (bin, smptealpha);

  gst_element_link_many (element, smptealpha, mixer, NULL);

  /* crack */
  if (smpteref) {
    *smpteref = smptealpha;
  }

  return G_OBJECT (smptealpha);
}

static void
ges_track_video_transition_duration_changed (GESTrackTransition * object,
    GstElement * gnlobj)
{
  GValue start_value = { 0, };
  GValue end_value = { 0, };
  guint64 duration;
  GESTrackVideoTransition *self = GES_TRACK_VIDEO_TRANSITION (object);

  GST_LOG ("updating controller");

  if (!gnlobj)
    return;

  if (!(self->controller))
    return;

  GST_LOG ("getting properties");
  g_object_get (G_OBJECT (gnlobj), "duration", (guint64 *) & duration, NULL);

  GST_INFO ("duration: %d\n", duration);
  g_value_init (&start_value, G_TYPE_DOUBLE);
  g_value_init (&end_value, G_TYPE_DOUBLE);
  g_value_set_double (&start_value, self->start_value);
  g_value_set_double (&end_value, self->end_value);

  GST_LOG ("setting values on controller");

  g_assert (GST_IS_CONTROLLER (self->controller));
  g_assert (GST_IS_CONTROL_SOURCE (self->control_source));

  gst_interpolation_control_source_unset_all (self->control_source);
  gst_interpolation_control_source_set (self->control_source, 0, &start_value);
  gst_interpolation_control_source_set (self->control_source,
      duration, &end_value);

  GST_LOG ("done updating controller");
}

void
ges_track_video_transition_set_type (GESTrackVideoTransition * self, gint type)
{
  if (((type == VTYPE_CROSSFADE) && (self->type != VTYPE_CROSSFADE)) ||
      ((type != VTYPE_CROSSFADE) && (self->type = VTYPE_CROSSFADE))) {
    GST_WARNING
        ("Changing between 'crossfade' and other types is not supported\n");
  }

  self->type = type;
  if (self->smpte && (type != VTYPE_CROSSFADE))
    g_object_set (self->smpte, "type", (gint) type, NULL);
}

GESTrackVideoTransition *
ges_track_video_transition_new (void)
{
  return g_object_new (GES_TYPE_TRACK_VIDEO_TRANSITION, NULL);
}
