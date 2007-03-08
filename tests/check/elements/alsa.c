/* GStreamer
 *
 * unit test for alsa elements
 *
 * Copyright (C) 2006  Tim-Philipp Müller  <tim centricular net>
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

#include <unistd.h>

#include <gst/check/gstcheck.h>
#include <gst/interfaces/propertyprobe.h>
#include <gst/interfaces/mixer.h>

/* just a simple test that runs device probing on
 * an alsasrc, alsasink and alsamixer instance */

GST_START_TEST (test_device_property_probe)
{
  const gchar *elements[] = { "alsasink", "alsasrc", "alsamixer" };
  gint n;

  for (n = 0; n < G_N_ELEMENTS (elements); ++n) {
    GstPropertyProbe *probe;
    GValueArray *arr;
    GstElement *element;
    gint i;

    element = gst_element_factory_make (elements[n], elements[n]);
    fail_unless (element != NULL);

    probe = GST_PROPERTY_PROBE (element);
    fail_unless (probe != NULL);

    arr = gst_property_probe_probe_and_get_values_name (probe, "device");
    if (arr) {
      for (i = 0; i < arr->n_values; ++i) {
        const gchar *device;
        GValue *val;

        val = g_value_array_get_nth (arr, i);
        fail_unless (val != NULL);
        fail_unless (G_VALUE_HOLDS_STRING (val));

        device = g_value_get_string (val);
        fail_unless (device != NULL);
        GST_LOG_OBJECT (element, "device[%d] = %s", i, device);
      }
      g_value_array_free (arr);
    } else {
      GST_LOG_OBJECT (element, "no devices found");
    }

    gst_object_unref (element);
  }
}

GST_END_TEST;

GST_START_TEST (test_alsa_mixer_track)
{
  GstStateChangeReturn state_ret;
  GstElement *mixer;
  GList *tracks, *l;

  mixer = gst_element_factory_make ("alsamixer", "alsamixer");
  fail_unless (mixer != NULL, "Failed to create 'alsamixer' element!");

  state_ret = gst_element_set_state (mixer, GST_STATE_READY);
  if (state_ret != GST_STATE_CHANGE_SUCCESS) {
    gst_object_unref (mixer);
    return;
  }

  GST_LOG ("opened alsamixer");
  fail_unless (GST_IS_MIXER (mixer), "is not a GstMixer?!");

  tracks = (GList *) gst_mixer_list_tracks (GST_MIXER (mixer));
  for (l = tracks; l != NULL; l = l->next) {
    GstMixerTrack *track;
    gchar *ulabel = NULL, *label = NULL;

    track = GST_MIXER_TRACK (l->data);
    g_object_get (track, "label", &label, "untranslated-label", &ulabel, NULL);
    fail_unless (label == NULL || g_utf8_validate (label, -1, NULL));
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
alsa_suite (void)
{
  Suite *s = suite_create ("alsa");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_device_property_probe);
  tcase_add_test (tc_chain, test_alsa_mixer_track);

  return s;
}

GST_CHECK_MAIN (alsa)
