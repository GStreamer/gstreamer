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
 * Represents all the output treams from a particular uri. It is assumed that
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

enum
{
  PROP_0,
  PROP_URI,
  PROP_MAX_DURATION,
  PROP_MUTE,
  PROP_SUPPORTED_FORMATS,
  PROP_IS_IMAGE,
};

static void
ges_timeline_filesource_set_mute (GESTimelineFileSource * self, gboolean mute);
static void
ges_timeline_filesource_set_max_duration (GESTimelineFileSource * self,
    guint64 maxduration);

static GESTrackObject
    * ges_timeline_filesource_create_track_object (GESTimelineObject * obj,
    GESTrack * track);

static void
ges_timeline_filesource_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GESTimelineFileSource *tfs = GES_TIMELINE_FILE_SOURCE (object);

  switch (property_id) {
    case PROP_URI:
      g_value_set_string (value, tfs->uri);
      break;
    case PROP_MUTE:
      g_value_set_boolean (value, tfs->mute);
      break;
    case PROP_MAX_DURATION:
      g_value_set_uint64 (value, tfs->maxduration);
      break;
    case PROP_SUPPORTED_FORMATS:
      g_value_set_flags (value, tfs->supportedformats);
      break;
    case PROP_IS_IMAGE:
      g_value_set_boolean (value, tfs->is_image);
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
      tfs->uri = g_value_dup_string (value);
      break;
    case PROP_MUTE:
      ges_timeline_filesource_set_mute (tfs, g_value_get_boolean (value));
      break;
    case PROP_MAX_DURATION:
      ges_timeline_filesource_set_max_duration (tfs,
          g_value_get_uint64 (value));
      break;
    case PROP_SUPPORTED_FORMATS:
      tfs->supportedformats = g_value_get_flags (value);
      break;
    case PROP_IS_IMAGE:
      tfs->is_image = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_timeline_filesource_finalize (GObject * object)
{
  GESTimelineFileSource *tfs = GES_TIMELINE_FILE_SOURCE (object);

  if (tfs->uri)
    g_free (tfs->uri);
  G_OBJECT_CLASS (ges_timeline_filesource_parent_class)->finalize (object);
}

static void
ges_timeline_filesource_class_init (GESTimelineFileSourceClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GESTimelineObjectClass *timobj_class = GES_TIMELINE_OBJECT_CLASS (klass);

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
   * GESTimelineFileSource:supported-formats:
   *
   * Whether the sound will be played or not.
   */
  g_object_class_install_property (object_class, PROP_SUPPORTED_FORMATS,
      g_param_spec_flags ("supported-formats", "Supported formats",
          "Formats supported by the file", GES_TYPE_TRACK_TYPE,
          GES_TRACK_TYPE_UNKNOWN, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

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
  /* Setting the duration to -1 by default. */
  GES_TIMELINE_OBJECT (self)->duration = GST_CLOCK_TIME_NONE;
}

static void
ges_timeline_filesource_set_mute (GESTimelineFileSource * self, gboolean mute)
{
  GList *tmp, *trackobjects;
  GESTimelineObject *object = (GESTimelineObject *) self;

  GST_DEBUG ("self:%p, mute:%d", self, mute);

  self->mute = mute;

  /* Go over tracked objects, and update 'active' status on all audio objects */
  trackobjects = ges_timeline_object_get_track_objects (object);
  for (tmp = trackobjects; tmp; tmp = tmp->next) {
    GESTrackObject *trackobject = (GESTrackObject *) tmp->data;

    if (trackobject->track->type == GES_TRACK_TYPE_AUDIO)
      ges_track_object_set_active (trackobject, !mute);

    g_object_unref (GES_TRACK_OBJECT (tmp->data));
  }
  g_list_free (trackobjects);
}

static void
ges_timeline_filesource_set_max_duration (GESTimelineFileSource * self,
    guint64 maxduration)
{
  GESTimelineObject *object = GES_TIMELINE_OBJECT (self);

  self->maxduration = maxduration;
  if (object->duration == GST_CLOCK_TIME_NONE || object->duration == 0) {
    /* If we don't have a valid duration, use the max duration */
    g_object_set (self, "duration", self->maxduration - object->inpoint, NULL);
  }
}

static GESTrackObject *
ges_timeline_filesource_create_track_object (GESTimelineObject * obj,
    GESTrack * track)
{
  GESTimelineFileSource *tfs = (GESTimelineFileSource *) obj;
  GESTrackObject *res;

  if (!(tfs->supportedformats & track->type)) {
    GST_DEBUG ("We don't support this track format");
    return NULL;
  }

  if (tfs->is_image) {
    if (track->type != GES_TRACK_TYPE_VIDEO) {
      GST_DEBUG ("Object is still image, creating silent audio source");
      res = (GESTrackObject *) ges_track_audio_test_source_new ();
    } else {
      GST_DEBUG ("Creating a GESTrackImageSource");
      res = (GESTrackObject *) ges_track_image_source_new (tfs->uri);
    }
  }

  else {
    GST_DEBUG ("Creating a GESTrackFileSource");

    /* FIXME : Implement properly ! */
    res = (GESTrackObject *) ges_track_filesource_new (tfs->uri);

    /* If mute and track is audio, deactivate the track object */
    if (track->type == GES_TRACK_TYPE_AUDIO && tfs->mute)
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
  /* FIXME : Check for validity/existence of URI */
  return g_object_new (GES_TYPE_TIMELINE_FILE_SOURCE, "uri", uri, NULL);
}
