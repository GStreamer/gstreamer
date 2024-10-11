/* GStreamer Editing Services
 * Copyright (C) 2016 Sjors Gielen <mixml-ges@sjorsgielen.nl>
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

#include "common.h"
#include "plugins/nle/nleobject.h"

static void late_ges_init (void);

typedef struct _PadEventData
{
  gchar *name;
  guint expect_num_segments;
  guint num_segments;
  GArray *expect_segment_time;
  GArray *expect_segment_num_seeks;
  guint expect_num_seeks;
  guint num_seeks;
  GArray *expect_seek_start;
  GArray *expect_seek_stop;
  GArray *expect_seek_num_segments;
  guint num_eos;
  guint expect_num_eos;
} PadEventData;

#define _SEGMENT_FORMAT "flags: %i, rate: %g, applied_rate: %g, format: %i" \
  ", base: %" G_GUINT64_FORMAT ", offset: %" G_GUINT64_FORMAT ", start: %" \
  G_GUINT64_FORMAT ", stop: %" G_GUINT64_FORMAT ", time: %" G_GUINT64_FORMAT \
  ", position: %" G_GUINT64_FORMAT ", duration: %" G_GUINT64_FORMAT

#define _SEGMENT_ARGS(seg) (seg).flags, (seg).rate, (seg).applied_rate, \
  (seg).format, (seg).base, (seg).offset, (seg).start, (seg).stop, \
  (seg).time, (seg).position, (seg).duration

static GstPadProbeReturn
_test_pad_events (GstPad * pad, GstPadProbeInfo * info, PadEventData * data)
{
  guint num;
  GstEvent *event = info->data;
  if (GST_EVENT_TYPE (event) == GST_EVENT_SEGMENT) {
    guint expect_num_seeks;
    const GstSegment *segment;
    /* copy the segment start, stop, position and duration since these are
     * not yet translated by nleghostpad. Also, don't care about flags. */
    GstSegment expect_segment;

    gst_event_parse_segment (event, &segment);
    GST_DEBUG ("%s segment: " _SEGMENT_FORMAT, data->name,
        _SEGMENT_ARGS (*segment));

    if (!GST_CLOCK_TIME_IS_VALID (segment->stop)) {
      GST_DEBUG ("%s: ignoring pre-roll segment", data->name);
      return GST_PAD_PROBE_OK;
    }

    data->num_segments++;
    num = data->num_segments;

    fail_unless (num <= data->expect_num_segments, "%s received %u "
        "segments, more than the expected %u segments", data->name, num,
        data->expect_num_segments);

    expect_num_seeks =
        g_array_index (data->expect_segment_num_seeks, gint, num - 1);

    fail_unless (data->num_seeks == expect_num_seeks, "%s has received %u "
        "segments, compared to %u seeks, but expected %u seeks",
        data->name, num, data->num_seeks, expect_num_seeks);

    /* copy the segment start, stop, position, duration, offset, base
     * since these are not yet translated by nleghostpad. */
    gst_segment_copy_into (segment, &expect_segment);
    expect_segment.rate = 1.0;
    expect_segment.applied_rate = 1.0;
    expect_segment.format = GST_FORMAT_TIME;
    expect_segment.time = g_array_index (data->expect_segment_time,
        GstClockTime, num - 1);

    fail_unless (gst_segment_is_equal (segment, &expect_segment),
        "%s %uth segment is not equal to the expected. Received:\n"
        _SEGMENT_FORMAT "\nExpected\n" _SEGMENT_FORMAT, data->name,
        num - 1, _SEGMENT_ARGS (*segment), _SEGMENT_ARGS (expect_segment));

  } else if (GST_EVENT_TYPE (event) == GST_EVENT_SEEK) {
    gdouble rate;
    GstFormat format;
    GstClockTime expect;
    gint64 start, stop;
    GstSeekType start_type, stop_type;
    guint expect_num_segments;

    gst_event_parse_seek (event, &rate, &format, NULL, &start_type, &start,
        &stop_type, &stop);

    GST_DEBUG ("%s seek: rate: %g, start: %" G_GINT64_FORMAT ", stop: %"
        G_GINT64_FORMAT, data->name, rate, start, stop);

    data->num_seeks++;
    num = data->num_seeks;

    fail_unless (num <= data->expect_num_seeks, "%s received %u "
        "seeks, more than the expected %u seeks", data->name, num,
        data->expect_num_seeks);

    expect_num_segments =
        g_array_index (data->expect_seek_num_segments, gint, num - 1);

    fail_unless (data->num_segments == expect_num_segments, "%s has "
        "received %u seeks, compared to %u segments, but expected %u "
        "segments", data->name, num, data->num_segments, expect_num_segments);

    fail_unless (rate == 1.0, "%s %uth seek has a rate of %g rather than 1.0",
        data->name, num - 1, rate);
    fail_unless (format == GST_FORMAT_TIME, "%s %uth seek has a format of %i "
        " than a time format", data->name, num - 1, format);

    /* expect seek-set or seek-none */
    fail_if (start_type == GST_SEEK_TYPE_END, "%s %uth seek-start is "
        "seek-end", data->name, num - 1);
    fail_if (stop_type == GST_SEEK_TYPE_END, "%s %uth seek-stop is "
        "seek-end", data->name, num - 1);

    expect = g_array_index (data->expect_seek_start, GstClockTime, num - 1);
    fail_unless (start == expect, "%s %uth seek start is %" GST_TIME_FORMAT
        ", rather than the expected %" GST_TIME_FORMAT, data->name, num - 1,
        GST_TIME_ARGS (start), GST_TIME_ARGS (expect));

    expect = g_array_index (data->expect_seek_stop, GstClockTime, num - 1);
    fail_unless (stop == expect, "%s %uth seek stop is %" GST_TIME_FORMAT
        ", rather than the expected %" GST_TIME_FORMAT, data->name, num - 1,
        GST_TIME_ARGS (stop), GST_TIME_ARGS (expect));

  } else if (GST_EVENT_TYPE (event) == GST_EVENT_EOS) {
    data->num_eos++;
    fail_unless (data->num_eos <= data->expect_num_eos, "%s received %u "
        "EOS, more than the expected %u EOS", data->name, data->num_eos,
        data->expect_num_seeks);
  }

  return GST_PAD_PROBE_OK;
}

static void
_pad_event_data_check_received (PadEventData * data)
{
  fail_unless (data->num_eos == data->expect_num_eos, "%s received %u "
      "EOS, rather than %u", data->num_eos, data->expect_num_eos);
  fail_unless (data->num_segments == data->expect_num_segments,
      "%s received %u segments, rather than %u", data->name,
      data->num_segments, data->expect_num_segments);
  fail_unless (data->num_seeks == data->expect_num_seeks,
      "%s received %u seeks, rather than %u", data->name, data->num_seeks,
      data->expect_num_seeks);
}

static void
_pad_event_data_free (PadEventData * data)
{
  g_free (data->name);
  g_array_unref (data->expect_segment_time);
  g_array_unref (data->expect_seek_start);
  g_array_unref (data->expect_seek_stop);
  g_array_unref (data->expect_segment_num_seeks);
  g_array_unref (data->expect_seek_num_segments);
  g_free (data);
}

static PadEventData *
_pad_event_data_new (GstElement * element, const gchar * pad_name,
    const gchar * suffix)
{
  GstPad *pad;
  PadEventData *data = g_new0 (PadEventData, 1);
  data->expect_segment_time = g_array_new (FALSE, FALSE, sizeof (GstClockTime));
  data->expect_seek_start = g_array_new (FALSE, FALSE, sizeof (GstClockTime));
  data->expect_seek_stop = g_array_new (FALSE, FALSE, sizeof (GstClockTime));
  data->expect_seek_num_segments = g_array_new (FALSE, FALSE, sizeof (guint));
  data->expect_segment_num_seeks = g_array_new (FALSE, FALSE, sizeof (guint));
  data->name = g_strdup_printf ("%s:%s(%s):%s", G_OBJECT_TYPE_NAME (element),
      GST_ELEMENT_NAME (element), pad_name, suffix);

  pad = gst_element_get_static_pad (element, pad_name);
  fail_unless (pad, "%s not found", data->name);
  gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM |
      GST_PAD_PROBE_TYPE_EVENT_UPSTREAM, (GstPadProbeCallback) _test_pad_events,
      data, (GDestroyNotify) _pad_event_data_free);
  gst_object_unref (pad);

  return data;
}

static void
_pad_event_data_add_expect_segment (PadEventData * data, GstClockTime time)
{
  data->expect_num_segments++;
  g_array_append_val (data->expect_segment_time, time);
  g_array_append_val (data->expect_segment_num_seeks, data->expect_num_seeks);
}

static void
_pad_event_data_add_expect_seek (PadEventData * data, GstClockTime start,
    GstClockTime stop)
{
  data->expect_num_seeks++;
  g_array_append_val (data->expect_seek_start, start);
  g_array_append_val (data->expect_seek_stop, stop);
  g_array_append_val (data->expect_seek_num_segments,
      data->expect_num_segments);
}

static void
_pad_event_data_add_expect_seek_then_segment (PadEventData * data,
    GstClockTime start, GstClockTime stop)
{
  _pad_event_data_add_expect_seek (data, start, stop);
  _pad_event_data_add_expect_segment (data, start);
}

#define _EXPECT_SEEK_SEGMENT(data, start, stop) \
  _pad_event_data_add_expect_seek_then_segment (data, start, stop)

static GstElement *
_get_source (GstElement * nle_source)
{
  GList *tmp;
  GstElement *bin, *src = NULL;

  fail_unless (g_list_length (GST_BIN_CHILDREN (nle_source)), 1);
  bin = GST_BIN_CHILDREN (nle_source)->data;
  fail_unless (GST_IS_BIN (bin));

  for (tmp = GST_BIN_CHILDREN (bin); src == NULL && tmp; tmp = tmp->next) {
    if (g_strrstr (GST_ELEMENT_NAME (tmp->data), "audiotestsrc"))
      src = tmp->data;
  }
  fail_unless (src);
  return src;
}

enum
{
  NLE_PREV_SRC,
  NLE_POST_SRC,
  NLE_SOURCE_SRC,
  NLE_OPER_SRC,
  NLE_OPER_SINK,
  NLE_IDENTITY_SRC,
  PREV_SRC,
  POST_SRC,
  SOURCE_SRC,
  PITCH_SRC,
  PITCH_SINK,
  IDENTITY_SRC,
  SINK_SINK,
  NUM_DATA
};

static PadEventData **
_setup_test (GstElement * pipeline, gdouble rate)
{
  GstElement *sink, *pitch, *src, *prev, *post, *identity;
  GstElement *comp, *nle_source, *nle_prev, *nle_post, *nle_oper, *nle_identity;
  gboolean ret;
  gchar *suffix;
  PadEventData **data = g_new0 (PadEventData *, NUM_DATA);

  /* composition */
  comp =
      gst_element_factory_make_or_warn ("nlecomposition", "test_composition");
  gst_element_set_state (comp, GST_STATE_READY);

  /* sink */
  sink = gst_element_factory_make_or_warn ("fakeaudiosink", "sink");
  g_object_set (sink, "sync", FALSE, NULL);
  gst_bin_add_many (GST_BIN (pipeline), comp, sink, NULL);

  gst_element_link (comp, sink);

  /* sources */
  nle_source =
      audiotest_bin_src ("nle_source", 3 * GST_SECOND, 4 * GST_SECOND, 3,
      FALSE);
  g_object_set (nle_source, "inpoint", (guint64) 7 * GST_SECOND, NULL);
  src = _get_source (nle_source);
  g_object_set (src, "name", "middle-source", NULL);

  nle_prev =
      audiotest_bin_src ("nle_previous", 0 * GST_SECOND, 3 * GST_SECOND, 2,
      FALSE);
  g_object_set (nle_prev, "inpoint", (guint64) 99 * GST_SECOND, NULL);
  prev = _get_source (nle_prev);
  g_object_set (src, "name", "previous-source", NULL);

  nle_post =
      audiotest_bin_src ("post", 7 * GST_SECOND, 5 * GST_SECOND, 2, FALSE);
  g_object_set (nle_post, "inpoint", (guint64) 20 * GST_SECOND, NULL);
  post = _get_source (nle_post);
  g_object_set (src, "name", "post-source", NULL);

  /* Operation, must share the same start and duration as the upstream
   * source */
  nle_oper =
      new_operation ("nle_oper", "pitch", 3 * GST_SECOND, 4 * GST_SECOND, 2);
  fail_unless (g_list_length (GST_BIN_CHILDREN (nle_oper)) == 1);
  pitch = GST_ELEMENT (GST_BIN_CHILDREN (nle_oper)->data);
  g_object_set (pitch, "rate", rate, NULL);

  /* cover with an identity operation
   * rate effect has lower priority, so we don't need the same start or
   * duration */
  nle_identity =
      new_operation ("nle_identity", "identity", 0, 12 * GST_SECOND, 1);
  g_object_set (nle_identity, "inpoint", (guint64) 5 * GST_SECOND, NULL);
  fail_unless (g_list_length (GST_BIN_CHILDREN (nle_oper)) == 1);
  identity = GST_ELEMENT (GST_BIN_CHILDREN (nle_identity)->data);

  nle_composition_add (GST_BIN (comp), nle_source);
  nle_composition_add (GST_BIN (comp), nle_prev);
  nle_composition_add (GST_BIN (comp), nle_post);
  nle_composition_add (GST_BIN (comp), nle_oper);
  nle_composition_add (GST_BIN (comp), nle_identity);
  ret = FALSE;
  commit_and_wait (comp, &ret);
  fail_unless (ret);

  check_start_stop_duration (nle_source, 3 * GST_SECOND, 7 * GST_SECOND,
      4 * GST_SECOND);
  check_start_stop_duration (nle_oper, 3 * GST_SECOND, 7 * GST_SECOND,
      4 * GST_SECOND);
  check_start_stop_duration (nle_prev, 0, 3 * GST_SECOND, 3 * GST_SECOND);
  check_start_stop_duration (nle_post, 7 * GST_SECOND, 12 * GST_SECOND,
      5 * GST_SECOND);
  check_start_stop_duration (nle_identity, 0, 12 * GST_SECOND, 12 * GST_SECOND);
  check_start_stop_duration (comp, 0, 12 * GST_SECOND, 12 * GST_SECOND);

  /* create data */
  suffix = g_strdup_printf ("rate=%g", rate);

  /* source */
  data[NLE_SOURCE_SRC] = _pad_event_data_new (nle_source, "src", suffix);
  data[NLE_PREV_SRC] = _pad_event_data_new (nle_prev, "src", suffix);
  data[NLE_POST_SRC] = _pad_event_data_new (nle_post, "src", suffix);
  data[SOURCE_SRC] = _pad_event_data_new (src, "src", suffix);
  data[PREV_SRC] = _pad_event_data_new (prev, "src", suffix);
  data[POST_SRC] = _pad_event_data_new (post, "src", suffix);

  /* rate operation */
  data[NLE_OPER_SRC] = _pad_event_data_new (nle_oper, "src", suffix);
  data[NLE_OPER_SINK] = _pad_event_data_new (nle_oper, "sink", suffix);
  data[PITCH_SRC] = _pad_event_data_new (pitch, "src", suffix);
  data[PITCH_SINK] = _pad_event_data_new (pitch, "sink", suffix);

  /* identity: only care about the source pads */
  data[NLE_IDENTITY_SRC] = _pad_event_data_new (nle_identity, "src", suffix);
  data[IDENTITY_SRC] = _pad_event_data_new (identity, "src", suffix);

  /* sink */
  data[SINK_SINK] = _pad_event_data_new (sink, "sink", suffix);

  g_free (suffix);

  return data;
}


GST_START_TEST (test_tempochange_play)
{
  GstElement *pipeline;
  GstBus *bus;
  GstMessage *message;
  gboolean carry_on;
  PadEventData **data;
  gdouble rates[3] = { 0.5, 4.0, 1.0 };
  guint i, j;

  late_ges_init ();

  for (i = 0; i < G_N_ELEMENTS (rates); i++) {
    gdouble rate = rates[i];
    GST_DEBUG ("rate = %g", rate);

    pipeline = gst_pipeline_new ("test_pipeline");

    data = _setup_test (pipeline, rate);

    /* initial seek */
    _EXPECT_SEEK_SEGMENT (data[SINK_SINK], 0, 3 * GST_SECOND);
    _EXPECT_SEEK_SEGMENT (data[NLE_IDENTITY_SRC], 0, 3 * GST_SECOND);
    /* nleobject will convert the seek by removing start and adding inpoint */
    _EXPECT_SEEK_SEGMENT (data[IDENTITY_SRC], 5 * GST_SECOND, 8 * GST_SECOND);
    _EXPECT_SEEK_SEGMENT (data[NLE_PREV_SRC], 0, 3 * GST_SECOND);
    _EXPECT_SEEK_SEGMENT (data[PREV_SRC], 99 * GST_SECOND, 102 * GST_SECOND);

    /* rate-stack seek */
    _EXPECT_SEEK_SEGMENT (data[SINK_SINK], 3 * GST_SECOND, 7 * GST_SECOND);
    _EXPECT_SEEK_SEGMENT (data[NLE_IDENTITY_SRC], 3 * GST_SECOND,
        7 * GST_SECOND);
    _EXPECT_SEEK_SEGMENT (data[IDENTITY_SRC], 8 * GST_SECOND, 12 * GST_SECOND);
    _EXPECT_SEEK_SEGMENT (data[NLE_OPER_SRC], 3 * GST_SECOND, 7 * GST_SECOND);
    _EXPECT_SEEK_SEGMENT (data[PITCH_SRC], 0, 4 * GST_SECOND);
    /* pitch element will change the stop time, e.g. if rate=2.0, then we
     * want to use up twice as much source, so the stop time doubles */
    _EXPECT_SEEK_SEGMENT (data[PITCH_SINK], 0, rate * 4 * GST_SECOND);
    _EXPECT_SEEK_SEGMENT (data[NLE_OPER_SINK], 3 * GST_SECOND,
        (GstClockTime) (rate * 4 * GST_SECOND) + 3 * GST_SECOND);
    _EXPECT_SEEK_SEGMENT (data[NLE_SOURCE_SRC], 3 * GST_SECOND,
        (GstClockTime) (rate * 4 * GST_SECOND) + 3 * GST_SECOND);
    _EXPECT_SEEK_SEGMENT (data[SOURCE_SRC], 7 * GST_SECOND,
        (GstClockTime) (rate * 4 * GST_SECOND) + 7 * GST_SECOND);

    /* final part only involves post source */
    _EXPECT_SEEK_SEGMENT (data[SINK_SINK], 7 * GST_SECOND, 12 * GST_SECOND);
    _EXPECT_SEEK_SEGMENT (data[NLE_IDENTITY_SRC], 7 * GST_SECOND,
        12 * GST_SECOND);
    _EXPECT_SEEK_SEGMENT (data[IDENTITY_SRC], 12 * GST_SECOND, 17 * GST_SECOND);
    _EXPECT_SEEK_SEGMENT (data[NLE_POST_SRC], 7 * GST_SECOND, 12 * GST_SECOND);
    /* nleobject will convert the seek by removing start and adding
     * inpoint */
    _EXPECT_SEEK_SEGMENT (data[POST_SRC], 20 * GST_SECOND, 25 * GST_SECOND);

    /* expect 1 EOS from each, apart from identity, which will get 3 since
     * part of 3 stacks */
    for (j = 0; j < NUM_DATA; j++)
      data[j]->expect_num_eos = 1;

    data[IDENTITY_SRC]->expect_num_eos = 3;
    data[NLE_IDENTITY_SRC]->expect_num_eos = 3;

    bus = gst_element_get_bus (GST_ELEMENT (pipeline));

    GST_DEBUG ("Setting pipeline to PLAYING");
    fail_if (gst_element_set_state (GST_ELEMENT (pipeline),
            GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE);

    GST_DEBUG ("Let's poll the bus");

    carry_on = TRUE;
    while (carry_on) {
      message = gst_bus_poll (bus, GST_MESSAGE_ANY, GST_SECOND / 10);
      if (message) {
        switch (GST_MESSAGE_TYPE (message)) {
          case GST_MESSAGE_EOS:
            if (message->src == GST_OBJECT (pipeline)) {
              GST_DEBUG ("Setting pipeline to NULL");
              fail_unless (gst_element_set_state (GST_ELEMENT (pipeline),
                      GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS);
              carry_on = FALSE;
            }
            break;
          case GST_MESSAGE_ERROR:
            fail_error_message (message);
            break;
          default:
            break;
        }
        gst_message_unref (message);
      }
    }

    for (j = 0; j < NUM_DATA; j++)
      _pad_event_data_check_received (data[j]);

    ASSERT_OBJECT_REFCOUNT_BETWEEN (pipeline, "main pipeline", 1, 2);
    gst_object_unref (pipeline);
    ASSERT_OBJECT_REFCOUNT_BETWEEN (bus, "main bus", 1, 2);
    gst_object_unref (bus);
    g_free (data);
  }
}

GST_END_TEST;

#define _WAIT_UNTIL_ASYNC_DONE \
{ \
  GST_DEBUG ("Let's poll the bus"); \
  carry_on = TRUE; \
  while (carry_on) { \
    message = gst_bus_poll (bus, GST_MESSAGE_ANY, GST_SECOND / 10); \
    if (message) { \
      switch (GST_MESSAGE_TYPE (message)) { \
        case GST_MESSAGE_EOS: \
          fail_if (TRUE, "Received EOS"); \
          break; \
        case GST_MESSAGE_ERROR: \
          fail_error_message (message); \
          break; \
        case GST_MESSAGE_ASYNC_DONE: \
          carry_on = FALSE; \
          break; \
        default: \
          break; \
      } \
      gst_message_unref (message); \
    } \
  } \
}

GST_START_TEST (test_tempochange_seek)
{
  GstElement *pipeline;
  GstBus *bus;
  GstMessage *message;
  gboolean carry_on;
  PadEventData **data;
  gdouble rates[3] = { 2.0, 0.25, 1.0 };
  guint i, j;
  GstClockTime offset = 0.1 * GST_SECOND;

  late_ges_init ();

  for (i = 0; i < G_N_ELEMENTS (rates); i++) {
    gdouble rate = rates[i];
    GST_DEBUG ("rate = %g", rate);

    pipeline = gst_pipeline_new ("test_pipeline");

    data = _setup_test (pipeline, rate);

    /* initial seek from the pause */
    _EXPECT_SEEK_SEGMENT (data[SINK_SINK], 0, 3 * GST_SECOND);
    _EXPECT_SEEK_SEGMENT (data[NLE_IDENTITY_SRC], 0, 3 * GST_SECOND);
    _EXPECT_SEEK_SEGMENT (data[IDENTITY_SRC], 5 * GST_SECOND, 8 * GST_SECOND);
    _EXPECT_SEEK_SEGMENT (data[NLE_PREV_SRC], 0, 3 * GST_SECOND);
    _EXPECT_SEEK_SEGMENT (data[PREV_SRC], 99 * GST_SECOND, 102 * GST_SECOND);

    GST_DEBUG ("Setting pipeline to PAUSED");
    fail_unless (gst_element_set_state (GST_ELEMENT (pipeline),
            GST_STATE_PAUSED) == GST_STATE_CHANGE_ASYNC);

    bus = gst_element_get_bus (GST_ELEMENT (pipeline));

    _WAIT_UNTIL_ASYNC_DONE;

    for (j = 0; j < NUM_DATA; j++)
      _pad_event_data_check_received (data[j]);

    /* first seek for just after the start of the rate effect */
    /* NOTE: neither prev nor post should receive anything */

    /* sink will receive two seeks: one that initiates the pre-roll, and
     * then the seek with the stop set */
    /* expect no segment for the first seek */
    _pad_event_data_add_expect_seek (data[SINK_SINK], 3 * GST_SECOND + offset,
        GST_CLOCK_TIME_NONE);
    _EXPECT_SEEK_SEGMENT (data[SINK_SINK], 3 * GST_SECOND + offset,
        7 * GST_SECOND);
    _EXPECT_SEEK_SEGMENT (data[NLE_IDENTITY_SRC], 3 * GST_SECOND + offset,
        7 * GST_SECOND);
    _EXPECT_SEEK_SEGMENT (data[IDENTITY_SRC], 8 * GST_SECOND + offset,
        12 * GST_SECOND);
    _EXPECT_SEEK_SEGMENT (data[NLE_OPER_SRC], 3 * GST_SECOND + offset,
        7 * GST_SECOND);
    _EXPECT_SEEK_SEGMENT (data[PITCH_SRC], offset, 4 * GST_SECOND);
    _EXPECT_SEEK_SEGMENT (data[PITCH_SINK], rate * offset,
        rate * 4 * GST_SECOND);
    _EXPECT_SEEK_SEGMENT (data[NLE_OPER_SINK],
        3 * GST_SECOND + (GstClockTime) (rate * offset),
        3 * GST_SECOND + (GstClockTime) (rate * 4 * GST_SECOND));
    _EXPECT_SEEK_SEGMENT (data[NLE_SOURCE_SRC],
        3 * GST_SECOND + (GstClockTime) (rate * offset),
        3 * GST_SECOND + (GstClockTime) (rate * 4 * GST_SECOND));
    _EXPECT_SEEK_SEGMENT (data[SOURCE_SRC],
        7 * GST_SECOND + (GstClockTime) (rate * offset),
        7 * GST_SECOND + (GstClockTime) (rate * 4 * GST_SECOND));

    /* perform seek */
    fail_unless (gst_element_seek_simple (pipeline, GST_FORMAT_TIME,
            GST_SEEK_FLAG_FLUSH, 3 * GST_SECOND + offset));

    _WAIT_UNTIL_ASYNC_DONE;

    for (j = 0; j < NUM_DATA; j++)
      _pad_event_data_check_received (data[j]);

    /* now seek to just before the end */
    _pad_event_data_add_expect_seek (data[SINK_SINK], 7 * GST_SECOND - offset,
        GST_CLOCK_TIME_NONE);
    _EXPECT_SEEK_SEGMENT (data[SINK_SINK], 7 * GST_SECOND - offset,
        7 * GST_SECOND);
    _EXPECT_SEEK_SEGMENT (data[NLE_IDENTITY_SRC], 7 * GST_SECOND - offset,
        7 * GST_SECOND);
    _EXPECT_SEEK_SEGMENT (data[IDENTITY_SRC], 12 * GST_SECOND - offset,
        12 * GST_SECOND);
    _EXPECT_SEEK_SEGMENT (data[NLE_OPER_SRC], 7 * GST_SECOND - offset,
        7 * GST_SECOND);
    _EXPECT_SEEK_SEGMENT (data[PITCH_SRC], 4 * GST_SECOND - offset,
        4 * GST_SECOND);
    _EXPECT_SEEK_SEGMENT (data[PITCH_SINK],
        rate * (4 * GST_SECOND) - rate * offset, rate * 4 * GST_SECOND);
    _EXPECT_SEEK_SEGMENT (data[NLE_OPER_SINK],
        3 * GST_SECOND + (GstClockTime) (rate * (4 * GST_SECOND - offset)),
        3 * GST_SECOND + (GstClockTime) (rate * 4 * GST_SECOND));
    _EXPECT_SEEK_SEGMENT (data[NLE_SOURCE_SRC],
        3 * GST_SECOND + (GstClockTime) (rate * (4 * GST_SECOND - offset)),
        3 * GST_SECOND + (GstClockTime) (rate * 4 * GST_SECOND));
    _EXPECT_SEEK_SEGMENT (data[SOURCE_SRC],
        7 * GST_SECOND + (GstClockTime) (rate * (4 * GST_SECOND - offset)),
        7 * GST_SECOND + (GstClockTime) (rate * 4 * GST_SECOND));

    /* perform seek */
    fail_unless (gst_element_seek_simple (pipeline, GST_FORMAT_TIME,
            GST_SEEK_FLAG_FLUSH, 7 * GST_SECOND - offset));

    _WAIT_UNTIL_ASYNC_DONE;

    for (j = 0; j < NUM_DATA; j++)
      _pad_event_data_check_received (data[j]);

    GST_DEBUG ("Setting pipeline to NULL");
    fail_unless (gst_element_set_state (GST_ELEMENT (pipeline),
            GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS);

    ASSERT_OBJECT_REFCOUNT_BETWEEN (pipeline, "main pipeline", 1, 2);
    gst_object_unref (pipeline);
    ASSERT_OBJECT_REFCOUNT_BETWEEN (bus, "main bus", 1, 2);
    gst_object_unref (bus);
    g_free (data);
  }
}

GST_END_TEST;

static void
late_ges_init ()
{
  /* We need to do this inside the test cases, not during the initialization
   * of the suite, as ges_init() will initialize thread pools, which cannot
   * work properly after a fork. */

  if (atexit (ges_deinit) != 0) {
    GST_ERROR ("failed to set ges_deinit as exit function");
  }

  ges_init ();
}

static Suite *
gnonlin_suite (void)
{
  Suite *s = suite_create ("nle");
  TCase *tc_chain = tcase_create ("tempochange");

  suite_add_tcase (s, tc_chain);

  /* give the tests a little more time than the default
   * CK_DEFAULT_TIMEOUT=20, this is sometimes needed for running under
   * valgrind */
  tcase_set_timeout (tc_chain, 40.0);

  tcase_add_test (tc_chain, test_tempochange_play);
  tcase_add_test (tc_chain, test_tempochange_seek);

  return s;
}

GST_CHECK_MAIN (gnonlin)
