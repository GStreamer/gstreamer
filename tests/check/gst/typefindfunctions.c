/* GStreamer unit tests for the -base typefind functions
 *
 * Copyright (C) 2007 Tim-Philipp Müller <tim centricular net>
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
# include <config.h>
#endif

#include <gst/check/gstcheck.h>
#include <gst/base/gsttypefindhelper.h>

GST_START_TEST (test_quicktime_mpeg4video)
{
  /* quicktime redirect file which starts with what could also be interpreted
   * as an MPEG-4 video object layer start code */
  const guint8 qt_redirect_396042[] =
      { 0x00, 0x00, 0x01, 0x22, 0x6d, 0x6f, 0x6f, 0x76, 0x00, 0x00, 0x01, 0x1a,
    0x72, 0x6d, 0x72, 0x61, 0x00, 0x00, 0x00, 0x86, 0x72, 0x6d, 0x64, 0x61,
    0x00, 0x00, 0x00, 0x54, 0x72, 0x64, 0x72, 0x66, 0x00, 0x00, 0x00, 0x00,
    0x75, 0x72, 0x6c, 0x20, 0x00, 0x00, 0x00, 0x40, 0x68, 0x74, 0x74, 0x70,
    0x3a, 0x2f, 0x2f, 0x73, 0x74, 0x72, 0x65, 0x61, 0x6d, 0x2e, 0x71, 0x74,
    0x76, 0x2e, 0x61, 0x70, 0x70, 0x6c, 0x65, 0x2e, 0x63, 0x6f, 0x6d, 0x2f,
    0x65, 0x76, 0x65, 0x6e, 0x74, 0x73, 0x2f, 0x6a, 0x61, 0x6e, 0x2f, 0x6a,
    0x34, 0x37, 0x64, 0x35, 0x32, 0x6f, 0x6f, 0x2f, 0x71, 0x74, 0x37, 0x72,
    0x65, 0x71, 0x75, 0x69, 0x72, 0x65, 0x64, 0x2e, 0x6d, 0x6f, 0x76, 0x00,
    0x00, 0x00, 0x00, 0x10, 0x72, 0x6d, 0x64, 0x72, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x0a, 0xf0, 0x00, 0x00, 0x00, 0x1a, 0x72, 0x6d, 0x76, 0x63,
    0x00, 0x00, 0x00, 0x00, 0x71, 0x74, 0x69, 0x6d, 0x06, 0x50, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x8c, 0x72, 0x6d,
    0x64, 0x61, 0x00, 0x00, 0x00, 0x5a, 0x72, 0x64, 0x72, 0x66, 0x00, 0x00,
    0x00, 0x00, 0x75, 0x72, 0x6c, 0x20, 0x00, 0x00, 0x00, 0x46, 0x68, 0x74,
    0x74, 0x70, 0x3a, 0x2f, 0x2f, 0x73, 0x74, 0x72, 0x65, 0x61, 0x6d, 0x2e,
    0x71, 0x74, 0x76, 0x2e, 0x61, 0x70, 0x70, 0x6c, 0x65, 0x2e, 0x63, 0x6f,
    0x6d, 0x2f, 0x65, 0x76, 0x65, 0x6e, 0x74, 0x73, 0x2f, 0x6a, 0x61, 0x6e,
    0x2f, 0x6a, 0x34, 0x37, 0x64, 0x35, 0x32, 0x6f, 0x6f, 0x2f, 0x38, 0x38,
    0x34, 0x38, 0x31, 0x32, 0x35, 0x5f, 0x32, 0x5f, 0x33, 0x35, 0x30, 0x5f,
    0x72, 0x65, 0x66, 0x2e, 0x6d, 0x6f, 0x76, 0x00, 0x00, 0x00, 0x00, 0x10,
    0x72, 0x6d, 0x64, 0x72, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0a, 0xf0,
    0x00, 0x00, 0x00, 0x1a, 0x72, 0x6d, 0x76, 0x63, 0x00, 0x00, 0x00, 0x00,
    0x71, 0x74, 0x69, 0x6d, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00
  };
  GstTypeFindProbability prob;
  const gchar *type;
  GstBuffer *buf;
  GstCaps *caps = NULL;

  buf = gst_buffer_new ();
  GST_BUFFER_DATA (buf) = (guint8 *) qt_redirect_396042;
  GST_BUFFER_SIZE (buf) = sizeof (qt_redirect_396042);
  GST_BUFFER_OFFSET (buf) = 0;

  caps = gst_type_find_helper_for_buffer (NULL, buf, &prob);
  fail_unless (caps != NULL);
  GST_LOG ("Found type: %" GST_PTR_FORMAT, caps);

  type = gst_structure_get_name (gst_caps_get_structure (caps, 0));
  fail_unless_equals_string (type, "video/quicktime");
  fail_unless (prob > GST_TYPE_FIND_MINIMUM && prob <= GST_TYPE_FIND_MAXIMUM);

  gst_buffer_unref (buf);
  gst_caps_unref (caps);
}

GST_END_TEST;

GST_START_TEST (test_broken_flac_in_ogg)
{
  const guint8 flac_id_packet[4] = { 'f', 'L', 'a', 'C' };
  GstTypeFindProbability prob;
  const gchar *type;
  GstBuffer *buf;
  GstCaps *caps = NULL;

  buf = gst_buffer_new ();
  GST_BUFFER_DATA (buf) = (guint8 *) flac_id_packet;
  GST_BUFFER_SIZE (buf) = sizeof (flac_id_packet);
  GST_BUFFER_OFFSET (buf) = 0;

  caps = gst_type_find_helper_for_buffer (NULL, buf, &prob);
  fail_unless (caps != NULL);
  GST_LOG ("Found type: %" GST_PTR_FORMAT, caps);

  type = gst_structure_get_name (gst_caps_get_structure (caps, 0));
  fail_unless_equals_string (type, "audio/x-flac");
  fail_unless (prob > GST_TYPE_FIND_MINIMUM && prob <= GST_TYPE_FIND_MAXIMUM);

  gst_buffer_unref (buf);
  gst_caps_unref (caps);
}

GST_END_TEST;

GST_START_TEST (test_jpeg_not_ac3)
{
  const gchar *type;
  GstBuffer *buf;
  GError *err = NULL;
  GstCaps *caps = NULL;
  gchar *path, *data = NULL;
  gsize data_len;

  path = g_build_filename (GST_TEST_FILES_PATH, "partialframe.mjpeg", NULL);
  GST_LOG ("reading file '%s'", path);
  if (!g_file_get_contents (path, &data, &data_len, &err)) {
    g_error ("error loading test file: %s", err->message);
  }

  buf = gst_buffer_new ();
  GST_BUFFER_DATA (buf) = (guint8 *) data;
  GST_BUFFER_SIZE (buf) = data_len;
  GST_BUFFER_OFFSET (buf) = 0;

  caps = gst_type_find_helper_for_buffer (NULL, buf, NULL);
  fail_unless (caps != NULL);
  GST_LOG ("Found type: %" GST_PTR_FORMAT, caps);

  type = gst_structure_get_name (gst_caps_get_structure (caps, 0));
  fail_unless_equals_string (type, "image/jpeg");

  gst_buffer_unref (buf);
  gst_caps_unref (caps);
  g_free (data);
  g_free (path);
}

GST_END_TEST;

#define TEST_RANDOM_DATA_SIZE (4*1024)

/* typefind random data, to make sure all typefinders are called */
GST_START_TEST (test_random_data)
{
  GstTypeFindProbability prob;
  const gchar *seed_env;
  GstBuffer *buf;
  GstCaps *caps;
  guint32 seed;
  guint8 *data;
  gint i;

  seed_env = g_getenv ("GST_TYPEFIND_TEST_SEED");
  if (seed_env != NULL)
    seed = atoi (seed_env);
  else
    seed = (guint32) time (NULL);

  g_random_set_seed (seed);

  data = g_malloc (TEST_RANDOM_DATA_SIZE);
  for (i = 0; i < TEST_RANDOM_DATA_SIZE; ++i)
    data[i] = g_random_int () & 0xff;

  buf = gst_buffer_new ();
  GST_BUFFER_DATA (buf) = (guint8 *) data;
  GST_BUFFER_SIZE (buf) = TEST_RANDOM_DATA_SIZE;
  GST_BUFFER_OFFSET (buf) = 0;

  caps = gst_type_find_helper_for_buffer (NULL, buf, &prob);
  GST_INFO ("caps: %" GST_PTR_FORMAT ", probability=%u", caps, prob);
  /* for now we just print an error log message */
  if (caps != NULL /* && prob >= GST_TYPE_FIND_LIKELY */ ) {
    GST_ERROR ("typefinder thinks random data is %" GST_PTR_FORMAT ", with a "
        "probability of %u (seed was %u)", caps, prob, seed);
    gst_caps_unref (caps);
  }

  gst_buffer_unref (buf);
  g_free (data);
}

GST_END_TEST;

static Suite *
typefindfunctions_suite (void)
{
  Suite *s = suite_create ("typefindfunctions");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_quicktime_mpeg4video);
  tcase_add_test (tc_chain, test_broken_flac_in_ogg);
  tcase_add_test (tc_chain, test_jpeg_not_ac3);
  tcase_add_test (tc_chain, test_random_data);

  return s;
}

GST_CHECK_MAIN (typefindfunctions);
