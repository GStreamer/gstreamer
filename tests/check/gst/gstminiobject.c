/* GStreamer
 *
 * unit test for GstMiniObject
 *
 * Copyright (C) <2005> Thomas Vander Stichele <thomas at apestaart dot org>
 * Copyright (C) <2005> Tim-Philipp Müller <tim centricular net>
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

GST_START_TEST (test_copy)
{
  GstBuffer *buffer, *copy;

  buffer = gst_buffer_new_and_alloc (4);

  copy = GST_BUFFER (gst_mini_object_copy (GST_MINI_OBJECT (buffer)));

  fail_if (copy == NULL, "Copy of buffer returned NULL");
  fail_unless (GST_BUFFER_SIZE (copy) == 4,
      "Copy of buffer has different size");
}

GST_END_TEST;

GST_START_TEST (test_is_writable)
{
  GstBuffer *buffer;
  GstMiniObject *mobj;

  buffer = gst_buffer_new_and_alloc (4);
  mobj = GST_MINI_OBJECT (buffer);

  fail_unless (gst_mini_object_is_writable (mobj),
      "A buffer with one ref should be writable");

  GST_MINI_OBJECT_FLAG_SET (mobj, GST_MINI_OBJECT_FLAG_READONLY);
  fail_if (gst_mini_object_is_writable (mobj),
      "A buffer with READONLY set should not be writable");
  GST_MINI_OBJECT_FLAG_UNSET (mobj, GST_MINI_OBJECT_FLAG_READONLY);
  fail_unless (gst_mini_object_is_writable (mobj),
      "A buffer with one ref and READONLY not set should be writable");

  fail_if (gst_mini_object_ref (mobj) == NULL, "Could not ref the mobj");

  fail_if (gst_mini_object_is_writable (mobj),
      "A buffer with two refs should not be writable");
}

GST_END_TEST;

GST_START_TEST (test_make_writable)
{
  GstBuffer *buffer;
  GstMiniObject *mobj, *mobj2, *mobj3;

  buffer = gst_buffer_new_and_alloc (4);
  mobj = GST_MINI_OBJECT (buffer);

  mobj2 = gst_mini_object_make_writable (mobj);
  fail_unless (GST_IS_BUFFER (mobj2), "make_writable did not return a buffer");
  fail_unless (mobj == mobj2,
      "make_writable returned a copy for a buffer with refcount 1");

  mobj2 = gst_mini_object_ref (mobj);
  mobj3 = gst_mini_object_make_writable (mobj);
  fail_unless (GST_IS_BUFFER (mobj3), "make_writable did not return a buffer");
  fail_if (mobj == mobj3,
      "make_writable returned same object for a buffer with refcount > 1");

  fail_unless (GST_MINI_OBJECT_REFCOUNT_VALUE (mobj) == 1,
      "refcount of original mobj object should be back to 1");

  mobj2 = gst_mini_object_make_writable (mobj);
  fail_unless (GST_IS_BUFFER (mobj2), "make_writable did not return a buffer");
  fail_unless (mobj == mobj2,
      "make_writable returned a copy for a buffer with refcount 1");

}

GST_END_TEST;

static gint num_threads = 10;
static gint refs_per_thread = 10000;

/* test thread-safe refcounting of GstMiniObject */
static void
thread_ref (GstMiniObject * mobj)
{
  int j;

  THREAD_START ();

  for (j = 0; j < refs_per_thread; ++j) {
    gst_mini_object_ref (mobj);

    if (j % num_threads == 0)
      THREAD_SWITCH ();
  }
  GST_DEBUG ("thread stopped");
}

GST_START_TEST (test_ref_threaded)
{
  GstBuffer *buffer;
  GstMiniObject *mobj;
  gint expected;

  buffer = gst_buffer_new_and_alloc (4);

  mobj = GST_MINI_OBJECT (buffer);

  MAIN_START_THREADS (num_threads, thread_ref, mobj);

  MAIN_STOP_THREADS ();

  expected = num_threads * refs_per_thread + 1;
  ASSERT_MINI_OBJECT_REFCOUNT (mobj, "miniobject", expected);
}

GST_END_TEST;

static void
thread_unref (GstMiniObject * mobj)
{
  int j;

  THREAD_START ();

  for (j = 0; j < refs_per_thread; ++j) {
    gst_mini_object_unref (mobj);

    if (j % num_threads == 0)
      THREAD_SWITCH ();
  }
}

GST_START_TEST (test_unref_threaded)
{
  GstBuffer *buffer;
  GstMiniObject *mobj;
  int i;

  buffer = gst_buffer_new_and_alloc (4);

  mobj = GST_MINI_OBJECT (buffer);

  for (i = 0; i < num_threads * refs_per_thread; ++i)
    gst_mini_object_ref (mobj);

  MAIN_START_THREADS (num_threads, thread_unref, mobj);

  MAIN_STOP_THREADS ();

  ASSERT_MINI_OBJECT_REFCOUNT (mobj, "miniobject", 1);

  /* final unref */
  gst_mini_object_unref (mobj);
}

GST_END_TEST;

/* ======== value collection test ======== */
typedef struct _MyFoo
{
  GObject object;
} MyFoo;

typedef struct _MyFooClass
{
  GObjectClass gobject_class;
} MyFooClass;

enum
{
  PROP_BUFFER = 1
};

G_DEFINE_TYPE (MyFoo, my_foo, G_TYPE_OBJECT);

static void
my_foo_init (MyFoo * foo)
{
}

static void
my_foo_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstBuffer *new_buf;

  g_assert (prop_id == PROP_BUFFER);

  new_buf = gst_buffer_new_and_alloc (1024);
  gst_value_set_mini_object (value, GST_MINI_OBJECT (new_buf));
  gst_buffer_unref (new_buf);
}

static void
my_foo_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstMiniObject *mini_obj;

  g_assert (prop_id == PROP_BUFFER);

  mini_obj = gst_value_get_mini_object (value);
  g_assert (GST_IS_MINI_OBJECT (mini_obj));
  g_assert (GST_IS_BUFFER (mini_obj));

#if 0
  /* gst_value_dup_mini_object() does not exist yet */
  mini_obj = gst_value_dup_mini_object (value);
  g_assert (GST_IS_MINI_OBJECT (mini_obj));
  g_assert (GST_IS_BUFFER (mini_obj));
  gst_mini_object_unref (mini_obj);
#endif
}


static void
my_foo_class_init (MyFooClass * klass)
{
  GObjectClass *gobject_klass = G_OBJECT_CLASS (klass);

  gobject_klass->get_property = my_foo_get_property;
  gobject_klass->set_property = my_foo_set_property;

  g_object_class_install_property (gobject_klass, PROP_BUFFER,
      gst_param_spec_mini_object ("buffer", "Buffer",
          "a newly created GstBuffer", GST_TYPE_BUFFER, G_PARAM_READWRITE));
}

GST_START_TEST (test_value_collection)
{
  GstBuffer *buf = NULL;
  MyFoo *foo;

  foo = (MyFoo *) g_object_new (my_foo_get_type (), NULL);

  /* test g_object_get() refcounting */
  g_object_get (foo, "buffer", &buf, NULL);
  g_assert (GST_IS_BUFFER (buf));
  g_assert (GST_MINI_OBJECT_REFCOUNT_VALUE (GST_MINI_OBJECT_CAST (buf)) == 1);
  gst_buffer_unref (buf);

  /* test g_object_set() refcounting */
  buf = gst_buffer_new_and_alloc (1024);
  g_object_set (foo, "buffer", buf, NULL);
  g_assert (GST_MINI_OBJECT_REFCOUNT_VALUE (GST_MINI_OBJECT_CAST (buf)) == 1);
  gst_buffer_unref (buf);

  g_object_unref (foo);
}

GST_END_TEST;


static Suite *
gst_mini_object_suite (void)
{
  Suite *s = suite_create ("GstMiniObject");
  TCase *tc_chain = tcase_create ("general");

  /* turn off timeout */
  tcase_set_timeout (tc_chain, 60);

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_copy);
  tcase_add_test (tc_chain, test_is_writable);
  tcase_add_test (tc_chain, test_make_writable);
  tcase_add_test (tc_chain, test_ref_threaded);
  tcase_add_test (tc_chain, test_unref_threaded);
  tcase_add_test (tc_chain, test_value_collection);
  return s;
}

GST_CHECK_MAIN (gst_mini_object);
