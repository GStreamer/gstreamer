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
#include "ges-track.h"
#include "ges-track-object.h"

/**
 * GESTrack
 *
 * Corresponds to one output format (i.e. audio OR video)
 *
 * Contains the compatible TrackObject(s)
 */

G_DEFINE_TYPE (GESTrack, ges_track, GST_TYPE_BIN);

static void
ges_track_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_track_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_track_dispose (GObject * object)
{
  G_OBJECT_CLASS (ges_track_parent_class)->dispose (object);
}

static void
ges_track_finalize (GObject * object)
{
  G_OBJECT_CLASS (ges_track_parent_class)->finalize (object);
}

static void
ges_track_class_init (GESTrackClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = ges_track_get_property;
  object_class->set_property = ges_track_set_property;
  object_class->dispose = ges_track_dispose;
  object_class->finalize = ges_track_finalize;
}

static void
ges_track_init (GESTrack * self)
{
}

GESTrack *
ges_track_new (void)
{
  return g_object_new (GES_TYPE_TRACK, NULL);
}

void
ges_track_set_timeline (GESTrack * track, GESTimeline * timeline)
{
  GST_DEBUG ("track:%p, timeline:%p", track, timeline);

  track->timeline = timeline;
}

gboolean
ges_track_add_object (GESTrack * track, GESTrackObject * object)
{
  GST_DEBUG ("track:%p, object:%p", track, object);

  if (G_UNLIKELY (object->track != NULL)) {
    GST_WARNING ("Object already belongs to another track");
    return FALSE;
  }

  if (G_UNLIKELY (object->gnlobject != NULL)) {
    GST_ERROR ("TrackObject doesn't have a gnlobject !");
    return FALSE;
  }

  ges_track_object_set_track (object, track);

  GST_DEBUG ("Adding object to ourself");

  /* make sure the object has a valid gnlobject ! */
  if (G_UNLIKELY (!gst_bin_add (GST_BIN (track->composition),
              object->gnlobject))) {
    GST_WARNING ("Couldn't add object to the GnlComposition");
    return FALSE;
  }

  return TRUE;
}
