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
 * SECTION:ges-track-filesource
 * @short_description: outputs a single media stream from a given file
 * 
 * Outputs a single media stream from a given file. The stream chosen depends on
 * the type of the track which contains the object.
 */

#include "ges-internal.h"
#include "ges-track-object.h"
#include "ges-track-filesource.h"

G_DEFINE_TYPE (GESTrackFileSource, ges_track_filesource, GES_TYPE_TRACK_SOURCE);

struct _GESTrackFileSourcePrivate
{
  guint64 maxduration;
};

enum
{
  PROP_0,
  PROP_URI,
  PROP_MAX_DURATION
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
    case PROP_MAX_DURATION:
      g_value_set_uint64 (value, tfs->priv->maxduration);
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
    case PROP_MAX_DURATION:
      tfs->priv->maxduration = g_value_get_uint64 (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_track_filesource_dispose (GObject * object)
{
  GESTrackFileSource *tfs = GES_TRACK_FILESOURCE (object);

  if (tfs->uri)
    g_free (tfs->uri);

  G_OBJECT_CLASS (ges_track_filesource_parent_class)->dispose (object);
}

static GstElement *
ges_track_filesource_create_gnl_object (GESTrackObject * object)
{
  GstElement *gnlobject;

  gnlobject = gst_element_factory_make ("gnlurisource", NULL);
  g_object_set (gnlobject, "uri", ((GESTrackFileSource *) object)->uri, NULL);

  return gnlobject;
}

static void
ges_track_filesource_class_init (GESTrackFileSourceClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GESTrackObjectClass *track_class = GES_TRACK_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GESTrackFileSourcePrivate));

  object_class->get_property = ges_track_filesource_get_property;
  object_class->set_property = ges_track_filesource_set_property;
  object_class->dispose = ges_track_filesource_dispose;

  /**
   * GESTrackFileSource:max-duration:
   *
   * The maximum duration (in nanoseconds) of the file.
   *
   * If not set before adding the object to a layer, it will be discovered
   * asynchronously. Connect to 'notify::max-duration' to be notified of it.
   */
  g_object_class_install_property (object_class, PROP_MAX_DURATION,
      g_param_spec_uint64 ("max-duration", "Maximum duration",
          "The duration of the file", 0, G_MAXUINT64, GST_CLOCK_TIME_NONE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

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
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_TRACK_FILESOURCE, GESTrackFileSourcePrivate);
}

/**
 * ges_track_filesource_new:
 * @uri: the URI the source should control
 *
 * Creates a new #GESTrackFileSource for the provided @uri.
 *
 * Returns: The newly created #GESTrackFileSource, or %NULL if there was an
 * error.
 */
GESTrackFileSource *
ges_track_filesource_new (gchar * uri)
{
  return g_object_new (GES_TYPE_TRACK_FILESOURCE, "uri", uri, NULL);
}
