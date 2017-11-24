/* GStreamer GstTocSetter interface unit tests
 * Copyright (C) 2010, 2012 Alexander Saprykin <xelfium@gmail.com>
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
#include <gst/gst.h>
#include <string.h>

#define ENTRY_ED1       "/edition1"
#define ENTRY_ED2       "/edition2"
#define ENTRY_ED3       "test-edition"

#define ENTRY_CH1       "/edition1/chapter1"
#define ENTRY_CH2       "/edition1/chapter2"
#define ENTRY_CH3       "/edition2/chapter3"
#define ENTRY_CH4       "/test-chapter"

#define ENTRY_SUB1      "/edition2/chapter3/subchapter1"

#define ENTRY_TAG       "EntryTag"
#define TOC_TAG         "TocTag"

static void
CHECK_TOC_ENTRY (GstTocEntry * entry_c, GstTocEntryType type_c,
    const gchar * uid_c)
{
  GstTagList *tags;
  gchar *tag_c;

  fail_unless_equals_string (gst_toc_entry_get_uid (entry_c), uid_c);
  fail_unless (gst_toc_entry_get_entry_type (entry_c) == type_c);

  tags = gst_toc_entry_get_tags (entry_c);
  fail_unless (tags != NULL);
  fail_unless (gst_tag_list_get_string (tags, GST_TAG_TITLE, &tag_c));
  fail_unless_equals_string (tag_c, ENTRY_TAG);
  g_free (tag_c);
}

static void
CHECK_TOC (GstToc * toc_t)
{
  GstTocEntry *entry_t, *subentry_t;
  GstTagList *tags;
  GList *entries, *subentries, *subsubentries;
  gchar *tag_t;

  /* dump TOC */
  gst_toc_dump (toc_t);

  /* check TOC */
  tags = gst_toc_get_tags (toc_t);
  fail_unless (tags != NULL);
  fail_unless (gst_tag_list_get_string (tags, GST_TAG_TITLE, &tag_t));
  fail_unless_equals_string (tag_t, TOC_TAG);
  g_free (tag_t);

  entries = gst_toc_get_entries (toc_t);
  fail_unless_equals_int (g_list_length (entries), 2);
  /* check edition1 */
  entry_t = g_list_nth_data (entries, 0);
  fail_if (entry_t == NULL);
  subentries = gst_toc_entry_get_sub_entries (entry_t);
  fail_unless_equals_int (g_list_length (subentries), 2);
  CHECK_TOC_ENTRY (entry_t, GST_TOC_ENTRY_TYPE_EDITION, ENTRY_ED1);
  /* check chapter1 */
  subentry_t = g_list_nth_data (subentries, 0);
  fail_if (subentry_t == NULL);
  subsubentries = gst_toc_entry_get_sub_entries (subentry_t);
  fail_unless_equals_int (g_list_length (subsubentries), 0);
  CHECK_TOC_ENTRY (subentry_t, GST_TOC_ENTRY_TYPE_CHAPTER, ENTRY_CH1);
  /* check chapter2 */
  subentry_t = g_list_nth_data (subentries, 1);
  fail_if (subentry_t == NULL);
  subsubentries = gst_toc_entry_get_sub_entries (subentry_t);
  fail_unless_equals_int (g_list_length (subsubentries), 0);
  CHECK_TOC_ENTRY (subentry_t, GST_TOC_ENTRY_TYPE_CHAPTER, ENTRY_CH2);
  /* check edition2 */
  entry_t = g_list_nth_data (entries, 1);
  fail_if (entry_t == NULL);
  subentries = gst_toc_entry_get_sub_entries (entry_t);
  fail_unless_equals_int (g_list_length (subentries), 1);
  CHECK_TOC_ENTRY (entry_t, GST_TOC_ENTRY_TYPE_EDITION, ENTRY_ED2);
  /* check chapter3 */
  subentry_t = g_list_nth_data (subentries, 0);
  fail_if (subentry_t == NULL);
  subsubentries = gst_toc_entry_get_sub_entries (subentry_t);
  fail_unless_equals_int (g_list_length (subsubentries), 1);
  CHECK_TOC_ENTRY (subentry_t, GST_TOC_ENTRY_TYPE_CHAPTER, ENTRY_CH3);
  /* check subchapter1 */
  subentry_t = g_list_nth_data (subsubentries, 0);
  fail_if (subentry_t == NULL);
  subsubentries = gst_toc_entry_get_sub_entries (subentry_t);
  fail_unless_equals_int (g_list_length (subsubentries), 0);
  CHECK_TOC_ENTRY (subentry_t, GST_TOC_ENTRY_TYPE_CHAPTER, ENTRY_SUB1);
}

/* some minimal GstTocSetter object */
#define GST_TYPE_DUMMY_ENC gst_dummy_enc_get_type()

typedef GstElement GstDummyEnc;
typedef GstElementClass GstDummyEncClass;

GType gst_dummy_enc_get_type (void);
G_DEFINE_TYPE_WITH_CODE (GstDummyEnc, gst_dummy_enc,
    GST_TYPE_ELEMENT, G_IMPLEMENT_INTERFACE (GST_TYPE_TOC_SETTER, NULL));

static void
gst_dummy_enc_class_init (GstDummyEncClass * klass)
{
}

static void
gst_dummy_enc_init (GstDummyEnc * enc)
{
}

static GstToc *
create_toc (void)
{
  GstToc *toc;
  GstTocEntry *ed, *ch, *subch;
  GstTagList *tags;

  toc = gst_toc_new (GST_TOC_SCOPE_GLOBAL);
  tags = gst_tag_list_new_empty ();
  gst_tag_list_add (tags, GST_TAG_MERGE_APPEND, GST_TAG_TITLE, TOC_TAG, NULL);
  gst_toc_set_tags (toc, tags);

  /* create edition1 */
  ed = gst_toc_entry_new (GST_TOC_ENTRY_TYPE_EDITION, ENTRY_ED1);
  tags = gst_tag_list_new_empty ();
  gst_tag_list_add (tags, GST_TAG_MERGE_APPEND, GST_TAG_TITLE, ENTRY_TAG, NULL);
  gst_toc_entry_set_tags (ed, tags);

  /* append chapter1 to edition1 */
  ch = gst_toc_entry_new (GST_TOC_ENTRY_TYPE_CHAPTER, ENTRY_CH1);
  tags = gst_tag_list_new_empty ();
  gst_tag_list_add (tags, GST_TAG_MERGE_APPEND, GST_TAG_TITLE, ENTRY_TAG, NULL);
  gst_toc_entry_set_tags (ch, tags);

  gst_toc_entry_append_sub_entry (ed, ch);

  /* append chapter2 to edition1 */
  ch = gst_toc_entry_new (GST_TOC_ENTRY_TYPE_CHAPTER, ENTRY_CH2);
  tags = gst_tag_list_new_empty ();
  gst_tag_list_add (tags, GST_TAG_MERGE_APPEND, GST_TAG_TITLE, ENTRY_TAG, NULL);
  gst_toc_entry_set_tags (ch, tags);

  gst_toc_entry_append_sub_entry (ed, ch);

  /* append edition1 to the TOC */
  gst_toc_append_entry (toc, ed);

  /* create edition2 */
  ed = gst_toc_entry_new (GST_TOC_ENTRY_TYPE_EDITION, ENTRY_ED2);
  tags = gst_tag_list_new_empty ();
  gst_tag_list_add (tags, GST_TAG_MERGE_APPEND, GST_TAG_TITLE, ENTRY_TAG, NULL);
  gst_toc_entry_set_tags (ed, tags);

  /* create chapter3 */
  ch = gst_toc_entry_new (GST_TOC_ENTRY_TYPE_CHAPTER, ENTRY_CH3);
  tags = gst_tag_list_new_empty ();
  gst_tag_list_add (tags, GST_TAG_MERGE_APPEND, GST_TAG_TITLE, ENTRY_TAG, NULL);
  gst_toc_entry_set_tags (ch, tags);

  /* create subchapter1 */
  subch = gst_toc_entry_new (GST_TOC_ENTRY_TYPE_CHAPTER, ENTRY_SUB1);
  tags = gst_tag_list_new_empty ();
  gst_tag_list_add (tags, GST_TAG_MERGE_APPEND, GST_TAG_TITLE, ENTRY_TAG, NULL);
  gst_toc_entry_set_tags (subch, tags);

  /* append subchapter1 to chapter3 */
  gst_toc_entry_append_sub_entry (ch, subch);

  /* append chapter3 to edition2 */
  gst_toc_entry_append_sub_entry (ed, ch);

  /* finally append edition2 to the TOC */
  gst_toc_append_entry (toc, ed);

  return toc;
}

GST_START_TEST (test_set)
{
  GstToc *toc;
#if 0
  GstTocEntry *entry, *ed;
#endif
  GstTocSetter *setter;
  GstElement *enc;

  enc = g_object_new (GST_TYPE_DUMMY_ENC, NULL);
  fail_unless (enc != NULL);

  setter = GST_TOC_SETTER (enc);

  toc = create_toc ();
  fail_unless (toc != NULL);

  gst_toc_setter_set_toc (setter, toc);

  gst_toc_unref (toc);
  toc = gst_toc_setter_get_toc (setter);

  CHECK_TOC (toc);

#if 0
  /* test entry adding into the root TOC */
  entry = g_list_last (toc->entries)->data;
  toc->entries = g_list_remove (toc->entries, entry);

  gst_toc_setter_set_toc (setter, toc);
  gst_toc_setter_add_toc_entry (setter, "0", entry);

  gst_toc_unref (toc);
  gst_toc_entry_unref (entry);
  toc = gst_toc_setter_get_toc (setter);

  CHECK_TOC (toc);
#endif

#if 0
  /* test entry adding into the arbitrary entry */
  entry = gst_toc_find_entry (toc, ENTRY_CH2);
  fail_if (entry == NULL);

  ed = toc->entries->data;
  ed->subentries = g_list_remove (ed->subentries, entry);

  gst_toc_setter_add_toc_entry (setter, ed->uid, entry);

  CHECK_TOC (toc);
#endif

  gst_toc_unref (toc);
  gst_toc_setter_reset (setter);
  toc = gst_toc_setter_get_toc (setter);

  fail_unless (toc == NULL);

  g_object_unref (enc);
}

GST_END_TEST static int spin_and_wait = 1;
static int threads_running = 0;

#define THREADS_TEST_SECONDS 1.5

static gpointer
test_threads_thread_func1 (gpointer data)
{
  GstToc *toc;
  GstTocSetter *setter = GST_TOC_SETTER (data);
  GTimer *timer;

  toc = create_toc ();
  timer = g_timer_new ();

  g_atomic_int_inc (&threads_running);
  while (g_atomic_int_get (&spin_and_wait))
    g_usleep (0);

  GST_INFO ("Go!");
  g_timer_start (timer);

  while (g_timer_elapsed (timer, NULL) < THREADS_TEST_SECONDS)
    gst_toc_setter_set_toc (setter, toc);

  gst_toc_unref (toc);
  g_timer_destroy (timer);
  GST_INFO ("Done");

  return NULL;
}

static gpointer
test_threads_thread_func2 (gpointer data)
{
  GstToc *toc;
  GstTocSetter *setter = GST_TOC_SETTER (data);
  GTimer *timer;

  toc = create_toc ();
  timer = g_timer_new ();

  g_atomic_int_inc (&threads_running);
  while (g_atomic_int_get (&spin_and_wait))
    g_usleep (0);

  GST_INFO ("Go!");
  g_timer_start (timer);

  while (g_timer_elapsed (timer, NULL) < THREADS_TEST_SECONDS)
    gst_toc_setter_set_toc (setter, toc);

  gst_toc_unref (toc);
  g_timer_destroy (timer);
  GST_INFO ("Done");

  return NULL;
}

static gpointer
test_threads_thread_func3 (gpointer data)
{
  GstTocSetter *setter = GST_TOC_SETTER (data);
  GTimer *timer;

  timer = g_timer_new ();

  g_atomic_int_inc (&threads_running);
  while (g_atomic_int_get (&spin_and_wait))
    g_usleep (0);

  GST_INFO ("Go!");
  g_timer_start (timer);

  while (g_timer_elapsed (timer, NULL) < THREADS_TEST_SECONDS) {
    gst_toc_setter_reset (setter);
  }

  g_timer_destroy (timer);
  GST_INFO ("Done");

  return NULL;
}

GST_START_TEST (test_threads)
{
  GstTocSetter *setter;
  GThread *threads[3];

  setter = GST_TOC_SETTER (g_object_new (GST_TYPE_DUMMY_ENC, NULL));

  spin_and_wait = TRUE;
  threads[0] = g_thread_try_new ("gst-check", test_threads_thread_func1,
      setter, NULL);
  threads[1] = g_thread_try_new ("gst-check", test_threads_thread_func2,
      setter, NULL);
  threads[2] = g_thread_try_new ("gst-check", test_threads_thread_func3,
      setter, NULL);

  while (g_atomic_int_get (&threads_running) < 3)
    g_usleep (10);

  g_atomic_int_set (&spin_and_wait, FALSE);

  g_thread_join (threads[0]);
  g_thread_join (threads[1]);
  g_thread_join (threads[2]);

  g_object_unref (G_OBJECT (setter));
}

GST_END_TEST static Suite *
gst_toc_setter_suite (void)
{
  Suite *s = suite_create ("GstTocSetter");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_set);
  tcase_add_test (tc_chain, test_threads);

  return s;
}

GST_CHECK_MAIN (gst_toc_setter);
