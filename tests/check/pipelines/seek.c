/* GStreamer simple seek unit test
 * Copyright (C) 2012 Collabora Ltd.
 *   Author: Tim-Philipp Müller <tim.muller@collabora.co.uk>
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
# include <config.h>
#endif

#include <gst/gst.h>
#include <gst/base/gstbasesrc.h>

#include <gst/check/gstcheck.h>
#include <gst/check/gstconsistencychecker.h>

/* ========================================================================
 *  Dummy source, like a stripped down audio test source
 * ======================================================================== */

#define SAMPLERATE 44100
#define CHUNKS_PER_SEC 10

typedef struct
{
  GstBaseSrc parent;
  GstClockTime next_time;
} TimedTestSrc;

typedef struct
{
  GstBaseSrcClass parent_class;
} TimedTestSrcClass;

static GstStaticPadTemplate timed_test_src_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("timed/audio"));

static GType timed_test_src_get_type (void);

G_DEFINE_TYPE (TimedTestSrc, timed_test_src, GST_TYPE_BASE_SRC);

static gboolean timed_test_src_is_seekable (GstBaseSrc * basesrc);
static gboolean timed_test_src_do_seek (GstBaseSrc * basesrc,
    GstSegment * segment);
static gboolean timed_test_src_start (GstBaseSrc * basesrc);
static gboolean timed_test_src_stop (GstBaseSrc * basesrc);
static GstFlowReturn timed_test_src_create (GstBaseSrc * basesrc,
    guint64 offset, guint length, GstBuffer ** buffer);

static void
timed_test_src_class_init (TimedTestSrcClass * klass)
{
  GstBaseSrcClass *gstbasesrc_class = (GstBaseSrcClass *) klass;

  gstbasesrc_class->is_seekable = timed_test_src_is_seekable;
  gstbasesrc_class->do_seek = timed_test_src_do_seek;
  gstbasesrc_class->start = timed_test_src_start;
  gstbasesrc_class->stop = timed_test_src_stop;
  gstbasesrc_class->create = timed_test_src_create;

  gst_element_class_add_static_pad_template (GST_ELEMENT_CLASS (klass),
      &timed_test_src_src_template);
}

static void
timed_test_src_init (TimedTestSrc * src)
{
  gst_base_src_set_format (GST_BASE_SRC (src), GST_FORMAT_TIME);
  gst_base_src_set_live (GST_BASE_SRC (src), FALSE);
}

static gboolean
timed_test_src_start (GstBaseSrc * basesrc)
{
  TimedTestSrc *src = (TimedTestSrc *) basesrc;

  src->next_time = 0;
  return TRUE;
}

static gboolean
timed_test_src_stop (GstBaseSrc * basesrc)
{
  return TRUE;
}

static gboolean
timed_test_src_do_seek (GstBaseSrc * basesrc, GstSegment * segment)
{
  TimedTestSrc *src = (TimedTestSrc *) basesrc;

  src->next_time = segment->position;
  return TRUE;
}

static gboolean
timed_test_src_is_seekable (GstBaseSrc * basesrc)
{
  return TRUE;
}

static GstFlowReturn
timed_test_src_create (GstBaseSrc * basesrc, guint64 offset, guint length,
    GstBuffer ** buf)
{
  TimedTestSrc *src = (TimedTestSrc *) basesrc;

  *buf = gst_buffer_new_and_alloc (SAMPLERATE / CHUNKS_PER_SEC);
  GST_BUFFER_TIMESTAMP (*buf) = src->next_time;
  GST_BUFFER_DURATION (*buf) = GST_SECOND / CHUNKS_PER_SEC;
  src->next_time += GST_BUFFER_DURATION (*buf);
  return GST_FLOW_OK;
}

/* ======================================================================== */

GST_START_TEST (test_seek)
{
  GstMessage *msg;
  GstElement *bin, *src1, *sink;
  gboolean res;
  GstPad *srcpad;
  GstBus *bus;

  GST_INFO ("preparing test");

  /* build pipeline */
  bin = gst_pipeline_new ("pipeline");
  bus = gst_element_get_bus (bin);
  gst_bus_add_signal_watch_full (bus, G_PRIORITY_HIGH);

  src1 = g_object_new (timed_test_src_get_type (), "name", "testsrc", NULL);

  sink = gst_element_factory_make ("fakesink", "sink");
  gst_bin_add_many (GST_BIN (bin), src1, sink, NULL);

  res = gst_element_link (src1, sink);
  fail_unless (res == TRUE, NULL);

  srcpad = gst_element_get_static_pad (src1, "src");
  gst_object_unref (srcpad);

  GST_INFO ("starting test");

  /* prepare playing */
  res = gst_element_set_state (bin, GST_STATE_PAUSED);
  fail_unless (res != GST_STATE_CHANGE_FAILURE, NULL);

  /* wait for completion */
  res =
      gst_element_get_state (GST_ELEMENT (bin), NULL, NULL,
      GST_CLOCK_TIME_NONE);
  fail_unless (res != GST_STATE_CHANGE_FAILURE, NULL);

  res = gst_element_send_event (bin,
      gst_event_new_seek (1.0, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH,
          GST_SEEK_TYPE_SET, (GstClockTime) 0,
          GST_SEEK_TYPE_SET, (GstClockTime) 2 * GST_SECOND));
  fail_unless (res == TRUE, NULL);

  GST_INFO ("seeked");

  /* run pipeline */
  res = gst_element_set_state (bin, GST_STATE_PLAYING);
  fail_unless (res != GST_STATE_CHANGE_FAILURE, NULL);

  msg = gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE,
      GST_MESSAGE_EOS | GST_MESSAGE_ERROR);
  fail_unless_equals_string (GST_MESSAGE_TYPE_NAME (msg), "eos");
  gst_message_unref (msg);

  res = gst_element_set_state (bin, GST_STATE_NULL);
  fail_unless (res != GST_STATE_CHANGE_FAILURE, NULL);

  /* cleanup */
  gst_bus_remove_signal_watch (bus);
  gst_object_unref (bus);
  gst_object_unref (bin);
}

GST_END_TEST;

static Suite *
pipelines_seek_suite (void)
{
  Suite *s = suite_create ("pipelines-seek");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_seek);

  return s;
}

GST_CHECK_MAIN (pipelines_seek);
