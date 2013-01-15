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
 * SECTION:ges-timeline-filesource
 * @short_description: An object for manipulating media files in a GESTimeline
 *
 * Represents all the output streams from a particular uri. It is assumed that
 * the URI points to a file of some type.
 */

#include "ges-internal.h"
#include "ges-timeline-file-source.h"
#include "ges-timeline-source.h"
#include "ges-track-filesource.h"
#include "ges-asset-file-source.h"
#include "ges-asset-track-object.h"
#include "ges-extractable.h"
#include "ges-track-image-source.h"
#include "ges-track-audio-test-source.h"

static void ges_extractable_interface_init (GESExtractableInterface * iface);

#define parent_class ges_timeline_filesource_parent_class

G_DEFINE_TYPE_WITH_CODE (GESTimelineFileSource, ges_timeline_filesource,
    GES_TYPE_TIMELINE_SOURCE,
    G_IMPLEMENT_INTERFACE (GES_TYPE_EXTRACTABLE,
        ges_extractable_interface_init));

GESExtractableInterface *parent_extractable_iface;

struct _GESTimelineFileSourcePrivate
{
  gchar *uri;

  gboolean mute;
  gboolean is_image;
};

enum
{
  PROP_0,
  PROP_URI,
  PROP_MUTE,
  PROP_IS_IMAGE,
  PROP_SUPPORTED_FORMATS,
};


static GList *ges_timeline_filesource_create_track_objects (GESTimelineObject *
    obj, GESTrackType type);
static GESTrackObject
    * ges_timeline_filesource_create_track_object (GESTimelineObject * obj,
    GESTrackType type);
void ges_timeline_filesource_set_uri (GESTimelineFileSource * self,
    gchar * uri);

gboolean
filesource_set_max_duration (GESTimelineElement * element,
    GstClockTime maxduration);

static void
ges_timeline_filesource_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GESTimelineFileSourcePrivate *priv = GES_TIMELINE_FILE_SOURCE (object)->priv;

  switch (property_id) {
    case PROP_URI:
      g_value_set_string (value, priv->uri);
      break;
    case PROP_MUTE:
      g_value_set_boolean (value, priv->mute);
      break;
    case PROP_IS_IMAGE:
      g_value_set_boolean (value, priv->is_image);
      break;
    case PROP_SUPPORTED_FORMATS:
      g_value_set_flags (value,
          ges_timeline_object_get_supported_formats (GES_TIMELINE_OBJECT
              (object)));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_timeline_filesource_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GESTimelineFileSource *tfs = GES_TIMELINE_FILE_SOURCE (object);

  switch (property_id) {
    case PROP_URI:
      ges_timeline_filesource_set_uri (tfs, g_value_dup_string (value));
      break;
    case PROP_MUTE:
      ges_timeline_filesource_set_mute (tfs, g_value_get_boolean (value));
      break;
    case PROP_IS_IMAGE:
      ges_timeline_filesource_set_is_image (tfs, g_value_get_boolean (value));
      break;
    case PROP_SUPPORTED_FORMATS:
      ges_timeline_object_set_supported_formats (GES_TIMELINE_OBJECT (tfs),
          g_value_get_flags (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_timeline_filesource_finalize (GObject * object)
{
  GESTimelineFileSourcePrivate *priv = GES_TIMELINE_FILE_SOURCE (object)->priv;

  if (priv->uri)
    g_free (priv->uri);
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
ges_timeline_filesource_class_init (GESTimelineFileSourceClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GESTimelineObjectClass *timobj_class = GES_TIMELINE_OBJECT_CLASS (klass);
  GESTimelineElementClass *element_class = GES_TIMELINE_ELEMENT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GESTimelineFileSourcePrivate));

  object_class->get_property = ges_timeline_filesource_get_property;
  object_class->set_property = ges_timeline_filesource_set_property;
  object_class->finalize = ges_timeline_filesource_finalize;


  /**
   * GESTimelineFileSource:uri:
   *
   * The location of the file/resource to use.
   */
  g_object_class_install_property (object_class, PROP_URI,
      g_param_spec_string ("uri", "URI", "uri of the resource", NULL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

  /**
   * GESTimelineFileSource:mute:
   *
   * Whether the sound will be played or not.
   */
  g_object_class_install_property (object_class, PROP_MUTE,
      g_param_spec_boolean ("mute", "Mute", "Mute audio track",
          FALSE, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  /**
   * GESTimelineFileSource:is-image:
   *
   * Whether this filesource represents a still image or not. This must be set
   * before create_track_objects is called.
   */
  g_object_class_install_property (object_class, PROP_IS_IMAGE,
      g_param_spec_boolean ("is-image", "Is still image",
          "Whether the timeline object represents a still image or not",
          FALSE, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  /* Redefine the supported formats property so the default value is UNKNOWN
   * and not AUDIO | VIDEO */
  g_object_class_install_property (object_class, PROP_SUPPORTED_FORMATS,
      g_param_spec_flags ("supported-formats",
          "Supported formats", "Formats supported by the file",
          GES_TYPE_TRACK_TYPE, GES_TRACK_TYPE_UNKNOWN,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  element_class->set_max_duration = filesource_set_max_duration;

  timobj_class->create_track_objects =
      ges_timeline_filesource_create_track_objects;
  timobj_class->create_track_object =
      ges_timeline_filesource_create_track_object;
  timobj_class->need_fill_track = FALSE;

}

static gchar *
extractable_check_id (GType type, const gchar * id)
{
  if (gst_uri_is_valid (id))
    return g_strdup (id);

  return NULL;
}

static GParameter *
extractable_get_parameters_from_id (const gchar * id, guint * n_params)
{
  GParameter *params = g_new0 (GParameter, 2);

  params[0].name = "uri";
  g_value_init (&params[0].value, G_TYPE_STRING);
  g_value_set_string (&params[0].value, id);

  *n_params = 1;

  return params;
}

static gchar *
extractable_get_id (GESExtractable * self)
{
  return g_strdup (GES_TIMELINE_FILE_SOURCE (self)->priv->uri);
}

static void
extractable_set_asset (GESExtractable * self, GESAsset * asset)
{
  GESTimelineFileSource *tfs = GES_TIMELINE_FILE_SOURCE (self);
  GESAssetFileSource *filesource_asset = GES_ASSET_FILESOURCE (asset);
  GESTimelineObject *tlobj = GES_TIMELINE_OBJECT (self);

  if (GST_CLOCK_TIME_IS_VALID (GES_TIMELINE_ELEMENT (tlobj)) == FALSE)
    _set_duration0 (GES_TIMELINE_ELEMENT (tfs),
        ges_asset_filesource_get_duration (filesource_asset));

  ges_timeline_element_set_max_duration (GES_TIMELINE_ELEMENT (tfs),
      ges_asset_filesource_get_duration (filesource_asset));
  ges_timeline_filesource_set_is_image (tfs,
      ges_asset_filesource_is_image (filesource_asset));

  if (ges_timeline_object_get_supported_formats (tlobj) ==
      GES_TRACK_TYPE_UNKNOWN) {

    ges_timeline_object_set_supported_formats (tlobj,
        ges_asset_timeline_object_get_supported_formats
        (GES_ASSET_TIMELINE_OBJECT (filesource_asset)));
  }

  GES_TIMELINE_ELEMENT (tfs)->asset = asset;
}

static void
ges_extractable_interface_init (GESExtractableInterface * iface)
{
  iface->asset_type = GES_TYPE_ASSET_FILESOURCE;
  iface->check_id = (GESExtractableCheckId) extractable_check_id;
  iface->get_parameters_from_id = extractable_get_parameters_from_id;
  iface->get_id = extractable_get_id;
  iface->set_asset = extractable_set_asset;
}

static void
ges_timeline_filesource_init (GESTimelineFileSource * self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_TIMELINE_FILE_SOURCE, GESTimelineFileSourcePrivate);

  /* Setting the duration to -1 by default. */
  GES_TIMELINE_ELEMENT (self)->duration = GST_CLOCK_TIME_NONE;
}

/**
 * ges_timeline_filesource_set_mute:
 * @self: the #GESTimelineFileSource on which to mute or unmute the audio track
 * @mute: %TRUE to mute @self audio track, %FALSE to unmute it
 *
 * Sets whether the audio track of this timeline object is muted or not.
 *
 */
void
ges_timeline_filesource_set_mute (GESTimelineFileSource * self, gboolean mute)
{
  GList *tmp, *trackobjects;
  GESTimelineObject *object = (GESTimelineObject *) self;

  GST_DEBUG ("self:%p, mute:%d", self, mute);

  self->priv->mute = mute;

  /* Go over tracked objects, and update 'active' status on all audio objects */
  trackobjects = ges_timeline_object_get_track_objects (object);
  for (tmp = trackobjects; tmp; tmp = tmp->next) {
    GESTrackObject *trackobject = (GESTrackObject *) tmp->data;

    if (ges_track_object_get_track (trackobject)->type == GES_TRACK_TYPE_AUDIO)
      ges_track_object_set_active (trackobject, !mute);

    g_object_unref (GES_TRACK_OBJECT (tmp->data));
  }
  g_list_free (trackobjects);
}

gboolean
filesource_set_max_duration (GESTimelineElement * element,
    GstClockTime maxduration)
{
  if (_DURATION (element) == GST_CLOCK_TIME_NONE || _DURATION (element) == 0)
    /* If we don't have a valid duration, use the max duration */
    _set_duration0 (element, maxduration - _INPOINT (element));

  return
      GES_TIMELINE_ELEMENT_CLASS (parent_class)->set_max_duration (element,
      maxduration);
}

/**
 * ges_timeline_filesource_set_is_image:
 * @self: the #GESTimelineFileSource
 * @is_image: %TRUE if @self is a still image, %FALSE otherwise
 *
 * Sets whether the timeline object is a still image or not.
 */
void
ges_timeline_filesource_set_is_image (GESTimelineFileSource * self,
    gboolean is_image)
{
  self->priv->is_image = is_image;
}

/**
 * ges_timeline_filesource_is_muted:
 * @self: the #GESTimelineFileSource
 *
 * Lets you know if the audio track of @self is muted or not.
 *
 * Returns: %TRUE if the audio track of @self is muted, %FALSE otherwise.
 */
gboolean
ges_timeline_filesource_is_muted (GESTimelineFileSource * self)
{
  return self->priv->mute;
}

/**
 * ges_timeline_filesource_is_image:
 * @self: the #GESTimelineFileSource
 *
 * Lets you know if @self is an image or not.
 *
 * Returns: %TRUE if @self is a still image %FALSE otherwise.
 */
gboolean
ges_timeline_filesource_is_image (GESTimelineFileSource * self)
{
  return self->priv->is_image;
}

/**
 * ges_timeline_filesource_get_uri:
 * @self: the #GESTimelineFileSource
 *
 * Get the location of the resource.
 *
 * Returns: The location of the resource.
 */
const gchar *
ges_timeline_filesource_get_uri (GESTimelineFileSource * self)
{
  return self->priv->uri;
}

static GList *
ges_timeline_filesource_create_track_objects (GESTimelineObject * obj,
    GESTrackType type)
{
  GList *res = NULL;
  const GList *tmp, *stream_assets;

  g_return_val_if_fail (GES_TIMELINE_ELEMENT (obj)->asset, NULL);

  stream_assets =
      ges_asset_filesource_get_stream_assets (GES_ASSET_FILESOURCE
      (GES_TIMELINE_ELEMENT (obj)->asset));
  for (tmp = stream_assets; tmp; tmp = tmp->next) {
    GESAssetTrackObject *asset = GES_ASSET_TRACK_OBJECT (tmp->data);

    if (ges_asset_track_object_get_track_type (asset) == type)
      res = g_list_prepend (res, ges_asset_extract (GES_ASSET (asset), NULL));
  }

  return res;
}

static GESTrackObject *
ges_timeline_filesource_create_track_object (GESTimelineObject * obj,
    GESTrackType type)
{
  GESTimelineFileSourcePrivate *priv = GES_TIMELINE_FILE_SOURCE (obj)->priv;
  GESTrackObject *res;

  if (priv->is_image) {
    if (type != GES_TRACK_TYPE_VIDEO) {
      GST_DEBUG ("Object is still image, not adding any audio source");
      return NULL;
    } else {
      GST_DEBUG ("Creating a GESTrackImageSource");
      res = (GESTrackObject *) ges_track_image_source_new (priv->uri);
    }

  } else {
    GST_DEBUG ("Creating a GESTrackFileSource");

    /* FIXME : Implement properly ! */
    res = (GESTrackObject *) ges_track_filesource_new (priv->uri);

    /* If mute and track is audio, deactivate the track object */
    if (type == GES_TRACK_TYPE_AUDIO && priv->mute)
      ges_track_object_set_active (res, FALSE);
  }

  if (res)
    ges_track_object_set_track_type (res, type);

  return res;
}

/**
 * ges_timeline_filesource_new:
 * @uri: the URI the source should control
 *
 * Creates a new #GESTimelineFileSource for the provided @uri.
 *
 * Returns: The newly created #GESTimelineFileSource, or NULL if there was an
 * error.
 */
GESTimelineFileSource *
ges_timeline_filesource_new (gchar * uri)
{
  GESTimelineFileSource *res = NULL;

  if (gst_uri_is_valid (uri))
    res = g_object_new (GES_TYPE_TIMELINE_FILE_SOURCE, "uri", uri, NULL);

  return res;
}

void
ges_timeline_filesource_set_uri (GESTimelineFileSource * self, gchar * uri)
{
  GESTimelineObject *tlobj = GES_TIMELINE_OBJECT (self);
  GList *tckobjs = ges_timeline_object_get_track_objects (tlobj);

  if (tckobjs) {
    /* FIXME handle this case properly */
    GST_WARNING_OBJECT (tlobj, "Can not change uri when already"
        "containing TrackObjects");

    return;
  }

  self->priv->uri = uri;
}
