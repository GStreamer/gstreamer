/* GStreamer Editing Services
 * Copyright (C) 2009 Edward Hervey <edward.hervey@collabora.co.uk>
 *               2009 Nokia Corporation
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
 * SECTION:ges-track-source
 * @short_description: Base Class for single-media sources
 */

#include "ges-internal.h"
#include "ges-track-object.h"
#include "ges-track-source.h"

G_DEFINE_TYPE (GESTrackSource, ges_track_source, GES_TYPE_TRACK_OBJECT);

static void
ges_track_source_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_track_source_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_track_source_dispose (GObject * object)
{
  G_OBJECT_CLASS (ges_track_source_parent_class)->dispose (object);
}

static void
ges_track_source_finalize (GObject * object)
{
  G_OBJECT_CLASS (ges_track_source_parent_class)->finalize (object);
}

static gboolean
ges_track_source_create_gnl_object (GESTrackObject * object)
{
  object->gnlobject = gst_element_factory_make ("gnlsource", NULL);

  return TRUE;
}

static void
ges_track_source_class_init (GESTrackSourceClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GESTrackObjectClass *track_class = GES_TRACK_OBJECT_CLASS (klass);

  object_class->get_property = ges_track_source_get_property;
  object_class->set_property = ges_track_source_set_property;
  object_class->dispose = ges_track_source_dispose;
  object_class->finalize = ges_track_source_finalize;

  track_class->create_gnl_object = ges_track_source_create_gnl_object;
}

static void
ges_track_source_init (GESTrackSource * self)
{
}

GESTrackSource *
ges_track_source_new (void)
{
  return g_object_new (GES_TYPE_TRACK_SOURCE, NULL);
}
