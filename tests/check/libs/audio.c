/* GStreamer
 *
 * unit tests for audio support library
 *
 * Copyright (C) 2006 Tim-Philipp Müller <tim centricular net>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/check/gstcheck.h>

#include <gst/audio/audio.h>
#include <gst/audio/multichannel.h>
#include <string.h>

static gboolean
structure_contains_channel_positions (const GstStructure * s)
{
  return (gst_structure_get_value (s, "channel-positions") != NULL);
}

#if 0
static gboolean
fixed_caps_have_channel_positions (const GstCaps * caps)
{
  GstStructure *s;

  fail_unless (caps != NULL);

  s = gst_caps_get_structure (caps, 0);
  fail_unless (s != NULL);

  return structure_contains_channel_positions (s);
}
#endif

GST_START_TEST (test_multichannel_checks)
{
  GstAudioChannelPosition pos_2_mixed[2] = {
    GST_AUDIO_CHANNEL_POSITION_FRONT_MONO,
    GST_AUDIO_CHANNEL_POSITION_NONE
  };
  GstAudioChannelPosition pos_2_none[2] = {
    GST_AUDIO_CHANNEL_POSITION_NONE,
    GST_AUDIO_CHANNEL_POSITION_NONE
  };
  GstAudioChannelPosition pos_2_flr[2] = {
    GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
    GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT
  };
  GstAudioChannelPosition pos_2_frr[2] = {
    GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
    GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT
  };
  GstStructure *s;

  s = gst_structure_new ("audio/x-raw-int", "channels", G_TYPE_INT, 2, NULL);

  /* this should not work and issue a warning: FRONT_MONO + NONE */
  _gst_check_expecting_log = TRUE;
  gst_audio_set_channel_positions (s, pos_2_mixed);
  _gst_check_expecting_log = FALSE;
  fail_if (structure_contains_channel_positions (s));

  /* this should work: NONE + NONE */
  gst_audio_set_channel_positions (s, pos_2_none);
  fail_unless (structure_contains_channel_positions (s));
  gst_structure_remove_field (s, "channel-positions");

  /* this should also work: FRONT_LEFT + FRONT_RIGHT */
  gst_audio_set_channel_positions (s, pos_2_flr);
  fail_unless (structure_contains_channel_positions (s));
  gst_structure_remove_field (s, "channel-positions");

  /* this should not work and issue a warning: FRONT_RIGHT twice */
  _gst_check_expecting_log = TRUE;
  gst_audio_set_channel_positions (s, pos_2_frr);
  _gst_check_expecting_log = FALSE;

/* FIXME: did I misunderstand _set_structure_channel_positions_list? */
#if  0
  /* this should not work and issue a warning: FRONT_RIGHT twice */
  _gst_check_expecting_log = TRUE;
  gst_audio_set_structure_channel_positions_list (s, pos_2_frr, 2);
  _gst_check_expecting_log = FALSE;

  /* this should not work and issue a warning: FRONT_MONO + NONE */
  _gst_check_expecting_log = TRUE;
  gst_audio_set_structure_channel_positions_list (s, pos_2_mixed, 2);
  _gst_check_expecting_log = FALSE;

  /* this should not work either (channel count mismatch) */
  _gst_check_expecting_log = TRUE;
  gst_audio_set_structure_channel_positions_list (s, pos_2_none, 44);
  _gst_check_expecting_log = FALSE;
  fail_if (structure_contains_channel_positions (s));
#endif

  gst_structure_free (s);
}

GST_END_TEST;

static Suite *
audio_suite (void)
{
  Suite *s = suite_create ("audio support library");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_multichannel_checks);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = audio_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
