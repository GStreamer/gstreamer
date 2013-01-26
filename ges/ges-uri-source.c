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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:ges-uri-source
 * @short_description: outputs a single media stream from a given file
 *
 * Outputs a single media stream from a given file. The stream chosen depends on
 * the type of the track which contains the object.
 */

#include "ges-internal.h"
#include "ges-track-element.h"
#include "ges-uri-source.h"
#include "ges-uri-asset.h"
#include "ges-extractable.h"

static gchar *
ges_extractable_check_id (GType type, const gchar * id, GError ** error)
{
  return g_strdup (id);
}

static void
extractable_set_asset (GESExtractable * self, GESAsset * asset)
{
  /* FIXME That should go into #GESTrackElement, but
   * some work is needed to make sure it works properly */

  if (ges_track_element_get_track_type (GES_TRACK_ELEMENT (self)) ==
      GES_TRACK_TYPE_UNKNOWN) {
    ges_track_element_set_track_type (GES_TRACK_ELEMENT (self),
        ges_track_element_asset_get_track_type (GES_TRACK_ELEMENT_ASSET
            (asset)));
  }
}

static void
ges_extractable_interface_init (GESExtractableInterface * iface)
{
  iface->asset_type = GES_TYPE_ASSET_TRACK_FILESOURCE;
  iface->check_id = ges_extractable_check_id;
  iface->set_asset = extractable_set_asset;
}

G_DEFINE_TYPE_WITH_CODE (GESUriSource, ges_track_filesource,
    GES_TYPE_SOURCE,
    G_IMPLEMENT_INTERFACE (GES_TYPE_EXTRACTABLE,
        ges_extractable_interface_init));

struct _GESUriSourcePrivate
{
  void *dummy;
};

enum
{
  PROP_0,
  PROP_URI
};

static void
ges_track_filesource_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GESUriSource *uriclip = GES_TRACK_FILESOURCE (object);

  switch (property_id) {
    case PROP_URI:
      g_value_set_string (value, uriclip->uri);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_track_filesource_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GESUriSource *uriclip = GES_TRACK_FILESOURCE (object);

  switch (property_id) {
    case PROP_URI:
      uriclip->uri = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_track_filesource_dispose (GObject * object)
{
  GESUriSource *uriclip = GES_TRACK_FILESOURCE (object);

  if (uriclip->uri)
    g_free (uriclip->uri);

  G_OBJECT_CLASS (ges_track_filesource_parent_class)->dispose (object);
}

static GstElement *
ges_track_filesource_create_gnl_object (GESTrackElement * object)
{
  GstElement *gnlobject;

  gnlobject = gst_element_factory_make ("gnlurisource", NULL);
  g_object_set (gnlobject, "uri", ((GESUriSource *) object)->uri, NULL);

  return gnlobject;
}

static void
ges_track_filesource_class_init (GESUriSourceClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GESTrackElementClass *track_class = GES_TRACK_ELEMENT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GESUriSourcePrivate));

  object_class->get_property = ges_track_filesource_get_property;
  object_class->set_property = ges_track_filesource_set_property;
  object_class->dispose = ges_track_filesource_dispose;

  /**
   * GESUriSource:uri:
   *
   * The location of the file/resource to use.
   */
  g_object_class_install_property (object_class, PROP_URI,
      g_param_spec_string ("uri", "URI", "uri of the resource",
          NULL, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
  track_class->create_gnl_object = ges_track_filesource_create_gnl_object;
}

static void
ges_track_filesource_init (GESUriSource * self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_TRACK_FILESOURCE, GESUriSourcePrivate);
}

/**
 * ges_track_filesource_new:
 * @uri: the URI the source should control
 *
 * Creates a new #GESUriSource for the provided @uri.
 *
 * Returns: The newly created #GESUriSource, or %NULL if there was an
 * error.
 */
GESUriSource *
ges_track_filesource_new (gchar * uri)
{
  return g_object_new (GES_TYPE_TRACK_FILESOURCE, "uri", uri, NULL);
}
