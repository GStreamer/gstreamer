/* GStreamer
 *
 * unit test for GstToc
 *
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

/*  -------  TOC  -------
 *           /  \
 *   edition1    edition2
 *   |           |
 *   -chapter1   -chapter3
 *   -chapter2    |
 *                -subchapter1
 */

#include <gst/check/gstcheck.h>

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

#define TEST_UID        "129537542"

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
  CHECK_TOC_ENTRY (entry_t, GST_TOC_ENTRY_TYPE_EDITION, ENTRY_ED2);
  subentries = gst_toc_entry_get_sub_entries (entry_t);
  fail_unless_equals_int (g_list_length (subentries), 1);
  /* check chapter3 */
  subentry_t = g_list_nth_data (subentries, 0);
  fail_if (subentry_t == NULL);
  CHECK_TOC_ENTRY (subentry_t, GST_TOC_ENTRY_TYPE_CHAPTER, ENTRY_CH3);
  subsubentries = gst_toc_entry_get_sub_entries (subentry_t);
  fail_unless_equals_int (g_list_length (subsubentries), 1);
  /* check subchapter1 */
  subentry_t = g_list_nth_data (subsubentries, 0);
  fail_if (subentry_t == NULL);
  CHECK_TOC_ENTRY (subentry_t, GST_TOC_ENTRY_TYPE_CHAPTER, ENTRY_SUB1);
  subsubentries = gst_toc_entry_get_sub_entries (subentry_t);
  fail_unless_equals_int (g_list_length (subsubentries), 0);
}

/* This whole test is a bit pointless now that we just stuff a ref of
 * the original TOC into the message/query/event */
GST_START_TEST (test_serializing)
{
  GstToc *toc, *test_toc = NULL;
  GstTocEntry *ed, *ch, *subch;
  GstTagList *tags;
  GstEvent *event;
  GstMessage *message;
  gboolean updated;
  gchar *uid;
  gint64 start = -1, stop = -1;

  toc = gst_toc_new (GST_TOC_SCOPE_GLOBAL);
  fail_unless_equals_int (gst_toc_get_scope (toc), GST_TOC_SCOPE_GLOBAL);
  fail_if (toc == NULL);
  tags = gst_tag_list_new (GST_TAG_TITLE, TOC_TAG, NULL);
  gst_toc_set_tags (toc, tags);

  /* create edition1 */
  ed = gst_toc_entry_new (GST_TOC_ENTRY_TYPE_EDITION, ENTRY_ED1);
  fail_if (ed == NULL);
  tags = gst_tag_list_new (GST_TAG_TITLE, ENTRY_TAG, NULL);
  gst_toc_entry_set_tags (ed, tags);

  CHECK_TOC_ENTRY (ed, GST_TOC_ENTRY_TYPE_EDITION, ENTRY_ED1);

  /* append chapter1 to edition1 */
  ch = gst_toc_entry_new (GST_TOC_ENTRY_TYPE_CHAPTER, ENTRY_CH1);
  fail_if (ch == NULL);
  tags = gst_tag_list_new (GST_TAG_TITLE, ENTRY_TAG, NULL);
  gst_toc_entry_set_tags (ch, tags);

  CHECK_TOC_ENTRY (ch, GST_TOC_ENTRY_TYPE_CHAPTER, ENTRY_CH1);

  gst_toc_entry_append_sub_entry (ed, ch);
  fail_unless_equals_int (g_list_length (gst_toc_entry_get_sub_entries (ed)),
      1);

  /* append chapter2 to edition1 */
  ch = gst_toc_entry_new (GST_TOC_ENTRY_TYPE_CHAPTER, ENTRY_CH2);
  fail_if (ch == NULL);
  tags = gst_tag_list_new (GST_TAG_TITLE, ENTRY_TAG, NULL);
  gst_toc_entry_set_tags (ch, tags);

  CHECK_TOC_ENTRY (ch, GST_TOC_ENTRY_TYPE_CHAPTER, ENTRY_CH2);

  gst_toc_entry_append_sub_entry (ed, ch);
  fail_unless_equals_int (g_list_length (gst_toc_entry_get_sub_entries (ed)),
      2);

  /* append edition1 to the TOC */
  gst_toc_append_entry (toc, ed);
  fail_unless_equals_int (g_list_length (gst_toc_get_entries (toc)), 1);

  /* test gst_toc_entry_find() */
  ed = NULL;
  ed = gst_toc_find_entry (toc, ENTRY_ED1);

  fail_if (ed == NULL);

  CHECK_TOC_ENTRY (ed, GST_TOC_ENTRY_TYPE_EDITION, ENTRY_ED1);

  /* test info GstStructure */
  gst_toc_entry_set_start_stop_times (ch, 100, 1000);
  fail_if (!gst_toc_entry_get_start_stop_times (ch, &start, &stop));
  fail_unless (start == 100);
  fail_unless (stop == 1000);

  /* create edition2 */
  ed = gst_toc_entry_new (GST_TOC_ENTRY_TYPE_EDITION, ENTRY_ED2);
  fail_if (ed == NULL);
  tags = gst_tag_list_new (GST_TAG_TITLE, ENTRY_TAG, NULL);
  gst_toc_entry_set_tags (ed, tags);

  CHECK_TOC_ENTRY (ed, GST_TOC_ENTRY_TYPE_EDITION, ENTRY_ED2);

  /* create chapter3 */
  ch = gst_toc_entry_new (GST_TOC_ENTRY_TYPE_CHAPTER, ENTRY_CH3);
  fail_if (ch == NULL);
  tags = gst_tag_list_new (GST_TAG_TITLE, ENTRY_TAG, NULL);
  gst_toc_entry_set_tags (ch, tags);

  CHECK_TOC_ENTRY (ch, GST_TOC_ENTRY_TYPE_CHAPTER, ENTRY_CH3);

  /* create subchapter1 */
  subch = gst_toc_entry_new (GST_TOC_ENTRY_TYPE_CHAPTER, ENTRY_SUB1);
  fail_if (subch == NULL);
  tags = gst_tag_list_new (GST_TAG_TITLE, ENTRY_TAG, NULL);
  gst_toc_entry_set_tags (subch, tags);

  CHECK_TOC_ENTRY (subch, GST_TOC_ENTRY_TYPE_CHAPTER, ENTRY_SUB1);

  /* append subchapter1 to chapter3 */
  gst_toc_entry_append_sub_entry (ch, subch);
  fail_unless_equals_int (g_list_length (gst_toc_entry_get_sub_entries (ch)),
      1);

  /* append chapter3 to edition2 */
  gst_toc_entry_append_sub_entry (ed, ch);
  fail_unless_equals_int (g_list_length (gst_toc_entry_get_sub_entries (ed)),
      1);

  /* finally append edition2 to the TOC */
  gst_toc_append_entry (toc, ed);
  fail_unless_equals_int (g_list_length (gst_toc_get_entries (toc)), 2);

  GST_INFO ("check original TOC");
  CHECK_TOC (toc);

  /* test gst_toc_copy() */
  test_toc = gst_toc_copy (toc);
  fail_if (test_toc == NULL);
  GST_INFO ("check TOC copy");
  CHECK_TOC (test_toc);
  gst_toc_unref (test_toc);
  test_toc = NULL;

  /* check TOC event handling */
  event = gst_event_new_toc (toc, TRUE);
  fail_if (event == NULL);
  fail_unless (event->type == GST_EVENT_TOC);
  ASSERT_MINI_OBJECT_REFCOUNT (GST_MINI_OBJECT (event), "GstEvent", 1);

  gst_event_parse_toc (event, &test_toc, &updated);
  fail_unless (updated == TRUE);
  fail_if (test_toc == NULL);
  GST_INFO ("check TOC parsed from event");
  CHECK_TOC (test_toc);
  gst_toc_unref (test_toc);
  gst_event_unref (event);
  updated = FALSE;
  test_toc = NULL;

  /* check TOC message handling */
  message = gst_message_new_toc (NULL, toc, TRUE);
  fail_if (message == NULL);
  fail_unless (message->type == GST_MESSAGE_TOC);
  ASSERT_MINI_OBJECT_REFCOUNT (GST_MINI_OBJECT (message), "GstMessage", 1);

  gst_message_parse_toc (message, &test_toc, &updated);
  fail_unless (updated == TRUE);
  fail_if (test_toc == NULL);
  CHECK_TOC (test_toc);
  gst_toc_unref (test_toc);
  gst_message_unref (message);
  test_toc = NULL;

  /* check TOC select event handling */
  event = gst_event_new_toc_select (TEST_UID);
  fail_if (event == NULL);
  fail_unless (event->type == GST_EVENT_TOC_SELECT);
  ASSERT_MINI_OBJECT_REFCOUNT (GST_MINI_OBJECT (event), "GstEvent", 1);

  gst_event_parse_toc_select (event, &uid);
  fail_unless_equals_string (uid, TEST_UID);
  gst_event_unref (event);
  g_free (uid);

  /* FIXME: toc validation / verification should probably be done on the fly
   * while creating it, and not when putting the toc in events or messages ? */
#if 0
  /* that's wrong code, we should fail */
  ch = gst_toc_entry_new (GST_TOC_ENTRY_TYPE_CHAPTER, ENTRY_CH4);
  toc->entries = g_list_prepend (toc->entries, ch);
  ASSERT_CRITICAL (message = gst_message_new_toc (NULL, toc, TRUE));

  /* and yet another one */
  toc->entries = g_list_remove (toc->entries, ch);
  gst_toc_entry_unref (ch);
  ed = gst_toc_entry_new (GST_TOC_ENTRY_TYPE_EDITION, ENTRY_ED3);
  ch = (GstTocEntry *) (toc->entries->data);
  ch->subentries = g_list_prepend (ch->subentries, ed);
  ASSERT_WARNING (message = gst_message_new_toc (NULL, toc, TRUE));
#endif

  gst_toc_unref (toc);
}

GST_END_TEST;

static Suite *
gst_toc_suite (void)
{
  Suite *s = suite_create ("GstToc");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_serializing);

  return s;
}

GST_CHECK_MAIN (gst_toc);
