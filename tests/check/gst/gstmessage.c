/* GStreamer
 *
 * unit test for GstMessage
 *
 * Copyright (C) <2005> Wim Taymans <wim at fluendo dot com>
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

#include <gst/check/gstcheck.h>

static GQuark domain;

GST_START_TEST (test_parsing)
{
  GstMessage *message;

  domain = g_quark_from_static_string ("test");

  /* GST_MESSAGE_EOS */
  {
    message = gst_message_new_eos (NULL);
    fail_if (message == NULL);
    fail_unless (GST_MESSAGE_TYPE (message) == GST_MESSAGE_EOS);
    fail_unless (GST_MESSAGE_SRC (message) == NULL);
    gst_message_unref (message);
  }
  /* GST_MESSAGE_ERROR */
  {
    GError *error = NULL;
    gchar *debug;

    error = g_error_new (domain, 10, "test error");
    fail_if (error == NULL);
    message = gst_message_new_error (NULL, error, "error string");
    fail_if (message == NULL);
    fail_unless (GST_MESSAGE_TYPE (message) == GST_MESSAGE_ERROR);
    fail_unless (GST_MESSAGE_SRC (message) == NULL);

    g_error_free (error);
    error = NULL;
    debug = NULL;

    gst_message_parse_error (message, &error, &debug);
    fail_if (error == NULL);
    fail_if (debug == NULL);
    fail_unless (strcmp (error->message, "test error") == 0);
    fail_unless (error->domain == domain);
    fail_unless (error->code == 10);
    fail_unless (strcmp (debug, "error string") == 0);

    gst_message_unref (message);
    g_error_free (error);
    g_free (debug);
  }
  /* GST_MESSAGE_WARNING   */
  {
    GError *warning = NULL;
    gchar *debug;

    warning = g_error_new (domain, 10, "test warning");
    fail_if (warning == NULL);
    message = gst_message_new_warning (NULL, warning, "warning string");
    fail_if (message == NULL);
    fail_unless (GST_MESSAGE_TYPE (message) == GST_MESSAGE_WARNING);
    fail_unless (GST_MESSAGE_SRC (message) == NULL);

    g_error_free (warning);
    warning = NULL;
    debug = NULL;

    gst_message_parse_warning (message, &warning, &debug);
    fail_if (warning == NULL);
    fail_if (debug == NULL);
    fail_unless (strcmp (warning->message, "test warning") == 0);
    fail_unless (warning->domain == domain);
    fail_unless (warning->code == 10);
    fail_unless (strcmp (debug, "warning string") == 0);

    gst_message_unref (message);
    g_error_free (warning);
    g_free (debug);
  }
  /* GST_MESSAGE_INFO   */
  {
  }
  /* GST_MESSAGE_TAG  */
  {
    GstTagList *tag;

    /* FIXME, do some more tag adding */
    tag = gst_tag_list_new ();
    fail_if (tag == NULL);
    message = gst_message_new_tag (NULL, tag);
    fail_if (message == NULL);
    fail_unless (GST_MESSAGE_TYPE (message) == GST_MESSAGE_TAG);
    fail_unless (GST_MESSAGE_SRC (message) == NULL);
    tag = NULL;
    gst_message_parse_tag (message, &tag);
    fail_if (tag == NULL);
    /* FIXME, check the actual tags */
    gst_message_unref (message);
    gst_tag_list_free (tag);
  }
  /* GST_MESSAGE_BUFFERING   */
  {
  }
  /* GST_MESSAGE_STATE_CHANGED   */
  {
    GstState oldstate, newstate, pending;

    oldstate = GST_STATE_PAUSED;
    newstate = GST_STATE_PLAYING;
    pending = GST_STATE_VOID_PENDING;

    message = gst_message_new_state_changed (NULL, oldstate, newstate, pending);
    fail_if (message == NULL);
    fail_unless (GST_MESSAGE_TYPE (message) == GST_MESSAGE_STATE_CHANGED);
    fail_unless (GST_MESSAGE_SRC (message) == NULL);

    /* set some wrong values to check if the parse method overwrites them
     * with the good values */
    oldstate = GST_STATE_READY;
    newstate = GST_STATE_READY;
    pending = GST_STATE_READY;
    gst_message_parse_state_changed (message, &oldstate, &newstate, &pending);
    fail_unless (oldstate == GST_STATE_PAUSED);
    fail_unless (newstate == GST_STATE_PLAYING);
    fail_unless (pending == GST_STATE_VOID_PENDING);

    gst_message_unref (message);
  }
  /* GST_MESSAGE_STEP_DONE   */
  {
  }
  /* GST_MESSAGE_NEW_CLOCK  */
  {
  }
  /* GST_MESSAGE_STRUCTURE_CHANGE  */
  {
  }
  /* GST_MESSAGE_STREAM_STATUS  */
  {
  }
  /* GST_MESSAGE_APPLICATION */
  {
    GstStructure *structure;
    const GstStructure *struc;
    gint some_int;
    gdouble a_double;

    structure = gst_structure_new ("test_struct",
        "some_int", G_TYPE_INT, 10,
        "a_double", G_TYPE_DOUBLE, (gdouble) 1.8, NULL);
    fail_if (structure == NULL);
    message = gst_message_new_application (NULL, structure);
    fail_if (message == NULL);
    struc = gst_message_get_structure (message);
    fail_if (struc == NULL);
    fail_unless (gst_structure_get_int (struc, "some_int", &some_int));
    fail_unless (gst_structure_get_double (struc, "a_double", &a_double));
    fail_unless (some_int == 10);
    fail_unless (a_double == 1.8);

    gst_message_unref (message);
  }

  /*
     void            gst_message_parse_tag           (GstMessage *message, GstTagList **tag_list);
     void            gst_message_parse_state_changed (GstMessage *message, GstState *old_state,
     GstState *new_state);
     void            gst_message_parse_error         (GstMessage *message, GError **gerror, gchar **debug);
     void            gst_message_parse_warning       (GstMessage *message, GError **gerror, gchar **debug);
   */

  /* GST_MESSAGE_STREAM_STATUS   */
  {
    GstStreamStatusType type;
    GstTask *task, *task2;
    GValue value = { 0 };
    const GValue *val;

    message =
        gst_message_new_stream_status (NULL, GST_STREAM_STATUS_TYPE_ENTER,
        NULL);
    fail_if (message == NULL);
    fail_unless (GST_MESSAGE_TYPE (message) == GST_MESSAGE_STREAM_STATUS);
    fail_unless (GST_MESSAGE_SRC (message) == NULL);

    /* set some wrong values to check if the parse method overwrites them
     * with the good values */
    type = GST_STREAM_STATUS_TYPE_START;
    gst_message_parse_stream_status (message, &type, NULL);
    fail_unless (type == GST_STREAM_STATUS_TYPE_ENTER);

    /* create a task with some dummy function, we're not actually going to run
     * the task here */
    task = gst_task_create ((GstTaskFunction) gst_object_unref, NULL);

    ASSERT_OBJECT_REFCOUNT (task, "task", 1);

    /* set the task */
    g_value_init (&value, GST_TYPE_TASK);
    g_value_set_object (&value, task);

    ASSERT_OBJECT_REFCOUNT (task, "task", 2);

    gst_message_set_stream_status_object (message, &value);
    ASSERT_OBJECT_REFCOUNT (task, "task", 3);
    g_value_unset (&value);
    ASSERT_OBJECT_REFCOUNT (task, "task", 2);
    gst_object_unref (task);
    ASSERT_OBJECT_REFCOUNT (task, "task", 1);

    /* get the object back, no refcount is changed */
    val = gst_message_get_stream_status_object (message);
    ASSERT_OBJECT_REFCOUNT (task, "task", 1);

    task2 = g_value_get_object (val);

    fail_unless (GST_IS_TASK (task2));
    fail_unless (task2 == task);

    ASSERT_OBJECT_REFCOUNT (task, "task", 1);
    ASSERT_OBJECT_REFCOUNT (task2, "task", 1);

    gst_message_unref (message);
  }

  /* GST_MESSAGE_REQUEST_STATE   */
  {
    GstState state;

    state = GST_STATE_PAUSED;

    message = gst_message_new_request_state (NULL, state);
    fail_if (message == NULL);
    fail_unless (GST_MESSAGE_TYPE (message) == GST_MESSAGE_REQUEST_STATE);
    fail_unless (GST_MESSAGE_SRC (message) == NULL);

    /* set some wrong values to check if the parse method overwrites them
     * with the good values */
    state = GST_STATE_READY;
    gst_message_parse_request_state (message, &state);
    fail_unless (state == GST_STATE_PAUSED);

    gst_message_unref (message);
  }
  /* GST_MESSAGE_QOS   */
  {
    gboolean live;
    GstClockTime running_time;
    GstClockTime stream_time;
    GstClockTime timestamp, duration;
    gint64 jitter;
    gdouble proportion;
    gint quality;
    GstFormat format;
    guint64 processed;
    guint64 dropped;

    running_time = 1 * GST_SECOND;
    stream_time = 2 * GST_SECOND;
    timestamp = 3 * GST_SECOND;
    duration = 4 * GST_SECOND;

    message =
        gst_message_new_qos (NULL, TRUE, running_time, stream_time, timestamp,
        duration);
    fail_if (message == NULL);
    fail_unless (GST_MESSAGE_TYPE (message) == GST_MESSAGE_QOS);
    fail_unless (GST_MESSAGE_SRC (message) == NULL);

    /* check defaults */
    gst_message_parse_qos_values (message, &jitter, &proportion, &quality);
    fail_unless (jitter == 0);
    fail_unless (proportion == 1.0);
    fail_unless (quality == 1000000);

    gst_message_parse_qos_stats (message, &format, &processed, &dropped);
    fail_unless (format == GST_FORMAT_UNDEFINED);
    fail_unless (processed == -1);
    fail_unless (dropped == -1);

    /* set some wrong values to check if the parse method overwrites them
     * with the good values */
    running_time = stream_time = timestamp = duration = 5 * GST_SECOND;
    live = FALSE;
    gst_message_parse_qos (message, &live, &running_time, &stream_time,
        &timestamp, &duration);
    fail_unless (live == TRUE);
    fail_unless (running_time == 1 * GST_SECOND);
    fail_unless (stream_time == 2 * GST_SECOND);
    fail_unless (timestamp == 3 * GST_SECOND);
    fail_unless (duration == 4 * GST_SECOND);

    /* change some values */
    gst_message_set_qos_values (message, -10, 2.0, 5000);
    gst_message_parse_qos_values (message, &jitter, &proportion, &quality);
    fail_unless (jitter == -10);
    fail_unless (proportion == 2.0);
    fail_unless (quality == 5000);

    gst_message_set_qos_stats (message, GST_FORMAT_DEFAULT, 1030, 65);
    gst_message_parse_qos_stats (message, &format, &processed, &dropped);
    fail_unless (format == GST_FORMAT_DEFAULT);
    fail_unless (processed == 1030);
    fail_unless (dropped == 65);

    gst_message_unref (message);
  }
}

GST_END_TEST;

static Suite *
gst_message_suite (void)
{
  Suite *s = suite_create ("GstMessage");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_parsing);

  return s;
}

GST_CHECK_MAIN (gst_message);
