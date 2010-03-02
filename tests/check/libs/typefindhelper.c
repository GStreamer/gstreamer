/* GStreamer
 *
 * unit test for typefind helper
 *
 * Copyright (C) 2006 Tim-Philipp Müller  <tim centricular net>
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

#include <gst/base/gsttypefindhelper.h>

static const guint8 vorbisid[30] = { 0x01, 0x76, 0x6f, 0x72, 0x62, 0x69, 0x73,
  0x00, 0x00, 0x00, 0x00, 0x02, 0x44, 0xac, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x03, 0xf4, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0xb8, 0x01
};

static void foobar_typefind (GstTypeFind * tf, gpointer unused);

static GstStaticCaps foobar_caps = GST_STATIC_CAPS ("foo/x-bar");

#define FOOBAR_CAPS (gst_static_caps_get (&foobar_caps))

/* make sure the entire data in the buffer is available for peeking */
GST_START_TEST (test_buffer_range)
{
  static gchar *foobar_exts[] = { (char *) "foobar", NULL };

  GstStructure *s;
  GstBuffer *buf;
  GstCaps *caps;

  fail_unless (gst_type_find_register (NULL, "foo/x-bar",
          GST_RANK_PRIMARY + 50, foobar_typefind, (gchar **) foobar_exts,
          FOOBAR_CAPS, NULL, NULL));

  buf = gst_buffer_new ();
  fail_unless (buf != NULL);
  GST_BUFFER_DATA (buf) = (guint8 *) vorbisid;
  GST_BUFFER_SIZE (buf) = 30;
  GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_READONLY);

  caps = gst_type_find_helper_for_buffer (NULL, buf, NULL);
  fail_unless (caps != NULL);
  fail_unless (GST_CAPS_IS_SIMPLE (caps));
  fail_unless (gst_caps_is_fixed (caps));

  s = gst_caps_get_structure (caps, 0);
  fail_unless (s != NULL);
  fail_unless (gst_structure_has_name (s, "foo/x-bar"));

  gst_caps_unref (caps);
  gst_buffer_unref (buf);
}

GST_END_TEST;

static Suite *
gst_typefindhelper_suite (void)
{
  Suite *s = suite_create ("typefindhelper");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_buffer_range);

  return s;
}

GST_CHECK_MAIN (gst_typefindhelper);

static void
foobar_typefind (GstTypeFind * tf, gpointer unused)
{
  guint8 *data;

  data = gst_type_find_peek (tf, 0, 10);
  fail_unless (data != NULL);
  fail_unless (memcmp (data, vorbisid, 10) == 0);

  data = gst_type_find_peek (tf, 0, 20);
  fail_unless (data != NULL);
  fail_unless (memcmp (data, vorbisid, 20) == 0);

  data = gst_type_find_peek (tf, 0, 30);
  fail_unless (data != NULL);
  fail_unless (memcmp (data, vorbisid, 30) == 0);

  fail_unless (gst_type_find_peek (tf, 0, 31) == NULL);
  fail_unless (gst_type_find_peek (tf, 1, 30) == NULL);
  fail_unless (gst_type_find_peek (tf, 25, 6) == NULL);

  data = gst_type_find_peek (tf, 1, 29);
  fail_unless (data != NULL);
  fail_unless (memcmp (data, vorbisid + 1, 29) == 0);

  data = gst_type_find_peek (tf, 25, 4);
  fail_unless (data != NULL);
  fail_unless (memcmp (data, vorbisid + 25, 4) == 0);

  fail_unless (gst_type_find_peek (tf, -1, 29) == NULL);
  fail_unless (gst_type_find_peek (tf, -1, 1) == NULL);
  fail_unless (gst_type_find_peek (tf, -1, 0) == NULL);

  gst_type_find_suggest (tf, GST_TYPE_FIND_MAXIMUM, FOOBAR_CAPS);
}
