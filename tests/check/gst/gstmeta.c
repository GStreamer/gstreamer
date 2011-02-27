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
} GstTestMeta;

static const GstMetaInfo *gst_test_meta_get_info (void);

#define GST_TEST_META_GET(buf) ((GstTestMeta *)gst_buffer_get_meta(buf,gst_test_meta_get_info()))
#define GST_TEST_META_ADD(buf) ((GstTestMeta *)gst_buffer_add_meta(buf,gst_test_meta_get_info(),NULL))

#if 0
/* unused currently. This is a user function to fill the metadata with default
 * values. We don't call this from the init function because the user is mostly
 * likely going to override the values immediately after */
static void
gst_test_meta_init (GstTestMeta * meta)
{
  meta->pts = GST_CLOCK_TIME_NONE;
  meta->dts = GST_CLOCK_TIME_NONE;
  meta->duration = GST_CLOCK_TIME_NONE;
  meta->clock_rate = GST_SECOND;
}
#endif

static void
test_init_func (GstTestMeta * meta, GstBuffer * buffer)
{
  GST_DEBUG ("init called on buffer %p, meta %p", buffer, meta);
  /* nothing to init really, the init function is mostly for allocating
   * additional memory or doing special setup as part of adding the metadata to
   * the buffer*/
}

static void
test_free_func (GstTestMeta * meta, GstBuffer * buffer)
{
  GST_DEBUG ("free called on buffer %p, meta %p", buffer, meta);
  /* nothing to free really */
}

static void
test_copy_func (GstBuffer * copy, GstTestMeta * meta, GstBuffer * buffer)
{
  GstTestMeta *test;

  GST_DEBUG ("copy called from buffer %p to %p, meta %p", buffer, copy, meta);

  test = GST_TEST_META_ADD (copy);
  test->pts = meta->pts;
  test->dts = meta->dts;
  test->duration = meta->duration;
  test->clock_rate = meta->clock_rate;
}

static void
test_sub_func (GstBuffer * sub, GstTestMeta * meta, GstBuffer * buffer,
    guint offset, guint size)
{
  GstTestMeta *test;

  GST_DEBUG ("sub called from buffer %p to %p, meta %p, %u-%u", buffer, sub,
      meta, offset, size);

  test = GST_TEST_META_ADD (sub);
  if (offset == 0) {
    /* same offset, copy timestamps */
    test->pts = meta->pts;
    test->dts = meta->dts;
    if (size == GST_BUFFER_SIZE (buffer)) {
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
gst_test_meta_get_info (void)
{
  static const GstMetaInfo *test_meta_info = NULL;

  if (test_meta_info == NULL) {
    test_meta_info = gst_meta_register ("GstTestMeta", "GstTestMeta",
        sizeof (GstTestMeta),
        (GstMetaInitFunction) test_init_func,
        (GstMetaFreeFunction) test_free_func,
        (GstMetaCopyFunction) test_copy_func,
        (GstMetaSubFunction) test_sub_func, NULL, NULL);
  }
  return test_meta_info;
}

GST_START_TEST (test_metadata)
{
  GstBuffer *buffer, *copy, *subbuf;
  GstTestMeta *meta;

  buffer = gst_buffer_new_and_alloc (4);
  memset (GST_BUFFER_DATA (buffer), 0, 4);

  /* add some metadata */
  meta = GST_TEST_META_ADD (buffer);
  fail_if (meta == NULL);
  /* fill some values */
  meta->pts = 1000;
  meta->dts = 2000;
  meta->duration = 1000;
  meta->clock_rate = 1000;

  /* copy of the buffer */
  copy = gst_buffer_copy (buffer);
  /* get metadata of the buffer */
  meta = GST_TEST_META_GET (copy);
  fail_if (meta == NULL);
  fail_if (meta->pts != 1000);
  fail_if (meta->dts != 2000);
  fail_if (meta->duration != 1000);
  fail_if (meta->clock_rate != 1000);
  gst_buffer_unref (copy);

  /* make subbuffer */
  subbuf = gst_buffer_create_sub (buffer, 0, 1);
  /* get metadata of the buffer */
  meta = GST_TEST_META_GET (subbuf);
  fail_if (meta == NULL);
  fail_if (meta->pts != 1000);
  fail_if (meta->dts != 2000);
  fail_if (meta->duration != -1);
  fail_if (meta->clock_rate != 1000);
  gst_buffer_unref (subbuf);

  /* make another subbuffer */
  subbuf = gst_buffer_create_sub (buffer, 1, 3);
  /* get metadata of the buffer */
  meta = GST_TEST_META_GET (subbuf);
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


static Suite *
gst_buffermeta_suite (void)
{
  Suite *s = suite_create ("GstMeta");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_metadata);

  return s;
}

GST_CHECK_MAIN (gst_buffermeta);
