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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
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
  GstStructure *s;
  GstBuffer *buf;
  GstCaps *caps;

  fail_unless (gst_type_find_register (NULL, "foo/x-bar",
          GST_RANK_PRIMARY + 50, foobar_typefind, "foobar",
          FOOBAR_CAPS, NULL, NULL));

  buf = gst_buffer_new ();
  fail_unless (buf != NULL);

  gst_buffer_insert_memory (buf, -1,
      gst_memory_new_wrapped (GST_MEMORY_FLAG_READONLY,
          (gpointer) vorbisid, 30, 0, 30, NULL, NULL));

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

/* A stream long enough for the helper's 4096-byte pull and for a request
 * that reaches past it. */
#define BOUNDARY_STREAM_SIZE 8192

/* Counts the getrange calls made while the boundary finder runs; other
 * finders' pulls are excluded so the numbers are deterministic. */
static guint boundary_pulls;
static gboolean boundary_in_finder;
static guint boundary_pulls_in_finder;

static GstFlowReturn
boundary_get_range (GstObject * obj, GstObject * parent, guint64 offset,
    guint length, GstBuffer ** buffer)
{
  guint8 *data;
  guint n;

  if (offset >= BOUNDARY_STREAM_SIZE)
    return GST_FLOW_EOS;

  n = MIN (length, (guint) (BOUNDARY_STREAM_SIZE - offset));
  data = g_malloc (n);
  for (guint i = 0; i < n; i++)
    data[i] = (guint8) (offset + i);
  *buffer = gst_buffer_new_wrapped (data, n);
  GST_BUFFER_OFFSET (*buffer) = offset;

  boundary_pulls++;
  if (boundary_in_finder)
    boundary_pulls_in_finder++;

  return GST_FLOW_OK;
}

static void
boundary_typefind (GstTypeFind * tf, gpointer unused)
{
  const guint8 *data;

  boundary_in_finder = TRUE;

  /* The first peek pulls the stream. */
  data = gst_type_find_peek (tf, 0, 4096);
  if (data == NULL) {
    boundary_in_finder = FALSE;
    return;
  }
  fail_unless_equals_int (boundary_pulls_in_finder, 1);

  /* The same range again ends exactly at the end of the cached buffer and
   * is covered by it: it must not read the stream again. */
  fail_unless (gst_type_find_peek (tf, 0, 4096) != NULL);
  fail_unless_equals_int (boundary_pulls_in_finder, 1);

  /* A range inside the cached buffer is covered too. */
  fail_unless (gst_type_find_peek (tf, 1024, 1024) != NULL);
  fail_unless_equals_int (boundary_pulls_in_finder, 1);

  /* A range that reaches past the cached buffer needs another read. */
  fail_unless (gst_type_find_peek (tf, 0, 5000) != NULL);
  fail_unless_equals_int (boundary_pulls_in_finder, 2);

  /* And the extended buffer now covers that range exactly. */
  fail_unless (gst_type_find_peek (tf, 0, 5000) != NULL);
  fail_unless_equals_int (boundary_pulls_in_finder, 2);

  /* A zero-length peek asks for no data and reads nothing. */
  fail_unless (gst_type_find_peek (tf, 0, 0) == NULL);
  fail_unless_equals_int (boundary_pulls_in_finder, 2);

  /* A buffer at another offset, so the next hit is not on the head buffer. */
  fail_unless (gst_type_find_peek (tf, 4096, 4096) != NULL);
  fail_unless_equals_int (boundary_pulls_in_finder, 3);

  /* This range ends exactly at the end of the buffer at offset 0, which is
   * now second in the list. */
  fail_unless (gst_type_find_peek (tf, 1000, 4000) != NULL);
  fail_unless_equals_int (boundary_pulls_in_finder, 3);

  boundary_in_finder = FALSE;

  /* Three reads in total: the first two buffers and the one at 4096. */
  fail_unless_equals_int (boundary_pulls, 3);

  gst_type_find_suggest_empty_simple (tf, GST_TYPE_FIND_MAXIMUM,
      "foo/x-boundary");
}

/* A peek that ends exactly at the end of a cached buffer is covered by it.
 * The helper used to treat that boundary as a miss, read the same range
 * again and insert a duplicate buffer, so every finder's first peek re-read
 * the file and the buffer list grew without bound.
 *
 * The factory stays registered for the rest of the test binary, like the
 * foo/x-bar one above; its rank keeps it ahead of every other factory and
 * its MAXIMUM suggestion stops the helper after it. */
GST_START_TEST (test_pull_range_boundary)
{
  GstObject *obj;
  GstCaps *caps;
  GstTypeFindProbability prob = GST_TYPE_FIND_NONE;

  boundary_pulls = 0;
  boundary_pulls_in_finder = 0;
  boundary_in_finder = FALSE;

  fail_unless (gst_type_find_register (NULL, "foo/x-boundary",
          GST_RANK_PRIMARY + 1000, boundary_typefind, NULL, NULL, NULL, NULL));

  obj = GST_OBJECT (gst_bin_new (NULL));
  caps = NULL;
  fail_unless_equals_int (gst_type_find_helper_get_range_full (obj, NULL,
          boundary_get_range, BOUNDARY_STREAM_SIZE, NULL, &caps, &prob),
      GST_FLOW_OK);
  fail_unless (caps != NULL);
  fail_unless_equals_int (prob, GST_TYPE_FIND_MAXIMUM);

  gst_caps_unref (caps);
  gst_object_unref (obj);
}

GST_END_TEST;

static Suite *
gst_typefindhelper_suite (void)
{
  Suite *s = suite_create ("typefindhelper");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_buffer_range);
  tcase_add_test (tc_chain, test_pull_range_boundary);

  return s;
}

GST_CHECK_MAIN (gst_typefindhelper);

static void
foobar_typefind (GstTypeFind * tf, gpointer unused)
{
  const guint8 *data;

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
