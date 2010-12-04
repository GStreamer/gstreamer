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
 * @short_description: base class for audio and video transitions
 *
 */

#include "ges-internal.h"
#include "ges-track-object.h"
#include "ges-track-transition.h"
#include "ges-timeline-transition.h"

G_DEFINE_TYPE (GESTrackTransition, ges_track_transition, GES_TYPE_TRACK_OBJECT);

struct _GESTrackTransitionPrivate
{
  /*  Dummy variable */
  void *nothing;
};

GstElement *ges_track_transition_create_element (GESTrackTransition * self);

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

  element = klass->create_element (self);
  if (!GST_IS_ELEMENT (element))
    return FALSE;

  gst_bin_add (GST_BIN (object->gnlobject), element);

  return TRUE;
}

GstElement *
ges_track_transition_create_element (GESTrackTransition * self)
{
  GST_WARNING ("transitions don't handle this track type!");

  return NULL;
}

static void
ges_track_transition_class_init (GESTrackTransitionClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GESTrackObjectClass *track_class = GES_TRACK_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GESTrackTransitionPrivate));

  object_class->get_property = ges_track_transition_get_property;
  object_class->set_property = ges_track_transition_set_property;
  object_class->dispose = ges_track_transition_dispose;
  object_class->finalize = ges_track_transition_finalize;

  track_class->create_gnl_object = ges_track_transition_create_gnl_object;
  klass->create_element = ges_track_transition_create_element;
}

static void
ges_track_transition_init (GESTrackTransition * self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_TRACK_TRANSITION, GESTrackTransitionPrivate);

}
