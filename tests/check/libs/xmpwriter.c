/* GStreamer
 *
 * unit tests for xmp config library
 *
 * Copyright (C) 2011 Thiago Santos <thiago.sousa.santos@collabora.co.uk>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/check/gstcheck.h>

#include <gst/tag/tag.h>
#include <gst/tag/xmpwriter.h>

#include <string.h>

#define TEST_ELEMENT_TYPE (test_element_get_type())

typedef struct TestElement TestElement;
typedef struct TestElementClass TestElementClass;

struct TestElement
{
  GstElement parent;
};

struct TestElementClass
{
  GstElementClass parent_class;
};

GType test_element_get_type (void);

static void init_interface (GType type);

GST_BOILERPLATE_FULL (TestElement, test_element, GstElement, GST_TYPE_ELEMENT,
    init_interface);

static void
init_interface (GType type)
{
  static const GInterfaceInfo tagxmpwriter_info = {
    NULL,
    NULL,
    NULL,
  };

  g_type_add_interface_static (type, GST_TYPE_TAG_XMP_WRITER,
      &tagxmpwriter_info);
}

static void
test_element_base_init (gpointer klass)
{
}

static void
test_element_class_init (TestElementClass * klass)
{
}

static void
test_element_init (TestElement * this, TestElementClass * klass)
{
}

static void
tag_list_equals (GstTagList * taglist, GstTagList * taglist2)
{
  const gchar *name_sent, *name_recv;
  const GValue *value_sent, *value_recv;
  gboolean found;
  gint comparison;
  gint n_recv;
  gint n_sent;
  gint i, j;

  /* verify tags */
  fail_unless (taglist2 != NULL);
  n_recv = gst_structure_n_fields (taglist2);
  n_sent = gst_structure_n_fields (taglist);
  fail_unless (n_recv == n_sent);
  fail_unless (n_sent > 0);

  /* FIXME: compare taglist values */
  for (i = 0; i < n_sent; i++) {
    name_sent = gst_structure_nth_field_name (taglist, i);
    value_sent = gst_structure_get_value (taglist, name_sent);
    found = FALSE;
    for (j = 0; j < n_recv; j++) {
      name_recv = gst_structure_nth_field_name (taglist2, j);
      if (!strcmp (name_sent, name_recv)) {
        value_recv = gst_structure_get_value (taglist2, name_recv);
        comparison = gst_value_compare (value_sent, value_recv);
        if (comparison != GST_VALUE_EQUAL) {
          gchar *vs = g_strdup_value_contents (value_sent);
          gchar *vr = g_strdup_value_contents (value_recv);
          GST_DEBUG ("sent = %s:'%s', recv = %s:'%s'",
              G_VALUE_TYPE_NAME (value_sent), vs,
              G_VALUE_TYPE_NAME (value_recv), vr);
          g_free (vs);
          g_free (vr);
        }
        if (comparison != GST_VALUE_EQUAL &&
            G_VALUE_HOLDS (value_sent, G_TYPE_DOUBLE)) {
          gdouble vs;
          gdouble vr;

          /* add some tolerance for doubles */
          vs = g_value_get_double (value_sent);
          vr = g_value_get_double (value_recv);
          if (vr >= vs - 0.001 && vr <= vs + 0.001)
            comparison = GST_VALUE_EQUAL;
        }
        fail_unless (comparison == GST_VALUE_EQUAL,
            "tag item %s has been received with different type or value",
            name_sent);
        found = TRUE;
        break;
      }
    }
    fail_unless (found, "tag item %s is lost", name_sent);
  }
}

static gboolean
gst_buffer_equals (GstBuffer * buf_a, GstBuffer * buf_b)
{
  if (GST_BUFFER_SIZE (buf_a) != GST_BUFFER_SIZE (buf_b))
    return FALSE;

  return memcmp (GST_BUFFER_DATA (buf_a), GST_BUFFER_DATA (buf_b),
      GST_BUFFER_SIZE (buf_a)) == 0;
}

static GstTagList *
create_taglist (void)
{
  return gst_tag_list_new_full (GST_TAG_ARTIST, "artist",
      GST_TAG_TITLE, "title", GST_TAG_COPYRIGHT, "copyright", NULL);
}

GST_START_TEST (test_no_xmp)
{
  GstTagList *taglist = create_taglist ();
  GstElement *test_element =
      (GstElement *) g_object_new (TEST_ELEMENT_TYPE, NULL);

  gst_tag_xmp_writer_remove_all_schemas (GST_TAG_XMP_WRITER (test_element));

  fail_unless (gst_tag_xmp_writer_tag_list_to_xmp_buffer (GST_TAG_XMP_WRITER
          (test_element), taglist, TRUE) == NULL);

  gst_object_unref (test_element);
  gst_tag_list_free (taglist);
}

GST_END_TEST;


GST_START_TEST (test_default)
{
  GstTagList *taglist = create_taglist ();
  GstElement *test_element =
      (GstElement *) g_object_new (TEST_ELEMENT_TYPE, NULL);
  GstBuffer *buf;
  GstBuffer *buf2;

  buf =
      gst_tag_xmp_writer_tag_list_to_xmp_buffer (GST_TAG_XMP_WRITER
      (test_element), taglist, TRUE);
  buf2 = gst_tag_list_to_xmp_buffer (taglist, TRUE);
  fail_unless (gst_buffer_equals (buf, buf2));

  gst_object_unref (test_element);
  gst_buffer_unref (buf);
  gst_buffer_unref (buf2);
  gst_tag_list_free (taglist);
}

GST_END_TEST;


GST_START_TEST (test_disable)
{
  GstTagList *taglist;
  GstTagList *taglist2;
  GstElement *test_element =
      (GstElement *) g_object_new (TEST_ELEMENT_TYPE, NULL);
  GstBuffer *buf;
  const gchar *str;

  taglist = gst_tag_list_new_full (GST_TAG_ARTIST, "artist", NULL);

  /* add a tag that is mapped on xmp schema (as of Mar, 21th 2011) */
  gst_tag_list_add (taglist, GST_TAG_MERGE_REPLACE, GST_TAG_USER_RATING, 5,
      NULL);

  buf =
      gst_tag_xmp_writer_tag_list_to_xmp_buffer (GST_TAG_XMP_WRITER
      (test_element), taglist, TRUE);
  taglist2 = gst_tag_list_from_xmp_buffer (buf);
  tag_list_equals (taglist, taglist2);
  gst_tag_list_free (taglist2);
  gst_buffer_unref (buf);

  gst_tag_xmp_writer_remove_schema (GST_TAG_XMP_WRITER (test_element), "xap");
  buf =
      gst_tag_xmp_writer_tag_list_to_xmp_buffer (GST_TAG_XMP_WRITER
      (test_element), taglist, TRUE);
  taglist2 = gst_tag_list_from_xmp_buffer (buf);

  /* artist should be there, but rating shouldn't */
  fail_unless (gst_tag_list_peek_string_index (taglist2, GST_TAG_ARTIST, 0,
          &str));
  fail_unless (gst_tag_list_get_value_index (taglist2, GST_TAG_USER_RATING,
          0) == NULL);

  gst_tag_list_free (taglist2);
  gst_buffer_unref (buf);

  gst_object_unref (test_element);
  gst_tag_list_free (taglist);
}

GST_END_TEST;


static Suite *
xmp_config_suite (void)
{
  Suite *s = suite_create ("xmpconfig interface");
  TCase *tc_chain = tcase_create ("configuration");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_no_xmp);
  tcase_add_test (tc_chain, test_default);
  tcase_add_test (tc_chain, test_disable);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = xmp_config_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
