/* GStreamer Editing Services
 * Copyright (C) 2009 Edward Hervey <bilboed@bilboed.com>
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

#include "ges-internal.h"
#include "ges-timeline-file-source.h"
#include "ges-timeline-source.h"
#include "ges-track-filesource.h"

G_DEFINE_TYPE (GESTimelineFileSource, ges_tl_filesource,
    GES_TYPE_TIMELINE_SOURCE);

enum
{
  PROP_0,
  PROP_URI,
  PROP_MUTE
};

static void
ges_tl_filesource_set_mute (GESTimelineFileSource * self, gboolean mute);

static GESTrackObject
    * ges_tl_filesource_create_track_object (GESTimelineObject * obj,
    GESTrack * track);

static void
ges_tl_filesource_get_property (GObject * object, guint property_id,
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
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_tl_filesource_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GESTimelineFileSource *tfs = GES_TIMELINE_FILE_SOURCE (object);

  switch (property_id) {
    case PROP_URI:
      tfs->uri = g_value_dup_string (value);
      break;
    case PROP_MUTE:
      ges_tl_filesource_set_mute (tfs, g_value_get_boolean (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_tl_filesource_dispose (GObject * object)
{
  G_OBJECT_CLASS (ges_tl_filesource_parent_class)->dispose (object);
}

static void
ges_tl_filesource_finalize (GObject * object)
{
  GESTimelineFileSource *tfs = GES_TIMELINE_FILE_SOURCE (object);

  if (tfs->uri)
    g_free (tfs->uri);
  G_OBJECT_CLASS (ges_tl_filesource_parent_class)->finalize (object);
}

static void
ges_tl_filesource_class_init (GESTimelineFileSourceClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GESTimelineObjectClass *timobj_class = GES_TIMELINE_OBJECT_CLASS (klass);

  object_class->get_property = ges_tl_filesource_get_property;
  object_class->set_property = ges_tl_filesource_set_property;
  object_class->dispose = ges_tl_filesource_dispose;
  object_class->finalize = ges_tl_filesource_finalize;


  /**
   * GESTimelineFileSource:uri
   *
   * The location of the file/resource to use.
   */
  g_object_class_install_property (object_class, PROP_URI,
      g_param_spec_string ("uri", "URI", "uri of the resource",
          NULL, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

  /**
   * GESTimelineFileSource:mute
   *
   * Whether the sound will be played or not.
   */
  g_object_class_install_property (object_class, PROP_MUTE,
      g_param_spec_boolean ("mute", "Mute", "Mute audio track",
          FALSE, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  timobj_class->create_track_object = ges_tl_filesource_create_track_object;
  timobj_class->need_fill_track = FALSE;
}

static void
ges_tl_filesource_init (GESTimelineFileSource * self)
{
}

static void
ges_tl_filesource_set_mute (GESTimelineFileSource * self, gboolean mute)
{
  GList *tmp;
  GESTimelineObject *object = (GESTimelineObject *) self;

  GST_DEBUG ("self:%p, mute:%d", self, mute);

  self->mute = mute;

  /* Go over tracked objects, and update 'active' status on all audio objects */
  for (tmp = object->trackobjects; tmp; tmp = tmp->next) {
    GESTrackObject *trackobject = (GESTrackObject *) tmp->data;

    if (trackobject->track->type == GES_TRACK_TYPE_AUDIO)
      ges_track_object_set_active (trackobject, mute);
  }
}

static GESTrackObject *
ges_tl_filesource_create_track_object (GESTimelineObject * obj,
    GESTrack * track)
{
  GESTimelineFileSource *tfs = (GESTimelineFileSource *) obj;
  GESTrackObject *res;

  GST_DEBUG ("Creating a GESTrackFileSource");

  /* FIXME : Implement properly ! */
  res = (GESTrackObject *) ges_track_filesource_new (tfs->uri);

  /* If mute and track is audio, deactivate the track object */
  if (track->type == GES_TRACK_TYPE_AUDIO)
    ges_track_object_set_active (res, tfs->mute);

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
