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
#include "ges-track-image-source.h"
#include "ges-track-audio-test-source.h"

G_DEFINE_TYPE (GESTimelineFileSource, ges_timeline_filesource,
    GES_TYPE_TIMELINE_SOURCE);

struct _GESTimelineFileSourcePrivate
{
  gchar *uri;

  gboolean mute;
  gboolean is_image;

  guint64 maxduration;
};

enum
{
  PROP_0,
  PROP_URI,
  PROP_MAX_DURATION,
  PROP_MUTE,
  PROP_IS_IMAGE,
};


static GESTrackObject
    * ges_timeline_filesource_create_track_object (GESTimelineObject * obj,
    GESTrack * track);

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
    case PROP_MAX_DURATION:
      g_value_set_uint64 (value, priv->maxduration);
      break;
    case PROP_IS_IMAGE:
      g_value_set_boolean (value, priv->is_image);
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
      tfs->priv->uri = g_value_dup_string (value);
      break;
    case PROP_MUTE:
      ges_timeline_filesource_set_mute (tfs, g_value_get_boolean (value));
      break;
    case PROP_MAX_DURATION:
      ges_timeline_filesource_set_max_duration (tfs,
          g_value_get_uint64 (value));
      break;
    case PROP_IS_IMAGE:
      ges_timeline_filesource_set_is_image (tfs, g_value_get_boolean (value));
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
  G_OBJECT_CLASS (ges_timeline_filesource_parent_class)->finalize (object);
}

static void
ges_timeline_filesource_class_init (GESTimelineFileSourceClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GESTimelineObjectClass *timobj_class = GES_TIMELINE_OBJECT_CLASS (klass);

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
      g_param_spec_string ("uri", "URI", "uri of the resource",
          NULL, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

  /**
   * GESTimelineFileSource:max-duration:
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

  timobj_class->create_track_object =
      ges_timeline_filesource_create_track_object;
  timobj_class->need_fill_track = FALSE;
}

static void
ges_timeline_filesource_init (GESTimelineFileSource * self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_TIMELINE_FILE_SOURCE, GESTimelineFileSourcePrivate);

  /* Setting the duration to -1 by default. */
  GES_TIMELINE_OBJECT (self)->duration = GST_CLOCK_TIME_NONE;
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

/**
 * ges_timeline_filesource_set_max_duration:
 * @self: the #GESTimelineFileSource to set the maximum duration on
 * @maxduration: the maximum duration of @self
 *
 * Sets the maximum duration (in nanoseconds) of the file.
 *
 */
void
ges_timeline_filesource_set_max_duration (GESTimelineFileSource * self,
    guint64 maxduration)
{
  GESTimelineObject *object = GES_TIMELINE_OBJECT (self);

  self->priv->maxduration = maxduration;
  if (object->duration == GST_CLOCK_TIME_NONE || object->duration == 0) {
    /* If we don't have a valid duration, use the max duration */
    g_object_set (self, "duration", self->priv->maxduration - object->inpoint,
        NULL);
  }
}

/**
 * ges_timeline_filesource_set_supported_formats:
 * @self: the #GESTimelineFileSource to set supported formats on
 * @supportedformats: the #GESTrackType defining formats supported by @self
 *
 * Sets the formats supported by the file.
 *
 */
void
ges_timeline_filesource_set_supported_formats (GESTimelineFileSource * self,
    GESTrackType supportedformats)
{
  ges_timeline_object_set_supported_formats (GES_TIMELINE_OBJECT (self),
      supportedformats);
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
 * ges_timeline_filesource_get_max_duration:
 * @self: the #GESTimelineFileSource 
 *
 * Get the duration of the object.
 *
 * Returns: The duration of @self.
 */
guint64
ges_timeline_filesource_get_max_duration (GESTimelineFileSource * self)
{
  return self->priv->maxduration;
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

/**
 * ges_timeline_filesource_get_supported_formats:
 * @self: the #GESTimelineFileSource 
 *
 * Get the formats supported by @self.
 *
 * Returns: The formats supported by @self.
 */
GESTrackType
ges_timeline_filesource_get_supported_formats (GESTimelineFileSource * self)
{
  return ges_timeline_object_get_supported_formats (GES_TIMELINE_OBJECT (self));
}

static GESTrackObject *
ges_timeline_filesource_create_track_object (GESTimelineObject * obj,
    GESTrack * track)
{
  GESTimelineFileSourcePrivate *priv = GES_TIMELINE_FILE_SOURCE (obj)->priv;
  GESTrackObject *res;

  if (!(ges_timeline_object_get_supported_formats (obj) & track->type)) {
    GST_DEBUG ("We don't support this track format");
    return NULL;
  }

  if (priv->is_image) {
    if (track->type != GES_TRACK_TYPE_VIDEO) {
      GST_DEBUG ("Object is still image, not adding any audio source");
      return NULL;
    } else {
      GST_DEBUG ("Creating a GESTrackImageSource");
      res = (GESTrackObject *) ges_track_image_source_new (priv->uri);
    }
  }

  else {
    GST_DEBUG ("Creating a GESTrackFileSource");

    /* FIXME : Implement properly ! */
    res = (GESTrackObject *) ges_track_filesource_new (priv->uri);

    /* If mute and track is audio, deactivate the track object */
    if (track->type == GES_TRACK_TYPE_AUDIO && priv->mute)
      ges_track_object_set_active (res, FALSE);
  }

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
