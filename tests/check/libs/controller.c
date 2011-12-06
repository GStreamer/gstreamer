/* GStreamer
 *
 * unit test for the controller library
 *
 * Copyright (C) <2005> Stefan Kost <ensonic at users dot sf dot net>
 * Copyright (C) <2006-2007> Sebastian Dröge <slomo@circular-chaos.org>
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
#include <gst/controller/gstinterpolationcontrolsource.h>
#include <gst/controller/gstlfocontrolsource.h>

/* LOCAL TEST ELEMENT */

enum
{
  ARG_ULONG = 1,
  ARG_FLOAT,
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
  gfloat val_float;
  gdouble val_double;
  gboolean val_boolean;
};
struct _GstTestMonoSourceClass
{
  GstElementClass parent_class;
};

static GType gst_test_mono_source_get_type (void);

static void
gst_test_mono_source_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec)
{
  GstTestMonoSource *self = GST_TEST_MONO_SOURCE (object);

  switch (property_id) {
    case ARG_ULONG:
      g_value_set_ulong (value, self->val_ulong);
      break;
    case ARG_FLOAT:
      g_value_set_float (value, self->val_float);
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
      GST_DEBUG ("test value ulong=%lu", self->val_ulong);
      break;
    case ARG_FLOAT:
      self->val_float = g_value_get_float (value);
      GST_DEBUG ("test value float=%f", self->val_float);
      break;
    case ARG_DOUBLE:
      self->val_double = g_value_get_double (value);
      GST_DEBUG ("test value double=%f", self->val_double);
      break;
    case ARG_BOOLEAN:
      self->val_boolean = g_value_get_boolean (value);
      GST_DEBUG ("test value boolean=%d", self->val_boolean);
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

  g_object_class_install_property (gobject_class, ARG_FLOAT,
      g_param_spec_float ("float",
          "float prop",
          "float number parameter for the test_mono_source",
          0.0, 100.0, 0.0, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));

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
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_details_simple (element_class,
      "Monophonic source for unit tests",
      "Source/Audio/MonoSource",
      "Use in unit tests", "Stefan Kost <ensonic@users.sf.net>");
}

static GType
gst_test_mono_source_get_type (void)
{
  static volatile gsize test_mono_source_type = 0;

  if (g_once_init_enter (&test_mono_source_type)) {
    GType type;
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
    g_once_init_leave (&test_mono_source_type, type);
  }
  return test_mono_source_type;
}

static void
setup (void)
{
  gst_element_register (NULL, "testmonosource", GST_RANK_NONE,
      GST_TYPE_TEST_MONO_SOURCE);
}

static void
teardown (void)
{
}


/* TESTS */

/* tests for an element with no controlled params */
GST_START_TEST (controller_new_fail1)
{
  GstElement *elem;
  GstInterpolationControlSource *cs;
  gboolean res;

  elem = gst_element_factory_make ("fakesrc", "test_source");
  cs = gst_interpolation_control_source_new ();

  /* that property should not exist */
  res = gst_object_set_control_source (GST_OBJECT (elem), "_schrompf_",
      GST_CONTROL_SOURCE (cs));
  fail_unless (res == FALSE, NULL);

  g_object_unref (cs);
  gst_object_unref (elem);
}

GST_END_TEST;

/* tests for readonly params */
GST_START_TEST (controller_new_fail2)
{
  GstElement *elem;
  GstInterpolationControlSource *cs;
  gboolean res;

  elem = gst_element_factory_make ("testmonosource", "test_source");
  cs = gst_interpolation_control_source_new ();

  /* that property should exist and but is readonly */
  ASSERT_CRITICAL (res = gst_object_set_control_source (GST_OBJECT (elem),
          "readonly", GST_CONTROL_SOURCE (cs)));
  fail_unless (res == FALSE, NULL);

  g_object_unref (cs);
  gst_object_unref (elem);
}

GST_END_TEST;

/* tests for static params */
GST_START_TEST (controller_new_fail3)
{
  GstElement *elem;
  GstInterpolationControlSource *cs;
  gboolean res;

  elem = gst_element_factory_make ("testmonosource", "test_source");
  cs = gst_interpolation_control_source_new ();

  /* that property should exist and but is not controlable */
  ASSERT_CRITICAL (res = gst_object_set_control_source (GST_OBJECT (elem),
          "static", GST_CONTROL_SOURCE (cs)));
  fail_unless (res == FALSE, NULL);

  g_object_unref (cs);
  gst_object_unref (elem);
}

GST_END_TEST;

/* tests for construct-only params */
GST_START_TEST (controller_new_fail4)
{
  GstElement *elem;
  GstInterpolationControlSource *cs;
  gboolean res;

  elem = gst_element_factory_make ("testmonosource", "test_source");
  cs = gst_interpolation_control_source_new ();

  /* that property should exist and but is construct-only */
  ASSERT_CRITICAL (res =
      gst_object_set_control_source (GST_OBJECT (elem), "construct-only",
          GST_CONTROL_SOURCE (cs)));
  fail_unless (res == FALSE, NULL);

  g_object_unref (cs);
  gst_object_unref (elem);
}

GST_END_TEST;


/* tests for an element with controlled params */
GST_START_TEST (controller_new_okay1)
{
  GstElement *elem;
  GstInterpolationControlSource *cs;
  gboolean res;

  elem = gst_element_factory_make ("testmonosource", "test_source");
  cs = gst_interpolation_control_source_new ();

  /* that property should exist and should be controllable */
  res = gst_object_set_control_source (GST_OBJECT (elem), "ulong",
      GST_CONTROL_SOURCE (cs));
  fail_unless (res == TRUE, NULL);

  g_object_unref (cs);
  gst_object_unref (elem);
}

GST_END_TEST;

/* tests for an element with several controlled params */
GST_START_TEST (controller_new_okay2)
{
  GstElement *elem;
  GstInterpolationControlSource *cs1, *cs2;
  gboolean res;

  elem = gst_element_factory_make ("testmonosource", "test_source");
  cs1 = gst_interpolation_control_source_new ();
  cs2 = gst_interpolation_control_source_new ();

  /* these properties should exist and should be controllable */
  res = gst_object_set_control_source (GST_OBJECT (elem), "ulong",
      GST_CONTROL_SOURCE (cs1));
  fail_unless (res == TRUE, NULL);

  res = gst_object_set_control_source (GST_OBJECT (elem), "boolean",
      GST_CONTROL_SOURCE (cs2));
  fail_unless (res == TRUE, NULL);

  g_object_unref (cs1);
  g_object_unref (cs2);
  gst_object_unref (elem);
}

GST_END_TEST;

/* controlling a param twice should be handled */
GST_START_TEST (controller_param_twice)
{
  GstElement *elem;
  GstInterpolationControlSource *cs;
  gboolean res;

  elem = gst_element_factory_make ("testmonosource", "test_source");
  cs = gst_interpolation_control_source_new ();

  /* that property should exist and should be controllable */
  res = gst_object_set_control_source (GST_OBJECT (elem), "ulong",
      GST_CONTROL_SOURCE (cs));
  fail_unless (res, NULL);

  /* setting it again should not work */
  res = gst_object_set_control_source (GST_OBJECT (elem), "ulong",
      GST_CONTROL_SOURCE (cs));
  fail_unless (!res, NULL);

  /* it should have been added at least once, let remove it */
  res = gst_object_set_control_source (GST_OBJECT (elem), "ulong", NULL);
  fail_unless (res, NULL);

  /* removing it again should not work */
  res = gst_object_set_control_source (GST_OBJECT (elem), "ulong", NULL);
  fail_unless (!res, NULL);

  g_object_unref (cs);
  gst_object_unref (elem);
}

GST_END_TEST;

/* tests if we cleanup properly */
GST_START_TEST (controller_controlsource_refcounts)
{
  GstElement *elem;
  GstControlSource *csource, *test_csource;

  elem = gst_element_factory_make ("testmonosource", "test_source");

  csource = (GstControlSource *) gst_interpolation_control_source_new ();
  fail_unless (csource != NULL, NULL);

  fail_unless_equals_int (G_OBJECT (csource)->ref_count, 1);
  fail_unless (gst_object_set_control_source (GST_OBJECT (elem), "ulong",
          csource));
  fail_unless_equals_int (G_OBJECT (csource)->ref_count, 2);

  test_csource = gst_object_get_control_source (GST_OBJECT (elem), "ulong");
  fail_unless (test_csource != NULL, NULL);
  fail_unless (test_csource == csource);
  fail_unless_equals_int (G_OBJECT (csource)->ref_count, 3);
  g_object_unref (test_csource);
  g_object_unref (csource);

  gst_object_unref (elem);
}

GST_END_TEST;

/* tests if we don't fail on empty interpolation controlsources */
GST_START_TEST (controller_controlsource_empty1)
{
  GstElement *elem;
  GstControlSource *csource;

  elem = gst_element_factory_make ("testmonosource", "test_source");

  csource = (GstControlSource *) gst_interpolation_control_source_new ();
  fail_unless (csource != NULL, NULL);

  fail_unless (gst_object_set_control_source (GST_OBJECT (elem), "ulong",
          csource));

  /* don't fail on empty control point lists */
  gst_object_sync_values (GST_OBJECT (elem), 0 * GST_SECOND);

  /* unref objects */
  g_object_unref (csource);
  gst_object_unref (elem);
}

GST_END_TEST;

/* tests if we don't fail on interpolation controlsources that are empty again */
GST_START_TEST (controller_controlsource_empty2)
{
  GstElement *elem;
  GstInterpolationControlSource *csource;
  GValue val = { 0, };

  elem = gst_element_factory_make ("testmonosource", "test_source");

  csource = gst_interpolation_control_source_new ();
  fail_unless (csource != NULL, NULL);

  fail_unless (gst_object_set_control_source (GST_OBJECT (elem), "ulong",
          (GstControlSource *) csource));

  /* set control values */
  g_value_init (&val, G_TYPE_ULONG);
  g_value_set_ulong (&val, 0);
  gst_interpolation_control_source_set (csource, 0 * GST_SECOND, &val);

  /* ... and unset the value */
  gst_interpolation_control_source_unset (csource, 0 * GST_SECOND);

  /* don't fail on empty control point lists */
  gst_object_sync_values (GST_OBJECT (elem), 0 * GST_SECOND);

  /* unref objects */
  g_object_unref (csource);
  gst_object_unref (elem);
}

GST_END_TEST;

/* test timed value handling without interpolation */
GST_START_TEST (controller_interpolate_none)
{
  GstInterpolationControlSource *csource;
  GstElement *elem;
  gboolean res;
  GValue val_ulong = { 0, };

  elem = gst_element_factory_make ("testmonosource", "test_source");

  /* Get interpolation control source */
  csource = gst_interpolation_control_source_new ();

  fail_unless (csource != NULL);
  fail_unless (gst_object_set_control_source (GST_OBJECT (elem), "ulong",
          GST_CONTROL_SOURCE (csource)));

  /* set interpolation mode */
  fail_unless (gst_interpolation_control_source_set_interpolation_mode (csource,
          GST_INTERPOLATE_NONE));

  fail_unless (gst_interpolation_control_source_get_count (csource) == 0);

  /* set control values */
  g_value_init (&val_ulong, G_TYPE_ULONG);
  g_value_set_ulong (&val_ulong, 0);
  res =
      gst_interpolation_control_source_set (csource, 0 * GST_SECOND,
      &val_ulong);
  fail_unless (res, NULL);
  fail_unless (gst_interpolation_control_source_get_count (csource) == 1);
  g_value_set_ulong (&val_ulong, 100);
  res =
      gst_interpolation_control_source_set (csource, 2 * GST_SECOND,
      &val_ulong);
  fail_unless (res, NULL);
  fail_unless (gst_interpolation_control_source_get_count (csource) == 2);

  g_object_unref (csource);

  /* now pull in values for some timestamps */
  gst_object_sync_values (GST_OBJECT (elem), 0 * GST_SECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 0);
  gst_object_sync_values (GST_OBJECT (elem), 1 * GST_SECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 0);
  gst_object_sync_values (GST_OBJECT (elem), 2 * GST_SECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 100);

  gst_object_unref (elem);
}

GST_END_TEST;

/* test timed value handling in trigger mode */
GST_START_TEST (controller_interpolate_trigger)
{
  GstInterpolationControlSource *csource;
  GstElement *elem;
  gboolean res;
  GValue val_ulong = { 0, };

  elem = gst_element_factory_make ("testmonosource", "test_source");

  /* Get interpolation control source */
  csource = gst_interpolation_control_source_new ();

  fail_unless (csource != NULL);
  fail_unless (gst_object_set_control_source (GST_OBJECT (elem), "ulong",
          GST_CONTROL_SOURCE (csource)));

  /* set interpolation mode */
  fail_unless (gst_interpolation_control_source_set_interpolation_mode (csource,
          GST_INTERPOLATE_TRIGGER));

  g_value_init (&val_ulong, G_TYPE_ULONG);
  fail_if (gst_control_source_get_value (GST_CONTROL_SOURCE (csource),
          0 * GST_SECOND, &val_ulong));

  /* set control values */
  g_value_set_ulong (&val_ulong, 50);
  res =
      gst_interpolation_control_source_set (csource, 0 * GST_SECOND,
      &val_ulong);
  fail_unless (res, NULL);
  g_value_set_ulong (&val_ulong, 100);
  res =
      gst_interpolation_control_source_set (csource, 2 * GST_SECOND,
      &val_ulong);
  fail_unless (res, NULL);


  /* now pull in values for some timestamps */
  fail_unless (gst_control_source_get_value (GST_CONTROL_SOURCE (csource),
          0 * GST_SECOND, &val_ulong));
  gst_object_sync_values (GST_OBJECT (elem), 0 * GST_SECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 50);
  fail_unless (gst_control_source_get_value (GST_CONTROL_SOURCE (csource),
          1 * GST_SECOND, &val_ulong));
  gst_object_sync_values (GST_OBJECT (elem), 1 * GST_SECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 0);
  fail_unless (gst_control_source_get_value (GST_CONTROL_SOURCE (csource),
          2 * GST_SECOND, &val_ulong));
  gst_object_sync_values (GST_OBJECT (elem), 2 * GST_SECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 100);

  g_object_unref (csource);
  gst_object_unref (elem);
}

GST_END_TEST;

/* test timed value handling with linear interpolation */
GST_START_TEST (controller_interpolate_linear)
{
  GstInterpolationControlSource *csource;
  GstElement *elem;
  gboolean res;
  GValue val_ulong = { 0, };

  elem = gst_element_factory_make ("testmonosource", "test_source");

  /* Get interpolation control source */
  csource = gst_interpolation_control_source_new ();

  fail_unless (csource != NULL);
  fail_unless (gst_object_set_control_source (GST_OBJECT (elem), "ulong",
          GST_CONTROL_SOURCE (csource)));

  /* set interpolation mode */
  fail_unless (gst_interpolation_control_source_set_interpolation_mode (csource,
          GST_INTERPOLATE_LINEAR));

  /* set control values */
  g_value_init (&val_ulong, G_TYPE_ULONG);
  g_value_set_ulong (&val_ulong, 0);
  res =
      gst_interpolation_control_source_set (csource, 0 * GST_SECOND,
      &val_ulong);
  fail_unless (res, NULL);
  g_value_set_ulong (&val_ulong, 100);
  res =
      gst_interpolation_control_source_set (csource, 2 * GST_SECOND,
      &val_ulong);
  fail_unless (res, NULL);

  g_object_unref (csource);

  /* now pull in values for some timestamps */
  gst_object_sync_values (GST_OBJECT (elem), 0 * GST_SECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 0);
  gst_object_sync_values (GST_OBJECT (elem), 1 * GST_SECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 50);
  gst_object_sync_values (GST_OBJECT (elem), 2 * GST_SECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 100);

  gst_object_unref (elem);
}

GST_END_TEST;

/* test timed value handling with cubic interpolation */
GST_START_TEST (controller_interpolate_cubic)
{
  GstInterpolationControlSource *csource;
  GstElement *elem;
  gboolean res;
  GValue val_double = { 0, };

  elem = gst_element_factory_make ("testmonosource", "test_source");

  /* Get interpolation control source */
  csource = gst_interpolation_control_source_new ();

  fail_unless (csource != NULL);
  fail_unless (gst_object_set_control_source (GST_OBJECT (elem), "double",
          GST_CONTROL_SOURCE (csource)));

  /* set interpolation mode */
  fail_unless (gst_interpolation_control_source_set_interpolation_mode (csource,
          GST_INTERPOLATE_CUBIC));

  /* set control values */
  g_value_init (&val_double, G_TYPE_DOUBLE);
  g_value_set_double (&val_double, 0.0);
  res =
      gst_interpolation_control_source_set (csource, 0 * GST_SECOND,
      &val_double);
  fail_unless (res, NULL);
  g_value_set_double (&val_double, 5.0);
  res =
      gst_interpolation_control_source_set (csource, 1 * GST_SECOND,
      &val_double);
  fail_unless (res, NULL);
  g_value_set_double (&val_double, 2.0);
  res =
      gst_interpolation_control_source_set (csource, 2 * GST_SECOND,
      &val_double);
  fail_unless (res, NULL);
  g_value_set_double (&val_double, 8.0);
  res =
      gst_interpolation_control_source_set (csource, 4 * GST_SECOND,
      &val_double);
  fail_unless (res, NULL);

  g_object_unref (csource);

  /* now pull in values for some timestamps */
  gst_object_sync_values (GST_OBJECT (elem), 0 * GST_SECOND);
  fail_unless_equals_float (GST_TEST_MONO_SOURCE (elem)->val_double, 0.0);
  gst_object_sync_values (GST_OBJECT (elem), 1 * GST_SECOND);
  fail_unless_equals_float (GST_TEST_MONO_SOURCE (elem)->val_double, 5.0);
  gst_object_sync_values (GST_OBJECT (elem), 2 * GST_SECOND);
  fail_unless_equals_float (GST_TEST_MONO_SOURCE (elem)->val_double, 2.0);
  gst_object_sync_values (GST_OBJECT (elem), 3 * GST_SECOND);
  fail_unless (GST_TEST_MONO_SOURCE (elem)->val_double > 2.0 &&
      GST_TEST_MONO_SOURCE (elem)->val_double < 8.0, NULL);
  gst_object_sync_values (GST_OBJECT (elem), 4 * GST_SECOND);
  fail_unless_equals_float (GST_TEST_MONO_SOURCE (elem)->val_double, 8.0);
  gst_object_sync_values (GST_OBJECT (elem), 5 * GST_SECOND);
  fail_unless_equals_float (GST_TEST_MONO_SOURCE (elem)->val_double, 8.0);

  gst_object_unref (elem);
}

GST_END_TEST;

/* test timed value handling with cubic interpolation */
GST_START_TEST (controller_interpolate_cubic_too_few_cp)
{
  GstInterpolationControlSource *csource;
  GstElement *elem;
  gboolean res;
  GValue val_double = { 0, };

  elem = gst_element_factory_make ("testmonosource", "test_source");

  /* Get interpolation control source */
  csource = gst_interpolation_control_source_new ();

  fail_unless (csource != NULL);
  fail_unless (gst_object_set_control_source (GST_OBJECT (elem), "double",
          GST_CONTROL_SOURCE (csource)));

  /* set interpolation mode */
  fail_unless (gst_interpolation_control_source_set_interpolation_mode (csource,
          GST_INTERPOLATE_CUBIC));

  /* set 2 control values */
  g_value_init (&val_double, G_TYPE_DOUBLE);
  g_value_set_double (&val_double, 0.0);
  res =
      gst_interpolation_control_source_set (csource, 0 * GST_SECOND,
      &val_double);
  fail_unless (res, NULL);
  g_value_set_double (&val_double, 4.0);
  res =
      gst_interpolation_control_source_set (csource, 2 * GST_SECOND,
      &val_double);
  fail_unless (res, NULL);

  g_object_unref (csource);

  /* now pull in values for some timestamps and verify that it used linear
   * interpolation as we don't gave enough control points
   */
  gst_object_sync_values (GST_OBJECT (elem), 0 * GST_SECOND);
  fail_unless_equals_float (GST_TEST_MONO_SOURCE (elem)->val_double, 0.0);
  gst_object_sync_values (GST_OBJECT (elem), 1 * GST_SECOND);
  fail_unless_equals_float (GST_TEST_MONO_SOURCE (elem)->val_double, 2.0);
  gst_object_sync_values (GST_OBJECT (elem), 2 * GST_SECOND);
  fail_unless_equals_float (GST_TEST_MONO_SOURCE (elem)->val_double, 4.0);

  gst_object_unref (elem);
}

GST_END_TEST;

/* make sure we don't crash when someone sets an unsupported interpolation
 * mode */
GST_START_TEST (controller_interpolate_unimplemented)
{
  GstInterpolationControlSource *csource;
  GstElement *elem;

  elem = gst_element_factory_make ("testmonosource", "test_source");

  /* Get interpolation control source */
  csource = gst_interpolation_control_source_new ();

  fail_unless (csource != NULL);
  fail_unless (gst_object_set_control_source (GST_OBJECT (elem), "ulong",
          GST_CONTROL_SOURCE (csource)));

  /* set completely bogus interpolation mode */
  fail_if (gst_interpolation_control_source_set_interpolation_mode (csource,
          (GstInterpolateMode) 93871));

  g_object_unref (csource);

  gst_object_unref (elem);
}

GST_END_TEST;

/* test _unset() */
GST_START_TEST (controller_interpolation_unset)
{
  GstInterpolationControlSource *csource;
  GstElement *elem;
  gboolean res;
  GValue val_ulong = { 0, };

  elem = gst_element_factory_make ("testmonosource", "test_source");

  /* Get interpolation control source */
  csource = gst_interpolation_control_source_new ();

  fail_unless (csource != NULL);
  fail_unless (gst_object_set_control_source (GST_OBJECT (elem), "ulong",
          GST_CONTROL_SOURCE (csource)));

  /* set interpolation mode */
  fail_unless (gst_interpolation_control_source_set_interpolation_mode (csource,
          GST_INTERPOLATE_NONE));

  /* set control values */
  g_value_init (&val_ulong, G_TYPE_ULONG);
  g_value_set_ulong (&val_ulong, 0);
  res =
      gst_interpolation_control_source_set (csource, 0 * GST_SECOND,
      &val_ulong);
  fail_unless (res, NULL);
  g_value_set_ulong (&val_ulong, 100);
  res =
      gst_interpolation_control_source_set (csource, 1 * GST_SECOND,
      &val_ulong);
  fail_unless (res, NULL);
  g_value_set_ulong (&val_ulong, 50);
  res =
      gst_interpolation_control_source_set (csource, 2 * GST_SECOND,
      &val_ulong);
  fail_unless (res, NULL);

  /* verify values */
  gst_object_sync_values (GST_OBJECT (elem), 0 * GST_SECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 0);
  gst_object_sync_values (GST_OBJECT (elem), 1 * GST_SECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 100);
  gst_object_sync_values (GST_OBJECT (elem), 2 * GST_SECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 50);

  /* unset second */
  res = gst_interpolation_control_source_unset (csource, 1 * GST_SECOND);
  fail_unless (res, NULL);

  /* verify value again */
  gst_object_sync_values (GST_OBJECT (elem), 1 * GST_SECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 0);
  gst_object_sync_values (GST_OBJECT (elem), 2 * GST_SECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 50);

  /* unset all values, reset and try to unset again */
  fail_unless (gst_interpolation_control_source_unset (csource,
          0 * GST_SECOND));
  fail_unless (gst_interpolation_control_source_unset (csource,
          2 * GST_SECOND));
  gst_interpolation_control_source_unset_all (csource);
  fail_if (gst_interpolation_control_source_unset (csource, 2 * GST_SECOND));

  g_object_unref (csource);

  gst_object_unref (elem);
}

GST_END_TEST;

/* test _unset_all() */
GST_START_TEST (controller_interpolation_unset_all)
{
  GstInterpolationControlSource *csource;
  GstElement *elem;
  gboolean res;
  GValue val_ulong = { 0, };

  elem = gst_element_factory_make ("testmonosource", "test_source");

  /* Get interpolation control source */
  csource = gst_interpolation_control_source_new ();

  fail_unless (csource != NULL);
  fail_unless (gst_object_set_control_source (GST_OBJECT (elem), "ulong",
          GST_CONTROL_SOURCE (csource)));

  /* set interpolation mode */
  fail_unless (gst_interpolation_control_source_set_interpolation_mode (csource,
          GST_INTERPOLATE_NONE));

  /* set control values */
  g_value_init (&val_ulong, G_TYPE_ULONG);
  g_value_set_ulong (&val_ulong, 0);
  res =
      gst_interpolation_control_source_set (csource, 0 * GST_SECOND,
      &val_ulong);
  fail_unless (res, NULL);
  g_value_set_ulong (&val_ulong, 100);
  res =
      gst_interpolation_control_source_set (csource, 1 * GST_SECOND,
      &val_ulong);
  fail_unless (res, NULL);

  /* verify values */
  gst_object_sync_values (GST_OBJECT (elem), 0 * GST_SECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 0);
  gst_object_sync_values (GST_OBJECT (elem), 1 * GST_SECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 100);

  /* unset all */
  gst_interpolation_control_source_unset_all (csource);

  g_object_unref (csource);

  /* verify value again */
  gst_object_sync_values (GST_OBJECT (elem), 1 * GST_SECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 0);

  gst_object_unref (elem);
}

GST_END_TEST;

/* test retrieval of an array of values with get_value_array() */
GST_START_TEST (controller_interpolation_linear_value_array)
{
  GstInterpolationControlSource *csource;
  GstElement *elem;
  gboolean res;
  GValue val_ulong = { 0, };
  gulong *values;

  elem = gst_element_factory_make ("testmonosource", "test_source");

  /* Get interpolation control source */
  csource = gst_interpolation_control_source_new ();

  fail_unless (csource != NULL);
  fail_unless (gst_object_set_control_source (GST_OBJECT (elem), "ulong",
          GST_CONTROL_SOURCE (csource)));

  /* set interpolation mode */
  fail_unless (gst_interpolation_control_source_set_interpolation_mode (csource,
          GST_INTERPOLATE_LINEAR));

  /* set control values */
  g_value_init (&val_ulong, G_TYPE_ULONG);
  g_value_set_ulong (&val_ulong, 0);
  res =
      gst_interpolation_control_source_set (csource, 0 * GST_SECOND,
      &val_ulong);
  fail_unless (res, NULL);
  g_value_set_ulong (&val_ulong, 100);
  res =
      gst_interpolation_control_source_set (csource, 2 * GST_SECOND,
      &val_ulong);
  fail_unless (res, NULL);

  /* now pull in values for some timestamps */
  values = g_new (gulong, 3);

  fail_unless (gst_control_source_get_value_array (GST_CONTROL_SOURCE (csource),
          0, GST_SECOND, 3, (gpointer) values));
  fail_unless_equals_int ((values)[0], 0);
  fail_unless_equals_int ((values)[1], 50);
  fail_unless_equals_int ((values)[2], 100);

  g_object_unref (csource);

  g_free (values);
  gst_object_unref (elem);
}

GST_END_TEST;

/* test if values below minimum and above maximum are clipped */
GST_START_TEST (controller_interpolation_linear_invalid_values)
{
  GstInterpolationControlSource *csource;
  GstElement *elem;
  gboolean res;
  GValue val_float = { 0, };

  elem = gst_element_factory_make ("testmonosource", "test_source");

  /* Get interpolation control source */
  csource = gst_interpolation_control_source_new ();

  fail_unless (csource != NULL);
  fail_unless (gst_object_set_control_source (GST_OBJECT (elem), "float",
          GST_CONTROL_SOURCE (csource)));

  /* set interpolation mode */
  fail_unless (gst_interpolation_control_source_set_interpolation_mode (csource,
          GST_INTERPOLATE_LINEAR));

  /* set control values */
  g_value_init (&val_float, G_TYPE_FLOAT);
  g_value_set_float (&val_float, 200.0);
  res =
      gst_interpolation_control_source_set (csource, 0 * GST_SECOND,
      &val_float);
  fail_unless (res, NULL);
  g_value_set_float (&val_float, -200.0);
  res =
      gst_interpolation_control_source_set (csource, 4 * GST_SECOND,
      &val_float);
  fail_unless (res, NULL);

  g_object_unref (csource);

  /* now pull in values for some timestamps and see if clipping works */
  /* 200.0 */
  gst_object_sync_values (GST_OBJECT (elem), 0 * GST_SECOND);
  fail_unless_equals_float (GST_TEST_MONO_SOURCE (elem)->val_float, 100.0);
  /* 100.0 */
  gst_object_sync_values (GST_OBJECT (elem), 1 * GST_SECOND);
  fail_unless_equals_float (GST_TEST_MONO_SOURCE (elem)->val_float, 100.0);
  /* 50.0 */
  gst_object_sync_values (GST_OBJECT (elem),
      1 * GST_SECOND + 500 * GST_MSECOND);
  fail_unless_equals_float (GST_TEST_MONO_SOURCE (elem)->val_float, 50.0);
  /* 0.0 */
  gst_object_sync_values (GST_OBJECT (elem), 2 * GST_SECOND);
  fail_unless_equals_float (GST_TEST_MONO_SOURCE (elem)->val_float, 0.0);
  /* -100.0 */
  gst_object_sync_values (GST_OBJECT (elem), 3 * GST_SECOND);
  fail_unless_equals_float (GST_TEST_MONO_SOURCE (elem)->val_float, 0.0);
  /* -200.0 */
  gst_object_sync_values (GST_OBJECT (elem), 4 * GST_SECOND);
  fail_unless_equals_float (GST_TEST_MONO_SOURCE (elem)->val_float, 0.0);

  gst_object_unref (elem);
}

GST_END_TEST;

GST_START_TEST (controller_interpolation_linear_default_values)
{
  GstInterpolationControlSource *csource;
  GstElement *elem;
  gboolean res;
  GValue val_ulong = { 0, };

  elem = gst_element_factory_make ("testmonosource", "test_source");

  /* Get interpolation control source */
  csource = gst_interpolation_control_source_new ();

  fail_unless (csource != NULL);
  fail_unless (gst_object_set_control_source (GST_OBJECT (elem), "ulong",
          GST_CONTROL_SOURCE (csource)));

  /* set interpolation mode */
  fail_unless (gst_interpolation_control_source_set_interpolation_mode (csource,
          GST_INTERPOLATE_LINEAR));

  g_value_init (&val_ulong, G_TYPE_ULONG);

  /* Should fail if no value was set yet
   * FIXME: will not fail, as interpolation assumes val[0]=default_value if
   * nothing else is set.
   fail_if (gst_control_source_get_value (GST_CONTROL_SOURCE (csource),
   1 * GST_SECOND, &val_ulong));
   */

  /* set control values */
  g_value_set_ulong (&val_ulong, 0);
  res =
      gst_interpolation_control_source_set (csource, 1 * GST_SECOND,
      &val_ulong);
  fail_unless (res, NULL);
  g_value_set_ulong (&val_ulong, 100);
  res =
      gst_interpolation_control_source_set (csource, 3 * GST_SECOND,
      &val_ulong);
  fail_unless (res, NULL);

  /* now pull in values for some timestamps */
  /* should give the value of the first control point for timestamps before it */
  gst_object_sync_values (GST_OBJECT (elem), 0 * GST_SECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 0);
  gst_object_sync_values (GST_OBJECT (elem), 1 * GST_SECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 0);
  gst_object_sync_values (GST_OBJECT (elem), 2 * GST_SECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 50);
  gst_object_sync_values (GST_OBJECT (elem), 3 * GST_SECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 100);

  /* set control values */
  g_value_set_ulong (&val_ulong, 0);
  res =
      gst_interpolation_control_source_set (csource, 0 * GST_SECOND,
      &val_ulong);
  fail_unless (res, NULL);
  g_value_set_ulong (&val_ulong, 100);
  res =
      gst_interpolation_control_source_set (csource, 2 * GST_SECOND,
      &val_ulong);
  fail_unless (res, NULL);

  /* unset the old ones */
  res = gst_interpolation_control_source_unset (csource, 1 * GST_SECOND);
  fail_unless (res, NULL);
  res = gst_interpolation_control_source_unset (csource, 3 * GST_SECOND);
  fail_unless (res, NULL);

  /* now pull in values for some timestamps */
  /* should now give our value for timestamp 0 */
  gst_object_sync_values (GST_OBJECT (elem), 0 * GST_SECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 0);
  gst_object_sync_values (GST_OBJECT (elem), 1 * GST_SECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 50);
  gst_object_sync_values (GST_OBJECT (elem), 2 * GST_SECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 100);

  g_object_unref (csource);

  gst_object_unref (elem);
}

GST_END_TEST;

/* test gst_controller_set_disabled() with linear interpolation */
GST_START_TEST (controller_interpolate_linear_disabled)
{
  GstInterpolationControlSource *csource, *csource2;
  GstElement *elem;
  gboolean res;
  GValue val_ulong = { 0, }
  , val_double = {
  0,};

  elem = gst_element_factory_make ("testmonosource", "test_source");

  /* Get interpolation control source */
  csource = gst_interpolation_control_source_new ();
  csource2 = gst_interpolation_control_source_new ();

  fail_unless (csource != NULL);
  fail_unless (gst_object_set_control_source (GST_OBJECT (elem), "ulong",
          GST_CONTROL_SOURCE (csource)));
  fail_unless (csource2 != NULL);
  fail_unless (gst_object_set_control_source (GST_OBJECT (elem), "double",
          GST_CONTROL_SOURCE (csource2)));

  /* set interpolation mode */
  fail_unless (gst_interpolation_control_source_set_interpolation_mode (csource,
          GST_INTERPOLATE_LINEAR));
  fail_unless (gst_interpolation_control_source_set_interpolation_mode
      (csource2, GST_INTERPOLATE_LINEAR));

  /* set control values */
  g_value_init (&val_ulong, G_TYPE_ULONG);
  g_value_set_ulong (&val_ulong, 0);
  res =
      gst_interpolation_control_source_set (csource, 0 * GST_SECOND,
      &val_ulong);
  fail_unless (res, NULL);
  g_value_set_ulong (&val_ulong, 100);
  res =
      gst_interpolation_control_source_set (csource, 2 * GST_SECOND,
      &val_ulong);
  fail_unless (res, NULL);

  g_object_unref (csource);

/* set control values */
  g_value_init (&val_double, G_TYPE_DOUBLE);
  g_value_set_double (&val_double, 2.0);
  res =
      gst_interpolation_control_source_set (csource2, 0 * GST_SECOND,
      &val_double);
  fail_unless (res, NULL);
  g_value_set_double (&val_double, 4.0);
  res =
      gst_interpolation_control_source_set (csource2, 2 * GST_SECOND,
      &val_double);
  fail_unless (res, NULL);

  g_object_unref (G_OBJECT (csource2));

  /* now pull in values for some timestamps */
  gst_object_sync_values (GST_OBJECT (elem), 0 * GST_SECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 0);
  fail_unless (GST_TEST_MONO_SOURCE (elem)->val_double == 2.0, NULL);
  gst_object_sync_values (GST_OBJECT (elem), 1 * GST_SECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 50);
  fail_unless (GST_TEST_MONO_SOURCE (elem)->val_double == 3.0, NULL);
  gst_object_sync_values (GST_OBJECT (elem), 2 * GST_SECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 100);
  fail_unless (GST_TEST_MONO_SOURCE (elem)->val_double == 4.0, NULL);

  /* now pull in values for some timestamps, prop double disabled */
  GST_TEST_MONO_SOURCE (elem)->val_ulong = 0;
  GST_TEST_MONO_SOURCE (elem)->val_double = 0.0;
  gst_object_set_controlled_property_disabled (GST_OBJECT (elem), "double",
      TRUE);
  gst_object_sync_values (GST_OBJECT (elem), 0 * GST_SECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 0);
  fail_unless (GST_TEST_MONO_SOURCE (elem)->val_double == 0.0, NULL);
  gst_object_sync_values (GST_OBJECT (elem), 1 * GST_SECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 50);
  fail_unless (GST_TEST_MONO_SOURCE (elem)->val_double == 0.0, NULL);
  gst_object_sync_values (GST_OBJECT (elem), 2 * GST_SECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 100);
  fail_unless (GST_TEST_MONO_SOURCE (elem)->val_double == 0.0, NULL);

  /* now pull in values for some timestamps, after enabling double again */
  GST_TEST_MONO_SOURCE (elem)->val_ulong = 0;
  GST_TEST_MONO_SOURCE (elem)->val_double = 0.0;
  gst_object_set_controlled_property_disabled (GST_OBJECT (elem), "double",
      FALSE);
  gst_object_sync_values (GST_OBJECT (elem), 0 * GST_SECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 0);
  fail_unless (GST_TEST_MONO_SOURCE (elem)->val_double == 2.0, NULL);
  gst_object_sync_values (GST_OBJECT (elem), 1 * GST_SECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 50);
  fail_unless (GST_TEST_MONO_SOURCE (elem)->val_double == 3.0, NULL);
  gst_object_sync_values (GST_OBJECT (elem), 2 * GST_SECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 100);
  fail_unless (GST_TEST_MONO_SOURCE (elem)->val_double == 4.0, NULL);

  /* now pull in values for some timestamps, after disabling all props */
  GST_TEST_MONO_SOURCE (elem)->val_ulong = 0;
  GST_TEST_MONO_SOURCE (elem)->val_double = 0.0;
  gst_object_set_controlled_properties_disabled (GST_OBJECT (elem), TRUE);
  gst_object_sync_values (GST_OBJECT (elem), 0 * GST_SECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 0);
  fail_unless (GST_TEST_MONO_SOURCE (elem)->val_double == 0.0, NULL);
  gst_object_sync_values (GST_OBJECT (elem), 1 * GST_SECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 0);
  fail_unless (GST_TEST_MONO_SOURCE (elem)->val_double == 0.0, NULL);
  gst_object_sync_values (GST_OBJECT (elem), 2 * GST_SECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 0);
  fail_unless (GST_TEST_MONO_SOURCE (elem)->val_double == 0.0, NULL);

  /* now pull in values for some timestamps, enabling double again */
  GST_TEST_MONO_SOURCE (elem)->val_ulong = 0;
  GST_TEST_MONO_SOURCE (elem)->val_double = 0.0;
  gst_object_set_controlled_property_disabled (GST_OBJECT (elem), "double",
      FALSE);
  gst_object_sync_values (GST_OBJECT (elem), 0 * GST_SECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 0);
  fail_unless (GST_TEST_MONO_SOURCE (elem)->val_double == 2.0, NULL);
  gst_object_sync_values (GST_OBJECT (elem), 1 * GST_SECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 0);
  fail_unless (GST_TEST_MONO_SOURCE (elem)->val_double == 3.0, NULL);
  gst_object_sync_values (GST_OBJECT (elem), 2 * GST_SECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 0);
  fail_unless (GST_TEST_MONO_SOURCE (elem)->val_double == 4.0, NULL);

  /* now pull in values for some timestamps, enabling all */
  GST_TEST_MONO_SOURCE (elem)->val_ulong = 0;
  GST_TEST_MONO_SOURCE (elem)->val_double = 0.0;
  gst_object_set_controlled_properties_disabled (GST_OBJECT (elem), FALSE);
  gst_object_sync_values (GST_OBJECT (elem), 0 * GST_SECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 0);
  fail_unless (GST_TEST_MONO_SOURCE (elem)->val_double == 2.0, NULL);
  gst_object_sync_values (GST_OBJECT (elem), 1 * GST_SECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 50);
  fail_unless (GST_TEST_MONO_SOURCE (elem)->val_double == 3.0, NULL);
  gst_object_sync_values (GST_OBJECT (elem), 2 * GST_SECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 100);
  fail_unless (GST_TEST_MONO_SOURCE (elem)->val_double == 4.0, NULL);

  gst_object_unref (elem);
}

GST_END_TEST;


GST_START_TEST (controller_interpolation_set_from_list)
{
  GstInterpolationControlSource *csource;
  GstTimedValue *tval;
  GstElement *elem;
  GSList *list = NULL;

  /* test that an invalid timestamp throws a warning of some sort */
  elem = gst_element_factory_make ("testmonosource", "test_source");

  /* Get interpolation control source */
  csource = gst_interpolation_control_source_new ();

  fail_unless (csource != NULL);
  fail_unless (gst_object_set_control_source (GST_OBJECT (elem), "ulong",
          GST_CONTROL_SOURCE (csource)));

  /* set interpolation mode */
  fail_unless (gst_interpolation_control_source_set_interpolation_mode (csource,
          GST_INTERPOLATE_LINEAR));

  /* set control value */
  tval = g_new0 (GstTimedValue, 1);
  tval->timestamp = GST_CLOCK_TIME_NONE;
  g_value_init (&tval->value, G_TYPE_ULONG);
  g_value_set_ulong (&tval->value, 0);

  list = g_slist_append (list, tval);

  fail_if (gst_interpolation_control_source_set_from_list (csource, list));

  /* try again with a valid stamp, should work now */
  tval->timestamp = 0;
  fail_unless (gst_interpolation_control_source_set_from_list (csource, list));

  g_object_unref (csource);

  /* allocated GstTimedValue now belongs to the controller, but list not */
  g_value_unset (&tval->value);
  g_free (tval);
  g_slist_free (list);
  gst_object_unref (elem);
}

GST_END_TEST;

/* test lfo control source with sine waveform */
GST_START_TEST (controller_lfo_sine)
{
  GstLFOControlSource *csource;
  GstElement *elem;
  GValue amp = { 0, };
  GValue off = { 0, };

  elem = gst_element_factory_make ("testmonosource", "test_source");

  /* Get interpolation control source */
  csource = gst_lfo_control_source_new ();

  fail_unless (csource != NULL);
  fail_unless (gst_object_set_control_source (GST_OBJECT (elem), "ulong",
          GST_CONTROL_SOURCE (csource)));

  /* set amplitude and offset values */
  g_value_init (&amp, G_TYPE_ULONG);
  g_value_init (&off, G_TYPE_ULONG);
  g_value_set_ulong (&amp, 100);
  g_value_set_ulong (&off, 100);

  /* set waveform mode */
  g_object_set (csource, "waveform", GST_LFO_WAVEFORM_SINE,
      "frequency", 1.0, "timeshift", (GstClockTime) 0,
      "amplitude", &amp, "offset", &off, NULL);

  g_object_unref (csource);

  /* now pull in values for some timestamps */
  gst_object_sync_values (GST_OBJECT (elem), 0 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 100);
  gst_object_sync_values (GST_OBJECT (elem), 250 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 200);
  gst_object_sync_values (GST_OBJECT (elem), 500 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 100);
  gst_object_sync_values (GST_OBJECT (elem), 750 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 0);
  gst_object_sync_values (GST_OBJECT (elem), 1000 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 100);
  gst_object_sync_values (GST_OBJECT (elem), 1250 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 200);
  gst_object_sync_values (GST_OBJECT (elem), 1500 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 100);
  gst_object_sync_values (GST_OBJECT (elem), 1750 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 0);
  gst_object_sync_values (GST_OBJECT (elem), 2000 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 100);
  gst_object_sync_values (GST_OBJECT (elem), 1250 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 200);
  gst_object_sync_values (GST_OBJECT (elem), 1500 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 100);
  gst_object_sync_values (GST_OBJECT (elem), 1750 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 0);

  gst_object_unref (elem);
}

GST_END_TEST;

/* test lfo control source with sine waveform and timeshift */
GST_START_TEST (controller_lfo_sine_timeshift)
{
  GstLFOControlSource *csource;
  GstElement *elem;
  GValue amp = { 0, };
  GValue off = { 0, };

  elem = gst_element_factory_make ("testmonosource", "test_source");

  /* Get interpolation control source */
  csource = gst_lfo_control_source_new ();

  fail_unless (csource != NULL);
  fail_unless (gst_object_set_control_source (GST_OBJECT (elem), "ulong",
          GST_CONTROL_SOURCE (csource)));

  /* set amplitude and offset values */
  g_value_init (&amp, G_TYPE_ULONG);
  g_value_init (&off, G_TYPE_ULONG);
  g_value_set_ulong (&amp, 100);
  g_value_set_ulong (&off, 100);

  /* set waveform mode */
  g_object_set (csource, "waveform", GST_LFO_WAVEFORM_SINE,
      "frequency", 1.0, "timeshift", 250 * GST_MSECOND,
      "amplitude", &amp, "offset", &off, NULL);

  g_object_unref (csource);

/* now pull in values for some timestamps */
  gst_object_sync_values (GST_OBJECT (elem), 0 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 0);
  gst_object_sync_values (GST_OBJECT (elem), 250 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 100);
  gst_object_sync_values (GST_OBJECT (elem), 500 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 200);
  gst_object_sync_values (GST_OBJECT (elem), 750 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 100);
  gst_object_sync_values (GST_OBJECT (elem), 1000 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 0);
  gst_object_sync_values (GST_OBJECT (elem), 1250 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 100);
  gst_object_sync_values (GST_OBJECT (elem), 1500 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 200);
  gst_object_sync_values (GST_OBJECT (elem), 1750 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 100);
  gst_object_sync_values (GST_OBJECT (elem), 2000 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 0);
  gst_object_sync_values (GST_OBJECT (elem), 1250 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 100);
  gst_object_sync_values (GST_OBJECT (elem), 1500 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 200);
  gst_object_sync_values (GST_OBJECT (elem), 1750 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 100);

  gst_object_unref (elem);
}

GST_END_TEST;

/* test lfo control source with square waveform */
GST_START_TEST (controller_lfo_square)
{
  GstLFOControlSource *csource;
  GstElement *elem;
  GValue amp = { 0, };
  GValue off = { 0, };

  elem = gst_element_factory_make ("testmonosource", "test_source");

  /* Get interpolation control source */
  csource = gst_lfo_control_source_new ();

  fail_unless (csource != NULL);
  fail_unless (gst_object_set_control_source (GST_OBJECT (elem), "ulong",
          GST_CONTROL_SOURCE (csource)));

  /* set amplitude and offset values */
  g_value_init (&amp, G_TYPE_ULONG);
  g_value_init (&off, G_TYPE_ULONG);
  g_value_set_ulong (&amp, 100);
  g_value_set_ulong (&off, 100);

  /* set waveform mode */
  g_object_set (csource, "waveform", GST_LFO_WAVEFORM_SQUARE,
      "frequency", 1.0, "timeshift", (GstClockTime) 0,
      "amplitude", &amp, "offset", &off, NULL);

  g_object_unref (csource);

  /* now pull in values for some timestamps */
  gst_object_sync_values (GST_OBJECT (elem), 0 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 0);
  gst_object_sync_values (GST_OBJECT (elem), 250 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 0);
  gst_object_sync_values (GST_OBJECT (elem), 500 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 200);
  gst_object_sync_values (GST_OBJECT (elem), 750 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 200);
  gst_object_sync_values (GST_OBJECT (elem), 1000 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 0);
  gst_object_sync_values (GST_OBJECT (elem), 1250 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 0);
  gst_object_sync_values (GST_OBJECT (elem), 1500 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 200);
  gst_object_sync_values (GST_OBJECT (elem), 1750 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 200);
  gst_object_sync_values (GST_OBJECT (elem), 2000 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 0);
  gst_object_sync_values (GST_OBJECT (elem), 1250 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 0);
  gst_object_sync_values (GST_OBJECT (elem), 1500 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 200);
  gst_object_sync_values (GST_OBJECT (elem), 1750 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 200);

  gst_object_unref (elem);
}

GST_END_TEST;

/* test lfo control source with saw waveform */
GST_START_TEST (controller_lfo_saw)
{
  GstLFOControlSource *csource;
  GstElement *elem;
  GValue amp = { 0, };
  GValue off = { 0, };

  elem = gst_element_factory_make ("testmonosource", "test_source");

  /* Get interpolation control source */
  csource = gst_lfo_control_source_new ();

  fail_unless (csource != NULL);
  fail_unless (gst_object_set_control_source (GST_OBJECT (elem), "ulong",
          GST_CONTROL_SOURCE (csource)));

  /* set amplitude and offset values */
  g_value_init (&amp, G_TYPE_ULONG);
  g_value_init (&off, G_TYPE_ULONG);
  g_value_set_ulong (&amp, 100);
  g_value_set_ulong (&off, 100);

  /* set waveform mode */
  g_object_set (csource, "waveform", GST_LFO_WAVEFORM_SAW,
      "frequency", 1.0, "timeshift", (GstClockTime) 0,
      "amplitude", &amp, "offset", &off, NULL);

  g_object_unref (csource);

  /* now pull in values for some timestamps */
  gst_object_sync_values (GST_OBJECT (elem), 0 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 200);
  gst_object_sync_values (GST_OBJECT (elem), 250 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 150);
  gst_object_sync_values (GST_OBJECT (elem), 500 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 100);
  gst_object_sync_values (GST_OBJECT (elem), 750 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 50);
  gst_object_sync_values (GST_OBJECT (elem), 1000 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 200);
  gst_object_sync_values (GST_OBJECT (elem), 1250 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 150);
  gst_object_sync_values (GST_OBJECT (elem), 1500 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 100);
  gst_object_sync_values (GST_OBJECT (elem), 1750 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 50);
  gst_object_sync_values (GST_OBJECT (elem), 2000 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 200);
  gst_object_sync_values (GST_OBJECT (elem), 1250 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 150);
  gst_object_sync_values (GST_OBJECT (elem), 1500 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 100);
  gst_object_sync_values (GST_OBJECT (elem), 1750 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 50);

  gst_object_unref (elem);
}

GST_END_TEST;

/* test lfo control source with reverse saw waveform */
GST_START_TEST (controller_lfo_rsaw)
{
  GstLFOControlSource *csource;
  GstElement *elem;
  GValue amp = { 0, };
  GValue off = { 0, };

  elem = gst_element_factory_make ("testmonosource", "test_source");

  /* Get interpolation control source */
  csource = gst_lfo_control_source_new ();

  fail_unless (csource != NULL);
  fail_unless (gst_object_set_control_source (GST_OBJECT (elem), "ulong",
          GST_CONTROL_SOURCE (csource)));

  /* set amplitude and offset values */
  g_value_init (&amp, G_TYPE_ULONG);
  g_value_init (&off, G_TYPE_ULONG);
  g_value_set_ulong (&amp, 100);
  g_value_set_ulong (&off, 100);

  /* set waveform mode */
  g_object_set (csource, "waveform", GST_LFO_WAVEFORM_REVERSE_SAW,
      "frequency", 1.0, "timeshift", (GstClockTime) 0,
      "amplitude", &amp, "offset", &off, NULL);

  g_object_unref (csource);

  /* now pull in values for some timestamps */
  gst_object_sync_values (GST_OBJECT (elem), 0 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 0);
  gst_object_sync_values (GST_OBJECT (elem), 250 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 50);
  gst_object_sync_values (GST_OBJECT (elem), 500 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 100);
  gst_object_sync_values (GST_OBJECT (elem), 750 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 150);
  gst_object_sync_values (GST_OBJECT (elem), 1000 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 0);
  gst_object_sync_values (GST_OBJECT (elem), 1250 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 50);
  gst_object_sync_values (GST_OBJECT (elem), 1500 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 100);
  gst_object_sync_values (GST_OBJECT (elem), 1750 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 150);
  gst_object_sync_values (GST_OBJECT (elem), 2000 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 0);
  gst_object_sync_values (GST_OBJECT (elem), 1250 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 50);
  gst_object_sync_values (GST_OBJECT (elem), 1500 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 100);
  gst_object_sync_values (GST_OBJECT (elem), 1750 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 150);

  gst_object_unref (elem);
}

GST_END_TEST;

/* test lfo control source with saw waveform */
GST_START_TEST (controller_lfo_triangle)
{
  GstLFOControlSource *csource;
  GstElement *elem;
  GValue amp = { 0, };
  GValue off = { 0, };

  elem = gst_element_factory_make ("testmonosource", "test_source");

  /* Get interpolation control source */
  csource = gst_lfo_control_source_new ();

  fail_unless (csource != NULL);
  fail_unless (gst_object_set_control_source (GST_OBJECT (elem), "ulong",
          GST_CONTROL_SOURCE (csource)));

  /* set amplitude and offset values */
  g_value_init (&amp, G_TYPE_ULONG);
  g_value_init (&off, G_TYPE_ULONG);
  g_value_set_ulong (&amp, 100);
  g_value_set_ulong (&off, 100);

  /* set waveform mode */
  g_object_set (csource, "waveform", GST_LFO_WAVEFORM_TRIANGLE,
      "frequency", 1.0, "timeshift", (GstClockTime) 0,
      "amplitude", &amp, "offset", &off, NULL);

  g_object_unref (csource);

  /* now pull in values for some timestamps */
  gst_object_sync_values (GST_OBJECT (elem), 0 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 100);
  gst_object_sync_values (GST_OBJECT (elem), 250 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 200);
  gst_object_sync_values (GST_OBJECT (elem), 500 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 100);
  gst_object_sync_values (GST_OBJECT (elem), 750 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 0);
  gst_object_sync_values (GST_OBJECT (elem), 1000 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 100);
  gst_object_sync_values (GST_OBJECT (elem), 1250 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 200);
  gst_object_sync_values (GST_OBJECT (elem), 1500 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 100);
  gst_object_sync_values (GST_OBJECT (elem), 1750 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 0);
  gst_object_sync_values (GST_OBJECT (elem), 2000 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 100);
  gst_object_sync_values (GST_OBJECT (elem), 1250 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 200);
  gst_object_sync_values (GST_OBJECT (elem), 1500 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 100);
  gst_object_sync_values (GST_OBJECT (elem), 1750 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 0);

  gst_object_unref (elem);
}

GST_END_TEST;

/* test lfo control source with nothing set */
GST_START_TEST (controller_lfo_none)
{
  GstLFOControlSource *csource;
  GstElement *elem;

  elem = gst_element_factory_make ("testmonosource", "test_source");

  /* Get interpolation control source */
  csource = gst_lfo_control_source_new ();

  fail_unless (csource != NULL);
  fail_unless (gst_object_set_control_source (GST_OBJECT (elem), "ulong",
          GST_CONTROL_SOURCE (csource)));

  g_object_unref (csource);

  /* now pull in values for some timestamps */
  gst_object_sync_values (GST_OBJECT (elem), 0 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 0);
  gst_object_sync_values (GST_OBJECT (elem), 250 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 0);
  gst_object_sync_values (GST_OBJECT (elem), 500 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 0);
  gst_object_sync_values (GST_OBJECT (elem), 750 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 0);
  gst_object_sync_values (GST_OBJECT (elem), 1000 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 0);
  gst_object_sync_values (GST_OBJECT (elem), 1250 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 0);
  gst_object_sync_values (GST_OBJECT (elem), 1500 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 0);
  gst_object_sync_values (GST_OBJECT (elem), 1750 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 0);
  gst_object_sync_values (GST_OBJECT (elem), 2000 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 0);
  gst_object_sync_values (GST_OBJECT (elem), 1250 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 0);
  gst_object_sync_values (GST_OBJECT (elem), 1500 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 0);
  gst_object_sync_values (GST_OBJECT (elem), 1750 * GST_MSECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 0);

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
  res = gst_object_sync_values (GST_OBJECT (elem), 0LL);
  /* Syncing should still succeed as there's nothing to sync */
  fail_unless (res == TRUE, NULL);

  gst_object_unref (elem);
}

GST_END_TEST;

/* test linear interpolation for ts < first control point */
GST_START_TEST (controller_interpolate_linear_before_ts0)
{
  GstInterpolationControlSource *csource;
  GstElement *elem;
  gboolean res;
  GValue val_ulong = { 0, };

  elem = gst_element_factory_make ("testmonosource", "test_source");

  /* Get interpolation control source */
  csource = gst_interpolation_control_source_new ();

  fail_unless (csource != NULL);
  fail_unless (gst_object_set_control_source (GST_OBJECT (elem), "ulong",
          GST_CONTROL_SOURCE (csource)));

  /* set interpolation mode */
  fail_unless (gst_interpolation_control_source_set_interpolation_mode (csource,
          GST_INTERPOLATE_LINEAR));

  /* set control values */
  g_value_init (&val_ulong, G_TYPE_ULONG);
  g_value_set_ulong (&val_ulong, 100);
  res =
      gst_interpolation_control_source_set (csource, 2 * GST_SECOND,
      &val_ulong);
  fail_unless (res, NULL);
  g_value_set_ulong (&val_ulong, 0);
  res =
      gst_interpolation_control_source_set (csource, 4 * GST_SECOND,
      &val_ulong);
  fail_unless (res, NULL);

  g_object_unref (csource);

  /* now pull in values for some timestamps after first control point */
  gst_object_sync_values (GST_OBJECT (elem), 2 * GST_SECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 100);
  gst_object_sync_values (GST_OBJECT (elem), 3 * GST_SECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 50);
  gst_object_sync_values (GST_OBJECT (elem), 4 * GST_SECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 0);

  /* now pull in values for some timestamps before first control point */
  gst_object_sync_values (GST_OBJECT (elem), 1 * GST_SECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 50);
  gst_object_sync_values (GST_OBJECT (elem), 0 * GST_SECOND);
  fail_unless_equals_int (GST_TEST_MONO_SOURCE (elem)->val_ulong, 0);

  gst_object_unref (elem);
}

GST_END_TEST;

/* test control-point handling in interpolation control source */
GST_START_TEST (controller_interpolation_cp_count)
{
  GstInterpolationControlSource *csource;
  GstElement *elem;
  gboolean res;
  GValue val_ulong = { 0, };

  elem = gst_element_factory_make ("testmonosource", "test_source");

  /* Get interpolation control source */
  csource = gst_interpolation_control_source_new ();

  fail_unless (csource != NULL);
  fail_unless (gst_object_set_control_source (GST_OBJECT (elem), "ulong",
          GST_CONTROL_SOURCE (csource)));

  /* set interpolation mode */
  fail_unless (gst_interpolation_control_source_set_interpolation_mode (csource,
          GST_INTERPOLATE_NONE));

  fail_unless (gst_interpolation_control_source_get_count (csource) == 0);

  /* set control values */
  g_value_init (&val_ulong, G_TYPE_ULONG);
  g_value_set_ulong (&val_ulong, 0);
  res =
      gst_interpolation_control_source_set (csource, 0 * GST_SECOND,
      &val_ulong);
  fail_unless (res, NULL);
  fail_unless (gst_interpolation_control_source_get_count (csource) == 1);
  g_value_set_ulong (&val_ulong, 100);
  res =
      gst_interpolation_control_source_set (csource, 2 * GST_SECOND,
      &val_ulong);
  fail_unless (res, NULL);
  fail_unless (gst_interpolation_control_source_get_count (csource) == 2);

  /* now unset control values */
  res = gst_interpolation_control_source_unset (csource, 2 * GST_SECOND);
  fail_unless (res, NULL);
  fail_unless (gst_interpolation_control_source_get_count (csource) == 1);

  res = gst_interpolation_control_source_unset (csource, 0 * GST_SECOND);
  fail_unless (res, NULL);
  fail_unless (gst_interpolation_control_source_get_count (csource) == 0);

  g_object_unref (csource);

  gst_object_unref (elem);
}

GST_END_TEST;


static Suite *
gst_controller_suite (void)
{
  Suite *s = suite_create ("Controller");
  TCase *tc = tcase_create ("general");

  suite_add_tcase (s, tc);
  tcase_add_checked_fixture (tc, setup, teardown);
  tcase_add_test (tc, controller_new_fail1);
  tcase_add_test (tc, controller_new_fail2);
  tcase_add_test (tc, controller_new_fail3);
  tcase_add_test (tc, controller_new_fail4);
  tcase_add_test (tc, controller_new_okay1);
  tcase_add_test (tc, controller_new_okay2);
  tcase_add_test (tc, controller_param_twice);
  tcase_add_test (tc, controller_controlsource_refcounts);
  tcase_add_test (tc, controller_controlsource_empty1);
  tcase_add_test (tc, controller_controlsource_empty2);
  tcase_add_test (tc, controller_interpolate_none);
  tcase_add_test (tc, controller_interpolate_trigger);
  tcase_add_test (tc, controller_interpolate_linear);
  tcase_add_test (tc, controller_interpolate_cubic);
  tcase_add_test (tc, controller_interpolate_cubic_too_few_cp);
  tcase_add_test (tc, controller_interpolate_unimplemented);
  tcase_add_test (tc, controller_interpolation_unset);
  tcase_add_test (tc, controller_interpolation_unset_all);
  tcase_add_test (tc, controller_interpolation_linear_value_array);
  tcase_add_test (tc, controller_interpolation_linear_invalid_values);
  tcase_add_test (tc, controller_interpolation_linear_default_values);
  tcase_add_test (tc, controller_interpolate_linear_disabled);
  tcase_add_test (tc, controller_interpolation_set_from_list);
  tcase_add_test (tc, controller_lfo_sine);
  tcase_add_test (tc, controller_lfo_sine_timeshift);
  tcase_add_test (tc, controller_lfo_square);
  tcase_add_test (tc, controller_lfo_saw);
  tcase_add_test (tc, controller_lfo_rsaw);
  tcase_add_test (tc, controller_lfo_triangle);
  tcase_add_test (tc, controller_lfo_none);
  tcase_add_test (tc, controller_helper_any_gobject);
  tcase_add_test (tc, controller_interpolate_linear_before_ts0);
  tcase_add_test (tc, controller_interpolation_cp_count);

  return s;
}

GST_CHECK_MAIN (gst_controller);
