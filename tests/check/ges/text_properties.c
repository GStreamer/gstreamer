/* GStreamer Editing Services
 * Copyright (C) 2010 Brandon Lewis <brandon.lewis@collabora.co.uk>
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

#include "test-utils.h"
#include <ges/ges.h>
#include <gst/check/gstcheck.h>

GST_START_TEST (test_text_properties_in_layer)
{
  GESTimeline *timeline;
  GESLayer *layer;
  GESTrack *a, *v;
  GESTrackElement *track_element;
  GESTestClip *source;
  gchar *text;
  gint halign, valign;

  ges_init ();

  timeline = ges_timeline_new ();
  layer = ges_layer_new ();
  a = GES_TRACK (ges_audio_track_new ());
  v = GES_TRACK (ges_video_track_new ());

  ges_timeline_add_track (timeline, a);
  ges_timeline_add_track (timeline, v);
  ges_timeline_add_layer (timeline, layer);

  source = ges_test_clip_new ();

  g_object_set (source, "duration", (guint64) GST_SECOND, NULL);

  ges_layer_add_clip (layer, (GESClip *) source);

  track_element =
      ges_clip_find_track_element (GES_CLIP (source), v, GES_TYPE_TEXT_OVERLAY);

  fail_unless (track_element != NULL);
  assert_equals_int (track_element->active, FALSE);

  /* specifically test the text property */
  g_object_set (source, "text", (gchar *) "some text", NULL);
  g_object_get (source, "text", &text, NULL);
  assert_equals_string ("some text", text);
  g_free (text);

  assert_equals_int (track_element->active, TRUE);

  /* test the font-desc property */
  g_object_set (source, "font-desc", (gchar *) "sans 72", NULL);
  g_object_get (source, "font-desc", &text, NULL);
  assert_equals_string ("sans 72", text);
  g_free (text);

  assert_equals_string ("sans 72",
      ges_text_overlay_get_font_desc (GES_TEXT_OVERLAY (track_element)));

  g_object_set (source, "text", (gchar *) NULL, NULL);
  assert_equals_int (track_element->active, FALSE);

  /* test halign and valign */
  g_object_set (source, "halignment", (gint)
      GES_TEXT_HALIGN_LEFT, "valignment", (gint) GES_TEXT_VALIGN_TOP, NULL);
  g_object_get (source, "halignment", &halign, "valignment", &valign, NULL);
  assert_equals_int (halign, GES_TEXT_HALIGN_LEFT);
  assert_equals_int (valign, GES_TEXT_VALIGN_TOP);

  halign = ges_text_overlay_get_halignment (GES_TEXT_OVERLAY (track_element));
  valign = ges_text_overlay_get_valignment (GES_TEXT_OVERLAY (track_element));
  assert_equals_int (halign, GES_TEXT_HALIGN_LEFT);
  assert_equals_int (valign, GES_TEXT_VALIGN_TOP);

  GST_DEBUG ("removing the source");

  ges_layer_remove_clip (layer, (GESClip *) source);

  GST_DEBUG ("removing the layer");

  gst_object_unref (track_element);
  gst_object_unref (timeline);
}

GST_END_TEST;

static Suite *
ges_suite (void)
{
  Suite *s = suite_create ("ges-text-properties");
  TCase *tc_chain = tcase_create ("text_properties");

  suite_add_tcase (s, tc_chain);

  /* Disabled until adding overlays/effect to generic sources
   * is re-added. (Edward, 15th Dec 2010) */
  if (0) {
    tcase_add_test (tc_chain, test_text_properties_in_layer);
  }
  return s;
}

GST_CHECK_MAIN (ges);
