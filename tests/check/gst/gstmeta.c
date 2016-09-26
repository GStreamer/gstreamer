/* GStreamer
 *
 * unit test for GstMeta
 *
 * Copyright (C) <2009> Wim Taymans <wim.taymans@gmail.com>
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
# include "config.h"
#endif

#include <gst/check/gstcheck.h>

/* test metadata for PTS/DTS and duration */
typedef struct
{
  GstMeta meta;

  GstClockTime pts;
  GstClockTime dts;
  GstClockTime duration;
  GstClockTime clock_rate;
} GstMetaTest;

static GType gst_meta_test_api_get_type (void);
#define GST_META_TEST_API_TYPE (gst_meta_test_api_get_type())

static const GstMetaInfo *gst_meta_test_get_info (void);
#define GST_META_TEST_INFO (gst_meta_test_get_info())

#define GST_META_TEST_GET(buf) ((GstMetaTest *)gst_buffer_get_meta(buf,GST_META_TEST_API_TYPE))
#define GST_META_TEST_ADD(buf) ((GstMetaTest *)gst_buffer_add_meta(buf,GST_META_TEST_INFO,NULL))

#if 0
/* unused currently. This is a user function to fill the metadata with default
 * values. We don't call this from the init function because the user is mostly
 * likely going to override the values immediately after */
static void
gst_meta_test_init (GstMetaTest * meta)
{
  meta->pts = GST_CLOCK_TIME_NONE;
  meta->dts = GST_CLOCK_TIME_NONE;
  meta->duration = GST_CLOCK_TIME_NONE;
  meta->clock_rate = GST_SECOND;
}
#endif

static gboolean
test_init_func (GstMeta * meta, gpointer params, GstBuffer * buffer)
{
  GST_DEBUG ("init called on buffer %p, meta %p", buffer, meta);
  /* nothing to init really, the init function is mostly for allocating
   * additional memory or doing special setup as part of adding the metadata to
   * the buffer*/
  return TRUE;
}

static void
test_free_func (GstMeta * meta, GstBuffer * buffer)
{
  GST_DEBUG ("free called on buffer %p, meta %p", buffer, meta);
  /* nothing to free really */
}

static gboolean
test_transform_func (GstBuffer * transbuf, GstMeta * meta,
    GstBuffer * buffer, GQuark type, gpointer data)
{
  GstMetaTest *test, *tmeta = (GstMetaTest *) meta;

  GST_DEBUG ("transform %s called from buffer %p to %p, meta %p",
      g_quark_to_string (type), buffer, transbuf, meta);

  if (GST_META_TRANSFORM_IS_COPY (type)) {
    GstMetaTransformCopy *copy_data = data;

    test = GST_META_TEST_ADD (transbuf);

    if (copy_data->offset == 0) {
      /* same offset, copy timestamps */
      test->pts = tmeta->pts;
      test->dts = tmeta->dts;
      if (!copy_data->region) {
        fail_unless (gst_buffer_get_size (buffer) == copy_data->size);
        /* same size, copy duration */
        test->duration = tmeta->duration;
      } else {
        fail_unless (gst_buffer_get_size (buffer) > copy_data->size);
        /* else clear */
        test->duration = GST_CLOCK_TIME_NONE;
      }
    } else {
      fail_unless (copy_data->region == TRUE);
      test->pts = -1;
      test->dts = -1;
      test->duration = -1;
    }
    test->clock_rate = tmeta->clock_rate;
  } else {
    /* return FALSE, if transform type is not supported */
    return FALSE;
  }
  return TRUE;
}

static GType
gst_meta_test_api_get_type (void)
{
  static volatile GType type;
  static const gchar *tags[] = { "timing", NULL };

  if (g_once_init_enter (&type)) {
    GType _type = gst_meta_api_type_register ("GstMetaTestAPI", tags);
    g_once_init_leave (&type, _type);
  }
  return type;
}

static const GstMetaInfo *
gst_meta_test_get_info (void)
{
  static const GstMetaInfo *meta_test_info = NULL;

  if (g_once_init_enter (&meta_test_info)) {
    const GstMetaInfo *mi = gst_meta_register (GST_META_TEST_API_TYPE,
        "GstMetaTest",
        sizeof (GstMetaTest),
        test_init_func, test_free_func, test_transform_func);
    g_once_init_leave (&meta_test_info, mi);
  }
  return meta_test_info;
}

GST_START_TEST (test_meta_test)
{
  GstBuffer *buffer, *copy, *subbuf;
  GstMetaTest *meta;
  GstMapInfo info;

  buffer = gst_buffer_new_and_alloc (4);
  fail_if (buffer == NULL);

  fail_unless (gst_buffer_map (buffer, &info, GST_MAP_WRITE));
  fail_if (info.data == NULL);
  memset (info.data, 0, 4);
  gst_buffer_unmap (buffer, &info);

  /* add some metadata */
  meta = GST_META_TEST_ADD (buffer);
  fail_if (meta == NULL);
  /* fill some values */
  meta->pts = 1000;
  meta->dts = 2000;
  meta->duration = 1000;
  meta->clock_rate = 1000;

  /* copy of the buffer */
  copy = gst_buffer_copy (buffer);
  /* get metadata of the buffer */
  meta = GST_META_TEST_GET (copy);
  fail_if (meta == NULL);
  fail_if (meta->pts != 1000);
  fail_if (meta->dts != 2000);
  fail_if (meta->duration != 1000);
  fail_if (meta->clock_rate != 1000);
  gst_buffer_unref (copy);

  /* make subbuffer */
  subbuf = gst_buffer_copy_region (buffer, GST_BUFFER_COPY_ALL, 0, 1);
  /* get metadata of the buffer */
  meta = GST_META_TEST_GET (subbuf);
  fail_if (meta == NULL);
  fail_if (meta->pts != 1000);
  fail_if (meta->dts != 2000);
  fail_if (meta->duration != -1);
  fail_if (meta->clock_rate != 1000);
  gst_buffer_unref (subbuf);

  /* make another subbuffer */
  subbuf = gst_buffer_copy_region (buffer, GST_BUFFER_COPY_ALL, 1, 3);
  /* get metadata of the buffer */
  meta = GST_META_TEST_GET (subbuf);
  fail_if (meta == NULL);
  fail_if (meta->pts != -1);
  fail_if (meta->dts != -1);
  fail_if (meta->duration != -1);
  fail_if (meta->clock_rate != 1000);
  gst_buffer_unref (subbuf);

  /* clean up */
  gst_buffer_unref (buffer);
}

GST_END_TEST;

static gboolean
foreach_meta (GstBuffer * buffer, GstMeta ** meta, gpointer user_data)
{
  /* try to remove */
  *meta = NULL;
  return TRUE;
}

GST_START_TEST (test_meta_locked)
{
  GstBuffer *buffer;
  GstMetaTest *meta;

  buffer = gst_buffer_new_and_alloc (4);
  fail_if (buffer == NULL);

  /* add some metadata */
  meta = GST_META_TEST_ADD (buffer);
  fail_if (meta == NULL);
  GST_META_FLAG_SET (meta, GST_META_FLAG_LOCKED);

  ASSERT_CRITICAL (gst_buffer_remove_meta (buffer, (GstMeta *) meta));
  ASSERT_CRITICAL (gst_buffer_foreach_meta (buffer, foreach_meta, NULL));

  GST_META_FLAG_UNSET (meta, GST_META_FLAG_LOCKED);

  gst_buffer_remove_meta (buffer, (GstMeta *) meta);

  /* clean up */
  gst_buffer_unref (buffer);
}

GST_END_TEST;

static Suite *
gst_buffermeta_suite (void)
{
  Suite *s = suite_create ("GstMeta");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_meta_test);
  tcase_add_test (tc_chain, test_meta_locked);

  return s;
}

GST_CHECK_MAIN (gst_buffermeta);
