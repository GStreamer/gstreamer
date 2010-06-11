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
 * SECTION:ges-timeline-backgroundsource
 * @short_description: An object for manipulating media files in a GESTimeline
 * 
 * Represents all the output treams from a particular uri. It is assumed that
 * the URI points to a file of some type.
 */

#include "ges-internal.h"
#include "ges-timeline-background-source.h"
#include "ges-timeline-source.h"
#include "ges-track-object.h"
#include "ges-track-video-background-source.h"
#include "ges-track-audio-background-source.h"

G_DEFINE_TYPE (GESTimelineBackgroundSource, ges_tl_bg_src,
    GES_TYPE_TIMELINE_SOURCE);

enum
{
  PROP_0,
  PROP_MUTE,
};

static void
ges_tl_bg_src_set_mute (GESTimelineBackgroundSource * self, gboolean mute);

static GESTrackObject
    * ges_tl_bg_src_create_track_object (GESTimelineObject * obj,
    GESTrack * track);

static void
ges_tl_bg_src_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GESTimelineBackgroundSource *tfs = GES_TIMELINE_BACKGROUND_SOURCE (object);

  switch (property_id) {
    case PROP_MUTE:
      g_value_set_boolean (value, tfs->mute);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_tl_bg_src_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GESTimelineBackgroundSource *tfs = GES_TIMELINE_BACKGROUND_SOURCE (object);

  switch (property_id) {
    case PROP_MUTE:
      ges_tl_bg_src_set_mute (tfs, g_value_get_boolean (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_tl_bg_src_dispose (GObject * object)
{
  G_OBJECT_CLASS (ges_tl_bg_src_parent_class)->dispose (object);
}

static void
ges_tl_bg_src_finalize (GObject * object)
{
  G_OBJECT_CLASS (ges_tl_bg_src_parent_class)->finalize (object);
}

static void
ges_tl_bg_src_class_init (GESTimelineBackgroundSourceClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GESTimelineObjectClass *timobj_class = GES_TIMELINE_OBJECT_CLASS (klass);

  object_class->get_property = ges_tl_bg_src_get_property;
  object_class->set_property = ges_tl_bg_src_set_property;
  object_class->dispose = ges_tl_bg_src_dispose;
  object_class->finalize = ges_tl_bg_src_finalize;

  /**
   * GESTimelineBackgroundSource:mute:
   *
   * Whether the sound will be played or not.
   */
  g_object_class_install_property (object_class, PROP_MUTE,
      g_param_spec_boolean ("mute", "Mute", "Mute audio track",
          FALSE, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  timobj_class->create_track_object = ges_tl_bg_src_create_track_object;
  timobj_class->need_fill_track = FALSE;
}

static void
ges_tl_bg_src_init (GESTimelineBackgroundSource * self)
{
  /* Setting the duration to -1 by default. */
  GES_TIMELINE_OBJECT (self)->duration = GST_CLOCK_TIME_NONE;
}

static void
ges_tl_bg_src_set_mute (GESTimelineBackgroundSource * self, gboolean mute)
{
  GList *tmp;
  GESTimelineObject *object = (GESTimelineObject *) self;

  GST_DEBUG ("self:%p, mute:%d", self, mute);

  self->mute = mute;

  /* Go over tracked objects, and update 'active' status on all audio objects */
  for (tmp = object->trackobjects; tmp; tmp = tmp->next) {
    GESTrackObject *trackobject = (GESTrackObject *) tmp->data;

    if (trackobject->track->type == GES_TRACK_TYPE_AUDIO)
      ges_track_object_set_active (trackobject, !mute);
  }
}

static GESTrackObject *
ges_tl_bg_src_create_track_object (GESTimelineObject * obj, GESTrack * track)
{
  GESTimelineBackgroundSource *tfs = (GESTimelineBackgroundSource *) obj;
  GESTrackObject *res = NULL;

  GST_DEBUG ("Creating a GESTrackBackgroundSource");

  if (track->type == GES_TRACK_TYPE_VIDEO) {
    res = (GESTrackObject *) ges_track_video_background_source_new ();
  }

  else if (track->type == GES_TRACK_TYPE_AUDIO) {
    res = (GESTrackObject *) ges_track_audio_background_source_new ();
    if (tfs->mute)
      ges_track_object_set_active (res, FALSE);
  }

  else {
    res = (GESTrackObject *) ges_track_background_source_new ();
  }

  return res;
}

/**
 * ges_timeline_backgroundsource_new:
 * @uri: the URI the source should control
 *
 * Creates a new #GESTimelineBackgroundSource for the provided @uri.
 *
 * Returns: The newly created #GESTimelineBackgroundSource, or NULL if there was an
 * error.
 */
GESTimelineBackgroundSource *
ges_timeline_background_source_new (void)
{
  /* FIXME : Check for validity/existence of URI */
  return g_object_new (GES_TYPE_TIMELINE_BACKGROUND_SOURCE, NULL);
}
