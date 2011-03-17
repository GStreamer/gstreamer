/* GStreamer
 *
 * unit test for GstBufferList
 *
 * Copyright (C) 2009 Axis Communications <dev-gstreamer at axis dot com>
 * @author Jonas Holmberg <jonas dot holmberg at axis dot com>
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
#include <gst/gstbufferlist.h>
#include <string.h>

#define TIMESTAMP 42

static GstBufferList *list;
static GstCaps *caps;

static void
setup (void)
{
  list = gst_buffer_list_new ();
  caps = gst_caps_new_simple ("text/plain", NULL);
}

static void
cleanup (void)
{
  gst_caps_unref (caps);
  gst_buffer_list_unref (list);
}

static GstBuffer *
buffer_from_string (const gchar * str)
{
  guint size;
  GstBuffer *buf;

  size = strlen (str);
  buf = gst_buffer_new_and_alloc (size);
  gst_buffer_set_caps (buf, caps);
  GST_BUFFER_TIMESTAMP (buf) = TIMESTAMP;
  memcpy (GST_BUFFER_DATA (buf), str, size);
  GST_BUFFER_SIZE (buf) = size;

  return buf;
}

GST_START_TEST (test_add_and_iterate)
{
  GstBufferListIterator *it;
  GstBuffer *buf1;
  GstBuffer *buf2;
  GstBuffer *buf3;
  GstBuffer *buf4;
  GstBuffer *buf;

  /* buffer list is initially empty */
  fail_unless (gst_buffer_list_n_groups (list) == 0);

  it = gst_buffer_list_iterate (list);

  ASSERT_CRITICAL (gst_buffer_list_iterator_add (it, NULL));
  ASSERT_CRITICAL (gst_buffer_list_iterator_add (NULL, NULL));

  /* cannot add buffer without adding a group first */
  buf1 = gst_buffer_new ();
  ASSERT_CRITICAL (gst_buffer_list_iterator_add (it, buf1));

  /* add a group of 2 buffers */
  fail_unless (gst_buffer_list_iterator_n_buffers (it) == 0);
  gst_buffer_list_iterator_add_group (it);
  fail_unless (gst_buffer_list_n_groups (list) == 1);
  ASSERT_CRITICAL (gst_buffer_list_iterator_add (it, NULL));
  ASSERT_BUFFER_REFCOUNT (buf1, "buf1", 1);
  gst_buffer_list_iterator_add (it, buf1);
  ASSERT_BUFFER_REFCOUNT (buf1, "buf1", 1);     /* list takes ownership */
  fail_unless (gst_buffer_list_n_groups (list) == 1);
  fail_unless (gst_buffer_list_iterator_n_buffers (it) == 0);
  buf2 = gst_buffer_new ();
  gst_buffer_list_iterator_add (it, buf2);
  ASSERT_BUFFER_REFCOUNT (buf2, "buf2", 1);
  fail_unless (gst_buffer_list_n_groups (list) == 1);
  fail_unless (gst_buffer_list_iterator_n_buffers (it) == 0);

  /* add another group of 2 buffers */
  gst_buffer_list_iterator_add_group (it);
  fail_unless (gst_buffer_list_n_groups (list) == 2);
  buf3 = gst_buffer_new ();
  gst_buffer_list_iterator_add (it, buf3);
  ASSERT_BUFFER_REFCOUNT (buf3, "buf3", 1);
  fail_unless (gst_buffer_list_n_groups (list) == 2);
  fail_unless (gst_buffer_list_iterator_n_buffers (it) == 0);
  buf4 = gst_buffer_new ();
  gst_buffer_list_iterator_add (it, buf4);
  ASSERT_BUFFER_REFCOUNT (buf4, "buf4", 1);
  fail_unless (gst_buffer_list_n_groups (list) == 2);
  fail_unless (gst_buffer_list_iterator_n_buffers (it) == 0);

  /* freeing iterator does not affect list */
  gst_buffer_list_iterator_free (it);
  fail_unless (gst_buffer_list_n_groups (list) == 2);

  /* create a new iterator */
  it = gst_buffer_list_iterate (list);

  /* iterate list */
  fail_unless (gst_buffer_list_iterator_next (it) == NULL);
  fail_unless (gst_buffer_list_iterator_next_group (it));
  fail_unless (gst_buffer_list_iterator_n_buffers (it) == 2);
  buf = gst_buffer_list_iterator_next (it);
  fail_unless (buf == buf1);
  fail_unless (gst_buffer_list_iterator_n_buffers (it) == 1);
  buf = gst_buffer_list_iterator_next (it);
  fail_unless (buf == buf2);
  fail_unless (gst_buffer_list_iterator_n_buffers (it) == 0);
  fail_unless (gst_buffer_list_iterator_next (it) == NULL);
  fail_unless (gst_buffer_list_iterator_next_group (it));
  fail_unless (gst_buffer_list_iterator_n_buffers (it) == 2);
  buf = gst_buffer_list_iterator_next (it);
  fail_unless (buf == buf3);
  fail_unless (gst_buffer_list_iterator_n_buffers (it) == 1);
  buf = gst_buffer_list_iterator_next (it);
  fail_unless (buf == buf4);
  fail_unless (gst_buffer_list_iterator_n_buffers (it) == 0);
  fail_unless (gst_buffer_list_iterator_next (it) == NULL);
  fail_if (gst_buffer_list_iterator_next_group (it));

  gst_buffer_list_iterator_free (it);
}

GST_END_TEST;

GST_START_TEST (test_make_writable)
{
  GstBufferListIterator *it;
  GstBufferList *wlist;
  GstBuffer *buf1;
  GstBuffer *buf2;
  GstBuffer *buf3;
  GstBuffer *buf;

  /* add buffers to list */
  it = gst_buffer_list_iterate (list);
  gst_buffer_list_iterator_add_group (it);
  buf1 = gst_buffer_new_and_alloc (1);
  gst_buffer_list_iterator_add (it, buf1);
  gst_buffer_list_iterator_add_group (it);
  buf2 = gst_buffer_new_and_alloc (2);
  gst_buffer_list_iterator_add (it, buf2);
  buf3 = gst_buffer_new_and_alloc (3);
  gst_buffer_list_iterator_add (it, buf3);
  gst_buffer_list_iterator_free (it);

  /* making it writable with refcount 1 returns the same list */
  wlist = gst_buffer_list_make_writable (list);
  fail_unless (wlist == list);
  it = gst_buffer_list_iterate (list);
  fail_unless (gst_buffer_list_iterator_next_group (it));
  buf = gst_buffer_list_iterator_next (it);
  fail_unless (buf == buf1);
  ASSERT_BUFFER_REFCOUNT (buf1, "buf1", 1);
  fail_unless (gst_buffer_list_iterator_next (it) == NULL);
  fail_unless (gst_buffer_list_iterator_next_group (it));
  buf = gst_buffer_list_iterator_next (it);
  fail_unless (buf == buf2);
  ASSERT_BUFFER_REFCOUNT (buf2, "buf2", 1);
  buf = gst_buffer_list_iterator_next (it);
  fail_unless (buf == buf3);
  ASSERT_BUFFER_REFCOUNT (buf3, "buf3", 1);
  fail_unless (gst_buffer_list_iterator_next (it) == NULL);
  fail_if (gst_buffer_list_iterator_next_group (it));
  gst_buffer_list_iterator_free (it);

  /* making it writable with refcount 2 returns a copy of the list with
   * increased refcount on the buffers in the list */
  gst_buffer_list_ref (list);
  fail_unless (GST_MINI_OBJECT_REFCOUNT_VALUE (list) == 2);
  wlist = gst_buffer_list_make_writable (list);
  fail_unless (GST_MINI_OBJECT_REFCOUNT_VALUE (list) == 1);
  fail_unless (GST_MINI_OBJECT_REFCOUNT_VALUE (wlist) == 1);
  fail_unless (wlist != list);
  it = gst_buffer_list_iterate (wlist);
  fail_unless (gst_buffer_list_iterator_next_group (it));
  buf = gst_buffer_list_iterator_next (it);
  fail_unless (buf == buf1);
  ASSERT_BUFFER_REFCOUNT (buf1, "buf1", 2);
  fail_unless (gst_buffer_list_iterator_next (it) == NULL);
  fail_unless (gst_buffer_list_iterator_next_group (it));
  buf = gst_buffer_list_iterator_next (it);
  fail_unless (buf == buf2);
  ASSERT_BUFFER_REFCOUNT (buf2, "buf2", 2);
  buf = gst_buffer_list_iterator_next (it);
  fail_unless (buf == buf3);
  ASSERT_BUFFER_REFCOUNT (buf3, "buf3", 2);
  fail_unless (gst_buffer_list_iterator_next (it) == NULL);
  fail_if (gst_buffer_list_iterator_next_group (it));
  gst_buffer_list_iterator_free (it);
  gst_buffer_list_unref (wlist);
}

GST_END_TEST;

GST_START_TEST (test_copy)
{
  GstBufferListIterator *it;
  GstBufferList *list_copy;
  GstBuffer *buf1;
  GstBuffer *buf2;
  GstBuffer *buf3;
  GstBuffer *buf;

  /* add buffers to the list */
  it = gst_buffer_list_iterate (list);
  gst_buffer_list_iterator_add_group (it);
  buf1 = gst_buffer_new ();
  gst_buffer_list_iterator_add (it, buf1);
  gst_buffer_list_iterator_add_group (it);
  buf2 = gst_buffer_new ();
  gst_buffer_list_iterator_add (it, buf2);
  buf3 = gst_buffer_new ();
  gst_buffer_list_iterator_add (it, buf3);
  gst_buffer_list_iterator_free (it);

  /* make a copy */
  list_copy = gst_buffer_list_copy (list);
  fail_unless (GST_MINI_OBJECT_REFCOUNT_VALUE (list) == 1);
  fail_unless (GST_MINI_OBJECT_REFCOUNT_VALUE (list_copy) == 1);
  fail_unless (list_copy != list);
  it = gst_buffer_list_iterate (list_copy);
  fail_unless (gst_buffer_list_iterator_next_group (it));
  buf = gst_buffer_list_iterator_next (it);
  fail_unless (buf == buf1);
  ASSERT_BUFFER_REFCOUNT (buf1, "buf1", 2);
  fail_unless (gst_buffer_list_iterator_next (it) == NULL);
  fail_unless (gst_buffer_list_iterator_next_group (it));
  buf = gst_buffer_list_iterator_next (it);
  fail_unless (buf == buf2);
  ASSERT_BUFFER_REFCOUNT (buf2, "buf2", 2);
  buf = gst_buffer_list_iterator_next (it);
  fail_unless (buf == buf3);
  ASSERT_BUFFER_REFCOUNT (buf3, "buf3", 2);
  fail_unless (gst_buffer_list_iterator_next (it) == NULL);
  fail_if (gst_buffer_list_iterator_next_group (it));
  gst_buffer_list_iterator_free (it);
  gst_buffer_list_unref (list_copy);
}

GST_END_TEST;

GST_START_TEST (test_steal)
{
  GstBufferListIterator *it;
  GstBuffer *buf1;
  GstBuffer *buf2;
  GstBuffer *buf3;
  GstBuffer *buf;

  /* add buffers to the list */
  it = gst_buffer_list_iterate (list);
  gst_buffer_list_iterator_add_group (it);
  buf1 = gst_buffer_new ();
  gst_buffer_list_iterator_add (it, buf1);
  gst_buffer_list_iterator_add_group (it);
  buf2 = gst_buffer_new ();
  gst_buffer_list_iterator_add (it, buf2);
  buf3 = gst_buffer_new ();
  gst_buffer_list_iterator_add (it, buf3);
  gst_buffer_list_iterator_free (it);

  /* check some error handling */
  ASSERT_CRITICAL ((buf = gst_buffer_list_iterator_steal (NULL)));
  fail_unless (buf == NULL);
  it = gst_buffer_list_iterate (list);
  ASSERT_CRITICAL ((buf = gst_buffer_list_iterator_steal (it)));
  fail_unless (buf == NULL);

  /* steal the first buffer */
  ASSERT_CRITICAL ((buf = gst_buffer_list_iterator_steal (it)));
  fail_unless (gst_buffer_list_iterator_next_group (it));
  ASSERT_CRITICAL ((buf = gst_buffer_list_iterator_steal (it)));
  fail_unless (gst_buffer_list_iterator_next (it) == buf1);
  buf = gst_buffer_list_iterator_steal (it);
  fail_unless (buf == buf1);
  ASSERT_BUFFER_REFCOUNT (buf, "buf", 1);
  gst_buffer_unref (buf);
  ASSERT_CRITICAL ((buf = gst_buffer_list_iterator_steal (it)));
  fail_unless (buf == NULL);

  /* steal the second buffer */
  fail_unless (gst_buffer_list_iterator_next_group (it));
  ASSERT_CRITICAL ((buf = gst_buffer_list_iterator_steal (it)));
  fail_unless (gst_buffer_list_iterator_next (it) == buf2);
  buf = gst_buffer_list_iterator_steal (it);
  fail_unless (buf == buf2);
  ASSERT_BUFFER_REFCOUNT (buf, "buf", 1);
  gst_buffer_unref (buf);
  ASSERT_CRITICAL ((buf = gst_buffer_list_iterator_steal (it)));

  /* steal the third buffer */
  fail_unless (gst_buffer_list_iterator_next (it) == buf3);
  buf = gst_buffer_list_iterator_steal (it);
  fail_unless (buf == buf3);
  ASSERT_BUFFER_REFCOUNT (buf, "buf", 1);
  gst_buffer_unref (buf);
  ASSERT_CRITICAL ((buf = gst_buffer_list_iterator_steal (it)));

  gst_buffer_list_iterator_free (it);

  /* iterate again when all buffers have been stolen */
  it = gst_buffer_list_iterate (list);
  fail_unless (gst_buffer_list_iterator_next (it) == NULL);
  fail_unless (gst_buffer_list_iterator_next_group (it));
  fail_unless (gst_buffer_list_iterator_next (it) == NULL);
  fail_unless (gst_buffer_list_iterator_next_group (it));
  fail_unless (gst_buffer_list_iterator_next (it) == NULL);
  fail_if (gst_buffer_list_iterator_next_group (it));
  gst_buffer_list_iterator_free (it);
}

GST_END_TEST;

GST_START_TEST (test_take)
{
  GstBufferListIterator *it;
  GstBuffer *buf1;
  GstBuffer *buf2;
  GstBuffer *buf3;
  GstBuffer *buf;

  /* add buffers to the list */
  it = gst_buffer_list_iterate (list);
  gst_buffer_list_iterator_add_group (it);
  buf1 = gst_buffer_new ();
  gst_buffer_ref (buf1);
  gst_buffer_list_iterator_add (it, buf1);
  gst_buffer_list_iterator_add_group (it);
  buf2 = gst_buffer_new ();
  gst_buffer_ref (buf2);
  gst_buffer_list_iterator_add (it, buf2);
  buf3 = gst_buffer_new ();
  gst_buffer_ref (buf3);
  gst_buffer_list_iterator_add (it, buf3);
  gst_buffer_list_iterator_free (it);

  /* check some error handling */
  ASSERT_CRITICAL (gst_buffer_list_iterator_take (NULL, NULL));
  it = gst_buffer_list_iterate (list);
  ASSERT_CRITICAL (gst_buffer_list_iterator_take (it, NULL));
  buf = gst_buffer_new ();
  gst_buffer_ref (buf);
  ASSERT_CRITICAL (gst_buffer_list_iterator_take (NULL, buf));
  ASSERT_BUFFER_REFCOUNT (buf, "buf", 2);

  /* replace the first buffer */
  ASSERT_CRITICAL (gst_buffer_list_iterator_take (it, buf));
  ASSERT_BUFFER_REFCOUNT (buf, "buf", 2);
  fail_unless (gst_buffer_list_iterator_next_group (it));
  ASSERT_CRITICAL (gst_buffer_list_iterator_take (it, buf));
  ASSERT_BUFFER_REFCOUNT (buf, "buf", 2);
  fail_unless (gst_buffer_list_iterator_next (it) == buf1);
  ASSERT_CRITICAL (gst_buffer_list_iterator_take (it, NULL));
  ASSERT_BUFFER_REFCOUNT (buf1, "buf1", 2);
  gst_buffer_list_iterator_take (it, buf);
  ASSERT_BUFFER_REFCOUNT (buf, "buf", 2);
  ASSERT_BUFFER_REFCOUNT (buf1, "buf1", 1);
  gst_buffer_unref (buf1);

  /* replace the first buffer again, with itself */
  gst_buffer_ref (buf);
  gst_buffer_list_iterator_take (it, buf);
  ASSERT_BUFFER_REFCOUNT (buf, "buf", 2);

  /* replace the second buffer */
  gst_buffer_ref (buf);
  fail_unless (gst_buffer_list_iterator_next (it) == NULL);
  ASSERT_CRITICAL (gst_buffer_list_iterator_take (it, buf));
  ASSERT_BUFFER_REFCOUNT (buf, "buf", 3);
  fail_unless (gst_buffer_list_iterator_next_group (it));
  ASSERT_BUFFER_REFCOUNT (buf2, "buf2", 2);
  ASSERT_CRITICAL (gst_buffer_list_iterator_take (it, buf));
  ASSERT_BUFFER_REFCOUNT (buf, "buf", 3);
  ASSERT_BUFFER_REFCOUNT (buf2, "buf2", 2);
  fail_unless (gst_buffer_list_iterator_next (it) == buf2);
  ASSERT_CRITICAL (gst_buffer_list_iterator_take (it, NULL));
  ASSERT_BUFFER_REFCOUNT (buf2, "buf2", 2);
  gst_buffer_list_iterator_take (it, buf);
  ASSERT_BUFFER_REFCOUNT (buf, "buf", 3);
  ASSERT_BUFFER_REFCOUNT (buf2, "buf2", 1);
  gst_buffer_unref (buf2);

  /* replace the third buffer */
  gst_buffer_ref (buf);
  fail_unless (gst_buffer_list_iterator_next (it) == buf3);
  ASSERT_BUFFER_REFCOUNT (buf3, "buf3", 2);
  gst_buffer_list_iterator_take (it, buf);
  ASSERT_BUFFER_REFCOUNT (buf, "buf", 4);
  ASSERT_BUFFER_REFCOUNT (buf3, "buf3", 1);
  gst_buffer_unref (buf3);
  fail_if (gst_buffer_list_iterator_next_group (it));
  ASSERT_CRITICAL (gst_buffer_list_iterator_take (it, buf));
  ASSERT_BUFFER_REFCOUNT (buf, "buf", 4);
  gst_buffer_unref (buf);

  gst_buffer_list_iterator_free (it);
}

GST_END_TEST;

static gpointer do_data_func_data;
static gboolean notified;

static GstBuffer *
do_data_func (GstBuffer * buffer, gpointer data)
{
  do_data_func_data = data;
  fail_if (notified);

  return buffer;
}

static GstBuffer *
do_func_null (GstBuffer * buffer)
{
  gst_buffer_unref (buffer);

  return NULL;
}

GST_START_TEST (test_do)
{
  GstBufferListIterator *it;
  GstBuffer *buf1;
  GstBuffer *buf;
  gchar *data;

  /* error handling */
  ASSERT_CRITICAL ((buf = gst_buffer_list_iterator_do (NULL, NULL, NULL)));
  fail_unless (buf == NULL);
  fail_unless (buf == NULL);
  it = gst_buffer_list_iterate (list);
  ASSERT_CRITICAL ((buf = gst_buffer_list_iterator_do (it, NULL, NULL)));
  fail_unless (buf == NULL);
  fail_unless (buf == NULL);

  /* add buffers to the list */
  gst_buffer_list_iterator_add_group (it);
  buf1 = gst_buffer_new ();
  gst_buffer_ref (buf1);
  gst_buffer_list_iterator_add (it, buf1);
  gst_buffer_list_iterator_add_group (it);
  gst_buffer_list_iterator_free (it);

  /* call do-function */
  it = gst_buffer_list_iterate (list);
  fail_unless (gst_buffer_list_iterator_next_group (it));
  ASSERT_CRITICAL ((buf =
          gst_buffer_list_iterator_do (it,
              (GstBufferListDoFunction) gst_buffer_ref, NULL)));
  fail_unless (buf == NULL);
  data = (char *) "data";
  ASSERT_CRITICAL ((buf = gst_buffer_list_iterator_do (it, do_data_func,
              data)));
  fail_unless (buf == NULL);
  fail_unless (do_data_func_data != data);
  buf = gst_buffer_list_iterator_next (it);
  fail_unless (buf == buf1);
  ASSERT_BUFFER_REFCOUNT (buf1, "buf1", 2);
  buf =
      gst_buffer_list_iterator_do (it, (GstBufferListDoFunction) gst_buffer_ref,
      NULL);
  fail_unless (buf == buf1);
  ASSERT_BUFFER_REFCOUNT (buf1, "buf1", 3);
  gst_buffer_unref (buf);
  buf = gst_buffer_list_iterator_do (it, do_data_func, data);
  fail_unless (buf == buf1);
  fail_unless (do_data_func_data == data);

  /* do-function that return a new buffer replaces the buffer in the list */
  ASSERT_BUFFER_REFCOUNT (buf1, "buf1", 2);
  buf = gst_buffer_list_iterator_do (it,
      (GstBufferListDoFunction) gst_mini_object_make_writable, NULL);
  fail_unless (buf != buf1);
  ASSERT_BUFFER_REFCOUNT (buf, "buf", 1);
  ASSERT_BUFFER_REFCOUNT (buf, "buf1", 1);
  gst_buffer_replace (&buf1, buf);

  /* do-function that return NULL removes the buffer from the list */
  ASSERT_BUFFER_REFCOUNT (buf1, "buf1", 2);
  fail_unless (gst_buffer_list_iterator_do (it,
          (GstBufferListDoFunction) do_func_null, NULL) == NULL);
  ASSERT_BUFFER_REFCOUNT (buf1, "buf1", 1);
  ASSERT_CRITICAL ((buf =
          gst_buffer_list_iterator_do (it,
              (GstBufferListDoFunction) gst_buffer_ref, NULL)));
  fail_unless (buf == NULL);
  fail_unless (gst_buffer_list_iterator_next (it) == NULL);
  gst_buffer_list_iterator_free (it);
  it = gst_buffer_list_iterate (list);
  fail_unless (gst_buffer_list_iterator_next_group (it));
  fail_unless (gst_buffer_list_iterator_next (it) == NULL);
  fail_if (gst_buffer_list_iterator_next_group (it));
  gst_buffer_list_iterator_free (it);
  gst_buffer_unref (buf1);
}

GST_END_TEST;

GST_START_TEST (test_merge)
{
  GstBufferListIterator *it;
  GstBufferListIterator *merge_it;
  GstBuffer *merged_buf;
  GstBuffer *buf;

  it = gst_buffer_list_iterate (list);
  fail_unless (gst_buffer_list_iterator_merge_group (it) == NULL);

  /* create a new group and add a buffer */
  gst_buffer_list_iterator_add_group (it);
  fail_unless (gst_buffer_list_iterator_merge_group (it) == NULL);
  buf = buffer_from_string ("One");
  gst_buffer_ref (buf);
  gst_buffer_list_iterator_add (it, buf);

  /* merging a group with one buffer returns a copy of the buffer */
  merge_it = gst_buffer_list_iterate (list);
  fail_unless (gst_buffer_list_iterator_next_group (merge_it));
  merged_buf = gst_buffer_list_iterator_merge_group (merge_it);
  fail_unless (merged_buf != buf);
  ASSERT_BUFFER_REFCOUNT (merged_buf, "merged_buf", 1);
  gst_buffer_unref (buf);
  fail_unless (GST_BUFFER_CAPS (merged_buf) == caps);
  fail_unless (GST_BUFFER_TIMESTAMP (merged_buf) == TIMESTAMP);
  fail_unless (GST_BUFFER_SIZE (merged_buf) == 3);
  fail_unless (memcmp (GST_BUFFER_DATA (merged_buf), "One",
          GST_BUFFER_SIZE (merged_buf)) == 0);
  gst_buffer_unref (merged_buf);

  /* add another buffer to the same group */
  gst_buffer_list_iterator_add (it, buffer_from_string ("Group"));

  /* merging a group returns a new buffer with merged data */
  merged_buf = gst_buffer_list_iterator_merge_group (merge_it);
  ASSERT_BUFFER_REFCOUNT (merged_buf, "merged_buf", 1);
  fail_unless (GST_BUFFER_CAPS (merged_buf) == caps);
  fail_unless (GST_BUFFER_TIMESTAMP (merged_buf) == TIMESTAMP);
  fail_unless (GST_BUFFER_SIZE (merged_buf) == 8);
  fail_unless (memcmp (GST_BUFFER_DATA (merged_buf), "OneGroup",
          GST_BUFFER_SIZE (merged_buf)) == 0);

  /* merging the same group again should return a new buffer with merged data */
  buf = gst_buffer_list_iterator_merge_group (merge_it);
  ASSERT_BUFFER_REFCOUNT (buf, "buf", 1);
  fail_unless (buf != merged_buf);
  fail_unless (GST_BUFFER_SIZE (buf) == 8);
  fail_unless (memcmp (GST_BUFFER_DATA (buf), "OneGroup",
          GST_BUFFER_SIZE (buf)) == 0);
  gst_buffer_unref (buf);
  gst_buffer_unref (merged_buf);

  /* add a new group */
  gst_buffer_list_iterator_add_group (it);
  gst_buffer_list_iterator_add (it, buffer_from_string ("AnotherGroup"));
  gst_buffer_list_iterator_free (it);

  /* merge the first group again */
  merged_buf = gst_buffer_list_iterator_merge_group (merge_it);
  ASSERT_BUFFER_REFCOUNT (merged_buf, "merged_buf", 1);
  fail_unless (GST_BUFFER_CAPS (merged_buf) == caps);
  fail_unless (GST_BUFFER_TIMESTAMP (merged_buf) == TIMESTAMP);
  fail_unless (GST_BUFFER_SIZE (merged_buf) == 8);
  fail_unless (memcmp (GST_BUFFER_DATA (merged_buf), "OneGroup",
          GST_BUFFER_SIZE (merged_buf)) == 0);
  gst_buffer_unref (merged_buf);

  /* merge the second group */
  fail_unless (gst_buffer_list_iterator_next_group (merge_it));
  merged_buf = gst_buffer_list_iterator_merge_group (merge_it);
  ASSERT_BUFFER_REFCOUNT (merged_buf, "merged_buf", 1);
  fail_unless (GST_BUFFER_CAPS (merged_buf) == caps);
  fail_unless (GST_BUFFER_TIMESTAMP (merged_buf) == TIMESTAMP);
  fail_unless (GST_BUFFER_SIZE (merged_buf) == 12);
  fail_unless (memcmp (GST_BUFFER_DATA (merged_buf), "AnotherGroup",
          GST_BUFFER_SIZE (merged_buf)) == 0);
  gst_buffer_unref (merged_buf);

  gst_buffer_list_iterator_free (merge_it);

  /* steal the second buffer and merge the first group again */
  it = gst_buffer_list_iterate (list);
  fail_unless (gst_buffer_list_iterator_next_group (it));
  fail_unless (gst_buffer_list_iterator_next (it) != NULL);
  fail_unless (gst_buffer_list_iterator_next (it) != NULL);
  buf = gst_buffer_list_iterator_steal (it);
  gst_buffer_list_iterator_free (it);
  fail_unless (buf != NULL);
  fail_unless (memcmp (GST_BUFFER_DATA (buf), "Group",
          GST_BUFFER_SIZE (buf)) == 0);
  gst_buffer_unref (buf);
  merge_it = gst_buffer_list_iterate (list);
  fail_unless (gst_buffer_list_iterator_next_group (merge_it));
  merged_buf = gst_buffer_list_iterator_merge_group (merge_it);
  ASSERT_BUFFER_REFCOUNT (merged_buf, "merged_buf", 1);
  fail_unless (GST_BUFFER_CAPS (merged_buf) == caps);
  fail_unless (GST_BUFFER_TIMESTAMP (merged_buf) == TIMESTAMP);
  fail_unless (GST_BUFFER_SIZE (merged_buf) == 3);
  fail_unless (memcmp (GST_BUFFER_DATA (merged_buf), "One",
          GST_BUFFER_SIZE (merged_buf)) == 0);
  gst_buffer_unref (merged_buf);

  /* steal the first buffer too and merge the first group again */
  it = gst_buffer_list_iterate (list);
  fail_unless (gst_buffer_list_iterator_next_group (it));
  fail_unless (gst_buffer_list_iterator_next (it) != NULL);
  buf = gst_buffer_list_iterator_steal (it);
  fail_unless (buf != NULL);
  fail_unless (memcmp (GST_BUFFER_DATA (buf), "One",
          GST_BUFFER_SIZE (buf)) == 0);
  gst_buffer_unref (buf);
  gst_buffer_list_iterator_free (it);
  fail_unless (gst_buffer_list_iterator_merge_group (merge_it) == NULL);
  gst_buffer_list_iterator_free (merge_it);
}

GST_END_TEST;

typedef struct
{
  GstBuffer *buf[3][3];
  guint iter;
} ForeachData;

static GstBufferListItem
foreach_func1 (GstBuffer ** buffer, guint group, guint idx, ForeachData * data)
{
  fail_unless (buffer != NULL);
  fail_unless (*buffer == data->buf[group][idx]);

  data->iter++;

  return GST_BUFFER_LIST_CONTINUE;
}

static GstBufferListItem
foreach_func2 (GstBuffer ** buffer, guint group, guint idx, ForeachData * data)
{
  fail_unless (idx == 0);
  fail_unless (buffer != NULL);
  fail_unless (*buffer == data->buf[group][idx]);

  data->iter++;

  return GST_BUFFER_LIST_SKIP_GROUP;
}

static GstBufferListItem
foreach_func3 (GstBuffer ** buffer, guint group, guint idx, ForeachData * data)
{
  fail_unless (group == 0);
  fail_unless (idx == 0);
  fail_unless (buffer != NULL);
  fail_unless (*buffer == data->buf[group][idx]);

  data->iter++;

  return GST_BUFFER_LIST_END;
}

static GstBufferListItem
foreach_func4 (GstBuffer ** buffer, guint group, guint idx, ForeachData * data)
{
  fail_unless (idx == 0);
  fail_unless (buffer != NULL);
  fail_unless (*buffer == data->buf[group][idx]);

  gst_buffer_unref (*buffer);
  *buffer = NULL;
  data->iter++;

  return GST_BUFFER_LIST_SKIP_GROUP;
}

static GstBufferListItem
foreach_func5 (GstBuffer ** buffer, guint group, guint idx, ForeachData * data)
{
  fail_unless (buffer != NULL);

  data->iter++;

  return GST_BUFFER_LIST_CONTINUE;
}

GST_START_TEST (test_foreach)
{
  GstBufferListIterator *it;
  ForeachData data;

  /* add buffers to the list */
  it = gst_buffer_list_iterate (list);
  gst_buffer_list_iterator_add_group (it);
  data.buf[0][0] = gst_buffer_new ();
  gst_buffer_list_iterator_add (it, data.buf[0][0]);
  gst_buffer_list_iterator_add_group (it);
  data.buf[1][0] = gst_buffer_new ();
  gst_buffer_list_iterator_add (it, data.buf[1][0]);
  data.buf[1][1] = gst_buffer_new ();
  gst_buffer_list_iterator_add (it, data.buf[1][1]);
  gst_buffer_list_iterator_free (it);

  fail_unless (gst_buffer_list_get (list, 0, 0) == data.buf[0][0]);
  fail_unless (gst_buffer_list_get (list, 0, 1) == NULL);
  fail_unless (gst_buffer_list_get (list, 1, 0) == data.buf[1][0]);
  fail_unless (gst_buffer_list_get (list, 1, 1) == data.buf[1][1]);
  fail_unless (gst_buffer_list_get (list, 1, 2) == NULL);
  fail_unless (gst_buffer_list_get (list, 2, 0) == NULL);
  fail_unless (gst_buffer_list_get (list, 2, 1) == NULL);
  fail_unless (gst_buffer_list_get (list, 3, 3) == NULL);

  /* iterate everything */
  data.iter = 0;
  gst_buffer_list_foreach (list, (GstBufferListFunc) foreach_func1, &data);
  fail_unless (data.iter == 3);

  /* iterate only the first buffer of groups */
  data.iter = 0;
  gst_buffer_list_foreach (list, (GstBufferListFunc) foreach_func2, &data);
  fail_unless (data.iter == 2);

  /* iterate only the first buffer */
  data.iter = 0;
  gst_buffer_list_foreach (list, (GstBufferListFunc) foreach_func3, &data);
  fail_unless (data.iter == 1);

  /* remove the first buffer of each group */
  data.iter = 0;
  gst_buffer_list_foreach (list, (GstBufferListFunc) foreach_func4, &data);
  fail_unless (data.iter == 2);

  fail_unless (gst_buffer_list_get (list, 0, 0) == NULL);
  fail_unless (gst_buffer_list_get (list, 0, 1) == NULL);
  fail_unless (gst_buffer_list_get (list, 1, 0) == data.buf[1][1]);
  fail_unless (gst_buffer_list_get (list, 1, 1) == NULL);
  fail_unless (gst_buffer_list_get (list, 1, 2) == NULL);
  fail_unless (gst_buffer_list_get (list, 2, 0) == NULL);

  /* iterate everything, just one more buffer now */
  data.iter = 0;
  gst_buffer_list_foreach (list, (GstBufferListFunc) foreach_func5, &data);
  fail_unless (data.iter == 1);
}

GST_END_TEST;

GST_START_TEST (test_list)
{
  GstBufferListIterator *it;
  GList *l = NULL;
  gint i;

  for (i = 0; i < 10; i++) {
    gchar name[10];
    g_snprintf (name, 10, "%d", i);
    l = g_list_append (l, buffer_from_string (name));
  }

  /* add buffers to the list */
  it = gst_buffer_list_iterate (list);
  gst_buffer_list_iterator_add_group (it);
  gst_buffer_list_iterator_add_list (it, l);

  /* add a buffer */
  gst_buffer_list_iterator_add (it, buffer_from_string ("10"));

  /* add another list */
  l = g_list_append (NULL, buffer_from_string ("11"));
  gst_buffer_list_iterator_add_list (it, l);

  for (i = 0; i < 12; i++) {
    GstBuffer *buf;
    gchar name[10];

    buf = gst_buffer_list_get (list, 0, i);
    g_snprintf (name, 10, "%d", i);
    fail_unless (memcmp (name, (gchar *) GST_BUFFER_DATA (buf),
            GST_BUFFER_SIZE (buf)) == 0);
  }
  gst_buffer_list_iterator_free (it);
}

GST_END_TEST;

static Suite *
gst_buffer_list_suite (void)
{
  Suite *s = suite_create ("GstBufferList");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_checked_fixture (tc_chain, setup, cleanup);
  tcase_add_test (tc_chain, test_add_and_iterate);
  tcase_add_test (tc_chain, test_make_writable);
  tcase_add_test (tc_chain, test_copy);
  tcase_add_test (tc_chain, test_steal);
  tcase_add_test (tc_chain, test_take);
  tcase_add_test (tc_chain, test_do);
  tcase_add_test (tc_chain, test_merge);
  tcase_add_test (tc_chain, test_foreach);
  tcase_add_test (tc_chain, test_list);

  return s;
}

GST_CHECK_MAIN (gst_buffer_list);
