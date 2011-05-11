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

/* ======== weak ref test ======== */

static gboolean weak_ref_notify_succeeded = FALSE;

static void
on_weak_ref_notify (gpointer data, GstMiniObject * where_object_was)
{
  weak_ref_notify_succeeded = TRUE;
}

GST_START_TEST (test_weak_ref)
{
  GstBuffer *buffer;

  buffer = gst_buffer_new_and_alloc (4);

  gst_mini_object_weak_ref (GST_MINI_OBJECT (buffer), on_weak_ref_notify,
      &buffer);

  gst_buffer_unref (buffer);

  fail_unless (weak_ref_notify_succeeded,
      "No weak reference notification took place.");
}

GST_END_TEST;

/* ======== recycle test ======== */

static gint recycle_buffer_count = 10;

#define MY_TYPE_RECYCLE_BUFFER (my_recycle_buffer_get_type ())

#define MY_IS_RECYCLE_BUFFER(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
    MY_TYPE_RECYCLE_BUFFER))
#define MY_RECYCLE_BUFFER(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
    MY_TYPE_RECYCLE_BUFFER, MyRecycleBuffer))
#define MY_RECYCLE_BUFFER_CAST(obj) ((MyRecycleBuffer *) (obj))

typedef struct _MyBufferPool MyBufferPool;
typedef struct _MyRecycleBuffer MyRecycleBuffer;
typedef struct _MyRecycleBufferClass MyRecycleBufferClass;

struct _MyBufferPool
{
  GSList *buffers;

  volatile gboolean is_closed;
};

struct _MyRecycleBuffer
{
  GstBuffer buffer;

  MyBufferPool *pool;
};

struct _MyRecycleBufferClass
{
  GstBufferClass parent_class;
};

static void my_recycle_buffer_destroy (MyRecycleBuffer * buf);

static MyBufferPool *
my_buffer_pool_new (void)
{
  return g_new0 (MyBufferPool, 1);
}

static void
my_buffer_pool_free (MyBufferPool * self)
{
  while (self->buffers != NULL) {
    my_recycle_buffer_destroy (self->buffers->data);
    self->buffers = g_slist_delete_link (self->buffers, self->buffers);
  }

  g_free (self);
}

static void
my_buffer_pool_add (MyBufferPool * self, GstBuffer * buf)
{
  g_mutex_lock (mutex);
  self->buffers = g_slist_prepend (self->buffers, gst_buffer_ref (buf));
  g_mutex_unlock (mutex);
}

static GstBuffer *
my_buffer_pool_drain_one (MyBufferPool * self)
{
  GstBuffer *buf = NULL;

  g_mutex_lock (mutex);
  if (self->buffers != NULL) {
    buf = self->buffers->data;
    self->buffers = g_slist_delete_link (self->buffers, self->buffers);
  }
  g_mutex_unlock (mutex);

  return buf;
}

GType my_recycle_buffer_get_type (void);
G_DEFINE_TYPE (MyRecycleBuffer, my_recycle_buffer, GST_TYPE_BUFFER);

static void my_recycle_buffer_finalize (GstMiniObject * mini_object);

static void
my_recycle_buffer_class_init (MyRecycleBufferClass * klass)
{
  GstMiniObjectClass *miniobject_class = GST_MINI_OBJECT_CLASS (klass);

  miniobject_class->finalize = my_recycle_buffer_finalize;
}

static void
my_recycle_buffer_init (MyRecycleBuffer * self)
{
}

static void
my_recycle_buffer_finalize (GstMiniObject * mini_object)
{
  MyRecycleBuffer *self = MY_RECYCLE_BUFFER_CAST (mini_object);

  if (self->pool != NULL) {
    my_buffer_pool_add (self->pool, GST_BUFFER_CAST (self));
    g_usleep (G_USEC_PER_SEC / 100);
  } else {
    GST_MINI_OBJECT_CLASS (my_recycle_buffer_parent_class)->finalize
        (mini_object);
  }
}

static GstBuffer *
my_recycle_buffer_new (MyBufferPool * pool)
{
  MyRecycleBuffer *buf;

  buf = MY_RECYCLE_BUFFER (gst_mini_object_new (MY_TYPE_RECYCLE_BUFFER));
  buf->pool = pool;

  return GST_BUFFER_CAST (buf);
}

static void
my_recycle_buffer_destroy (MyRecycleBuffer * buf)
{
  buf->pool = NULL;
  gst_buffer_unref (GST_BUFFER_CAST (buf));
}

static void
thread_buffer_producer (MyBufferPool * pool)
{
  int j;

  THREAD_START ();

  for (j = 0; j < recycle_buffer_count; ++j) {
    GstBuffer *buf = my_recycle_buffer_new (pool);
    gst_buffer_unref (buf);
  }

  pool->is_closed = TRUE;
}

static void
thread_buffer_consumer (MyBufferPool * pool)
{
  THREAD_START ();

  do {
    GstBuffer *buf;

    buf = my_buffer_pool_drain_one (pool);
    if (buf != NULL)
      my_recycle_buffer_destroy (MY_RECYCLE_BUFFER_CAST (buf));

    THREAD_SWITCH ();
  }
  while (!pool->is_closed);
}

GST_START_TEST (test_recycle_threaded)
{
  MyBufferPool *pool;

  pool = my_buffer_pool_new ();

  MAIN_START_THREADS (1, thread_buffer_producer, pool);
  MAIN_START_THREADS (1, thread_buffer_consumer, pool);

  MAIN_STOP_THREADS ();

  my_buffer_pool_free (pool);
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

GType my_foo_get_type (void);
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


GST_START_TEST (test_dup_null_mini_object)
{
  GValue value = { 0, };
  GstMiniObject *mo;

  g_value_init (&value, GST_TYPE_BUFFER);

  gst_value_set_mini_object (&value, NULL);

  mo = gst_value_dup_mini_object (&value);
  g_assert (mo == NULL);

  g_value_unset (&value);
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
  tcase_add_test (tc_chain, test_weak_ref);
  tcase_add_test (tc_chain, test_recycle_threaded);
  tcase_add_test (tc_chain, test_value_collection);
  tcase_add_test (tc_chain, test_dup_null_mini_object);
  return s;
}

GST_CHECK_MAIN (gst_mini_object);
