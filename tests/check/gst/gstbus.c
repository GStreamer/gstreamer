/* GStreamer message bus unit tests
 * Copyright (C) 2005 Andy Wingo <wingo@pobox.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/check/gstcheck.h>

static GstBus *test_bus = NULL;
static GMainLoop *main_loop;

static GType foo_device_get_type (void);

#define NUM_MESSAGES 1000
#define NUM_THREADS 10

static gpointer
pound_bus_with_messages (gpointer data)
{
  gint thread_id = GPOINTER_TO_INT (data);
  gint i;

  for (i = 0; i < NUM_MESSAGES; i++) {
    GstMessage *m;
    GstStructure *s;

    s = gst_structure_new ("test_message",
        "thread_id", G_TYPE_INT, thread_id, "msg_id", G_TYPE_INT, i, NULL);
    m = gst_message_new_application (NULL, s);
    gst_bus_post (test_bus, m);
  }
  return NULL;
}

static void
pull_messages (void)
{
  GstMessage *m;
  const GstStructure *s;
  guint message_ids[NUM_THREADS];
  gint i;

  for (i = 0; i < NUM_THREADS; i++)
    message_ids[i] = 0;

  while (1) {
    gint _t, _i;

    m = gst_bus_pop (test_bus);
    if (!m)
      break;
    g_return_if_fail (GST_MESSAGE_TYPE (m) == GST_MESSAGE_APPLICATION);

    s = gst_message_get_structure (m);
    if (!gst_structure_get_int (s, "thread_id", &_t))
      g_critical ("Invalid message");
    if (!gst_structure_get_int (s, "msg_id", &_i))
      g_critical ("Invalid message");

    g_return_if_fail (_t < NUM_THREADS);
    g_return_if_fail (_i == message_ids[_t]++);

    gst_message_unref (m);
  }

  for (i = 0; i < NUM_THREADS; i++)
    g_return_if_fail (message_ids[i] == NUM_MESSAGES);
}

GST_START_TEST (test_hammer_bus)
{
  GThread *threads[NUM_THREADS];
  gint i;

  test_bus = gst_bus_new ();

  for (i = 0; i < NUM_THREADS; i++)
    threads[i] = g_thread_try_new ("gst-check", pound_bus_with_messages,
        GINT_TO_POINTER (i), NULL);

  for (i = 0; i < NUM_THREADS; i++)
    g_thread_join (threads[i]);

  pull_messages ();

  gst_object_unref ((GstObject *) test_bus);
}

GST_END_TEST;

static gboolean
message_func_eos (GstBus * bus, GstMessage * message, guint * p_counter)
{
  const GstStructure *s;
  gint i;

  g_return_val_if_fail (GST_MESSAGE_TYPE (message) == GST_MESSAGE_EOS, FALSE);

  GST_DEBUG ("got EOS message");

  s = gst_message_get_structure (message);
  if (!gst_structure_get_int (s, "msg_id", &i))
    g_critical ("Invalid message");

  if (p_counter != NULL)
    *p_counter += 1;

  return i != 9;
}

static gboolean
message_func_app (GstBus * bus, GstMessage * message, guint * p_counter)
{
  const GstStructure *s;
  gint i;

  g_return_val_if_fail (GST_MESSAGE_TYPE (message) == GST_MESSAGE_APPLICATION,
      FALSE);

  GST_DEBUG ("got APP message");

  s = gst_message_get_structure (message);
  if (!gst_structure_get_int (s, "msg_id", &i))
    g_critical ("Invalid message");

  if (p_counter != NULL)
    *p_counter += 1;

  return i != 9;
}

static gboolean
send_messages (gpointer data)
{
  GstMessage *m;
  GstStructure *s;
  gint i;

  for (i = 0; i < 10; i++) {
    s = gst_structure_new ("test_message", "msg_id", G_TYPE_INT, i, NULL);
    m = gst_message_new_application (NULL, s);
    gst_bus_post (test_bus, m);
    s = gst_structure_new ("test_message", "msg_id", G_TYPE_INT, i, NULL);
    m = gst_message_new_custom (GST_MESSAGE_EOS, NULL, s);
    gst_bus_post (test_bus, m);
  }

  return FALSE;
}

/* test if adding a signal watch for different message types calls the
 * respective callbacks. */
GST_START_TEST (test_watch)
{
  guint num_eos = 0;
  guint num_app = 0;
  guint id;

  test_bus = gst_bus_new ();

  main_loop = g_main_loop_new (NULL, FALSE);

  id = gst_bus_add_watch (test_bus, gst_bus_async_signal_func, NULL);
  fail_if (id == 0);
  g_signal_connect (test_bus, "message::eos", (GCallback) message_func_eos,
      &num_eos);
  g_signal_connect (test_bus, "message::application",
      (GCallback) message_func_app, &num_app);

  g_idle_add ((GSourceFunc) send_messages, NULL);
  while (g_main_context_pending (NULL))
    g_main_context_iteration (NULL, FALSE);

  fail_unless_equals_int (num_eos, 10);
  fail_unless_equals_int (num_app, 10);

  fail_unless (gst_bus_remove_watch (test_bus));
  g_main_loop_unref (main_loop);

  gst_object_unref ((GstObject *) test_bus);
}

GST_END_TEST;

/* test if adding a signal watch for different message types calls the
 * respective callbacks. */
GST_START_TEST (test_watch_with_custom_context)
{
  GMainContext *ctx;
  GSource *source;
  guint num_eos = 0;
  guint num_app = 0;
  guint id;

  test_bus = gst_bus_new ();

  ctx = g_main_context_new ();
  main_loop = g_main_loop_new (ctx, FALSE);

  source = gst_bus_create_watch (test_bus);
  g_source_set_callback (source, (GSourceFunc) gst_bus_async_signal_func, NULL,
      NULL);
  id = g_source_attach (source, ctx);
  g_source_unref (source);
  fail_if (id == 0);

  g_signal_connect (test_bus, "message::eos", (GCallback) message_func_eos,
      &num_eos);
  g_signal_connect (test_bus, "message::application",
      (GCallback) message_func_app, &num_app);

  source = g_idle_source_new ();
  g_source_set_callback (source, (GSourceFunc) send_messages, NULL, NULL);
  g_source_attach (source, ctx);
  g_source_unref (source);

  while (g_main_context_pending (ctx))
    g_main_context_iteration (ctx, FALSE);

  fail_unless_equals_int (num_eos, 10);
  fail_unless_equals_int (num_app, 10);

  if ((source = g_main_context_find_source_by_id (ctx, id)))
    g_source_destroy (source);
  g_main_loop_unref (main_loop);
  g_main_context_unref (ctx);

  gst_object_unref (test_bus);
}

GST_END_TEST;

/* test if adding a signal watch for different message types calls the
 * respective callbacks. */
GST_START_TEST (test_add_watch_with_custom_context)
{
  GMainContext *ctx;
  GSource *source;
  guint num_eos = 0;
  guint num_app = 0;

  test_bus = gst_bus_new ();

  ctx = g_main_context_new ();
  main_loop = g_main_loop_new (ctx, FALSE);

  g_main_context_push_thread_default (ctx);
  gst_bus_add_signal_watch (test_bus);
  g_main_context_pop_thread_default (ctx);

  g_signal_connect (test_bus, "message::eos", (GCallback) message_func_eos,
      &num_eos);
  g_signal_connect (test_bus, "message::application",
      (GCallback) message_func_app, &num_app);

  source = g_idle_source_new ();
  g_source_set_callback (source, (GSourceFunc) send_messages, NULL, NULL);
  g_source_attach (source, ctx);
  g_source_unref (source);

  while (g_main_context_pending (ctx))
    g_main_context_iteration (ctx, FALSE);

  fail_unless_equals_int (num_eos, 10);
  fail_unless_equals_int (num_app, 10);

  g_main_loop_unref (main_loop);
  g_main_context_unref (ctx);

  gst_object_unref (test_bus);
}

GST_END_TEST;

static gboolean
dummy_bus_func (GstBus * bus, GstMessage * msg, gpointer user_data)
{
  return TRUE;
}

GST_START_TEST (test_remove_watch)
{
  test_bus = gst_bus_new ();

  /* removing a non-existing watch should fail */
  fail_if (gst_bus_remove_watch (test_bus));

  gst_bus_add_watch (test_bus, dummy_bus_func, NULL);

  fail_unless (gst_bus_remove_watch (test_bus));

  /* now it should fail to remove the watch again */
  fail_if (gst_bus_remove_watch (test_bus));

  gst_object_unref (test_bus);
}

GST_END_TEST;

static gint messages_seen;

static void
message_func (GstBus * bus, GstMessage * message, gpointer data)
{
  g_return_if_fail (GST_MESSAGE_TYPE (message) == GST_MESSAGE_APPLICATION);

  messages_seen++;
}

static void
send_5app_1el_1err_2app_1eos_messages (guint interval_usecs)
{
  GstMessage *m;
  GstStructure *s;
  gint i;

  for (i = 0; i < 5; i++) {
    s = gst_structure_new ("test_message", "msg_id", G_TYPE_INT, i, NULL);
    m = gst_message_new_application (NULL, s);
    GST_LOG ("posting application message");
    gst_bus_post (test_bus, m);
    g_usleep (interval_usecs);
  }
  for (i = 0; i < 1; i++) {
    s = gst_structure_new ("test_message", "msg_id", G_TYPE_INT, i, NULL);
    m = gst_message_new_element (NULL, s);
    GST_LOG ("posting element message");
    gst_bus_post (test_bus, m);
    g_usleep (interval_usecs);
  }
  for (i = 0; i < 1; i++) {
    m = gst_message_new_error (NULL, NULL, "debug string");
    GST_LOG ("posting error message");
    gst_bus_post (test_bus, m);
    g_usleep (interval_usecs);
  }
  for (i = 0; i < 2; i++) {
    s = gst_structure_new ("test_message", "msg_id", G_TYPE_INT, i, NULL);
    m = gst_message_new_application (NULL, s);
    GST_LOG ("posting application message");
    gst_bus_post (test_bus, m);
    g_usleep (interval_usecs);
  }
  for (i = 0; i < 1; i++) {
    m = gst_message_new_eos (NULL);
    GST_LOG ("posting EOS message");
    gst_bus_post (test_bus, m);
    g_usleep (interval_usecs);
  }
}

static void
send_extended_messages (guint interval_usecs)
{
  GstMessage *msg;
  GstDevice *device;

  device = g_object_new (foo_device_get_type (), NULL);

  msg = gst_message_new_device_added (NULL, device);
  GST_LOG ("posting device-added message");
  gst_bus_post (test_bus, msg);
  g_usleep (interval_usecs);

  msg = gst_message_new_device_removed (NULL, device);
  GST_LOG ("posting device-removed message");
  gst_bus_post (test_bus, msg);
  g_usleep (interval_usecs);

  gst_object_unref (device);
}


static void
send_10_app_messages (void)
{
  GstMessage *m;
  GstStructure *s;
  gint i;

  for (i = 0; i < 10; i++) {
    s = gst_structure_new ("test_message", "msg_id", G_TYPE_INT, i, NULL);
    m = gst_message_new_application (NULL, s);
    gst_bus_post (test_bus, m);
  }
}

/* test that you get the same messages from a poll as from signal watches. */
GST_START_TEST (test_watch_with_poll)
{
  guint i;

  test_bus = gst_bus_new ();
  messages_seen = 0;

  gst_bus_add_signal_watch (test_bus);
  g_signal_connect (test_bus, "message", (GCallback) message_func, NULL);

  send_10_app_messages ();

  for (i = 0; i < 10; i++)
    gst_message_unref (gst_bus_poll (test_bus, GST_MESSAGE_APPLICATION,
            GST_CLOCK_TIME_NONE));

  fail_if (gst_bus_have_pending (test_bus), "unexpected messages on bus");
  fail_unless (messages_seen == 10, "signal handler didn't get 10 messages");

  gst_bus_remove_signal_watch (test_bus);

  gst_object_unref (test_bus);
}

GST_END_TEST;

/* test that you get the messages with pop. */
GST_START_TEST (test_timed_pop)
{
  guint i;

  test_bus = gst_bus_new ();

  send_10_app_messages ();

  for (i = 0; i < 10; i++)
    gst_message_unref (gst_bus_timed_pop (test_bus, GST_CLOCK_TIME_NONE));

  fail_if (gst_bus_have_pending (test_bus), "unexpected messages on bus");

  gst_object_unref (test_bus);
}

GST_END_TEST;

typedef struct
{
  GstDevice device;
} FooDevice;
typedef struct
{
  GstDeviceClass device_klass;
} FooDeviceClass;

G_DEFINE_TYPE (FooDevice, foo_device, GST_TYPE_DEVICE);

static void
foo_device_class_init (FooDeviceClass * klass)
{
  /* nothing to do here */
}

static void
foo_device_init (FooDevice * device)
{
  /* nothing to do here */
}

/* test that you get the messages with pop_filtered */
GST_START_TEST (test_timed_pop_filtered)
{
  GstMessage *msg;
  guint i;

  test_bus = gst_bus_new ();

  send_10_app_messages ();
  for (i = 0; i < 10; i++) {
    msg = gst_bus_timed_pop_filtered (test_bus, GST_CLOCK_TIME_NONE,
        GST_MESSAGE_ANY);
    fail_unless (msg != NULL);
    gst_message_unref (msg);
  }

  /* should flush all messages on the bus with types not matching */
  send_10_app_messages ();
  msg = gst_bus_timed_pop_filtered (test_bus, 0,
      GST_MESSAGE_ANY ^ GST_MESSAGE_APPLICATION);
  fail_unless (msg == NULL);
  msg = gst_bus_timed_pop_filtered (test_bus, GST_SECOND / 2,
      GST_MESSAGE_ANY ^ GST_MESSAGE_APPLICATION);
  fail_unless (msg == NULL);
  /* there should be nothing on the bus now */
  fail_if (gst_bus_have_pending (test_bus), "unexpected messages on bus");
  msg = gst_bus_timed_pop_filtered (test_bus, 0, GST_MESSAGE_ANY);
  fail_unless (msg == NULL);

  send_5app_1el_1err_2app_1eos_messages (0);
  msg = gst_bus_timed_pop_filtered (test_bus, 0,
      GST_MESSAGE_ANY ^ GST_MESSAGE_APPLICATION);
  fail_unless (msg != NULL);
  fail_unless_equals_int (GST_MESSAGE_TYPE (msg), GST_MESSAGE_ELEMENT);
  gst_message_unref (msg);
  fail_unless (gst_bus_have_pending (test_bus), "expected messages on bus");
  msg = gst_bus_timed_pop_filtered (test_bus, 0, GST_MESSAGE_APPLICATION);
  fail_unless (msg != NULL);
  fail_unless_equals_int (GST_MESSAGE_TYPE (msg), GST_MESSAGE_APPLICATION);
  gst_message_unref (msg);
  msg = gst_bus_timed_pop_filtered (test_bus, 0, GST_MESSAGE_ERROR);
  fail_unless (msg == NULL);

  gst_object_unref (test_bus);

  /* Test extended messages */
  GST_DEBUG
      ("Checking extended messages received from gst_bus_timed_pop_filtered");
  test_bus = gst_bus_new ();

  send_5app_1el_1err_2app_1eos_messages (0);
  send_extended_messages (0);
  send_5app_1el_1err_2app_1eos_messages (0);
  msg = gst_bus_timed_pop_filtered (test_bus, 0, GST_MESSAGE_EXTENDED);
  fail_unless (msg != NULL);
  fail_unless_equals_int (GST_MESSAGE_TYPE (msg), GST_MESSAGE_DEVICE_ADDED);
  gst_message_unref (msg);

  msg = gst_bus_timed_pop_filtered (test_bus, 0, GST_MESSAGE_EXTENDED);
  fail_unless (msg != NULL);
  fail_unless_equals_int (GST_MESSAGE_TYPE (msg), GST_MESSAGE_DEVICE_REMOVED);
  gst_message_unref (msg);
  gst_object_unref (test_bus);

  /* Now check extended messages don't appear when we don't ask for them */
  GST_DEBUG
      ("Checking extended messages *not* received from gst_bus_timed_pop_filtered when not wanted");
  test_bus = gst_bus_new ();

  send_extended_messages (0);
  send_5app_1el_1err_2app_1eos_messages (0);

  msg = gst_bus_timed_pop_filtered (test_bus, 0, GST_MESSAGE_ERROR);
  fail_unless (msg != NULL);
  fail_unless_equals_int (GST_MESSAGE_TYPE (msg), GST_MESSAGE_ERROR);
  gst_message_unref (msg);

  msg = gst_bus_timed_pop_filtered (test_bus, 0, GST_MESSAGE_EOS);
  fail_unless (msg != NULL);
  fail_unless_equals_int (GST_MESSAGE_TYPE (msg), GST_MESSAGE_EOS);
  gst_message_unref (msg);

  gst_object_unref (test_bus);
}

GST_END_TEST;

static gpointer
post_delayed_thread (gpointer data)
{
  THREAD_START ();
  send_5app_1el_1err_2app_1eos_messages (1 * G_USEC_PER_SEC);
  return NULL;
}

/* test that you get the messages with pop_filtered if there's a timeout*/
GST_START_TEST (test_timed_pop_filtered_with_timeout)
{
  GstMessage *msg;

  MAIN_INIT ();

  test_bus = gst_bus_new ();

  MAIN_START_THREAD_FUNCTIONS (1, post_delayed_thread, NULL);

  MAIN_SYNCHRONIZE ();

  msg = gst_bus_timed_pop_filtered (test_bus, 2 * GST_SECOND,
      GST_MESSAGE_ERROR);
  fail_unless (msg == NULL, "Got unexpected %s message",
      (msg) ? GST_MESSAGE_TYPE_NAME (msg) : "");
  msg = gst_bus_timed_pop_filtered (test_bus, (3 + 1 + 1 + 1) * GST_SECOND,
      GST_MESSAGE_ERROR | GST_MESSAGE_ELEMENT);
  fail_unless (msg != NULL, "expected element message, but got nothing");
  fail_unless_equals_int (GST_MESSAGE_TYPE (msg), GST_MESSAGE_ELEMENT);
  gst_message_unref (msg);
  msg = gst_bus_timed_pop_filtered (test_bus, GST_CLOCK_TIME_NONE,
      GST_MESSAGE_APPLICATION);
  fail_unless (msg != NULL, "expected application message, but got nothing");
  fail_unless_equals_int (GST_MESSAGE_TYPE (msg), GST_MESSAGE_APPLICATION);
  gst_message_unref (msg);
  msg = gst_bus_timed_pop_filtered (test_bus, GST_CLOCK_TIME_NONE,
      GST_MESSAGE_APPLICATION);
  fail_unless (msg != NULL, "expected application message, but got nothing");
  fail_unless_equals_int (GST_MESSAGE_TYPE (msg), GST_MESSAGE_APPLICATION);
  gst_message_unref (msg);
  msg = gst_bus_timed_pop_filtered (test_bus, GST_SECOND / 4,
      GST_MESSAGE_TAG | GST_MESSAGE_ERROR);
  fail_unless (msg == NULL, "Got unexpected %s message",
      (msg) ? GST_MESSAGE_TYPE_NAME (msg) : "");

  MAIN_STOP_THREADS ();

  gst_object_unref (test_bus);
}

GST_END_TEST;

/* test that you get the messages with pop from another thread. */
static gpointer
pop_thread (gpointer data)
{
  GstBus *bus = GST_BUS_CAST (data);
  guint i;

  for (i = 0; i < 10; i++)
    gst_message_unref (gst_bus_timed_pop (bus, GST_CLOCK_TIME_NONE));

  return NULL;
}

GST_START_TEST (test_timed_pop_thread)
{
  GThread *thread;
  GError *error = NULL;

  test_bus = gst_bus_new ();

  thread = g_thread_try_new ("gst-chek", pop_thread, test_bus, &error);
  fail_if (error != NULL);

  send_10_app_messages ();

  g_thread_join (thread);

  fail_if (gst_bus_have_pending (test_bus), "unexpected messages on bus");

  /* try to pop a message without timeout. */
  fail_if (gst_bus_timed_pop (test_bus, 0) != NULL);

  /* with a small timeout */
  fail_if (gst_bus_timed_pop (test_bus, 1000) != NULL);

  gst_object_unref (test_bus);
}

GST_END_TEST;

static gboolean
cb_bus_call (GstBus * bus, GstMessage * msg, gpointer data)
{
  GMainLoop *loop = data;

  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_EOS:
    {
      GST_INFO ("End-of-stream");
      g_main_loop_quit (loop);
      break;
    }
    case GST_MESSAGE_ERROR:
    {
      GError *err = NULL;

      gst_message_parse_error (msg, &err, NULL);
      g_error ("Error: %s", err->message);
      g_error_free (err);

      g_main_loop_quit (loop);
      break;
    }
    default:
    {
      GST_LOG ("BUS MESSAGE: type=%s", GST_MESSAGE_TYPE_NAME (msg));
      break;
    }
  }

  return TRUE;
}

GST_START_TEST (test_custom_main_context)
{
  GMainContext *ctx;
  GMainLoop *loop;
  GstElement *pipeline;
  GstElement *src;
  GstElement *sink;
  GSource *source;
  GstBus *bus;

  ctx = g_main_context_new ();
  loop = g_main_loop_new (ctx, FALSE);

  pipeline = gst_pipeline_new (NULL);
  src = gst_element_factory_make ("fakesrc", NULL);
  g_object_set (src, "num-buffers", 2000, NULL);

  sink = gst_element_factory_make ("fakesink", NULL);

  fail_unless (gst_bin_add (GST_BIN (pipeline), src));
  fail_unless (gst_bin_add (GST_BIN (pipeline), sink));
  fail_unless (gst_element_link (src, sink));

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  source = gst_bus_create_watch (bus);
  g_source_attach (source, ctx);
  g_source_set_callback (source, (GSourceFunc) cb_bus_call, loop, NULL);
  g_source_unref (source);
  gst_object_unref (bus);

  GST_INFO ("starting pipeline");

  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  gst_element_get_state (pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

  GST_INFO ("running event loop, ctx=%p", ctx);
  g_main_loop_run (loop);

  gst_element_set_state (pipeline, GST_STATE_NULL);

  /* clean up */
  if (ctx)
    g_main_context_unref (ctx);
  g_main_loop_unref (loop);
  gst_object_unref (pipeline);
}

GST_END_TEST;

static GstBusSyncReply
test_async_sync_handler (GstBus * bus, GstMessage * msg, gpointer user_data)
{
  GArray *timestamps = user_data;
  gint64 ts = g_get_monotonic_time () * 1000;   /* microsecs -> nanosecs */

  g_array_append_val (timestamps, ts);
  GST_INFO ("[msg %p] %" GST_PTR_FORMAT, msg, msg);

  return GST_BUS_ASYNC;
}

static gpointer
post_10_app_messages_thread (gpointer data)
{
  THREAD_START ();
  send_10_app_messages ();
  return NULL;
}

/* Test GST_BUS_ASYNC actually causes the thread posting the message to
 * block until the message has been freed. We spawn a thread to post ten
 * messages. We install a bus sync handler to get the timestamp of each
 * message as it is being posted, and to return GST_BUS_ASYNC. In the main
 * thread we sleep a bit after we pop off a message and before we free it.
 * The posting thread should be blocked while the main thread sleeps, so
 * we expect the interval as the messages are posted to be roughly the same
 * as the sleep time in the main thread. g_usleep() is not super-precise, so
 * we allow for some slack there, we just want to check that the posting
 * thread was blocked at all really. */
GST_START_TEST (test_async_message)
{
  GArray *timestamps;
  guint i;

  MAIN_INIT ();

  timestamps = g_array_sized_new (FALSE, FALSE, sizeof (gint64), 10);

  test_bus = gst_bus_new ();

  gst_bus_set_sync_handler (test_bus, test_async_sync_handler, timestamps,
      NULL);

  MAIN_START_THREAD_FUNCTIONS (1, post_10_app_messages_thread, NULL);

  MAIN_SYNCHRONIZE ();

  for (i = 0; i < 10; i++) {
    GstMessage *msg;

    GST_LOG ("(%d) waiting for message..", i);
    msg = gst_bus_timed_pop (test_bus, GST_CLOCK_TIME_NONE);
    GST_LOG ("(%d) got message, sleeping a bit", i);
    g_usleep (60 * GST_MSECOND / (GST_SECOND / G_USEC_PER_SEC));
    GST_LOG ("(%d) about to free message", i);
    gst_message_unref (msg);
  }

  for (i = 1; i < 10; i++) {
    gint64 prev_ts = g_array_index (timestamps, gint64, i - 1);
    gint64 ts = g_array_index (timestamps, gint64, i);
    gint64 diff = ts - prev_ts;

    fail_unless (prev_ts < ts);
    fail_unless (diff >= 20 * GST_MSECOND, "interval between messages being "
        "posted was just %" G_GINT64_FORMAT "ms", diff / GST_MSECOND);
  }

  fail_if (gst_bus_have_pending (test_bus), "unexpected messages on bus");

  MAIN_STOP_THREADS ();

  gst_object_unref (test_bus);

  g_array_unref (timestamps);
}

GST_END_TEST;

static Suite *
gst_bus_suite (void)
{
  Suite *s = suite_create ("GstBus");
  TCase *tc_chain = tcase_create ("stresstest");

  tcase_set_timeout (tc_chain, 60);

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_hammer_bus);
  tcase_add_test (tc_chain, test_watch);
  tcase_add_test (tc_chain, test_watch_with_poll);
  tcase_add_test (tc_chain, test_watch_with_custom_context);
  tcase_add_test (tc_chain, test_add_watch_with_custom_context);
  tcase_add_test (tc_chain, test_remove_watch);
  tcase_add_test (tc_chain, test_timed_pop);
  tcase_add_test (tc_chain, test_timed_pop_thread);
  tcase_add_test (tc_chain, test_timed_pop_filtered);
  tcase_add_test (tc_chain, test_timed_pop_filtered_with_timeout);
  tcase_add_test (tc_chain, test_custom_main_context);
  tcase_add_test (tc_chain, test_async_message);
  return s;
}

GST_CHECK_MAIN (gst_bus);
