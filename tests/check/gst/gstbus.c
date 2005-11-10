/* GStreamer
 * Copyright (C) 2005 Andy Wingo <wingo@pobox.com>
 *
 * gstbus.c: Unit test for the message bus
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

static GstBus *test_bus = NULL;
static GMainLoop *main_loop;

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
pull_messages ()
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
    threads[i] = g_thread_create (pound_bus_with_messages, GINT_TO_POINTER (i),
        TRUE, NULL);

  for (i = 0; i < NUM_THREADS; i++)
    g_thread_join (threads[i]);

  pull_messages ();

  gst_object_unref ((GstObject *) test_bus);
}
GST_END_TEST static gboolean
message_func_eos (GstBus * bus, GstMessage * message, gpointer data)
{
  const GstStructure *s;
  gint i;

  g_return_val_if_fail (GST_MESSAGE_TYPE (message) == GST_MESSAGE_EOS, FALSE);

  GST_DEBUG ("got EOS message");

  s = gst_message_get_structure (message);
  if (!gst_structure_get_int (s, "msg_id", &i))
    g_critical ("Invalid message");

  return i != 9;
}

static gboolean
message_func_app (GstBus * bus, GstMessage * message, gpointer data)
{
  const GstStructure *s;
  gint i;

  g_return_val_if_fail (GST_MESSAGE_TYPE (message) == GST_MESSAGE_APPLICATION,
      FALSE);

  GST_DEBUG ("got APP message");

  s = gst_message_get_structure (message);
  if (!gst_structure_get_int (s, "msg_id", &i))
    g_critical ("Invalid message");

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
  guint id;

  test_bus = gst_bus_new ();

  main_loop = g_main_loop_new (NULL, FALSE);

  id = gst_bus_add_watch (test_bus, gst_bus_async_signal_func, NULL);
  g_signal_connect (test_bus, "message::eos", (GCallback) message_func_eos,
      NULL);
  g_signal_connect (test_bus, "message::application",
      (GCallback) message_func_app, NULL);

  g_idle_add ((GSourceFunc) send_messages, NULL);
  while (g_main_context_pending (NULL))
    g_main_context_iteration (NULL, FALSE);

  g_source_remove (id);
  g_main_loop_unref (main_loop);

  gst_object_unref ((GstObject *) test_bus);
}
GST_END_TEST static gint messages_seen = 0;

static void
message_func (GstBus * bus, GstMessage * message, gpointer data)
{
  g_return_if_fail (GST_MESSAGE_TYPE (message) == GST_MESSAGE_APPLICATION);

  messages_seen++;
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
GST_END_TEST Suite * gstbus_suite (void)
{
  Suite *s = suite_create ("GstBus");
  TCase *tc_chain = tcase_create ("stresstest");

  tcase_set_timeout (tc_chain, 20);

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_hammer_bus);
  tcase_add_test (tc_chain, test_watch);
  tcase_add_test (tc_chain, test_watch_with_poll);
  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = gstbus_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
