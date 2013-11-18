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
 * SECTION:ges-video-uri-source
 * @short_description: outputs a single video stream from a given file
 */

#include <gst/pbutils/missing-plugins.h>

#include "ges-utils.h"
#include "ges-internal.h"
#include "ges-track-element.h"
#include "ges-video-uri-source.h"
#include "ges-uri-asset.h"
#include "ges-extractable.h"

struct _GESVideoUriSourcePrivate
{
  void *nothing;
};

enum
{
  PROP_0,
  PROP_URI
};

static void
post_missing_element_message (GstElement * element, const gchar * name)
{
  GstMessage *msg;

  msg = gst_missing_element_message_new (element, name);
  gst_element_post_message (element, msg);
}

/* GESSource VMethod */
static GstElement *
ges_video_uri_source_create_source (GESTrackElement * trksrc)
{
  GESVideoUriSource *self;
  GESTrack *track;
  GstDiscovererVideoInfo *info;
  GESUriSourceAsset *asset;
  GstElement *decodebin;

  self = (GESVideoUriSource *) trksrc;

  track = ges_track_element_get_track (trksrc);

  decodebin = gst_element_factory_make ("uridecodebin", NULL);

  g_object_set (decodebin, "caps", ges_track_get_caps (track),
      "expose-all-streams", FALSE, "uri", self->uri, NULL);

  if ((asset =
          GES_URI_SOURCE_ASSET (ges_extractable_get_asset (GES_EXTRACTABLE
                  (trksrc)))) != NULL) {
    info =
        GST_DISCOVERER_VIDEO_INFO (ges_uri_source_asset_get_stream_info
        (asset));

    g_assert (info);
    if (gst_discoverer_video_info_is_interlaced (info)) {
      GstElement *deinterlace;

      deinterlace = gst_element_factory_make ("deinterlace", "deinterlace");
      if (deinterlace == NULL) {
        deinterlace = gst_element_factory_make ("avdeinterlace", "deinterlace");
      }
      if (deinterlace == NULL) {
        post_missing_element_message (decodebin, "deinterlace");
        GST_ELEMENT_WARNING (decodebin, CORE, MISSING_PLUGIN,
            ("Missing element '%s' - check your GStreamer installation.",
                "deinterlace"), ("deinterlacing won't work"));
      }

      return ges_source_create_topbin ("deinterlace-bin", decodebin,
          gst_element_factory_make ("videoconvert", NULL), deinterlace, NULL);
    }
  }


  return decodebin;
}

/* Extractable interface implementation */

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
  iface->asset_type = GES_TYPE_URI_SOURCE_ASSET;
  iface->check_id = ges_extractable_check_id;
  iface->set_asset = extractable_set_asset;
}

G_DEFINE_TYPE_WITH_CODE (GESVideoUriSource, ges_video_uri_source,
    GES_TYPE_VIDEO_SOURCE,
    G_IMPLEMENT_INTERFACE (GES_TYPE_EXTRACTABLE,
        ges_extractable_interface_init));


/* GObject VMethods */

static void
ges_video_uri_source_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GESVideoUriSource *uriclip = GES_VIDEO_URI_SOURCE (object);

  switch (property_id) {
    case PROP_URI:
      g_value_set_string (value, uriclip->uri);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_video_uri_source_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GESVideoUriSource *uriclip = GES_VIDEO_URI_SOURCE (object);

  switch (property_id) {
    case PROP_URI:
      if (uriclip->uri) {
        GST_WARNING_OBJECT (object, "Uri already set to %s", uriclip->uri);
        return;
      }
      uriclip->uri = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_video_uri_source_dispose (GObject * object)
{
  GESVideoUriSource *uriclip = GES_VIDEO_URI_SOURCE (object);

  if (uriclip->uri)
    g_free (uriclip->uri);

  G_OBJECT_CLASS (ges_video_uri_source_parent_class)->dispose (object);
}

static void
ges_video_uri_source_class_init (GESVideoUriSourceClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GESVideoSourceClass *source_class = GES_VIDEO_SOURCE_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GESVideoUriSourcePrivate));

  object_class->get_property = ges_video_uri_source_get_property;
  object_class->set_property = ges_video_uri_source_set_property;
  object_class->dispose = ges_video_uri_source_dispose;

  /**
   * GESVideoUriSource:uri:
   *
   * The location of the file/resource to use.
   */
  g_object_class_install_property (object_class, PROP_URI,
      g_param_spec_string ("uri", "URI", "uri of the resource",
          NULL, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

  source_class->create_source = ges_video_uri_source_create_source;
}

static void
ges_video_uri_source_init (GESVideoUriSource * self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_VIDEO_URI_SOURCE, GESVideoUriSourcePrivate);
}

/**
 * ges_video_uri_source_new:
 * @uri: the URI the source should control
 *
 * Creates a new #GESVideoUriSource for the provided @uri.
 *
 * Returns: The newly created #GESVideoUriSource, or %NULL if there was an
 * error.
 */
GESVideoUriSource *
ges_video_uri_source_new (gchar * uri)
{
  return g_object_new (GES_TYPE_VIDEO_URI_SOURCE, "uri", uri, NULL);
}
