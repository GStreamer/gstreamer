/* Gstreamer Editing Services
 *
 * Copyright (C) <2012> Thibault Saunier <thibault.saunier@collabora.com>
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

#include <ges/ges.h>


#define NUM_OBJECTS 1000

gint
main (gint argc, gchar * argv[])
{
  guint i;
  GESAsset *asset;
  GESTimeline *timeline;
  GESTimelineLayer *layer;
  GESClip *object;
  GstClockTime start, start_ripple, end, end_ripple, max_rippling_time = 0,
      min_rippling_time = GST_CLOCK_TIME_NONE;

  gst_init (&argc, &argv);
  ges_init ();
  asset = ges_asset_request (GES_TYPE_TEST_CLIP, NULL, NULL);

  layer = ges_timeline_layer_new ();
  timeline = ges_timeline_new_audio_video ();
  ges_timeline_add_layer (timeline, layer);

  start = gst_util_get_timestamp ();
  object = GES_CLIP (ges_timeline_layer_add_asset (layer, asset, 0,
          0, 1000, 1, GES_TRACK_TYPE_UNKNOWN));

  for (i = 1; i < NUM_OBJECTS; i++)
    ges_timeline_layer_add_asset (layer, asset, i * 1000, 0,
        1000, 1, GES_TRACK_TYPE_UNKNOWN);
  end = gst_util_get_timestamp ();
  g_print ("%" GST_TIME_FORMAT " - adding %d object to the timeline\n",
      GST_TIME_ARGS (end - start), i);

  start_ripple = gst_util_get_timestamp ();
  for (i = 1; i < 501; i++) {
    start = gst_util_get_timestamp ();
    ges_clip_edit (object, NULL, 0, GES_EDIT_MODE_NORMAL,
        GES_EDGE_NONE, i * 1000);
    end = gst_util_get_timestamp ();
    max_rippling_time = MAX (max_rippling_time, end - start);
    min_rippling_time = MIN (min_rippling_time, end - start);
  }
  end_ripple = gst_util_get_timestamp ();
  g_print ("%" GST_TIME_FORMAT " - rippling %d times, max: %"
      GST_TIME_FORMAT " min: %" GST_TIME_FORMAT "\n",
      GST_TIME_ARGS (end_ripple - start_ripple), i - 1,
      GST_TIME_ARGS (max_rippling_time), GST_TIME_ARGS (min_rippling_time));


  min_rippling_time = GST_CLOCK_TIME_NONE;
  max_rippling_time = 0;
  ges_timeline_layer_set_auto_transition (layer, TRUE);
  start_ripple = gst_util_get_timestamp ();
  for (i = 1; i < 501; i++) {
    start = gst_util_get_timestamp ();
    ges_clip_edit (object, NULL, 0, GES_EDIT_MODE_NORMAL,
        GES_EDGE_NONE, i * 1000);
    end = gst_util_get_timestamp ();
    max_rippling_time = MAX (max_rippling_time, end - start);
    min_rippling_time = MIN (min_rippling_time, end - start);
  }
  end_ripple = gst_util_get_timestamp ();
  g_print ("%" GST_TIME_FORMAT " - rippling %d times, max: %"
      GST_TIME_FORMAT " min: %" GST_TIME_FORMAT " (with auto-transition on)\n",
      GST_TIME_ARGS (end_ripple - start_ripple), i - 1,
      GST_TIME_ARGS (max_rippling_time), GST_TIME_ARGS (min_rippling_time));

  start = gst_util_get_timestamp ();
  gst_object_unref (timeline);
  end = gst_util_get_timestamp ();
  g_print ("%" GST_TIME_FORMAT " - freeing the timeline\n",
      GST_TIME_ARGS (end - start));

  return 0;
}
