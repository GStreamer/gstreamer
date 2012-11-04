/* GStreamer unit tests for the sun audio elements
 *
 * Copyright (C) 2007 Tim-Philipp Müller  <tim centricular net>
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

#include <gst/check/gstcheck.h>
#include <gst/interfaces/propertyprobe.h>
#include <gst/interfaces/mixer.h>
#include <gst/gst.h>

GST_START_TEST (test_sun_audio_mixer_track)
{
  GstStateChangeReturn state_ret;
  GstElement *mixer;
  GList *tracks, *l;

  mixer = gst_element_factory_make ("sunaudiomixer", "sunaudiomixer");
  fail_unless (mixer != NULL, "Failed to create 'sunaudiomixer' element!");

  state_ret = gst_element_set_state (mixer, GST_STATE_READY);
  if (state_ret != GST_STATE_CHANGE_SUCCESS) {
    gst_object_unref (mixer);
    return;
  }

  GST_LOG ("opened sunaudiomixer");
  fail_unless (GST_IS_MIXER (mixer), "is not a GstMixer?!");

  tracks = (GList *) gst_mixer_list_tracks (GST_MIXER (mixer));
  for (l = tracks; l != NULL; l = l->next) {
    GObjectClass *klass;
    GstMixerTrack *track;
    gchar *ulabel = NULL, *label = NULL;

    track = GST_MIXER_TRACK (l->data);

    g_object_get (track, "label", &label, NULL);
    fail_unless (label == NULL || g_utf8_validate (label, -1, NULL));

    /* FIXME: remove this check once we depend on -base >= 0.10.12.1 */
    klass = G_OBJECT_GET_CLASS (track);
    if (g_object_class_find_property (klass, "untranslated-label")) {
      g_object_get (track, "untranslated-label", &ulabel, NULL);
    }

    if (ulabel != NULL) {
      gchar *p;

      for (p = ulabel; p != NULL && *p != '\0'; ++p) {
        fail_unless (g_ascii_isprint (*p),
            "untranslated label '%s' not printable ASCII", ulabel);
      }
    }
    GST_DEBUG ("%s: %s", GST_STR_NULL (ulabel), GST_STR_NULL (label));
    g_free (label);
    g_free (ulabel);
  }

  fail_unless_equals_int (gst_element_set_state (mixer, GST_STATE_NULL),
      GST_STATE_CHANGE_SUCCESS);

  gst_object_unref (mixer);
}

GST_END_TEST;


static Suite *
sunaudio_suite (void)
{
  Suite *s = suite_create ("sunaudio");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_sun_audio_mixer_track);

  return s;
}

GST_CHECK_MAIN (sunaudio)
