/* GStreamer Editing Services
 * Copyright (C) 2009 Edward Hervey <bilboed@bilboed.com>
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

#include "ges-timeline-object.h"

/**
 * GESTimelineObject
 *
 * Responsible for creating the TrackObject(s) for given TimelineTrack(s)
 *
 * Keeps a reference to the TrackObject(s) it created and sets/updates their properties.
 */


G_DEFINE_TYPE (GESTimelineObject, ges_timeline_object, G_TYPE_OBJECT)

#define GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), GES_TYPE_TIMELINE_OBJECT, GESTimelineObjectPrivate))

typedef struct _GESTimelineObjectPrivate GESTimelineObjectPrivate;

struct _GESTimelineObjectPrivate {
    int dummy;
};

static void
ges_timeline_object_get_property (GObject *object, guint property_id,
                              GValue *value, GParamSpec *pspec)
{
  switch (property_id) {
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_timeline_object_set_property (GObject *object, guint property_id,
                              const GValue *value, GParamSpec *pspec)
{
  switch (property_id) {
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_timeline_object_dispose (GObject *object)
{
  G_OBJECT_CLASS (ges_timeline_object_parent_class)->dispose (object);
}

static void
ges_timeline_object_finalize (GObject *object)
{
  G_OBJECT_CLASS (ges_timeline_object_parent_class)->finalize (object);
}

static void
ges_timeline_object_class_init (GESTimelineObjectClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GESTimelineObjectPrivate));

  object_class->get_property = ges_timeline_object_get_property;
  object_class->set_property = ges_timeline_object_set_property;
  object_class->dispose = ges_timeline_object_dispose;
  object_class->finalize = ges_timeline_object_finalize;
}

static void
ges_timeline_object_init (GESTimelineObject *self)
{
}

GESTimelineObject*
ges_timeline_object_new (void)
{
  return g_object_new (GES_TYPE_TIMELINE_OBJECT, NULL);
}

