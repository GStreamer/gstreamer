/* GStreamer
 *
 * unit test for the taglib-based apev2mux element
 *
 * Copyright (C) 2006 Tim-Philipp Müller  <tim centricular net>
 * Copyright (C) 2006 Sebastian Dröge <slomo@circular-chaos.org>
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

#include <gst/gst.h>
#include <string.h>

#define TEST_ARTIST           "Ar T\303\255st"
#define TEST_TITLE            "M\303\274llermilch!"
#define TEST_ALBUM            "Boom"
#define TEST_DATE             g_date_new_dmy(1,1,2006)
#define TEST_TRACK_NUMBER     7
#define TEST_TRACK_COUNT      19
#define TEST_TRACK_GAIN       1.45
#define TEST_ALBUM_GAIN       0.78

/* for dummy mp3 frame sized MP3_FRAME_SIZE bytes,
 * start: ff fb b0 44 00 00 08 00  00 4b 00 00 00 00 00 00 */
static const guint8 mp3_dummyhdr[] = { 0xff, 0xfb, 0xb0, 0x44, 0x00, 0x00,
  0x08, 0x00, 0x4b, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00
};

#define MP3_FRAME_SIZE 626

static GstTagList *
test_taglib_apev2mux_create_tags (guint32 mask)
{
  GstTagList *tags;

  tags = gst_tag_list_new_empty ();

  if (mask & (1 << 0)) {
    gst_tag_list_add (tags, GST_TAG_MERGE_KEEP,
        GST_TAG_ARTIST, TEST_ARTIST, NULL);
  }
  if (mask & (1 << 1)) {
    gst_tag_list_add (tags, GST_TAG_MERGE_KEEP,
        GST_TAG_TITLE, TEST_TITLE, NULL);
  }
  if (mask & (1 << 2)) {
    gst_tag_list_add (tags, GST_TAG_MERGE_KEEP,
        GST_TAG_ALBUM, TEST_ALBUM, NULL);
  }
  if (mask & (1 << 3)) {
    GDate *date;

    date = TEST_DATE;
    gst_tag_list_add (tags, GST_TAG_MERGE_KEEP, GST_TAG_DATE, date, NULL);
    g_date_free (date);
  }
  if (mask & (1 << 4)) {
    gst_tag_list_add (tags, GST_TAG_MERGE_KEEP,
        GST_TAG_TRACK_NUMBER, TEST_TRACK_NUMBER, NULL);
  }
  if (mask & (1 << 5)) {
    gst_tag_list_add (tags, GST_TAG_MERGE_KEEP,
        GST_TAG_TRACK_COUNT, TEST_TRACK_COUNT, NULL);
  }
  if (mask & (1 << 6)) {
    gst_tag_list_add (tags, GST_TAG_MERGE_KEEP,
        GST_TAG_TRACK_GAIN, TEST_TRACK_GAIN, NULL);
  }
  if (mask & (1 << 7)) {
    gst_tag_list_add (tags, GST_TAG_MERGE_KEEP,
        GST_TAG_ALBUM_GAIN, TEST_ALBUM_GAIN, NULL);
  }
  if (mask & (1 << 8)) {
  }
  if (mask & (1 << 9)) {
  }
  if (mask & (1 << 10)) {
  }
  if (mask & (1 << 11)) {
  }
  if (mask & (1 << 12)) {
  }
  if (mask & (1 << 13)) {
  }
  return tags;
}

static void
test_taglib_apev2mux_check_tags (GstTagList * tags, guint32 mask)
{
  if (mask & (1 << 0)) {
    gchar *s = NULL;

    fail_unless (gst_tag_list_get_string (tags, GST_TAG_ARTIST, &s));
    fail_unless (g_str_equal (s, TEST_ARTIST));
    g_free (s);
  }
  if (mask & (1 << 1)) {
    gchar *s = NULL;

    fail_unless (gst_tag_list_get_string (tags, GST_TAG_TITLE, &s));
    fail_unless (g_str_equal (s, TEST_TITLE));
    g_free (s);
  }
  if (mask & (1 << 2)) {
    gchar *s = NULL;

    fail_unless (gst_tag_list_get_string (tags, GST_TAG_ALBUM, &s));
    fail_unless (g_str_equal (s, TEST_ALBUM));
    g_free (s);
  }
  if (mask & (1 << 3)) {
    GDate *shouldbe, *date = NULL;

    shouldbe = TEST_DATE;
    fail_unless (gst_tag_list_get_date (tags, GST_TAG_DATE, &date));
    fail_unless (g_date_compare (shouldbe, date) == 0);
    g_date_free (shouldbe);
    g_date_free (date);
  }
  if (mask & (1 << 4)) {
    guint num;

    fail_unless (gst_tag_list_get_uint (tags, GST_TAG_TRACK_NUMBER, &num));
    fail_unless (num == TEST_TRACK_NUMBER);
  }
  if (mask & (1 << 5)) {
    guint count;

    fail_unless (gst_tag_list_get_uint (tags, GST_TAG_TRACK_COUNT, &count));
    fail_unless (count == TEST_TRACK_COUNT);
  }
  if (mask & (1 << 6)) {
    gdouble gain;

    fail_unless (gst_tag_list_get_double (tags, GST_TAG_TRACK_GAIN, &gain));
    fail_unless (gain == TEST_TRACK_GAIN);
  }
  if (mask & (1 << 7)) {
    gdouble gain;

    fail_unless (gst_tag_list_get_double (tags, GST_TAG_ALBUM_GAIN, &gain));
    fail_unless (gain == TEST_ALBUM_GAIN);
  }
  if (mask & (1 << 8)) {
  }
  if (mask & (1 << 9)) {
  }
  if (mask & (1 << 10)) {
  }
  if (mask & (1 << 11)) {
  }
  if (mask & (1 << 12)) {
  }
  if (mask & (1 << 13)) {
  }
}

static void
fill_mp3_buffer (GstElement * fakesrc, GstBuffer * buf, GstPad * pad,
    guint64 * p_offset)
{
  gsize size;

  size = gst_buffer_get_size (buf);

  fail_unless (size == MP3_FRAME_SIZE);

  GST_LOG ("filling buffer with fake mp3 data, offset = %" G_GUINT64_FORMAT,
      *p_offset);

  gst_buffer_fill (buf, 0, mp3_dummyhdr, sizeof (mp3_dummyhdr));

#if 0
  /* can't use gst_buffer_set_caps() here because the metadata isn't writable
   * because of the extra refcounts taken by the signal emission mechanism;
   * we know it's fine to use GST_BUFFER_CAPS() here though */
  GST_BUFFER_CAPS (buf) = gst_caps_new_simple ("audio/mpeg", "mpegversion",
      G_TYPE_INT, 1, "layer", G_TYPE_INT, 3, NULL);
#endif

  GST_BUFFER_OFFSET (buf) = *p_offset;
  *p_offset += size;
}

static void
got_buffer (GstElement * fakesink, GstBuffer * buf, GstPad * pad,
    GstBuffer ** p_buf)
{
  gint64 off;
  GstMapInfo map;

  off = GST_BUFFER_OFFSET (buf);

  gst_buffer_map (buf, &map, GST_MAP_READ);

  GST_LOG ("size=%" G_GSIZE_FORMAT ", offset=%" G_GINT64_FORMAT, map.size, off);

  fail_unless (GST_BUFFER_OFFSET_IS_VALID (buf));

  if (*p_buf == NULL || (off + map.size) > gst_buffer_get_size (*p_buf)) {
    GstBuffer *newbuf;

    /* not very elegant, but who cares */
    newbuf = gst_buffer_new_and_alloc (off + map.size);
    if (*p_buf) {
      GstMapInfo pmap;

      gst_buffer_map (*p_buf, &pmap, GST_MAP_READ);
      gst_buffer_fill (newbuf, 0, pmap.data, pmap.size);
      gst_buffer_unmap (*p_buf, &pmap);
    }
    gst_buffer_fill (newbuf, off, map.data, map.size);
    if (*p_buf)
      gst_buffer_unref (*p_buf);
    *p_buf = newbuf;
  } else {
    gst_buffer_fill (*p_buf, off, map.data, map.size);
  }
  gst_buffer_unmap (buf, &map);
}

static void
test_taglib_apev2mux_check_output_buffer (GstBuffer * buf)
{
  GstMapInfo map;
  guint off;

  gst_buffer_map (buf, &map, GST_MAP_READ);
  fail_unless_equals_uint64 (map.size % MP3_FRAME_SIZE, 0);

  for (off = 0; off < map.size; off += MP3_FRAME_SIZE) {
    fail_unless (memcmp (map.data + off, mp3_dummyhdr,
            sizeof (mp3_dummyhdr)) == 0);
  }
  gst_buffer_unmap (buf, &map);
}

static void
test_taglib_apev2mux_with_tags (GstTagList * tags, guint32 mask)
{
  GstMessage *msg;
  GstTagList *tags_read = NULL;
  GstElement *pipeline, *apev2mux, *apedemux, *fakesrc, *fakesink;
  GstBus *bus;
  guint64 offset;
  GstBuffer *outbuf = NULL;

  pipeline = gst_pipeline_new ("pipeline");
  g_assert_nonnull (pipeline);

  fakesrc = gst_element_factory_make ("fakesrc", "fakesrc");
  g_assert_nonnull (fakesrc);

  apev2mux = gst_element_factory_make ("apev2mux", "apev2mux");
  g_assert_nonnull (apev2mux);

  apedemux = gst_element_factory_make ("apedemux", "apedemux");
  g_assert_nonnull (apedemux);

  fakesink = gst_element_factory_make ("fakesink", "fakesink");
  g_assert_nonnull (fakesink);

  /* set up sink */
  outbuf = NULL;
  g_object_set (fakesink, "signal-handoffs", TRUE, NULL);
  g_signal_connect (fakesink, "handoff", G_CALLBACK (got_buffer), &outbuf);

  gst_bin_add (GST_BIN (pipeline), fakesrc);
  gst_bin_add (GST_BIN (pipeline), apev2mux);
  gst_bin_add (GST_BIN (pipeline), apedemux);
  gst_bin_add (GST_BIN (pipeline), fakesink);

  gst_tag_setter_merge_tags (GST_TAG_SETTER (apev2mux), tags,
      GST_TAG_MERGE_APPEND);

  gst_element_link_many (fakesrc, apev2mux, apedemux, fakesink, NULL);

  /* set up source */
  g_object_set (fakesrc, "signal-handoffs", TRUE, "can-activate-pull", FALSE,
      "filltype", 2, "sizetype", 2, "sizemax", MP3_FRAME_SIZE,
      "num-buffers", 16, NULL);

  offset = 0;
  g_signal_connect (fakesrc, "handoff", G_CALLBACK (fill_mp3_buffer), &offset);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  fail_unless (gst_element_get_state (pipeline, NULL, NULL,
          -1) == GST_STATE_CHANGE_SUCCESS);

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));

  GST_LOG ("Waiting for tag ...");
  msg =
      gst_bus_poll (bus, GST_MESSAGE_TAG | GST_MESSAGE_EOS | GST_MESSAGE_ERROR,
      -1);
  if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR) {
    GError *err;
    gchar *dbg;

    gst_message_parse_error (msg, &err, &dbg);
    g_printerr ("ERROR from element %s: %s\n%s\n",
        GST_OBJECT_NAME (msg->src), err->message, GST_STR_NULL (dbg));
    g_error_free (err);
    g_free (dbg);
  } else if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_EOS) {
    g_printerr ("EOS message, but were waiting for TAGS!\n");
  }
  fail_unless (msg->type == GST_MESSAGE_TAG);

  gst_message_parse_tag (msg, &tags_read);
  gst_message_unref (msg);

  GST_LOG ("Got tags: %" GST_PTR_FORMAT, tags_read);
  test_taglib_apev2mux_check_tags (tags_read, mask);
  gst_tag_list_unref (tags_read);

  GST_LOG ("Waiting for EOS ...");
  msg = gst_bus_poll (bus, GST_MESSAGE_EOS | GST_MESSAGE_ERROR, -1);
  if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR) {
    GError *err;
    gchar *dbg;

    gst_message_parse_error (msg, &err, &dbg);
    g_printerr ("ERROR from element %s: %s\n%s\n",
        GST_OBJECT_NAME (msg->src), err->message, GST_STR_NULL (dbg));
    g_error_free (err);
    g_free (dbg);
  }
  fail_unless (msg->type == GST_MESSAGE_EOS);
  gst_message_unref (msg);

  gst_object_unref (bus);

  GST_LOG ("Got EOS, shutting down ...");
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);

  test_taglib_apev2mux_check_output_buffer (outbuf);
  gst_buffer_unref (outbuf);

  GST_LOG ("Done");
}

GST_START_TEST (test_apev2mux)
{
  GstTagList *tags;
  gint i;

  g_random_set_seed (247166295);

  /* internal consistency check */
  tags = test_taglib_apev2mux_create_tags (0xFFFFFFFF);
  test_taglib_apev2mux_check_tags (tags, 0xFFFFFFFF);
  gst_tag_list_unref (tags);

  /* now the real tests */
  for (i = 0; i < 50; ++i) {
    guint32 mask;

    mask = g_random_int ();
    GST_LOG ("tag mask = %08x (i=%d)", mask, i);

    if (mask == 0)
      continue;

    /* create tags */
    tags = test_taglib_apev2mux_create_tags (mask);
    GST_LOG ("tags for mask %08x = %" GST_PTR_FORMAT, mask, tags);

    /* double-check for internal consistency */
    test_taglib_apev2mux_check_tags (tags, mask);

    /* test with pipeline */
    test_taglib_apev2mux_with_tags (tags, mask);

    /* free tags */
    gst_tag_list_unref (tags);
  }
}

GST_END_TEST;

static Suite *
apev2mux_suite (void)
{
  Suite *s = suite_create ("apev2mux");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_apev2mux);

  return s;
}

GST_CHECK_MAIN (apev2mux);
