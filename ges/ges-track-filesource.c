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
#include "ges-track-filesource.h"

G_DEFINE_TYPE (GESTrackFileSource, ges_track_filesource, GES_TYPE_TRACK_SOURCE);

enum
{
  PROP_0,
  PROP_URI
};

static void
ges_track_filesource_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GESTrackFileSource *tfs = GES_TRACK_FILESOURCE (object);

  switch (property_id) {
    case PROP_URI:
      g_value_set_string (value, tfs->uri);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_track_filesource_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GESTrackFileSource *tfs = GES_TRACK_FILESOURCE (object);

  switch (property_id) {
    case PROP_URI:
      tfs->uri = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_track_filesource_dispose (GObject * object)
{
  G_OBJECT_CLASS (ges_track_filesource_parent_class)->dispose (object);
}

static void
ges_track_filesource_finalize (GObject * object)
{
  G_OBJECT_CLASS (ges_track_filesource_parent_class)->finalize (object);
}

static gboolean
ges_track_filesource_create_gnl_object (GESTrackObject * object)
{
  object->gnlobject = gst_element_factory_make ("gnlurisource", NULL);
  g_object_set (object->gnlobject, "uri", ((GESTrackFileSource *) object)->uri,
      NULL);

  return TRUE;
}

static void
ges_track_filesource_class_init (GESTrackFileSourceClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GESTrackObjectClass *track_class = GES_TRACK_OBJECT_CLASS (klass);

  object_class->get_property = ges_track_filesource_get_property;
  object_class->set_property = ges_track_filesource_set_property;
  object_class->dispose = ges_track_filesource_dispose;
  object_class->finalize = ges_track_filesource_finalize;

  /**
   * GESTrackFileSource:uri
   *
   * The location of the file/resource to use.
   */
  g_object_class_install_property (object_class, PROP_URI,
      g_param_spec_string ("uri", "URI", "uri of the resource",
          NULL, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
  track_class->create_gnl_object = ges_track_filesource_create_gnl_object;
}

static void
ges_track_filesource_init (GESTrackFileSource * self)
{
}

GESTrackFileSource *
ges_track_filesource_new (gchar * uri)
{
  return g_object_new (GES_TYPE_TRACK_FILESOURCE, "uri", uri, NULL);
}
