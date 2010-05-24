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
  G_OBJECT_CLASS (ges_track_transition_parent_class)->dispose (object);
}

static void
ges_track_transition_finalize (GObject * object)
{
  G_OBJECT_CLASS (ges_track_transition_parent_class)->dispose (object);
}

static gboolean
ges_track_transition_create_gnl_object (GESTrackObject * object)
{
  object->gnlobject = gst_element_factory_make ("gnloperation", NULL);

  return TRUE;
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
}

GESTrackTransition *
ges_track_transition_new (void)
{
  return g_object_new (GES_TYPE_TRACK_TRANSITION, NULL);
}
