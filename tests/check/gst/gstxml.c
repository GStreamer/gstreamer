/* GStreamer
 * Copyright (C) 2006 Tim-Philipp Müller <tim centricular net>
 *
 * gstxml.c: Unit test for GstXML
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
#include <string.h>

#define XML_PIPELINE                                             \
  "<?xml version=\"1.0\"?>"                                      \
  "<gstreamer xmlns:gst=\"http://gstreamer.net/gst-core/1.0/\">" \
  "  <gst:element>"                                              \
  "    <gst:name>test-pipeline</gst:name>"                       \
  "    <gst:type>pipeline</gst:type>"                            \
  "    <gst:param>"                                              \
  "      <gst:name>name</gst:name>"                              \
  "      <gst:value>test-pipeline</gst:value>"                   \
  "    </gst:param>"                                             \
  "  </gst:element>"                                             \
  "</gstreamer>"


GST_START_TEST (test_pipeline_from_xml)
{
  GstElement *element;
  GstXML *xml;
  GList *elements;

  xml = gst_xml_new ();
  fail_unless (xml != NULL);
  ASSERT_OBJECT_REFCOUNT (xml, "GstXML object after creating it", 1);

  fail_unless (gst_xml_parse_memory (xml, (guchar *) XML_PIPELINE,
          strlen (XML_PIPELINE), NULL));

  elements = gst_xml_get_topelements (xml);
  fail_unless (elements != NULL);
  fail_unless (g_list_length (elements) == 1);

  element = GST_ELEMENT (elements->data);
  fail_unless (element != NULL);
  fail_unless (GST_IS_PIPELINE (element));
  fail_unless_equals_string (GST_OBJECT_NAME (element), "test-pipeline");

  ASSERT_OBJECT_REFCOUNT (element, "pipeline owned by GstXML", 1);
  gst_object_ref (element);
  ASSERT_OBJECT_REFCOUNT (element, "pipeline after we obtained a ref", 2);

  gst_object_unref (xml);

  ASSERT_OBJECT_REFCOUNT (element, "pipeline after GstXML was unrefed", 1);
  gst_object_unref (element);
}

GST_END_TEST;

static Suite *
gst_xml_suite (void)
{
  Suite *s = suite_create ("GstXML");
  TCase *tc_chain = tcase_create ("xml");

  tcase_set_timeout (tc_chain, 20);

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_pipeline_from_xml);
  return s;
}

GST_CHECK_MAIN (gst_xml);
