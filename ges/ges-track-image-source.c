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
 * SECTION:ges-track-image-source
 * @short_description: outputs the video stream from a media file as a still
 * image.
 * 
 * Outputs the video stream from a given file as a still frame. The frame
 * chosen will be determined by the in-point property on the track object. For
 * image files, do not set the in-point property.
 */

#include "ges-internal.h"
#include "ges-track-object.h"
#include "ges-track-image-source.h"

G_DEFINE_TYPE (GESTrackImageSource, ges_track_image_source,
    GES_TYPE_TRACK_SOURCE);

enum
{
  PROP_0,
  PROP_URI
};

static void
ges_track_image_source_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GESTrackImageSource *tfs = GES_TRACK_IMAGE_SOURCE (object);

  switch (property_id) {
    case PROP_URI:
      g_value_set_string (value, tfs->uri);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_track_image_source_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GESTrackImageSource *tfs = GES_TRACK_IMAGE_SOURCE (object);

  switch (property_id) {
    case PROP_URI:
      tfs->uri = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_track_image_source_dispose (GObject * object)
{
  GESTrackImageSource *tfs = GES_TRACK_IMAGE_SOURCE (object);

  if (tfs->uri)
    g_free (tfs->uri);

  G_OBJECT_CLASS (ges_track_image_source_parent_class)->dispose (object);
}

static void
ges_track_image_source_finalize (GObject * object)
{
  G_OBJECT_CLASS (ges_track_image_source_parent_class)->finalize (object);
}

static gboolean
ges_track_image_source_create_gnl_object (GESTrackObject * object)
{
  object->gnlobject = gst_element_factory_make ("gnlurisource", NULL);
  g_object_set (object->gnlobject, "uri", ((GESTrackImageSource *) object)->uri,
      NULL);

  return TRUE;
}

static void
ges_track_image_source_class_init (GESTrackImageSourceClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GESTrackObjectClass *track_class = GES_TRACK_OBJECT_CLASS (klass);

  object_class->get_property = ges_track_image_source_get_property;
  object_class->set_property = ges_track_image_source_set_property;
  object_class->dispose = ges_track_image_source_dispose;
  object_class->finalize = ges_track_image_source_finalize;

  /**
   * GESTrackImageSource:uri
   *
   * The location of the file/resource to use.
   */
  g_object_class_install_property (object_class, PROP_URI,
      g_param_spec_string ("uri", "URI", "uri of the resource",
          NULL, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
  track_class->create_gnl_object = ges_track_image_source_create_gnl_object;
}

static void
ges_track_image_source_init (GESTrackImageSource * self)
{
}

GESTrackImageSource *
ges_track_image_source_new (gchar * uri)
{
  return g_object_new (GES_TYPE_TRACK_IMAGE_SOURCE, "uri", uri, NULL);
}
