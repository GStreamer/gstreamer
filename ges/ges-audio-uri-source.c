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
 * SECTION:gesaudiourisource
 * @title: GESAudioUriSource
 * @short_description: outputs a single audio stream from a given file
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ges-utils.h"
#include "ges-internal.h"
#include "ges-track-element.h"
#include "ges-uri-source.h"
#include "ges-audio-uri-source.h"
#include "ges-uri-asset.h"
#include "ges-extractable.h"

struct _GESAudioUriSourcePrivate
{
  GESUriSource parent;
};

enum
{
  PROP_0,
  PROP_URI
};

/* GESSource VMethod */
static GstElement *
ges_audio_uri_source_create_source (GESSource * element)
{
  return ges_uri_source_create_source (GES_AUDIO_URI_SOURCE (element)->priv);
}

/* Extractable interface implementation */

static gchar *
ges_extractable_check_id (GType type, const gchar * id, GError ** error)
{
  return g_strdup (id);
}

static void
ges_extractable_interface_init (GESExtractableInterface * iface)
{
  iface->asset_type = GES_TYPE_URI_SOURCE_ASSET;
  iface->check_id = ges_extractable_check_id;
}

G_DEFINE_TYPE_WITH_CODE (GESAudioUriSource, ges_audio_uri_source,
    GES_TYPE_AUDIO_SOURCE, G_ADD_PRIVATE (GESAudioUriSource)
    G_IMPLEMENT_INTERFACE (GES_TYPE_EXTRACTABLE,
        ges_extractable_interface_init));


/* GObject VMethods */

static gboolean
_get_natural_framerate (GESTimelineElement * self, gint * framerate_n,
    gint * framerate_d)
{
  if (self->parent)
    return ges_timeline_element_get_natural_framerate (self->parent,
        framerate_n, framerate_d);

  return FALSE;
}

static void
ges_audio_uri_source_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GESAudioUriSource *uriclip = GES_AUDIO_URI_SOURCE (object);

  switch (property_id) {
    case PROP_URI:
      g_value_set_string (value, uriclip->uri);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_audio_uri_source_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GESAudioUriSource *uriclip = GES_AUDIO_URI_SOURCE (object);

  switch (property_id) {
    case PROP_URI:
      if (uriclip->uri) {
        GST_WARNING_OBJECT (object, "Uri already set to %s", uriclip->uri);
        return;
      }
      uriclip->priv->uri = uriclip->uri = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_audio_uri_source_finalize (GObject * object)
{
  GESAudioUriSource *uriclip = GES_AUDIO_URI_SOURCE (object);

  g_free (uriclip->uri);

  G_OBJECT_CLASS (ges_audio_uri_source_parent_class)->finalize (object);
}

static void
ges_audio_uri_source_class_init (GESAudioUriSourceClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GESTimelineElementClass *element_class = GES_TIMELINE_ELEMENT_CLASS (klass);
  GESSourceClass *src_class = GES_SOURCE_CLASS (klass);

  object_class->get_property = ges_audio_uri_source_get_property;
  object_class->set_property = ges_audio_uri_source_set_property;
  object_class->finalize = ges_audio_uri_source_finalize;

  /**
   * GESAudioUriSource:uri:
   *
   * The location of the file/resource to use.
   */
  g_object_class_install_property (object_class, PROP_URI,
      g_param_spec_string ("uri", "URI", "uri of the resource",
          NULL, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

  element_class->get_natural_framerate = _get_natural_framerate;

  src_class->select_pad = ges_uri_source_select_pad;
  src_class->create_source = ges_audio_uri_source_create_source;
}

static void
ges_audio_uri_source_init (GESAudioUriSource * self)
{
  self->priv = ges_audio_uri_source_get_instance_private (self);
  ges_uri_source_init (GES_TRACK_ELEMENT (self), self->priv);
}

/**
 * ges_audio_uri_source_new:
 * @uri: the URI the source should control
 *
 * Creates a new #GESAudioUriSource for the provided @uri.
 *
 * Returns: (transfer floating) (nullable): The newly created
 * #GESAudioUriSource, or %NULL if there was an error.
 */
GESAudioUriSource *
ges_audio_uri_source_new (gchar * uri)
{
  return g_object_new (GES_TYPE_AUDIO_URI_SOURCE, "uri", uri, NULL);
}
