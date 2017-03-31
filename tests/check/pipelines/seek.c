/* GStreamer simple seek unit test
 * Copyright (C) 2012 Collabora Ltd.
 *   Author: Tim-Philipp Müller <tim.muller@collabora.co.uk>
 * Copyright (C) Julien Isorce <jisorce@oblong.com>
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
#include <gst/base/gstbaseparse.h>
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

/* ========================================================================
 *  Dummy parser
 * ======================================================================== */

typedef struct
{
  GstBaseParse parent;
  gboolean caps_set;
} DummyParser;

typedef struct
{
  GstBaseParseClass parent_class;
} DummyParserClass;

static GType dummy_parser_get_type (void);

G_DEFINE_TYPE (DummyParser, dummy_parser, GST_TYPE_BASE_PARSE);

static gboolean
dummy_parser_start (GstBaseParse * parse)
{
  return TRUE;
}

static gboolean
dummy_parser_stop (GstBaseParse * parse)
{
  return TRUE;
}

static GstFlowReturn
dummy_parser_handle_frame (GstBaseParse * parse,
    GstBaseParseFrame * frame, gint * skipsize)
{
  if (((DummyParser *) parse)->caps_set == FALSE) {
    GstCaps *caps;
    /* push caps */
    caps = gst_caps_new_empty_simple ("ANY");
    gst_pad_set_caps (GST_BASE_PARSE_SRC_PAD (parse), caps);
    gst_caps_unref (caps);
    ((DummyParser *) parse)->caps_set = TRUE;
  }

  GST_BUFFER_DURATION (frame->buffer) = GST_SECOND / 10;

  return gst_base_parse_finish_frame (parse, frame,
      gst_buffer_get_size (frame->buffer));
}

static gboolean
dummy_parser_set_sink_caps (GstBaseParse * parse, GstCaps * caps)
{
  gst_pad_set_caps (GST_BASE_PARSE_SRC_PAD (parse), caps);
  return TRUE;
}

static gboolean
dummy_parser_src_event (GstBaseParse * parse, GstEvent * event)
{
  switch (GST_EVENT_TYPE (event)) {
    default:
      GST_INFO ("src event %s", GST_EVENT_TYPE_NAME (event));
      break;
  }
  return GST_BASE_PARSE_CLASS (dummy_parser_parent_class)->src_event (parse,
      event);
}

static void
dummy_parser_class_init (DummyParserClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseParseClass *baseparse_class = GST_BASE_PARSE_CLASS (klass);

  static GstStaticPadTemplate sink_templ = GST_STATIC_PAD_TEMPLATE ("sink",
      GST_PAD_SINK, GST_PAD_ALWAYS,
      GST_STATIC_CAPS_ANY);

  static GstStaticPadTemplate src_templ = GST_STATIC_PAD_TEMPLATE ("src",
      GST_PAD_SRC, GST_PAD_ALWAYS,
      GST_STATIC_CAPS_ANY);

  gst_element_class_add_static_pad_template (element_class, &sink_templ);
  gst_element_class_add_static_pad_template (element_class, &src_templ);

  gst_element_class_set_metadata (element_class,
      "DummyParser", "Parser/Video", "empty", "empty");

  baseparse_class->start = GST_DEBUG_FUNCPTR (dummy_parser_start);
  baseparse_class->stop = GST_DEBUG_FUNCPTR (dummy_parser_stop);
  baseparse_class->handle_frame = GST_DEBUG_FUNCPTR (dummy_parser_handle_frame);
  baseparse_class->set_sink_caps =
      GST_DEBUG_FUNCPTR (dummy_parser_set_sink_caps);
  baseparse_class->src_event = GST_DEBUG_FUNCPTR (dummy_parser_src_event);
}

static void
dummy_parser_init (DummyParser * parser)
{
  parser->caps_set = FALSE;
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


/* This test checks that the pipeline does not wait for nothing after
 * sending the non-flush seek event. */
GST_START_TEST (test_loopback_1)
{
  GstMessage *msg;
  GstElement *bin, *source, *parser, *sink;
  gboolean res;
  GstPad *sinkpad;
  GstBus *bus;
  guint seek_flags;

  GST_INFO
      ("construct the test pipeline fakesrc ! testparse ! fakesink sync=1");

  bin = gst_pipeline_new ("pipeline");
  bus = gst_element_get_bus (bin);
  gst_bus_add_signal_watch_full (bus, G_PRIORITY_HIGH);

  source = gst_element_factory_make ("fakesrc", "source");
  parser = g_object_new (dummy_parser_get_type (), "name", "testparse", NULL);
  sink = gst_element_factory_make ("fakesink", "sink");

  gst_bin_add_many (GST_BIN (bin), source, parser, sink, NULL);

  res = gst_element_link (source, parser);
  fail_unless (res == TRUE, NULL);
  res = gst_element_link (parser, sink);
  fail_unless (res == TRUE, NULL);

  GST_INFO ("configure elements");

  g_object_set (G_OBJECT (source),
      "format", GST_FORMAT_BYTES,
      "can-activate-pull", TRUE,
      "can-activate-push", FALSE,
      "is-live", FALSE,
      "sizemax", 65536, "sizetype", 2 /* FAKE_SRC_SIZETYPE_FIXED */ ,
      "num-buffers", 35, NULL);

  /* Sync true is required for this test. */
  g_object_set (G_OBJECT (sink), "sync", TRUE, NULL);

  GST_INFO ("set paused state");

  res = gst_element_set_state (bin, GST_STATE_PAUSED);
  fail_unless (res != GST_STATE_CHANGE_FAILURE, NULL);

  GST_INFO ("wait for completion");

  res =
      gst_element_get_state (GST_ELEMENT (bin), NULL, NULL,
      GST_CLOCK_TIME_NONE);
  fail_unless (res != GST_STATE_CHANGE_FAILURE, NULL);

  GST_INFO ("paused state reached");

  fail_unless (GST_BASE_SRC (source)->random_access);

  sinkpad = gst_element_get_static_pad (parser, "sink");
  fail_unless (GST_PAD_MODE (sinkpad) == GST_PAD_MODE_PULL);
  gst_object_unref (sinkpad);

  GST_INFO ("flush seek");

  seek_flags =
      GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE | GST_SEEK_FLAG_SEGMENT;

  res = gst_element_seek (bin, 1.0, GST_FORMAT_TIME, seek_flags,
      GST_SEEK_TYPE_SET, (GstClockTime) 0,
      GST_SEEK_TYPE_SET, (GstClockTime) 2 * GST_SECOND);
  fail_unless (res == TRUE, NULL);

  GST_INFO ("set playing state");

  res = gst_element_set_state (bin, GST_STATE_PLAYING);
  fail_unless (res != GST_STATE_CHANGE_FAILURE, NULL);

  msg = gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE,
      GST_MESSAGE_SEGMENT_DONE | GST_MESSAGE_ERROR);
  fail_unless_equals_string (GST_MESSAGE_TYPE_NAME (msg), "segment-done");
  gst_message_unref (msg);

  GST_INFO ("warm-up sequence done");

  seek_flags &= ~GST_SEEK_FLAG_FLUSH;
  fail_if (seek_flags & GST_SEEK_FLAG_FLUSH, NULL);

  GST_INFO ("non-flush seek");

  res =
      gst_element_seek (bin, 1.0, GST_FORMAT_TIME, seek_flags,
      GST_SEEK_TYPE_SET, (GstClockTime) 0, GST_SEEK_TYPE_SET,
      (GstClockTime) 3 * GST_SECOND);
  fail_unless (res == TRUE, NULL);

  GST_INFO ("wait for segment done message");

  msg = gst_bus_timed_pop_filtered (bus, (GstClockTime) 2 * GST_SECOND,
      GST_MESSAGE_SEGMENT_DONE | GST_MESSAGE_ERROR);
  /* Make sure the pipeline is not waiting for nothing because the base time is
   *  screwed up. */
  fail_unless (msg, "no message within the timed window");
  fail_unless_equals_string (GST_MESSAGE_TYPE_NAME (msg), "segment-done");
  gst_message_unref (msg);

  res = gst_element_set_state (bin, GST_STATE_NULL);
  fail_unless (res != GST_STATE_CHANGE_FAILURE, NULL);

  /* cleanup */
  gst_bus_remove_signal_watch (bus);
  gst_object_unref (bus);
  gst_object_unref (bin);
}

GST_END_TEST;

/* This test checks that the pipeline does not play the media instantly
 * after sending the non-flush seek event. */
GST_START_TEST (test_loopback_2)
{
  GstMessage *msg;
  GstElement *bin, *source, *parser, *sink;
  gboolean res;
  GstPad *sinkpad;
  GstBus *bus;
  guint seek_flags;
  gint64 position = GST_CLOCK_TIME_NONE;
  gint64 playback_duration = GST_CLOCK_TIME_NONE;
  gint64 start_absolute_time = GST_CLOCK_TIME_NONE;

  GST_INFO
      ("construct the test pipeline fakesrc ! testparse ! fakesink sync=1");

  bin = gst_pipeline_new ("pipeline");
  bus = gst_element_get_bus (bin);
  gst_bus_add_signal_watch_full (bus, G_PRIORITY_HIGH);

  source = gst_element_factory_make ("fakesrc", "source");
  parser = g_object_new (dummy_parser_get_type (), "name", "testparse", NULL);
  sink = gst_element_factory_make ("fakesink", "sink");

  gst_bin_add_many (GST_BIN (bin), source, parser, sink, NULL);

  res = gst_element_link (source, parser);
  fail_unless (res == TRUE, NULL);
  res = gst_element_link (parser, sink);
  fail_unless (res == TRUE, NULL);

  GST_INFO ("configure elements");

  g_object_set (G_OBJECT (source),
      "format", GST_FORMAT_BYTES,
      "can-activate-pull", TRUE,
      "can-activate-push", FALSE,
      "is-live", FALSE,
      "sizemax", 65536, "sizetype", 2 /* FAKE_SRC_SIZETYPE_FIXED */ ,
      NULL);

  /* Sync true is required for this test. */
  g_object_set (G_OBJECT (sink), "sync", TRUE, NULL);

  GST_INFO ("set paused state");

  res = gst_element_set_state (bin, GST_STATE_PAUSED);
  fail_unless (res != GST_STATE_CHANGE_FAILURE, NULL);

  GST_INFO ("wait for completion");

  res =
      gst_element_get_state (GST_ELEMENT (bin), NULL, NULL,
      GST_CLOCK_TIME_NONE);
  fail_unless (res != GST_STATE_CHANGE_FAILURE, NULL);

  GST_INFO ("paused state reached");

  fail_unless (GST_BASE_SRC (source)->random_access);

  sinkpad = gst_element_get_static_pad (parser, "sink");
  fail_unless (GST_PAD_MODE (sinkpad) == GST_PAD_MODE_PULL);
  gst_object_unref (sinkpad);

  GST_INFO ("flush seek");

  seek_flags =
      GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE | GST_SEEK_FLAG_SEGMENT;

  res = gst_element_seek (bin, 1.0, GST_FORMAT_TIME, seek_flags,
      GST_SEEK_TYPE_SET, (GstClockTime) 0,
      GST_SEEK_TYPE_SET, (GstClockTime) 2 * GST_SECOND);
  fail_unless (res == TRUE, NULL);

  GST_INFO ("set playing state");

  res = gst_element_set_state (bin, GST_STATE_PLAYING);
  fail_unless (res != GST_STATE_CHANGE_FAILURE, NULL);

  msg = gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE,
      GST_MESSAGE_SEGMENT_DONE | GST_MESSAGE_ERROR);
  fail_unless_equals_string (GST_MESSAGE_TYPE_NAME (msg), "segment-done");
  gst_message_unref (msg);

  GST_INFO ("warm-up sequence done");

  seek_flags &= ~GST_SEEK_FLAG_FLUSH;
  fail_if (seek_flags & GST_SEEK_FLAG_FLUSH, NULL);

  GST_INFO ("non-flush seek");

  start_absolute_time = gst_clock_get_time (GST_ELEMENT_CLOCK (bin));

  res =
      gst_element_seek (bin, 1.0, GST_FORMAT_TIME, seek_flags,
      GST_SEEK_TYPE_SET, (GstClockTime) 0, GST_SEEK_TYPE_SET,
      (GstClockTime) 2 * GST_SECOND);
  fail_unless (res == TRUE, NULL);

  GST_INFO ("wait for segment done message");

  msg = gst_bus_timed_pop_filtered (bus, (GstClockTime) 2 * GST_SECOND,
      GST_MESSAGE_SEGMENT_DONE | GST_MESSAGE_ERROR);
  fail_unless (msg, "no message within the timed window");
  fail_unless_equals_string (GST_MESSAGE_TYPE_NAME (msg), "segment-done");

  gst_message_parse_segment_done (msg, NULL, &position);
  gst_message_unref (msg);

  GST_INFO ("final position: %" G_GINT64_FORMAT, position);

  fail_unless (position == 2 * GST_SECOND);

  playback_duration =
      GST_CLOCK_DIFF (start_absolute_time,
      gst_clock_get_time (GST_ELEMENT_CLOCK (bin)));

  GST_INFO ("test duration: %" G_GINT64_FORMAT, playback_duration);

  fail_unless (playback_duration > GST_SECOND,
      "playback duration should be near 2 seconds");

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
  tcase_add_test (tc_chain, test_loopback_1);
  tcase_add_test (tc_chain, test_loopback_2);

  return s;
}

GST_CHECK_MAIN (pipelines_seek);
