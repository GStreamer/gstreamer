/* GStreamer
 *
 * unit test for timidity/wildmidi
 *
 * Copyright (C) <2007> Stefan Kost <ensonic@users.sf.net>
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

#include <unistd.h>

#include <gst/check/gstcheck.h>
#include <gst/base/gsttypefindhelper.h>


GST_START_TEST (test_midi_typefind)
{
  const guint8 midi_header[] = {
    0x4d, 0x54, 0x68, 0x64, 0x00, 0x00, 0x00, 0x06,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x60, 0x4d, 0x54,
    0x72, 0x6b, 0x00, 0x00
  };
  GstTypeFindProbability prob;
  const gchar *type;
  GstBuffer *buf;
  GstCaps *caps = NULL;

  buf = gst_buffer_new ();
  GST_BUFFER_DATA (buf) = (guint8 *) midi_header;
  GST_BUFFER_SIZE (buf) = sizeof (midi_header);
  GST_BUFFER_OFFSET (buf) = 0;

  caps = gst_type_find_helper_for_buffer (NULL, buf, &prob);
  fail_unless (caps != NULL);
  GST_LOG ("Found type: %" GST_PTR_FORMAT, caps);

  type = gst_structure_get_name (gst_caps_get_structure (caps, 0));
  fail_unless_equals_string (type, "audio/midi");
  fail_unless (prob > GST_TYPE_FIND_MINIMUM && prob <= GST_TYPE_FIND_MAXIMUM);

  gst_buffer_unref (buf);
  gst_caps_unref (caps);

}

GST_END_TEST;


static Suite *
timidity_suite (void)
{
  Suite *s = suite_create ("timidity");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_midi_typefind);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = timidity_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
