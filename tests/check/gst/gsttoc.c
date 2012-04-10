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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
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
#define INFO_NAME	"gst-toc-check"
#define INFO_FIELD      "info-test"
#define INFO_TEXT_EN    "info-text-entry"
#define INFO_TEXT_TOC   "info-text-toc"

#define CHECK_TOC_ENTRY(entry_c,type_c,uid_c)                            \
{                                                                        \
  gchar *tag_c;                                                          \
  const GValue *val;                                                     \
  GstStructure *struct_c;                                                \
                                                                         \
  fail_unless_equals_string (entry_c->uid, uid_c);                       \
  fail_unless (entry_c->type == type_c);                                 \
  fail_unless (entry_c->tags != NULL);                                   \
  fail_unless (entry_c->pads == NULL);                                   \
                                                                         \
  fail_unless (entry_c->info != NULL);                                   \
  gst_structure_get (entry_c->info, INFO_NAME, GST_TYPE_STRUCTURE,       \
      &struct_c, NULL);                                                  \
  fail_unless (struct_c != NULL);                                        \
  val = gst_structure_get_value (struct_c, INFO_FIELD);                  \
  fail_unless (val != NULL);                                             \
  fail_unless_equals_string (g_value_get_string (val), INFO_TEXT_EN);    \
                                                                         \
  fail_unless (gst_tag_list_get_string (entry_c->tags,                   \
      GST_TAG_TITLE, &tag_c));                                           \
  fail_unless_equals_string (tag_c, ENTRY_TAG);                          \
  g_free (tag_c);                                                        \
  gst_structure_free (struct_c);                                         \
}

#define CHECK_TOC(toc_t)                                                 \
{                                                                        \
  GstTocEntry *entry_t, *subentry_t;                                     \
  gchar *tag_t;                                                          \
  const GValue *val;                                                     \
  GstStructure *struct_toc;                                              \
                                                                         \
  /* check TOC */                                                        \
  fail_unless (g_list_length (toc_t->entries) == 2);                     \
  fail_unless (toc_t->tags != NULL);                                     \
  fail_unless (gst_tag_list_get_string (toc_t->tags,                     \
      GST_TAG_TITLE, &tag_t));                                           \
  fail_unless_equals_string (tag_t, TOC_TAG);                            \
  g_free (tag_t);                                                        \
                                                                         \
  fail_unless (toc_t->info != NULL);                                     \
  gst_structure_get (toc_t->info, INFO_NAME, GST_TYPE_STRUCTURE,         \
      &struct_toc, NULL);                                                \
  fail_unless (struct_toc != NULL);                                      \
  val = gst_structure_get_value (struct_toc, INFO_FIELD);                \
  fail_unless (val != NULL);                                             \
  fail_unless_equals_string (g_value_get_string (val), INFO_TEXT_TOC);   \
  gst_structure_free (struct_toc);                                       \
                                                                         \
  /* check edition1 */                                                   \
  entry_t = g_list_nth_data (toc_t->entries, 0);                         \
  fail_if (entry_t == NULL);                                             \
  fail_unless (g_list_length (entry_t->subentries) == 2);                \
  CHECK_TOC_ENTRY (entry_t, GST_TOC_ENTRY_TYPE_EDITION, ENTRY_ED1);      \
  /* check chapter1 */                                                   \
  subentry_t = g_list_nth_data (entry_t->subentries, 0);                 \
  fail_if (subentry_t == NULL);                                          \
  fail_unless (g_list_length (subentry_t->subentries) == 0);             \
  CHECK_TOC_ENTRY (subentry_t, GST_TOC_ENTRY_TYPE_CHAPTER, ENTRY_CH1);   \
  /* check chapter2 */                                                   \
  subentry_t = g_list_nth_data (entry_t->subentries, 1);                 \
  fail_if (subentry_t == NULL);                                          \
  fail_unless (g_list_length (subentry_t->subentries) == 0);             \
  CHECK_TOC_ENTRY (subentry_t, GST_TOC_ENTRY_TYPE_CHAPTER, ENTRY_CH2);   \
  /* check edition2 */                                                   \
  entry_t = g_list_nth_data (toc_t->entries, 1);                         \
  fail_if (entry_t == NULL);                                             \
  fail_unless (g_list_length (entry_t->subentries) == 1);                \
  CHECK_TOC_ENTRY (entry_t, GST_TOC_ENTRY_TYPE_EDITION, ENTRY_ED2);      \
  /* check chapter3 */                                                   \
  subentry_t = g_list_nth_data (entry_t->subentries, 0);                 \
  fail_if (subentry_t == NULL);                                          \
  fail_unless (g_list_length (subentry_t->subentries) == 1);             \
  CHECK_TOC_ENTRY (subentry_t, GST_TOC_ENTRY_TYPE_CHAPTER, ENTRY_CH3);   \
  /* check subchapter1 */                                                \
  subentry_t = g_list_nth_data (subentry_t->subentries, 0);              \
  fail_if (subentry_t == NULL);                                          \
  fail_unless (g_list_length (subentry_t->subentries) == 0);             \
  CHECK_TOC_ENTRY (subentry_t, GST_TOC_ENTRY_TYPE_CHAPTER, ENTRY_SUB1);  \
}

GST_START_TEST (test_serializing)
{
  GstStructure *structure;
  GstToc *toc, *test_toc = NULL;
  GstTocEntry *ed, *ch, *subch;
  GstEvent *event;
  GstMessage *message;
  GstQuery *query;
  gboolean updated;
  gchar *uid;
  gint64 start = -1, stop = -1;

  toc = gst_toc_new ();
  fail_if (toc == NULL);
  gst_tag_list_add (toc->tags, GST_TAG_MERGE_APPEND, GST_TAG_TITLE,
      TOC_TAG, NULL);
  structure =
      gst_structure_new (INFO_NAME, INFO_FIELD, G_TYPE_STRING, INFO_TEXT_TOC,
      NULL);
  gst_structure_set (toc->info, INFO_NAME, GST_TYPE_STRUCTURE, structure, NULL);
  gst_structure_free (structure);

  /* create edition1 */
  ed = gst_toc_entry_new (GST_TOC_ENTRY_TYPE_EDITION, ENTRY_ED1);
  fail_if (ed == NULL);
  gst_tag_list_add (ed->tags, GST_TAG_MERGE_APPEND, GST_TAG_TITLE,
      ENTRY_TAG, NULL);
  structure =
      gst_structure_new (INFO_NAME, INFO_FIELD, G_TYPE_STRING, INFO_TEXT_EN,
      NULL);
  gst_structure_set (ed->info, INFO_NAME, GST_TYPE_STRUCTURE, structure, NULL);
  gst_structure_free (structure);

  CHECK_TOC_ENTRY (ed, GST_TOC_ENTRY_TYPE_EDITION, ENTRY_ED1);

  /* append chapter1 to edition1 */
  ch = gst_toc_entry_new (GST_TOC_ENTRY_TYPE_CHAPTER, ENTRY_CH1);
  fail_if (ch == NULL);
  gst_tag_list_add (ch->tags, GST_TAG_MERGE_APPEND, GST_TAG_TITLE,
      ENTRY_TAG, NULL);
  structure =
      gst_structure_new (INFO_NAME, INFO_FIELD, G_TYPE_STRING, INFO_TEXT_EN,
      NULL);
  gst_structure_set (ch->info, INFO_NAME, GST_TYPE_STRUCTURE, structure, NULL);
  gst_structure_free (structure);

  CHECK_TOC_ENTRY (ch, GST_TOC_ENTRY_TYPE_CHAPTER, ENTRY_CH1);

  ed->subentries = g_list_append (ed->subentries, ch);
  fail_unless (g_list_length (ed->subentries) == 1);

  /* append chapter2 to edition1 */
  ch = gst_toc_entry_new (GST_TOC_ENTRY_TYPE_CHAPTER, ENTRY_CH2);
  fail_if (ch == NULL);
  gst_tag_list_add (ch->tags, GST_TAG_MERGE_APPEND, GST_TAG_TITLE,
      ENTRY_TAG, NULL);
  structure =
      gst_structure_new (INFO_NAME, INFO_FIELD, G_TYPE_STRING, INFO_TEXT_EN,
      NULL);
  gst_structure_set (ch->info, INFO_NAME, GST_TYPE_STRUCTURE, structure, NULL);
  gst_structure_free (structure);

  CHECK_TOC_ENTRY (ch, GST_TOC_ENTRY_TYPE_CHAPTER, ENTRY_CH2);

  ed->subentries = g_list_append (ed->subentries, ch);
  fail_unless (g_list_length (ed->subentries) == 2);

  /* append edition1 to the TOC */
  toc->entries = g_list_append (toc->entries, ed);
  fail_unless (g_list_length (toc->entries) == 1);

  /* test gst_toc_entry_find() */
  ed = NULL;
  ed = gst_toc_find_entry (toc, ENTRY_ED1);

  fail_if (ed == NULL);

  CHECK_TOC_ENTRY (ed, GST_TOC_ENTRY_TYPE_EDITION, ENTRY_ED1);

  /* test info GstStructure */
  gst_toc_entry_set_start_stop (ch, 100, 1000);
  fail_if (!gst_toc_entry_get_start_stop (ch, &start, &stop));
  fail_unless (start == 100);
  fail_unless (stop == 1000);

  /* create edition2 */
  ed = gst_toc_entry_new (GST_TOC_ENTRY_TYPE_EDITION, ENTRY_ED2);
  fail_if (ed == NULL);
  gst_tag_list_add (ed->tags, GST_TAG_MERGE_APPEND, GST_TAG_TITLE,
      ENTRY_TAG, NULL);
  structure =
      gst_structure_new (INFO_NAME, INFO_FIELD, G_TYPE_STRING, INFO_TEXT_EN,
      NULL);
  gst_structure_set (ed->info, INFO_NAME, GST_TYPE_STRUCTURE, structure, NULL);
  gst_structure_free (structure);

  CHECK_TOC_ENTRY (ed, GST_TOC_ENTRY_TYPE_EDITION, ENTRY_ED2);

  /* create chapter3 */
  ch = gst_toc_entry_new (GST_TOC_ENTRY_TYPE_CHAPTER, ENTRY_CH3);
  fail_if (ch == NULL);
  gst_tag_list_add (ch->tags, GST_TAG_MERGE_APPEND, GST_TAG_TITLE,
      ENTRY_TAG, NULL);
  structure =
      gst_structure_new (INFO_NAME, INFO_FIELD, G_TYPE_STRING, INFO_TEXT_EN,
      NULL);
  gst_structure_set (ch->info, INFO_NAME, GST_TYPE_STRUCTURE, structure, NULL);
  gst_structure_free (structure);

  CHECK_TOC_ENTRY (ch, GST_TOC_ENTRY_TYPE_CHAPTER, ENTRY_CH3);

  /* create subchapter1 */
  subch = gst_toc_entry_new (GST_TOC_ENTRY_TYPE_CHAPTER, ENTRY_SUB1);
  fail_if (subch == NULL);
  gst_tag_list_add (subch->tags, GST_TAG_MERGE_APPEND, GST_TAG_TITLE,
      ENTRY_TAG, NULL);
  structure =
      gst_structure_new (INFO_NAME, INFO_FIELD, G_TYPE_STRING, INFO_TEXT_EN,
      NULL);
  gst_structure_set (subch->info, INFO_NAME, GST_TYPE_STRUCTURE, structure,
      NULL);
  gst_structure_free (structure);

  CHECK_TOC_ENTRY (subch, GST_TOC_ENTRY_TYPE_CHAPTER, ENTRY_SUB1);

  /* append subchapter1 to chapter3 */
  ch->subentries = g_list_append (ch->subentries, subch);
  fail_unless (g_list_length (ch->subentries) == 1);

  /* append chapter3 to edition2 */
  ed->subentries = g_list_append (ed->subentries, ch);
  fail_unless (g_list_length (ed->subentries) == 1);

  /* finally append edition2 to the TOC */
  toc->entries = g_list_append (toc->entries, ed);
  fail_unless (g_list_length (toc->entries) == 2);

  /* test gst_toc_copy() */
  test_toc = gst_toc_copy (toc);
  fail_if (test_toc == NULL);
  CHECK_TOC (test_toc);
  gst_toc_free (test_toc);
  test_toc = NULL;

  /* check TOC event handling */
  event = gst_event_new_toc (toc, TRUE);
  fail_if (event == NULL);
  fail_if (event->structure == NULL);
  fail_unless (event->type == GST_EVENT_TOC);
  ASSERT_MINI_OBJECT_REFCOUNT (GST_MINI_OBJECT (event), "GstEvent", 1);

  gst_event_parse_toc (event, &test_toc, &updated);
  fail_unless (updated == TRUE);
  fail_if (test_toc == NULL);
  CHECK_TOC (test_toc);
  gst_toc_free (test_toc);
  gst_event_unref (event);
  updated = FALSE;
  test_toc = NULL;

  /* check TOC message handling */
  message = gst_message_new_toc (NULL, toc, TRUE);
  fail_if (message == NULL);
  fail_if (event->structure == NULL);
  fail_unless (message->type == GST_MESSAGE_TOC);
  ASSERT_MINI_OBJECT_REFCOUNT (GST_MINI_OBJECT (message), "GstMessage", 1);

  gst_message_parse_toc (message, &test_toc, &updated);
  fail_unless (updated == TRUE);
  fail_if (test_toc == NULL);
  CHECK_TOC (test_toc);
  gst_toc_free (test_toc);
  gst_message_unref (message);
  test_toc = NULL;

  /* check TOC select event handling */
  event = gst_event_new_toc_select (TEST_UID);
  fail_if (event == NULL);
  fail_if (event->structure == NULL);
  fail_unless (event->type == GST_EVENT_TOC_SELECT);
  ASSERT_MINI_OBJECT_REFCOUNT (GST_MINI_OBJECT (event), "GstEvent", 1);

  gst_event_parse_toc_select (event, &uid);
  fail_unless_equals_string (uid, TEST_UID);
  gst_event_unref (event);
  g_free (uid);

  /* check TOC query handling */
  query = gst_query_new_toc ();
  fail_if (query == NULL);
  gst_query_set_toc (query, toc, TEST_UID);
  fail_if (query->structure == NULL);
  fail_unless (query->type == GST_QUERY_TOC);
  ASSERT_MINI_OBJECT_REFCOUNT (GST_MINI_OBJECT (query), "GstQuery", 1);

  gst_query_parse_toc (query, &test_toc, &uid);
  fail_unless_equals_string (uid, TEST_UID);
  fail_if (test_toc == NULL);
  CHECK_TOC (test_toc);
  gst_toc_free (test_toc);
  gst_query_unref (query);
  g_free (uid);

  /* that's wrong code, we should fail */
  ch = gst_toc_entry_new (GST_TOC_ENTRY_TYPE_CHAPTER, ENTRY_CH4);
  toc->entries = g_list_prepend (toc->entries, ch);
  ASSERT_CRITICAL (message = gst_message_new_toc (NULL, toc, TRUE));

  /* and yet another one */
  toc->entries = g_list_remove (toc->entries, ch);
  gst_toc_entry_free (ch);
  ed = gst_toc_entry_new (GST_TOC_ENTRY_TYPE_EDITION, ENTRY_ED3);
  ch = (GstTocEntry *) (toc->entries->data);
  ch->subentries = g_list_prepend (ch->subentries, ed);
  ASSERT_WARNING (message = gst_message_new_toc (NULL, toc, TRUE));

  gst_toc_free (toc);
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
