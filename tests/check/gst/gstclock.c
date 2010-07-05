/* GStreamer
 * Copyright (C) 2010 Alessandro Decina <alessandro.decina@collabora.co.uk>
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

static void
weak_notify (gpointer data, GObject * object)
{
  *(gboolean *) data = FALSE;
}

static GstClockReturn
fake_wait_async (GstClock * clock, GstClockEntry * entry)
{
  return GST_CLOCK_OK;
}

GST_START_TEST (test_set_master_refcount)
{
  GstClock *master, *slave;
  GstClockClass *klass;
  gboolean master_alive = TRUE;

  /* create master and slave */
  master = g_object_new (GST_TYPE_CLOCK, "name", "TestClockMaster", NULL);
  slave = g_object_new (GST_TYPE_CLOCK, "name", "TestClockMaster", NULL);
  GST_OBJECT_FLAG_SET (slave, GST_CLOCK_FLAG_CAN_SET_MASTER);

  /* look ma! i'm doing monkey patching in C */
  klass = GST_CLOCK_GET_CLASS (master);
  klass->wait_async = fake_wait_async;

  fail_unless_equals_int (GST_OBJECT_REFCOUNT (master), 1);
  fail_unless_equals_int (GST_OBJECT_REFCOUNT (slave), 1);

  g_object_weak_ref (G_OBJECT (master), weak_notify, &master_alive);
  fail_unless_equals_int (GST_OBJECT_REFCOUNT (master), 1);

  gst_clock_set_master (slave, master);
  /* slave stores master in slave->master */
  fail_unless_equals_int (GST_OBJECT_REFCOUNT (master), 2);
  /* master stores a ref to slave in master->clockid */
  fail_unless_equals_int (GST_OBJECT_REFCOUNT (slave), 2);

  /* discard our ref */
  gst_object_unref (master);

  /* master should still be reffed inside slave */
  fail_unless_equals_int (GST_OBJECT_REFCOUNT (master), 1);
  fail_unless (master_alive);

  /* drop the last ref to mater */
  gst_clock_set_master (slave, NULL);

  fail_if (master_alive);
  fail_unless_equals_int (GST_OBJECT_REFCOUNT (slave), 1);

  gst_object_unref (slave);

  klass->wait_async = NULL;
}

GST_END_TEST;

static Suite *
gst_clock_suite (void)
{
  Suite *s = suite_create ("GstClock");
  TCase *tc_chain = tcase_create ("clock");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_set_master_refcount);

  return s;
}

GST_CHECK_MAIN (gst_clock);
