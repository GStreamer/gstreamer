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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include <gst/check/gstcheck.h>

typedef struct
{
  GstClock parent;
} TestClock;

typedef struct
{
  GstClockClass parent_class;
} TestClockClass;

#define TYPE_TEST_CLOCK                   (test_clock_get_type ())
#define TEST_CLOCK(obj)                   (G_TYPE_CHECK_INSTANCE_CAST ((obj), TYPE_TEST_CLOCK, TestClock))
#define TEST_CLOCK_CAST(obj)              ((TestClock *)(obj))
#define IS_TEST_CLOCK(obj)                (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TYPE_TEST_CLOCK))
#define TEST_CLOCK_CLASS(klass)           (G_TYPE_CHECK_CLASS_CAST ((klass), TYPE_TEST_CLOCK, TestClockClass))
#define IS_TEST_CLOCK_CLASS(klass)        (G_TYPE_CHECK_CLASS_TYPE ((klass), TYPE_TEST_CLOCK))
#define TEST_CLOCK_GET_CLASS(obj)         (G_TYPE_INSTANCE_GET_CLASS ((obj), TYPE_TEST_CLOCK, TestClockClass))


GType test_clock_get_type (void);
G_DEFINE_TYPE (TestClock, test_clock, GST_TYPE_CLOCK);

static GstClockReturn
fake_wait_async (GstClock * clock, GstClockEntry * entry)
{
  return GST_CLOCK_OK;
}

static void
test_clock_class_init (TestClockClass * klass)
{
  GstClockClass *clock_class;

  clock_class = GST_CLOCK_CLASS (klass);

  clock_class->wait_async = fake_wait_async;
}

static void
test_clock_init (TestClock * clock)
{
}


static void
weak_notify (gpointer data, GObject * object)
{
  *(gboolean *) data = FALSE;
}

GST_START_TEST (test_set_master_refcount)
{
  GstClock *master, *slave;
  gboolean master_alive = TRUE;

  /* create master and slave */
  master = g_object_new (TYPE_TEST_CLOCK, "name", "TestClockMaster", NULL);
  slave = g_object_new (TYPE_TEST_CLOCK, "name", "TestClockSlave", NULL);
  GST_OBJECT_FLAG_SET (slave, GST_CLOCK_FLAG_CAN_SET_MASTER);

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
