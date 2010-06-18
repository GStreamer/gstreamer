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
#include "ges-timeline-transition.h"

G_DEFINE_TYPE (GESTrackTransition, ges_track_transition, GES_TYPE_TRACK_OBJECT);

GstElement *ges_track_transition_create_element (GESTrackTransition * self,
    GESTrack * track);

static void
gnlobject_duration_cb (GstElement * gnlobject, GParamSpec * arg
    G_GNUC_UNUSED, GESTrackTransition * self)
{
  GESTrackTransitionClass *klass;

  klass = GES_TRACK_TRANSITION_GET_CLASS (self);
  GST_LOG ("got duration changed signal");

  klass = GES_TRACK_TRANSITION_GET_CLASS (self);
  klass->duration_changed (self, gnlobject);
}

static void
ges_track_transition_duration_changed (GESTrackTransition * self, GstElement
    * gnlobject)
{
  GESTrackType type;
  type = ((GESTrackObject *) self)->track->type;

  GST_WARNING ("transitions don't handle this track type!");
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

  GST_DEBUG ("disposing");
  GST_LOG ("mixer: %p smpte: %p sinka: %p sinkb: %p",
      self->vmixer, self->vsmpte, self->sinka, self->sinkb);

  if (self->vcontroller) {
    g_object_unref (self->vcontroller);
    self->vcontroller = NULL;
    if (self->vcontrol_source)
      gst_object_unref (self->vcontrol_source);
    self->vcontrol_source = NULL;
  }

  if (self->vmixer && self->sinka && self->sinkb) {
    GST_DEBUG ("releasing request pads for vmixer");
    gst_element_release_request_pad (self->vmixer, self->sinka);
    gst_element_release_request_pad (self->vmixer, self->sinkb);
    gst_object_unref (self->vmixer);
    gst_object_unref (self->sinka);
    gst_object_unref (self->sinkb);
    self->vmixer = NULL;
    self->sinka = NULL;
    self->sinkb = NULL;
  }

  G_OBJECT_CLASS (ges_track_transition_parent_class)->dispose (object);
}

static void
ges_track_transition_finalize (GObject * object)
{
  G_OBJECT_CLASS (ges_track_transition_parent_class)->finalize (object);
}

static gboolean
ges_track_transition_create_gnl_object (GESTrackObject * object)
{
  GESTrackTransition *self;
  GESTrackTransitionClass *klass;
  GstElement *element;
  gchar *name;
  static gint tnum = 0;

  self = GES_TRACK_TRANSITION (object);
  klass = GES_TRACK_TRANSITION_GET_CLASS (object);

  name = g_strdup_printf ("transition-operation%d", tnum++);
  object->gnlobject = gst_element_factory_make ("gnloperation", name);
  g_free (name);

  g_object_set (object->gnlobject, "priority", 0, NULL);
  g_signal_connect (G_OBJECT (object->gnlobject), "notify::duration",
      G_CALLBACK (gnlobject_duration_cb), object);

  element = klass->create_element (self, object->track);
  if (!GST_IS_ELEMENT (element))
    return FALSE;

  gst_bin_add (GST_BIN (object->gnlobject), element);

  klass->duration_changed (self, object->gnlobject);
  return TRUE;
}

GstElement *
ges_track_transition_create_element (GESTrackTransition * self,
    GESTrack * track)
{
  GST_WARNING ("transitions don't handle this track type!");

  return gst_element_factory_make ("identity", "invalid-track-type");
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
  klass->create_element = ges_track_transition_create_element;
  klass->duration_changed = ges_track_transition_duration_changed;
}

static void
ges_track_transition_init (GESTrackTransition * self)
{
  self->vcontroller = NULL;
  self->vcontrol_source = NULL;
  self->vsmpte = NULL;
  self->vmixer = NULL;
  self->sinka = NULL;
  self->sinkb = NULL;
  self->vtype = 0;
  self->vstart_value = 0.0;
  self->vend_value = 0.0;
}

void
ges_track_transition_set_vtype (GESTrackTransition * self, gint vtype)
{
  if (((vtype == VTYPE_CROSSFADE) && (self->vtype != VTYPE_CROSSFADE)) ||
      ((vtype != VTYPE_CROSSFADE) && (self->vtype = VTYPE_CROSSFADE))) {
    GST_WARNING
        ("Changing between 'crossfade' and other types is not supported\n");
  }

  self->vtype = vtype;
  if (self->vsmpte && (vtype != VTYPE_CROSSFADE))
    g_object_set (self->vsmpte, "type", (gint) vtype, NULL);
}

GESTrackTransition *
ges_track_transition_new (gint value)
{
  GESTrackTransition *ret = g_object_new (GES_TYPE_TRACK_TRANSITION, NULL);
  ret->vtype = value;

  return ret;
}
