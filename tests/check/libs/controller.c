/* GStreamer
 *
 * unit test for the controller library
 *
 * Copyright (C) <2005> Stefan Kost <ensonic at users dot sf dor net>
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
#include <gst/gst.h>
#include <gst/check/gstcheck.h>
#include <gst/controller/gstcontroller.h>

/* LOCAL TEST ELEMENT */

enum
{
  ARG_ULONG = 1,
  ARG_DOUBLE,
  ARG_BOOLEAN,
  ARG_READONLY,
  ARG_STATIC,
  ARG_CONSTRUCTONLY,
  ARG_COUNT
};

#define GST_TYPE_TEST_MONO_SOURCE            (gst_test_mono_source_get_type ())
#define GST_TEST_MONO_SOURCE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_TEST_MONO_SOURCE, GstTestMonoSource))
#define GST_TEST_MONO_SOURCE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_TEST_MONO_SOURCE, GstTestMonoSourceClass))
#define GST_IS_TEST_MONO_SOURCE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_TEST_MONO_SOURCE))
#define GST_IS_TEST_MONO_SOURCE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_TEST_MONO_SOURCE))
#define GST_TEST_MONO_SOURCE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_TEST_MONO_SOURCE, GstTestMonoSourceClass))

typedef struct _GstTestMonoSource GstTestMonoSource;
typedef struct _GstTestMonoSourceClass GstTestMonoSourceClass;

struct _GstTestMonoSource
{
  GstElement parent;
  gulong val_ulong;
  gdouble val_double;
  gboolean val_boolean;
};
struct _GstTestMonoSourceClass
{
  GstElementClass parent_class;
};

GType gst_test_mono_source_get_type (void);

static void
gst_test_mono_source_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec)
{
  GstTestMonoSource *self = GST_TEST_MONO_SOURCE (object);

  switch (property_id) {
    case ARG_ULONG:
      g_value_set_ulong (value, self->val_ulong);
      break;
    case ARG_DOUBLE:
      g_value_set_double (value, self->val_double);
      break;
    case ARG_BOOLEAN:
      g_value_set_boolean (value, self->val_boolean);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gst_test_mono_source_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec)
{
  GstTestMonoSource *self = GST_TEST_MONO_SOURCE (object);

  switch (property_id) {
    case ARG_ULONG:
      self->val_ulong = g_value_get_ulong (value);
      break;
    case ARG_DOUBLE:
      self->val_double = g_value_get_double (value);
      break;
    case ARG_BOOLEAN:
      self->val_boolean = g_value_get_boolean (value);
      break;
    case ARG_CONSTRUCTONLY:
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gst_test_mono_source_class_init (GstTestMonoSourceClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->set_property = gst_test_mono_source_set_property;
  gobject_class->get_property = gst_test_mono_source_get_property;

  g_object_class_install_property (gobject_class, ARG_ULONG,
      g_param_spec_ulong ("ulong",
          "ulong prop",
          "ulong number parameter for the test_mono_source",
          0, G_MAXULONG, 0, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));

  g_object_class_install_property (gobject_class, ARG_DOUBLE,
      g_param_spec_double ("double",
          "double prop",
          "double number parameter for the test_mono_source",
          0.0, 100.0, 0.0, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));

  g_object_class_install_property (gobject_class, ARG_BOOLEAN,
      g_param_spec_boolean ("boolean",
          "boolean prop",
          "boolean parameter for the test_mono_source",
          FALSE, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));

  g_object_class_install_property (gobject_class, ARG_READONLY,
      g_param_spec_ulong ("readonly",
          "readonly prop",
          "readonly parameter for the test_mono_source",
          0, G_MAXULONG, 0, G_PARAM_READABLE | GST_PARAM_CONTROLLABLE));

  g_object_class_install_property (gobject_class, ARG_STATIC,
      g_param_spec_ulong ("static",
          "static prop",
          "static parameter for the test_mono_source",
          0, G_MAXULONG, 0, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_CONSTRUCTONLY,
      g_param_spec_ulong ("construct-only",
          "construct-only prop",
          "construct-only parameter for the test_mono_source",
          0, G_MAXULONG, 0, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

static void
gst_test_mono_source_base_init (GstTestMonoSourceClass * klass)
{
  static const GstElementDetails details = {
    "Monophonic source for unit tests",
    "Source/Audio/MonoSource",
    "Use in unit tests",
    "Stefan Kost <ensonic@users.sf.net>"
  };
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_details (element_class, &details);
}

GType
gst_test_mono_source_get_type (void)
{
  static GType type = 0;

  if (type == 0) {
    static const GTypeInfo info = {
      (guint16) sizeof (GstTestMonoSourceClass),
      (GBaseInitFunc) gst_test_mono_source_base_init,   // base_init
      NULL,                     // base_finalize
      (GClassInitFunc) gst_test_mono_source_class_init, // class_init
      NULL,                     // class_finalize
      NULL,                     // class_data
      (guint16) sizeof (GstTestMonoSource),
      0,                        // n_preallocs
      NULL,                     // instance_init
      NULL                      // value_table
    };
    type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstTestMonoSource", &info,
        0);
  }
  return type;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  gboolean res = TRUE;

  res &= gst_element_register (plugin, "testmonosource", GST_RANK_NONE,
      GST_TYPE_TEST_MONO_SOURCE);
  return res;
}

GST_PLUGIN_DEFINE_STATIC (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "gst-test",
    "controller test plugin - several unit test support elements",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);

/*
static void __attribute__ ((constructor))
_gst_plugin_static_init__plugin_init (void)
{
  static GstPluginDesc plugin_desc_ = {
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "gst-test",
    "controller test plugin - several unit test support elements",
    plugin_init,
    VERSION,
    GST_LICENSE,
    PACKAGE,
    GST_PACKAGE,
    GST_ORIGIN,
    GST_PADDING_INIT
  };
  _gst_plugin_register_static (&plugin_desc_);
}
*/
/* TESTS */
/* double init should not harm */
GST_START_TEST (controller_init)
{
  gst_controller_init (NULL, NULL);
}

GST_END_TEST;

/* tests for an element with no controlled params */
GST_START_TEST (controller_new_fail1)
{
  GstController *ctrl;
  GstElement *elem;

  elem = gst_element_factory_make ("fakesrc", "test_source");

  /* that property should not exist */
  ctrl = gst_controller_new (G_OBJECT (elem), "_schrompf_", NULL);
  fail_unless (ctrl == NULL, NULL);

  gst_object_unref (elem);
}

GST_END_TEST;

/* tests for an element with controlled params, but none given */
GST_START_TEST (controller_new_fail2)
{
  GstController *ctrl;
  GstElement *elem;

  elem = gst_element_factory_make ("testmonosource", "test_source");

  /* no property given */
  ctrl = gst_controller_new (G_OBJECT (elem), NULL);
  fail_unless (ctrl == NULL, NULL);

  gst_object_unref (elem);
}

GST_END_TEST;

/* tests for readonly params */
GST_START_TEST (controller_new_fail3)
{
  GstController *ctrl;
  GstElement *elem;

  elem = gst_element_factory_make ("testmonosource", "test_source");

  /* that property should exist and but is readonly */
  ASSERT_CRITICAL (ctrl =
      gst_controller_new (G_OBJECT (elem), "readonly", NULL));
  fail_unless (ctrl == NULL, NULL);

  gst_object_unref (elem);
}

GST_END_TEST;

/* tests for static params */
GST_START_TEST (controller_new_fail4)
{
  GstController *ctrl;
  GstElement *elem;

  elem = gst_element_factory_make ("testmonosource", "test_source");

  /* that property should exist and but is not controlable */
  ASSERT_CRITICAL (ctrl = gst_controller_new (G_OBJECT (elem), "static", NULL));
  fail_unless (ctrl == NULL, NULL);

  gst_object_unref (elem);
}

GST_END_TEST;

/* tests for static params */
GST_START_TEST (controller_new_fail5)
{
  GstController *ctrl;
  GstElement *elem;

  elem = gst_element_factory_make ("testmonosource", "test_source");

  /* that property should exist and but is construct-only */
  ASSERT_CRITICAL (ctrl =
      gst_controller_new (G_OBJECT (elem), "construct-only", NULL));
  fail_unless (ctrl == NULL, NULL);

  gst_object_unref (elem);
}

GST_END_TEST;


/* tests for an element with controlled params */
GST_START_TEST (controller_new_okay1)
{
  GstController *ctrl;
  GstElement *elem;

  elem = gst_element_factory_make ("testmonosource", "test_source");

  /* that property should exist and should be controllable */
  ctrl = gst_controller_new (G_OBJECT (elem), "ulong", NULL);
  fail_unless (ctrl != NULL, NULL);

  GST_INFO ("controller->ref_count=%d", G_OBJECT (ctrl)->ref_count);
  g_object_unref (ctrl);
  gst_object_unref (elem);
}

GST_END_TEST;

/* tests for an element with several controlled params */
GST_START_TEST (controller_new_okay2)
{
  GstController *ctrl;
  GstElement *elem;

  elem = gst_element_factory_make ("testmonosource", "test_source");

  /* that property should exist and should be controllable */
  ctrl = gst_controller_new (G_OBJECT (elem), "ulong", "double", NULL);
  fail_unless (ctrl != NULL, NULL);

  GST_INFO ("controller->ref_count=%d", G_OBJECT (ctrl)->ref_count);
  g_object_unref (ctrl);
  gst_object_unref (elem);
}

GST_END_TEST;

/* controlling several params should return the same controller */
GST_START_TEST (controller_new_okay3)
{
  GstController *ctrl1, *ctrl2;
  GstElement *elem;

  elem = gst_element_factory_make ("testmonosource", "test_source");

  /* that property should exist and should be controllable */
  ctrl1 = gst_controller_new (G_OBJECT (elem), "ulong", NULL);
  fail_unless (ctrl1 != NULL, NULL);

  /* that property should exist and should be controllable */
  ctrl2 = gst_controller_new (G_OBJECT (elem), "double", NULL);
  fail_unless (ctrl2 != NULL, NULL);
  fail_unless (ctrl1 == ctrl2, NULL);

  GST_INFO ("controller->ref_count=%d", G_OBJECT (ctrl1)->ref_count);
  g_object_unref (ctrl1);
  gst_object_unref (elem);
}

GST_END_TEST;

/* controlling a params twice should be handled */
GST_START_TEST (controller_param_twice)
{
  GstController *ctrl;
  GstElement *elem;
  gboolean res;

  elem = gst_element_factory_make ("testmonosource", "test_source");

  /* that property should exist and should be controllable */
  ctrl = gst_controller_new (G_OBJECT (elem), "ulong", "ulong", NULL);
  fail_unless (ctrl != NULL, NULL);

  /* it should have been added at least once, let remove it */
  res = gst_controller_remove_properties (ctrl, "ulong", NULL);
  fail_unless (res, NULL);

  /* removing it agian should not work */
  res = gst_controller_remove_properties (ctrl, "ulong", NULL);
  fail_unless (!res, NULL);

  GST_INFO ("controller->ref_count=%d", G_OBJECT (ctrl)->ref_count);
  g_object_unref (ctrl);
  gst_object_unref (elem);
}

GST_END_TEST;

/* tests if we cleanup properly */
GST_START_TEST (controller_finalize)
{
  GstController *ctrl;
  GstElement *elem;

  elem = gst_element_factory_make ("testmonosource", "test_source");

  /* that property should exist and should be controllable */
  ctrl = gst_controller_new (G_OBJECT (elem), "ulong", NULL);
  fail_unless (ctrl != NULL, NULL);

  /* free the controller */
  g_object_unref (ctrl);

  /* object shouldn't have a controller anymore */
  ctrl = gst_object_get_controller (G_OBJECT (elem));
  fail_unless (ctrl == NULL, NULL);

  gst_object_unref (elem);
}

GST_END_TEST;

/* test timed value handling without interpolation */
GST_START_TEST (controller_interpolate_none)
{
  GstController *ctrl;
  GstElement *elem;
  gboolean res;
  GValue val_ulong = { 0, };

  elem = gst_element_factory_make ("testmonosource", "test_source");

  /* that property should exist and should be controllable */
  ctrl = gst_controller_new (G_OBJECT (elem), "ulong", NULL);
  fail_unless (ctrl != NULL, NULL);

  /* set interpolation mode */
  gst_controller_set_interpolation_mode (ctrl, "ulong", GST_INTERPOLATE_NONE);

  /* set control values */
  g_value_init (&val_ulong, G_TYPE_ULONG);
  g_value_set_ulong (&val_ulong, 0);
  res = gst_controller_set (ctrl, "ulong", 0 * GST_SECOND, &val_ulong);
  fail_unless (res, NULL);
  g_value_set_ulong (&val_ulong, 100);
  res = gst_controller_set (ctrl, "ulong", 2 * GST_SECOND, &val_ulong);
  fail_unless (res, NULL);

  /* now pull in values for some timestamps */
  gst_controller_sync_values (ctrl, 0 * GST_SECOND);
  fail_unless (GST_TEST_MONO_SOURCE (elem)->val_ulong == 0, NULL);
  gst_controller_sync_values (ctrl, 1 * GST_SECOND);
  fail_unless (GST_TEST_MONO_SOURCE (elem)->val_ulong == 0, NULL);
  gst_controller_sync_values (ctrl, 2 * GST_SECOND);
  fail_unless (GST_TEST_MONO_SOURCE (elem)->val_ulong == 100, NULL);

  GST_INFO ("controller->ref_count=%d", G_OBJECT (ctrl)->ref_count);
  g_object_unref (ctrl);
  gst_object_unref (elem);
}

GST_END_TEST;

/* test timed value handling in trigger mode */
GST_START_TEST (controller_interpolate_trigger)
{
  GstController *ctrl;
  GstElement *elem;
  gboolean res;
  GValue val_ulong = { 0, };

  elem = gst_element_factory_make ("testmonosource", "test_source");

  /* that property should exist and should be controllable */
  ctrl = gst_controller_new (G_OBJECT (elem), "ulong", NULL);
  fail_unless (ctrl != NULL, NULL);

  /* set interpolation mode */
  gst_controller_set_interpolation_mode (ctrl, "ulong",
      GST_INTERPOLATE_TRIGGER);

  /* set control values */
  g_value_init (&val_ulong, G_TYPE_ULONG);
  g_value_set_ulong (&val_ulong, 50);
  res = gst_controller_set (ctrl, "ulong", 0 * GST_SECOND, &val_ulong);
  fail_unless (res, NULL);
  g_value_set_ulong (&val_ulong, 100);
  res = gst_controller_set (ctrl, "ulong", 2 * GST_SECOND, &val_ulong);
  fail_unless (res, NULL);

  /* now pull in values for some timestamps */
  gst_controller_sync_values (ctrl, 0 * GST_SECOND);
  fail_unless (GST_TEST_MONO_SOURCE (elem)->val_ulong == 50, NULL);
  GST_TEST_MONO_SOURCE (elem)->val_ulong = 0;
  gst_controller_sync_values (ctrl, 1 * GST_SECOND);
  fail_unless (GST_TEST_MONO_SOURCE (elem)->val_ulong == 0, NULL);
  gst_controller_sync_values (ctrl, 2 * GST_SECOND);
  fail_unless (GST_TEST_MONO_SOURCE (elem)->val_ulong == 100, NULL);

  GST_INFO ("controller->ref_count=%d", G_OBJECT (ctrl)->ref_count);
  g_object_unref (ctrl);
  gst_object_unref (elem);
}

GST_END_TEST;

/* test timed value handling with linear interpolation */
GST_START_TEST (controller_interpolate_linear)
{
  GstController *ctrl;
  GstElement *elem;
  gboolean res;
  GValue val_ulong = { 0, };

  elem = gst_element_factory_make ("testmonosource", "test_source");

  /* that property should exist and should be controllable */
  ctrl = gst_controller_new (G_OBJECT (elem), "ulong", NULL);
  fail_unless (ctrl != NULL, NULL);

  /* set interpolation mode */
  gst_controller_set_interpolation_mode (ctrl, "ulong", GST_INTERPOLATE_LINEAR);

  /* set control values */
  g_value_init (&val_ulong, G_TYPE_ULONG);
  g_value_set_ulong (&val_ulong, 0);
  res = gst_controller_set (ctrl, "ulong", 0 * GST_SECOND, &val_ulong);
  fail_unless (res, NULL);
  g_value_set_ulong (&val_ulong, 100);
  res = gst_controller_set (ctrl, "ulong", 2 * GST_SECOND, &val_ulong);
  fail_unless (res, NULL);

  /* now pull in values for some timestamps */
  gst_controller_sync_values (ctrl, 0 * GST_SECOND);
  fail_unless (GST_TEST_MONO_SOURCE (elem)->val_ulong == 0, NULL);
  gst_controller_sync_values (ctrl, 1 * GST_SECOND);
  fail_unless (GST_TEST_MONO_SOURCE (elem)->val_ulong == 50, NULL);
  gst_controller_sync_values (ctrl, 2 * GST_SECOND);
  fail_unless (GST_TEST_MONO_SOURCE (elem)->val_ulong == 100, NULL);

  GST_INFO ("controller->ref_count=%d", G_OBJECT (ctrl)->ref_count);
  g_object_unref (ctrl);
  gst_object_unref (elem);
}

GST_END_TEST;

/* tests if we can run helper methods against any GObject */
GST_START_TEST (controller_helper_any_gobject)
{
  GstElement *elem;
  gboolean res;

  elem = gst_element_factory_make ("bin", "test_elem");

  /* that element is not controllable */
  res = gst_object_sync_values (G_OBJECT (elem), 0LL);
  fail_unless (res == FALSE, NULL);

  gst_object_unref (elem);
}

GST_END_TEST;


Suite *
gst_controller_suite (void)
{
  Suite *s = suite_create ("Controller");
  TCase *tc = tcase_create ("general");

  suite_add_tcase (s, tc);
  tcase_add_test (tc, controller_init);
  tcase_add_test (tc, controller_new_fail1);
  tcase_add_test (tc, controller_new_fail2);
  tcase_add_test (tc, controller_new_fail3);
  tcase_add_test (tc, controller_new_fail4);
  tcase_add_test (tc, controller_new_fail5);
  tcase_add_test (tc, controller_new_okay1);
  tcase_add_test (tc, controller_new_okay2);
  tcase_add_test (tc, controller_new_okay3);
  tcase_add_test (tc, controller_param_twice);
  tcase_add_test (tc, controller_finalize);
  tcase_add_test (tc, controller_interpolate_none);
  tcase_add_test (tc, controller_interpolate_trigger);
  tcase_add_test (tc, controller_interpolate_linear);
  tcase_add_test (tc, controller_helper_any_gobject);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = gst_controller_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);
  gst_controller_init (NULL, NULL);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
