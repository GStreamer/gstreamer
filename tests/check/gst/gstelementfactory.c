/* GStreamer
 * Copyright (C) 2011 Stefan Kost <ensonic@users.sf.net>
 *
 * gstelementfactory.c: Unit test for GstElementFactory
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


/* check if the elementfactory of a class is filled (see #131079) */
GST_START_TEST (test_class)
{
  GstElementClass *klass;
  GstElementFactory *factory, *tmp;
  GType type;

  GST_DEBUG ("finding factory for queue");
  factory = gst_element_factory_find ("queue");
  fail_if (factory == NULL);

  /* it may already be loaded if check is being run with CK_FORK=no */
  if (!GST_PLUGIN_FEATURE (factory)->loaded) {
    GST_DEBUG ("getting the type");
    /* feature is not loaded, should return 0 as the type */
    type = gst_element_factory_get_element_type (factory);
    fail_if (type != 0);
  }

  GST_DEBUG ("now loading the plugin");
  tmp =
      GST_ELEMENT_FACTORY (gst_plugin_feature_load (GST_PLUGIN_FEATURE
          (factory)));
  fail_if (tmp == NULL);

  gst_object_unref (factory);
  factory = tmp;

  /* feature is now loaded */
  type = gst_element_factory_get_element_type (factory);
  fail_if (type == 0);

  klass = g_type_class_ref (factory->type);
  fail_if (klass == NULL);

  GST_DEBUG ("checking the element factory class field");
  /* and elementfactory is filled in */
  fail_if (klass->elementfactory == NULL);
  fail_if (klass->elementfactory != factory);

}

GST_END_TEST;


static Suite *
gst_element_factory_suite (void)
{
  Suite *s = suite_create ("GstElementFactory");
  TCase *tc_chain = tcase_create ("element-factory tests");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_class);

  return s;
}

GST_CHECK_MAIN (gst_element_factory);
