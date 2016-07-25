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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
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

    gst_message_parse_error (message, NULL, NULL);

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
  /* GST_MESSAGE_ERROR with details */
  {
    GError *error = NULL;
    gchar *debug;
    GstStructure *d;
    const GstStructure *dc;

    error = g_error_new (domain, 10, "test error");
    fail_if (error == NULL);
    d = gst_structure_new ("title", "test-field", G_TYPE_STRING,
        "test-contents", NULL);
    message =
        gst_message_new_error_with_details (NULL, error, "error string", d);
    fail_if (message == NULL);
    fail_unless (GST_MESSAGE_TYPE (message) == GST_MESSAGE_ERROR);
    fail_unless (GST_MESSAGE_SRC (message) == NULL);

    g_error_free (error);
    error = NULL;
    debug = NULL;

    gst_message_parse_error (message, NULL, NULL);

    gst_message_parse_error (message, &error, &debug);
    fail_if (error == NULL);
    fail_if (debug == NULL);
    fail_unless (strcmp (error->message, "test error") == 0);
    fail_unless (error->domain == domain);
    fail_unless (error->code == 10);
    fail_unless (strcmp (debug, "error string") == 0);
    gst_message_parse_error_details (message, &dc);
    fail_unless (dc != NULL);
    fail_unless (gst_structure_has_field_typed (dc, "test-field",
            G_TYPE_STRING));
    fail_unless (gst_structure_get_string (dc, "test-field"), "test-contents");

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

    gst_message_parse_warning (message, NULL, NULL);

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
    GError *info = NULL;
    gchar *debug;

    info = g_error_new (domain, 10, "test info");
    fail_if (info == NULL);
    message = gst_message_new_info (NULL, info, "info string");
    fail_if (message == NULL);
    fail_unless (GST_MESSAGE_TYPE (message) == GST_MESSAGE_INFO);
    fail_unless (GST_MESSAGE_SRC (message) == NULL);

    g_error_free (info);
    info = NULL;
    debug = NULL;

    gst_message_parse_info (message, NULL, NULL);

    gst_message_parse_info (message, &info, &debug);
    fail_if (info == NULL);
    fail_if (debug == NULL);
    fail_unless (strcmp (info->message, "test info") == 0);
    fail_unless (info->domain == domain);
    fail_unless (info->code == 10);
    fail_unless (strcmp (debug, "info string") == 0);

    gst_message_unref (message);
    g_error_free (info);
    g_free (debug);
  }
  /* GST_MESSAGE_TAG  */
  {
    GstTagList *tag;

    /* FIXME, do some more tag adding */
    tag = gst_tag_list_new_empty ();
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
    gst_tag_list_unref (tag);
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
    task = gst_task_new ((GstTaskFunction) gst_object_unref, NULL, NULL);

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
  /* GST_MESSAGE_PROGRESS   */
  {
    GstProgressType type;
    gchar *category, *text;

    message =
        gst_message_new_progress (NULL, GST_PROGRESS_TYPE_START, "connecting",
        "Connecting to youtbue.com");
    fail_if (message == NULL);
    fail_unless (GST_MESSAGE_TYPE (message) == GST_MESSAGE_PROGRESS);
    fail_unless (GST_MESSAGE_SRC (message) == NULL);

    /* set some wrong values to check if the parse method overwrites them
     * with the good values */
    type = GST_PROGRESS_TYPE_ERROR;
    gst_message_parse_progress (message, &type, &category, &text);
    fail_unless (type == GST_PROGRESS_TYPE_START);
    fail_unless (!strcmp (category, "connecting"));
    fail_unless (!strcmp (text, "Connecting to youtbue.com"));
    g_free (category);
    g_free (text);

    gst_message_unref (message);
  }
  /* GST_MESSAGE_STREAM_COLLECTION */
  {
    GstMessage *message;
    GstStreamCollection *collection, *res = NULL;
    GstStream *stream1, *stream2;
    GstCaps *caps1, *caps2;

    /* Create a collection of two streams */
    caps1 = gst_caps_from_string ("some/caps");
    caps2 = gst_caps_from_string ("some/other-string");

    stream1 = gst_stream_new ("stream-1", caps1, GST_STREAM_TYPE_AUDIO, 0);
    stream2 = gst_stream_new ("stream-2", caps2, GST_STREAM_TYPE_VIDEO, 0);

    collection = gst_stream_collection_new ("something");
    fail_unless (gst_stream_collection_add_stream (collection, stream1));
    fail_unless (gst_stream_collection_add_stream (collection, stream2));

    message = gst_message_new_stream_collection (NULL, collection);
    fail_unless (message != NULL);

    gst_message_parse_stream_collection (message, &res);
    fail_unless (res != NULL);

    gst_message_unref (message);
    gst_object_unref (res);
    gst_object_unref (collection);
    gst_caps_unref (caps1);
    gst_caps_unref (caps2);
  }
  /* GST_MESSAGE_STREAMS_SELECTED */
  {
    GstMessage *message;
    GstStreamCollection *collection, *res = NULL;
    GstStream *stream1, *stream2, *stream3;
    GstCaps *caps1, *caps2;

    /* Create a collection of two streams */
    caps1 = gst_caps_from_string ("some/caps");
    caps2 = gst_caps_from_string ("some/other-string");

    stream1 = gst_stream_new ("stream-1", caps1, GST_STREAM_TYPE_AUDIO, 0);
    stream2 = gst_stream_new ("stream-2", caps2, GST_STREAM_TYPE_VIDEO, 0);

    collection = gst_stream_collection_new ("something");
    fail_unless (gst_stream_collection_add_stream (collection, stream1));
    fail_unless (gst_stream_collection_add_stream (collection, stream2));

    message = gst_message_new_streams_selected (NULL, collection);
    fail_unless (message != NULL);

    gst_message_parse_streams_selected (message, &res);
    fail_unless (res != NULL);

    fail_unless (gst_message_streams_selected_get_size (message) == 0);
    gst_object_unref (res);
    gst_message_unref (message);

    /* Once again, this time with a stream in it */
    message = gst_message_new_streams_selected (NULL, collection);
    fail_unless (message != NULL);

    gst_message_streams_selected_add (message, stream1);

    gst_message_parse_streams_selected (message, &res);
    fail_unless (res != NULL);

    /* There is only one stream ! */
    fail_unless (gst_message_streams_selected_get_size (message) == 1);

    stream3 = gst_message_streams_selected_get_stream (message, 0);
    fail_unless (stream3 != NULL);
    gst_object_unref (stream3);

    /* Shoul fail */
    ASSERT_CRITICAL (gst_message_streams_selected_get_stream (message, 1));

    gst_object_unref (res);
    gst_message_unref (message);

    gst_object_unref (collection);
    gst_caps_unref (caps1);
    gst_caps_unref (caps2);
  }
  /* GST_MESSAGE_REDIRECT */
  {
    const gchar *parsed_location;
    GstTagList *parsed_tag_list;
    const GstStructure *parsed_structure;
    const gchar *test_location = "some-location";
    const gchar *test_struct_name = "test-struct";
    const gchar *test_value_name = "foo";
    const gint test_value = 12345;
    const guint test_bitrate = 120000;
    gint value;
    guint bitrate;
    GstTagList *test_tag_list;
    GstStructure *test_structure;

    test_structure =
        gst_structure_new (test_struct_name, test_value_name, G_TYPE_INT,
        test_value, NULL);

    /* Create a test tag list. It is ref'd  before adding an entry to be able
     * to test that new_redirect takes ownership */
    test_tag_list = gst_tag_list_new (GST_TAG_BITRATE, test_bitrate, NULL);

    /* Create the message and add the first entry, which only has a location
     * and a tag list */
    gst_tag_list_ref (test_tag_list);
    message =
        gst_message_new_redirect (NULL, test_location, test_tag_list, NULL);
    fail_if (message == NULL);
    fail_unless (GST_MESSAGE_TYPE (message) == GST_MESSAGE_REDIRECT);
    fail_unless (GST_MESSAGE_SRC (message) == NULL);

    /* Add the second entry, which only has a location and a structure */
    gst_message_add_redirect_entry (message, test_location, NULL,
        gst_structure_copy (test_structure));

    /* Add the third entry, which has a location, a taglist, and a structure */
    gst_tag_list_ref (test_tag_list);
    gst_message_add_redirect_entry (message, test_location, test_tag_list,
        gst_structure_copy (test_structure));

    fail_unless (gst_message_get_num_redirect_entries (message) == 3);

    /* Check that the location of the first entry is correct and that the
     * structure pointer is set to NULL */
    parsed_location = NULL;
    parsed_tag_list = NULL;
    parsed_structure = (const GstStructure *) 0x1;
    gst_message_parse_redirect_entry (message, 0, &parsed_location,
        &parsed_tag_list, &parsed_structure);
    fail_unless (parsed_location != NULL);
    fail_unless (parsed_tag_list != NULL);
    fail_unless (parsed_structure == NULL);
    fail_unless (!strcmp (parsed_location, test_location));
    fail_unless (gst_tag_list_get_uint (parsed_tag_list, GST_TAG_BITRATE,
            &bitrate) && (bitrate == test_bitrate));

    /* Check that the structure of the second entry is correct and that the
     * tag list pointer is set to NULL */
    parsed_location = NULL;
    parsed_tag_list = (GstTagList *) 0x1;
    parsed_structure = NULL;
    gst_message_parse_redirect_entry (message, 1, &parsed_location,
        &parsed_tag_list, &parsed_structure);
    fail_unless (parsed_location != NULL);
    fail_unless (parsed_tag_list == NULL);
    fail_unless (parsed_structure != NULL);
    fail_unless (!strcmp (parsed_location, test_location));
    fail_unless (!strcmp (gst_structure_get_name (parsed_structure),
            test_struct_name));
    fail_unless (gst_structure_get_int (parsed_structure, test_value_name,
            &value) && (value == test_value));

    /* Check that the location, tag list, and structure pointers of the
     * third entry are correct */
    parsed_location = NULL;
    parsed_tag_list = NULL;
    parsed_structure = NULL;
    gst_message_parse_redirect_entry (message, 2, &parsed_location,
        &parsed_tag_list, &parsed_structure);
    fail_unless (parsed_location != NULL);
    fail_unless (parsed_tag_list != NULL);
    fail_unless (parsed_structure != NULL);
    fail_unless (!strcmp (parsed_location, test_location));
    fail_unless (!strcmp (gst_structure_get_name (parsed_structure),
            test_struct_name));
    fail_unless (gst_tag_list_get_uint (parsed_tag_list, GST_TAG_BITRATE,
            &bitrate) && (bitrate == test_bitrate));
    fail_unless (gst_structure_get_int (parsed_structure, test_value_name,
            &value) && (value == test_value));

    gst_message_unref (message);

    /* Since the message takes ownership over the tag list, its refcount
     * must have been decreased after each added entry */
    fail_unless_equals_int (GST_MINI_OBJECT_REFCOUNT_VALUE (test_tag_list), 1);

    gst_structure_free (test_structure);
    gst_tag_list_unref (test_tag_list);
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
