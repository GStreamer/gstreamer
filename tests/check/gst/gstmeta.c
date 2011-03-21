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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#ifdef HAVE_VALGRIND_H
# include <valgrind/valgrind.h>
#else
# define RUNNING_ON_VALGRIND FALSE
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

static const GstMetaInfo *gst_meta_test_get_info (void);
#define GST_META_TEST_INFO (gst_meta_test_get_info())

#define GST_META_TEST_GET(buf) ((GstMetaTest *)gst_buffer_get_meta(buf,GST_META_TEST_INFO))
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

static void
test_init_func (GstMetaTest * meta, GstBuffer * buffer)
{
  GST_DEBUG ("init called on buffer %p, meta %p", buffer, meta);
  /* nothing to init really, the init function is mostly for allocating
   * additional memory or doing special setup as part of adding the metadata to
   * the buffer*/
}

static void
test_free_func (GstMetaTest * meta, GstBuffer * buffer)
{
  GST_DEBUG ("free called on buffer %p, meta %p", buffer, meta);
  /* nothing to free really */
}

static void
test_transform_func (GstBuffer * transbuf, GstMetaTest * meta,
    GstBuffer * buffer, GstMetaTransformData * data)
{
  GstMetaTest *test;
  guint offset;
  guint size;

  if (data->type == GST_META_TRANSFORM_TRIM) {
    GstMetaTransformSubbuffer *subdata = (GstMetaTransformSubbuffer *) data;
    offset = subdata->offset;
    size = subdata->size;
  } else {
    offset = 0;
    size = gst_buffer_get_size (buffer);
  }

  GST_DEBUG ("trans called from buffer %p to %p, meta %p, %u-%u", buffer,
      transbuf, meta, offset, size);

  test = GST_META_TEST_ADD (transbuf);
  if (offset == 0) {
    /* same offset, copy timestamps */
    test->pts = meta->pts;
    test->dts = meta->dts;
    if (size == gst_buffer_get_size (buffer)) {
      /* same size, copy duration */
      test->duration = meta->duration;
    } else {
      /* else clear */
      test->duration = GST_CLOCK_TIME_NONE;
    }
  } else {
    test->pts = -1;
    test->dts = -1;
    test->duration = -1;
  }
  test->clock_rate = meta->clock_rate;
}

static const GstMetaInfo *
gst_meta_test_get_info (void)
{
  static const GstMetaInfo *meta_test_info = NULL;

  if (meta_test_info == NULL) {
    meta_test_info = gst_meta_register ("GstMetaTest", "GstMetaTest",
        sizeof (GstMetaTest),
        (GstMetaInitFunction) test_init_func,
        (GstMetaFreeFunction) test_free_func,
        (GstMetaTransformFunction) test_transform_func, NULL, NULL);
  }
  return meta_test_info;
}

GST_START_TEST (test_meta_test)
{
  GstBuffer *buffer, *copy, *subbuf;
  GstMetaTest *meta;
  gpointer data;

  buffer = gst_buffer_new_and_alloc (4);

  data = gst_buffer_map (buffer, NULL, NULL, GST_MAP_WRITE);
  memset (data, 0, 4);
  gst_buffer_unmap (buffer, data, 4);

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
  subbuf = gst_buffer_create_sub (buffer, 0, 1);
  /* get metadata of the buffer */
  meta = GST_META_TEST_GET (subbuf);
  fail_if (meta == NULL);
  fail_if (meta->pts != 1000);
  fail_if (meta->dts != 2000);
  fail_if (meta->duration != -1);
  fail_if (meta->clock_rate != 1000);
  gst_buffer_unref (subbuf);

  /* make another subbuffer */
  subbuf = gst_buffer_create_sub (buffer, 1, 3);
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

GST_START_TEST (test_meta_memory)
{
  GstBuffer *buffer, *copy;
  GstMetaMemory *meta;
  guint8 *data;
  gsize size;
  guint i;

  buffer = gst_buffer_new ();

  /* add some memory metadata */
  meta = gst_buffer_add_meta_memory (buffer, g_malloc (4), g_free, 4, 0);
  fail_if (meta == NULL);
  fail_if (meta->mmap_func == NULL);
  fail_if (meta->munmap_func == NULL);

  /* prepare for writing */
  data = gst_meta_memory_map (meta, 0, &size, GST_META_MAP_WRITE);
  fail_if (data == NULL);
  fail_if (size != 4);
  for (i = 0; i < 4; i++)
    data[i] = i;
  gst_meta_memory_unmap (meta, data, size);

  /* reading */
  meta = gst_buffer_get_meta_memory (buffer);
  fail_if (meta == NULL);

  data = gst_meta_memory_map (meta, 0, &size, GST_META_MAP_READ);
  fail_if (data == NULL);
  fail_if (size != 4);
  for (i = 0; i < 4; i++)
    fail_if (data[i] != i);
  gst_meta_memory_unmap (meta, data, size);

  /* copy of the buffer */
  copy = gst_buffer_copy (buffer);
  /* get metadata of the buffer */
  meta = gst_buffer_get_meta_memory (copy);
  fail_if (meta == NULL);
  data = gst_meta_memory_map (meta, 0, &size, GST_META_MAP_READ);
  fail_if (data == NULL);
  fail_if (size != 4);
  for (i = 0; i < 4; i++)
    fail_if (data[i] != i);
  gst_meta_memory_unmap (meta, data, size);
  gst_buffer_unref (copy);

#if 0
  /* FIXME, does not work yet */
  /* make a subbuffer */
  subbuf = gst_buffer_create_sub (buffer, 1, 3);
  meta = gst_buffer_get_meta_memory (subbuf);
  fail_if (meta == NULL);
  data = gst_meta_memory_map (meta, 0, &size, GST_META_MAP_READ);
  fail_if (data == NULL);
  fail_if (size != 3);
  for (i = 0; i < 3; i++)
    fail_if (data[i] != i + 1);
  gst_meta_memory_unmap (meta, data, size);
  gst_buffer_unref (subbuf);
#endif

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
  tcase_add_test (tc_chain, test_meta_memory);

  return s;
}

GST_CHECK_MAIN (gst_buffermeta);
