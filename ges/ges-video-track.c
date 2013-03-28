/* GStreamer Editing Services
 * Copyright (C) <2013> Thibault Saunier <thibault.saunier@collabora.com>
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
 * SECTION: ges-video-track:
 * @short_description: A standard GESTrack for raw video
 */

#include "ges-video-track.h"

struct _GESVideoTrackPrivate
{
  gpointer nothing;
};

#define GES_VIDEO_TRACK_GET_PRIVATE(o)  (G_TYPE_INSTANCE_GET_PRIVATE ((o), GES_TYPE_VIDEO_TRACK, GESVideoTrackPrivate))

G_DEFINE_TYPE (GESVideoTrack, ges_video_track, GES_TYPE_TRACK);

static GstElement *
create_element_for_raw_video_gap (GESTrack * track)
{
  return gst_parse_bin_from_description
      ("videotestsrc pattern=2 name=src ! capsfilter caps=video/x-raw", TRUE,
      NULL);
}

static void
ges_video_track_init (GESVideoTrack * ges_video_track)
{
/*   GESVideoTrackPrivate *priv = GES_VIDEO_TRACK_GET_PRIVATE (ges_video_track);
 */

  /* TODO: Add initialization code here */
}

static void
ges_video_track_finalize (GObject * object)
{
  /* TODO: Add deinitalization code here */

  G_OBJECT_CLASS (ges_video_track_parent_class)->finalize (object);
}

static void
ges_video_track_class_init (GESVideoTrackClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
/*   GESTrackClass *parent_class = GES_TRACK_CLASS (klass);
 */

  g_type_class_add_private (klass, sizeof (GESVideoTrackPrivate));

  object_class->finalize = ges_video_track_finalize;
}

/**
 * ges_video_track_new:
 *
 * Creates a new #GESVideoTrack of type #GES_TRACK_TYPE_VIDEO and with generic
 * raw video caps ("video/x-raw");
 *
 * Returns: A new #GESTrack.
 */
GESVideoTrack *
ges_video_track_new (void)
{
  GESVideoTrack *ret;
  GstCaps *caps = gst_caps_new_empty_simple ("video/x-raw");

  ret = g_object_new (GES_TYPE_VIDEO_TRACK, "track-type", GES_TRACK_TYPE_VIDEO,
      "caps", caps, NULL);

  ges_track_set_create_element_for_gap_func (GES_TRACK (ret),
      create_element_for_raw_video_gap);

  return ret;
}
