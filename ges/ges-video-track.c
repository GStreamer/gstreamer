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
 * SECTION: gesvideotrack
 * @short_description: A standard GESTrack for raw video
 */

#include "ges-video-track.h"
#include "ges-smart-video-mixer.h"

struct _GESVideoTrackPrivate
{
  gpointer nothing;
};

#define GES_VIDEO_TRACK_GET_PRIVATE(o)  (G_TYPE_INSTANCE_GET_PRIVATE ((o), GES_TYPE_VIDEO_TRACK, GESVideoTrackPrivate))

G_DEFINE_TYPE (GESVideoTrack, ges_video_track, GES_TYPE_TRACK);

static void
_sync_capsfilter_with_track (GESTrack * track, GstElement * capsfilter)
{
  GstCaps *restriction, *caps;
  gint fps_n, fps_d;
  GstStructure *structure;

  g_object_get (track, "restriction-caps", &restriction, NULL);
  if (restriction && gst_caps_get_size (restriction) > 0) {

    structure = gst_caps_get_structure (restriction, 0);
    if (!gst_structure_get_fraction (structure, "framerate", &fps_n, &fps_d))
      return;
  } else {
    return;
  }

  caps =
      gst_caps_new_simple ("video/x-raw", "framerate", GST_TYPE_FRACTION, fps_n,
      fps_d, NULL);

  g_object_set (capsfilter, "caps", caps, NULL);
}

static void
_track_restriction_changed_cb (GESTrack * track, GParamSpec * arg G_GNUC_UNUSED,
    GstElement * capsfilter)
{
  _sync_capsfilter_with_track (track, capsfilter);
}

static void
_weak_notify_cb (GESTrack * track, GstElement * capsfilter)
{
  g_signal_handlers_disconnect_by_func (track,
      (GCallback) _track_restriction_changed_cb, capsfilter);
}

static GstElement *
create_element_for_raw_video_gap (GESTrack * track)
{
  GstElement *bin;
  GstElement *capsfilter;

  bin = gst_parse_bin_from_description
      ("videotestsrc pattern=2 name=src ! videorate ! capsfilter name=gapfilter caps=video/x-raw",
      TRUE, NULL);

  capsfilter = gst_bin_get_by_name (GST_BIN (bin), "gapfilter");
  g_object_weak_ref (G_OBJECT (capsfilter), (GWeakNotify) _weak_notify_cb,
      track);
  g_signal_connect (track, "notify::restriction-caps",
      (GCallback) _track_restriction_changed_cb, capsfilter);

  _sync_capsfilter_with_track (track, capsfilter);

  gst_object_unref (capsfilter);

  return bin;
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

  GES_TRACK_CLASS (klass)->get_mixing_element = ges_smart_mixer_new;
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

  gst_caps_unref (caps);

  return ret;
}
