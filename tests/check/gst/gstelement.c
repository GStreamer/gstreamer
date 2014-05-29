/* GStreamer
 * Copyright (C) 2005 Thomas Vander Stichele <thomas at apestaart dot org>
 *
 * gstelement.c: Unit test for GstElement
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

GST_START_TEST (test_add_remove_pad)
{
  GstElement *e;
  GstPad *p;

  /* getting an existing element class is cheating, but easier */
  e = gst_element_factory_make ("fakesrc", "source");

  /* create a new floating pad with refcount 1 */
  p = gst_pad_new ("source", GST_PAD_SRC);
  ASSERT_OBJECT_REFCOUNT (p, "pad", 1);
  /* ref it for ourselves */
  gst_object_ref (p);
  ASSERT_OBJECT_REFCOUNT (p, "pad", 2);
  /* adding it sinks the pad -> not floating, same refcount */
  gst_element_add_pad (e, p);
  ASSERT_OBJECT_REFCOUNT (p, "pad", 2);

  /* removing it reduces the refcount */
  gst_element_remove_pad (e, p);
  ASSERT_OBJECT_REFCOUNT (p, "pad", 1);

  /* clean up our own reference */
  gst_object_unref (p);
  gst_object_unref (e);
}

GST_END_TEST;


GST_START_TEST (test_add_remove_readd_pad)
{
  GstElement *e;
  GstPad *p;

  /* getting an existing element class is cheating, but easier */
  e = gst_element_factory_make ("fakesrc", "source");

  /* create a new floating pad with refcount 1 */
  p = gst_pad_new ("source", GST_PAD_SRC);

  gst_object_ref (p);

  /* simulate a real scenario where the pad is activated before added */
  fail_unless (gst_pad_set_active (p, TRUE));
  gst_element_add_pad (e, p);

  /* now remove and deactivate it */
  fail_unless (gst_pad_set_active (p, FALSE));
  gst_element_remove_pad (e, p);

  /* should be able to reuse the same pad */
  fail_unless (gst_pad_set_active (p, TRUE));
  fail_unless (gst_element_add_pad (e, p));

  /* clean up our own reference */
  gst_object_unref (p);
  gst_object_unref (e);
}

GST_END_TEST;


GST_START_TEST (test_add_pad_unref_element)
{
  GstElement *e;
  GstPad *p;

  /* getting an existing element class is cheating, but easier */
  e = gst_element_factory_make ("fakesrc", "source");

  /* create a new floating pad with refcount 1 */
  p = gst_pad_new ("source", GST_PAD_SRC);
  ASSERT_OBJECT_REFCOUNT (p, "pad", 1);
  /* ref it for ourselves */
  gst_object_ref (p);
  ASSERT_OBJECT_REFCOUNT (p, "pad", 2);
  /* adding it sinks the pad -> not floating, same refcount */
  gst_element_add_pad (e, p);
  ASSERT_OBJECT_REFCOUNT (p, "pad", 2);

  /* unreffing the element should clean it up */
  gst_object_unref (GST_OBJECT (e));

  ASSERT_OBJECT_REFCOUNT (p, "pad", 1);

  /* clean up our own reference */
  gst_object_unref (p);
}

GST_END_TEST;

GST_START_TEST (test_error_no_bus)
{
  GstElement *e;
  GstBus *bus;

  e = gst_element_factory_make ("fakesrc", "source");

  /* get the bus, should be NULL */
  bus = gst_element_get_bus (e);
  fail_if (bus != NULL);

  /* I don't want errors shown */
  gst_debug_set_default_threshold (GST_LEVEL_NONE);

  GST_ELEMENT_ERROR (e, RESOURCE, OPEN_READ, ("I could not read"), ("debug"));

  gst_object_unref (e);
}

GST_END_TEST;

/* link and run two elements without putting them in a pipeline */
GST_START_TEST (test_link)
{
  GstElement *src, *sink;

  src = gst_element_factory_make ("fakesrc", "source");
  sink = gst_element_factory_make ("fakesink", "sink");

  fail_unless (gst_element_link_pads (src, "src", sink, "sink"));

  /* do sink to source state change */
  gst_element_set_state (sink, GST_STATE_PAUSED);
  gst_element_set_state (src, GST_STATE_PAUSED);

  /* wait for preroll */
  gst_element_get_state (sink, NULL, NULL, GST_CLOCK_TIME_NONE);

  /* play some more */
  gst_element_set_state (sink, GST_STATE_PLAYING);
  gst_element_set_state (src, GST_STATE_PLAYING);

  g_usleep (G_USEC_PER_SEC);

  /* and stop */
  gst_element_set_state (sink, GST_STATE_PAUSED);
  gst_element_set_state (src, GST_STATE_PAUSED);

  /* wait for preroll */
  gst_element_get_state (sink, NULL, NULL, GST_CLOCK_TIME_NONE);

  gst_element_set_state (sink, GST_STATE_NULL);
  gst_element_set_state (src, GST_STATE_NULL);

  gst_element_get_state (sink, NULL, NULL, GST_CLOCK_TIME_NONE);
  g_usleep (G_USEC_PER_SEC / 2);

  ASSERT_OBJECT_REFCOUNT (sink, "sink", 1);
  ASSERT_OBJECT_REFCOUNT (src, "src", 1);
  gst_element_unlink_pads (src, "src", sink, "sink");
  ASSERT_OBJECT_REFCOUNT (sink, "sink", 1);
  ASSERT_OBJECT_REFCOUNT (src, "src", 1);

  gst_object_unref (src);
  gst_object_unref (sink);
}

GST_END_TEST;

/* linking two elements without pads should fail */
GST_START_TEST (test_link_no_pads)
{
  GstElement *src, *sink;

  src = gst_bin_new ("src");
  sink = gst_bin_new ("sink");

  fail_if (gst_element_link (src, sink));

  gst_object_unref (src);
  gst_object_unref (sink);
}

GST_END_TEST;

typedef struct _GstTestElement
{
  GstElement parent;

} GstTestElement;

typedef struct _GstTestElementClass
{
  GstElementClass parent;

} GstTestElementClass;

static void
gst_test_element_class_init (GstTestElementClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstPadTemplate *templ;

  gst_element_class_set_metadata (element_class, "Test element",
      "Element", "Does nothing", "Foo Bar <foo@bar.com>");

  fail_unless_equals_int (g_list_length (gst_element_class_get_pad_template_list
          (element_class)), 0);

  fail_unless (gst_element_class_get_pad_template (element_class,
          "test") == NULL);

  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("test", GST_PAD_SRC, GST_PAD_ALWAYS, GST_CAPS_ANY));

  fail_unless_equals_int (g_list_length (gst_element_class_get_pad_template_list
          (element_class)), 1);

  fail_unless ((templ =
          gst_element_class_get_pad_template (element_class, "test")) != NULL);
  fail_unless (gst_caps_is_any (templ->caps));

  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("test2", GST_PAD_SRC, GST_PAD_ALWAYS,
          GST_CAPS_ANY));

  fail_unless_equals_int (g_list_length (gst_element_class_get_pad_template_list
          (element_class)), 2);

  fail_unless ((templ =
          gst_element_class_get_pad_template (element_class, "test2")) != NULL);
  fail_unless (gst_caps_is_any (templ->caps));

  /* Add "test" again, with NONE caps this time */
  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("test", GST_PAD_SRC, GST_PAD_ALWAYS,
          GST_CAPS_NONE));

  fail_unless_equals_int (g_list_length (gst_element_class_get_pad_template_list
          (element_class)), 2);

  fail_unless ((templ =
          gst_element_class_get_pad_template (element_class, "test")) != NULL);
  fail_unless (gst_caps_is_empty (templ->caps));
}

static GType
gst_test_element_get_type (void)
{
  static GType gst_test_element_type = G_TYPE_NONE;

  if (gst_test_element_type == G_TYPE_NONE) {
    static const GTypeInfo gst_test_element_info = {
      sizeof (GstTestElementClass),
      NULL,                     /* base_init */
      NULL,                     /* base_finalize */
      (GClassInitFunc) gst_test_element_class_init,
      NULL,
      NULL,
      sizeof (GstTestElement),
      0,
      NULL,                     /* instance_init */
      NULL
    };

    gst_test_element_type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstTestElement", &gst_test_element_info, 0);
  }
  return gst_test_element_type;
}

typedef struct _GstTestElement2
{
  GstTestElement parent;

} GstTestElement2;

typedef struct _GstTestElement2Class
{
  GstTestElementClass parent;

} GstTestElement2Class;

static void
gst_test_element2_class_init (GstTestElement2Class * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstPadTemplate *templ;

  gst_element_class_set_metadata (element_class, "Test element 2",
      "Element", "Does nothing", "Foo Bar <foo@bar.com>");

  fail_unless_equals_int (g_list_length (gst_element_class_get_pad_template_list
          (element_class)), 2);

  fail_unless ((templ =
          gst_element_class_get_pad_template (element_class, "test")) != NULL);
  fail_unless (gst_caps_is_empty (templ->caps));

  fail_unless ((templ =
          gst_element_class_get_pad_template (element_class, "test2")) != NULL);
  fail_unless (gst_caps_is_any (templ->caps));

  /* Add "test" pad with ANY caps, should have "test" pad with EMPTY caps before */
  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("test", GST_PAD_SRC, GST_PAD_ALWAYS, GST_CAPS_ANY));

  fail_unless_equals_int (g_list_length (gst_element_class_get_pad_template_list
          (element_class)), 2);

  fail_unless ((templ =
          gst_element_class_get_pad_template (element_class, "test")) != NULL);
  fail_unless (gst_caps_is_any (templ->caps));


  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("test4", GST_PAD_SRC, GST_PAD_ALWAYS,
          GST_CAPS_ANY));

  fail_unless_equals_int (g_list_length (gst_element_class_get_pad_template_list
          (element_class)), 3);

  fail_unless ((templ =
          gst_element_class_get_pad_template (element_class, "test4")) != NULL);
  fail_unless (gst_caps_is_any (templ->caps));
}

static GType
gst_test_element2_get_type (void)
{
  static GType gst_test_element2_type = G_TYPE_NONE;

  if (gst_test_element2_type == G_TYPE_NONE) {
    static const GTypeInfo gst_test_element2_info = {
      sizeof (GstTestElement2Class),
      NULL,                     /* base_init */
      NULL,                     /* base_finalize */
      (GClassInitFunc) gst_test_element2_class_init,
      NULL,
      NULL,
      sizeof (GstTestElement2),
      0,
      NULL,                     /* instance_init */
      NULL
    };

    gst_test_element2_type =
        g_type_register_static (gst_test_element_get_type (), "GstTestElement2",
        &gst_test_element2_info, 0);
  }
  return gst_test_element2_type;
}


GST_START_TEST (test_pad_templates)
{
  GstTestElement *test;
  GstTestElement2 *test2;

  test = g_object_new (gst_test_element_get_type (), NULL);
  test2 = g_object_new (gst_test_element2_get_type (), NULL);

  g_object_unref (test);
  g_object_unref (test2);
}

GST_END_TEST;

static Suite *
gst_element_suite (void)
{
  Suite *s = suite_create ("GstElement");
  TCase *tc_chain = tcase_create ("element tests");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_add_remove_pad);
  tcase_add_test (tc_chain, test_add_remove_readd_pad);
  tcase_add_test (tc_chain, test_add_pad_unref_element);
  tcase_add_test (tc_chain, test_error_no_bus);
  tcase_add_test (tc_chain, test_link);
  tcase_add_test (tc_chain, test_link_no_pads);
  tcase_add_test (tc_chain, test_pad_templates);

  return s;
}

GST_CHECK_MAIN (gst_element);
