/* GStreamer
 * Copyright (C) 2004 Benjamin Otte <otte@gnome.org>
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
#  include "config.h"
#endif

#include <gst/gst.h>
#include "tests.h"

GST_DEBUG_CATEGORY_STATIC (gst_test_debug);
#define GST_CAT_DEFAULT gst_test_debug

/* This plugin does all the tests registered in the tests.h file
 */

#define GST_TYPE_TEST \
  (gst_test_get_type())
#define GST_TEST(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_TEST,GstTest))
#define GST_TEST_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_TEST,GstTestClass))
#define GST_TEST_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj),GST_TYPE_TEST,GstTestClass))
#define GST_IS_TEST(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_TEST))
#define GST_IS_TEST_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_TEST))

typedef struct _GstTest GstTest;
typedef struct _GstTestClass GstTestClass;

struct _GstTest
{
  GstElement element;

  GstPad *sinkpad;

  gpointer tests[TESTS_COUNT];
  GValue values[TESTS_COUNT];
};

struct _GstTestClass
{
  GstElementClass parent_class;

  gchar *param_names[2 * TESTS_COUNT];
};

GST_BOILERPLATE (GstTest, gst_test, GstElement, GST_TYPE_ELEMENT)

     static void gst_test_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
     static void gst_test_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

     static void gst_test_chain (GstPad * pad, GstData * _data);

     static void gst_test_base_init (gpointer g_class)
{
  static GstElementDetails details = GST_ELEMENT_DETAILS ("gsttestsink",
      "Testing",
      "perform a number of tests",
      "Benjamin Otte <otte@gnome>");
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (gstelement_class, &details);
}

static void
gst_test_class_init (GstTestClass * klass)
{
  GObjectClass *object = G_OBJECT_CLASS (klass);
  guint i;

  object->set_property = GST_DEBUG_FUNCPTR (gst_test_set_property);
  object->get_property = GST_DEBUG_FUNCPTR (gst_test_get_property);

  for (i = 0; i < TESTS_COUNT; i++) {
    GParamSpec *spec;

    spec = tests[i].get_spec (&tests[i], FALSE);
    klass->param_names[2 * i] = g_strdup (g_param_spec_get_name (spec));
    g_object_class_install_property (object, 2 * i + 1, spec);
    spec = tests[i].get_spec (&tests[i], TRUE);
    klass->param_names[2 * i + 1] = g_strdup (g_param_spec_get_name (spec));
    g_object_class_install_property (object, 2 * i + 2, spec);
  }
}

static void
gst_test_init (GstTest * test)
{
  GstTestClass *klass;
  guint i;

  GST_FLAG_SET (test, GST_ELEMENT_EVENT_AWARE);

  test->sinkpad = gst_pad_new ("sink", GST_PAD_SINK);
  gst_element_add_pad (GST_ELEMENT (test), test->sinkpad);
  gst_pad_set_chain_function (test->sinkpad,
      GST_DEBUG_FUNCPTR (gst_test_chain));

  klass = GST_TEST_GET_CLASS (test);
  for (i = 0; i < TESTS_COUNT; i++) {
    GParamSpec *spec = g_object_class_find_property (G_OBJECT_CLASS (klass),
        klass->param_names[2 * i + 1]);

    g_value_init (&test->values[i], G_PARAM_SPEC_VALUE_TYPE (spec));
  }
}

static void
tests_unset (GstTest * test)
{
  guint i;

  for (i = 0; i < TESTS_COUNT; i++) {
    if (test->tests[i]) {
      tests[i].free (test->tests[i]);
      test->tests[i] = NULL;
    }
  }
}

static void
tests_set (GstTest * test)
{
  guint i;

  for (i = 0; i < TESTS_COUNT; i++) {
    g_assert (test->tests[i] == NULL);
    test->tests[i] = tests[i].new (&tests[i]);
  }
}

static void
gst_test_chain (GstPad * pad, GstData * data)
{
  guint i;
  GstTest *test = GST_TEST (gst_pad_get_parent (pad));
  GstTestClass *klass = GST_TEST_GET_CLASS (test);

  if (GST_IS_EVENT (data)) {
    GstEvent *event = GST_EVENT (data);

    switch (GST_EVENT_TYPE (event)) {
      case GST_EVENT_DISCONTINUOUS:
        if (GST_EVENT_DISCONT_NEW_MEDIA (event)) {
          tests_unset (test);
          tests_set (test);
        }
        break;
      case GST_EVENT_EOS:
        g_object_freeze_notify (G_OBJECT (test));
        for (i = 0; i < TESTS_COUNT; i++) {
          if (test->tests[i]) {
            if (!tests[i].finish (test->tests[i], &test->values[i])) {
              GValue v = { 0, };
              gchar *real, *expected;

              expected = gst_value_serialize (&test->values[i]);
              g_value_init (&v, G_VALUE_TYPE (&test->values[i]));
              g_object_get_property (G_OBJECT (test), klass->param_names[2 * i],
                  &v);
              real = gst_value_serialize (&v);
              g_value_unset (&v);
              GST_ELEMENT_ERROR (test, STREAM, FORMAT, (NULL),
                  ("test %s returned value \"%s\" and not expected value \"%s\"",
                      klass->param_names[2 * i], real, expected));
              g_free (real);
              g_free (expected);
            }
            g_object_notify (G_OBJECT (test), klass->param_names[2 * i]);
          }
        }
        g_object_thaw_notify (G_OBJECT (test));
        break;
      default:
        break;
    }
    gst_pad_event_default (pad, event);
    return;
  }

  for (i = 0; i < TESTS_COUNT; i++) {
    if (test->tests[i]) {
      tests[i].add (test->tests[i], GST_BUFFER (data));
    }
  }
  gst_data_unref (data);
}

static void
gst_test_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstTest *test = GST_TEST (object);

  if (prop_id == 0 || prop_id > 2 * TESTS_COUNT) {
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    return;
  }

  if (prop_id % 2) {
    /* real values can't be set */
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  } else {
    /* expected values */
    g_value_copy (value, &test->values[prop_id / 2 - 1]);
  }
}

static void
gst_test_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstTest *test = GST_TEST (object);
  guint id = (prop_id - 1) / 2;

  if (prop_id == 0 || prop_id > 2 * TESTS_COUNT) {
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    return;
  }

  if (prop_id % 2) {
    /* real values */
    tests[id].get_value (test->tests[id], value);
  } else {
    /* expected values */
    g_value_copy (&test->values[id], value);
  }
}

gboolean
gst_test_plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "testsink", GST_RANK_NONE, GST_TYPE_TEST))
    return FALSE;

  GST_DEBUG_CATEGORY_INIT (gst_test_debug, "testsink", 0,
      "debugging category for testsink element");

  return TRUE;
}
