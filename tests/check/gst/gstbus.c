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


#include "../gstcheck.h"


static GstBus *test_bus = NULL;

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
GST_END_TEST Suite *
gstbus_suite (void)
{
  Suite *s = suite_create ("GstBus");
  TCase *tc_chain = tcase_create ("stresstest");

  tcase_set_timeout (tc_chain, 20);

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_hammer_bus);
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
