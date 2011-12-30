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


/* local test element */

enum
{
  PROP_INT = 1,
  PROP_FLOAT,
  PROP_DOUBLE,
  PROP_BOOLEAN,
  PROP_READONLY,
  PROP_STATIC,
  PROP_CONSTRUCTONLY,
  PROP_COUNT
};

#define GST_TYPE_TEST_OBJ            (gst_test_obj_get_type ())
#define GST_TEST_OBJ(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_TEST_OBJ, GstTestObj))
#define GST_TEST_OBJ_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_TEST_OBJ, GstTestObjClass))
#define GST_IS_TEST_OBJ(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_TEST_OBJ))
#define GST_IS_TEST_OBJ_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_TEST_OBJ))
#define GST_TEST_OBJ_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_TEST_OBJ, GstTestObjClass))

typedef struct _GstTestObj GstTestObj;
typedef struct _GstTestObjClass GstTestObjClass;

struct _GstTestObj
{
  GstElement parent;
  gint val_int;
  gfloat val_float;
  gdouble val_double;
  gboolean val_boolean;
};
struct _GstTestObjClass
{
  GstElementClass parent_class;
};

static GType gst_test_obj_get_type (void);

static void
gst_test_obj_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec)
{
  GstTestObj *self = GST_TEST_OBJ (object);

  switch (property_id) {
    case PROP_INT:
      g_value_set_int (value, self->val_int);
      break;
    case PROP_FLOAT:
      g_value_set_float (value, self->val_float);
      break;
    case PROP_DOUBLE:
      g_value_set_double (value, self->val_double);
      break;
    case PROP_BOOLEAN:
      g_value_set_boolean (value, self->val_boolean);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gst_test_obj_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec)
{
  GstTestObj *self = GST_TEST_OBJ (object);

  switch (property_id) {
    case PROP_INT:
      self->val_int = g_value_get_int (value);
      GST_DEBUG ("test value int=%d", self->val_int);
      break;
    case PROP_FLOAT:
      self->val_float = g_value_get_float (value);
      GST_DEBUG ("test value float=%f", self->val_float);
      break;
    case PROP_DOUBLE:
      self->val_double = g_value_get_double (value);
      GST_DEBUG ("test value double=%lf", self->val_double);
      break;
    case PROP_BOOLEAN:
      self->val_boolean = g_value_get_boolean (value);
      GST_DEBUG ("test value boolean=%d", self->val_boolean);
      break;
    case PROP_CONSTRUCTONLY:
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gst_test_obj_class_init (GstTestObjClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->set_property = gst_test_obj_set_property;
  gobject_class->get_property = gst_test_obj_get_property;

  g_object_class_install_property (gobject_class, PROP_INT,
      g_param_spec_int ("int",
          "int prop",
          "int number parameter for the test_mono_source",
          0, 100, 0, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));

  g_object_class_install_property (gobject_class, PROP_FLOAT,
      g_param_spec_float ("float",
          "float prop",
          "float number parameter for the test_mono_source",
          0.0, 100.0, 0.0, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));

  g_object_class_install_property (gobject_class, PROP_DOUBLE,
      g_param_spec_double ("double",
          "double prop",
          "double number parameter for the test_mono_source",
          0.0, 100.0, 0.0, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));

  g_object_class_install_property (gobject_class, PROP_BOOLEAN,
      g_param_spec_boolean ("boolean",
          "boolean prop",
          "boolean parameter for the test_mono_source",
          FALSE, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));

  g_object_class_install_property (gobject_class, PROP_READONLY,
      g_param_spec_int ("readonly",
          "readonly prop",
          "readonly parameter for the test_mono_source",
          0, G_MAXINT, 0, G_PARAM_READABLE | GST_PARAM_CONTROLLABLE));

  g_object_class_install_property (gobject_class, PROP_STATIC,
      g_param_spec_int ("static",
          "static prop",
          "static parameter for the test_mono_source",
          0, G_MAXINT, 0, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_CONSTRUCTONLY,
      g_param_spec_int ("construct-only",
          "construct-only prop",
          "construct-only parameter for the test_mono_source",
          0, G_MAXINT, 0, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

static void
gst_test_obj_base_init (GstTestObjClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_details_simple (element_class,
      "test object for unit tests",
      "Test", "Use in unit tests", "Stefan Sauer <ensonic@users.sf.net>");
}

static GType
gst_test_obj_get_type (void)
{
  static volatile gsize test_obj_type = 0;

  if (g_once_init_enter (&test_obj_type)) {
    GType type;
    static const GTypeInfo info = {
      (guint16) sizeof (GstTestObjClass),
      (GBaseInitFunc) gst_test_obj_base_init,   // base_init
      NULL,                     // base_finalize
      (GClassInitFunc) gst_test_obj_class_init, // class_init
      NULL,                     // class_finalize
      NULL,                     // class_data
      (guint16) sizeof (GstTestObj),
      0,                        // n_preallocs
      NULL,                     // instance_init
      NULL                      // value_table
    };
    type = g_type_register_static (GST_TYPE_ELEMENT, "GstTestObj", &info, 0);
    g_once_init_leave (&test_obj_type, type);
  }
  return test_obj_type;
}

/* test control source */

#define GST_TYPE_TEST_CONTROL_SOURCE            (gst_test_control_source_get_type ())
#define GST_TEST_CONTROL_SOURCE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_TEST_CONTROL_SOURCE, GstTestControlSource))
#define GST_TEST_CONTROL_SOURCE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_TEST_CONTROL_SOURCE, GstTestControlSourceClass))
#define GST_IS_TEST_CONTROL_SOURCE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_TEST_CONTROL_SOURCE))
#define GST_IS_TEST_CONTROL_SOURCE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_TEST_CONTROL_SOURCE))
#define GST_TEST_CONTROL_SOURCE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_TEST_CONTROL_SOURCE, GstTestControlSourceClass))

typedef struct _GstTestControlSource GstTestControlSource;
typedef struct _GstTestControlSourceClass GstTestControlSourceClass;

struct _GstTestControlSource
{
  GstControlSource parent;

  gdouble value;
};
struct _GstTestControlSourceClass
{
  GstControlSourceClass parent_class;
};

static GType gst_test_control_source_get_type (void);

static GstTestControlSource *
gst_test_control_source_new (void)
{
  return g_object_newv (GST_TYPE_TEST_CONTROL_SOURCE, 0, NULL);
}

static gboolean
gst_test_control_source_get (GstTestControlSource * self,
    GstClockTime timestamp, gdouble * value)
{
  *value = self->value;
  return TRUE;
}

static gboolean
gst_test_control_source_get_value_array (GstTestControlSource * self,
    GstClockTime timestamp, GstClockTime interval, guint n_values,
    gdouble * values)
{
  guint i;

  for (i = 0; i < n_values; i++) {
    *values = self->value;
    values++;
  }
  return TRUE;
}


static void
gst_test_control_source_init (GstTestControlSource * self)
{
  GstControlSource *cs = (GstControlSource *) self;

  cs->get_value = (GstControlSourceGetValue) gst_test_control_source_get;
  cs->get_value_array = (GstControlSourceGetValueArray)
      gst_test_control_source_get_value_array;
  self->value = 0.0;
}

static GType
gst_test_control_source_get_type (void)
{
  static volatile gsize test_countrol_source_type = 0;

  if (g_once_init_enter (&test_countrol_source_type)) {
    GType type;
    static const GTypeInfo info = {
      (guint16) sizeof (GstTestControlSourceClass),
      NULL,                     // base_init
      NULL,                     // base_finalize
      NULL,                     // class_init
      NULL,                     // class_finalize
      NULL,                     // class_data
      (guint16) sizeof (GstTestControlSource),
      0,                        // n_preallocs
      (GInstanceInitFunc) gst_test_control_source_init, // instance_init
      NULL                      // value_table
    };
    type =
        g_type_register_static (GST_TYPE_CONTROL_SOURCE, "GstTestControlSource",
        &info, 0);
    g_once_init_leave (&test_countrol_source_type, type);
  }
  return test_countrol_source_type;
}




static void
setup (void)
{
  gst_element_register (NULL, "testobj", GST_RANK_NONE, GST_TYPE_TEST_OBJ);
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
  GstTestControlSource *cs;
  gboolean res;

  elem = gst_element_factory_make ("fakesrc", NULL);
  cs = gst_test_control_source_new ();

  /* that property should not exist */
  res = gst_object_set_control_source (GST_OBJECT (elem), "_schrompf_",
      GST_CONTROL_SOURCE (cs));
  fail_unless (res == FALSE, NULL);

  gst_object_unref (cs);
  gst_object_unref (elem);
}

GST_END_TEST;

/* tests for readonly params */
GST_START_TEST (controller_new_fail2)
{
  GstElement *elem;
  GstTestControlSource *cs;
  gboolean res;

  elem = gst_element_factory_make ("testobj", NULL);
  cs = gst_test_control_source_new ();

  /* that property should exist and but is readonly */
  ASSERT_CRITICAL (res = gst_object_set_control_source (GST_OBJECT (elem),
          "readonly", GST_CONTROL_SOURCE (cs)));
  fail_unless (res == FALSE, NULL);

  gst_object_unref (cs);
  gst_object_unref (elem);
}

GST_END_TEST;

/* tests for static params */
GST_START_TEST (controller_new_fail3)
{
  GstElement *elem;
  GstTestControlSource *cs;
  gboolean res;

  elem = gst_element_factory_make ("testobj", NULL);
  cs = gst_test_control_source_new ();

  /* that property should exist and but is not controlable */
  ASSERT_CRITICAL (res = gst_object_set_control_source (GST_OBJECT (elem),
          "static", GST_CONTROL_SOURCE (cs)));
  fail_unless (res == FALSE, NULL);

  gst_object_unref (cs);
  gst_object_unref (elem);
}

GST_END_TEST;

/* tests for construct-only params */
GST_START_TEST (controller_new_fail4)
{
  GstElement *elem;
  GstTestControlSource *cs;
  gboolean res;

  elem = gst_element_factory_make ("testobj", NULL);
  cs = gst_test_control_source_new ();

  /* that property should exist and but is construct-only */
  ASSERT_CRITICAL (res =
      gst_object_set_control_source (GST_OBJECT (elem), "construct-only",
          GST_CONTROL_SOURCE (cs)));
  fail_unless (res == FALSE, NULL);

  gst_object_unref (cs);
  gst_object_unref (elem);
}

GST_END_TEST;


/* tests for an element with controlled params */
GST_START_TEST (controller_new_okay1)
{
  GstElement *elem;
  GstTestControlSource *cs;
  gboolean res;

  elem = gst_element_factory_make ("testobj", NULL);
  cs = gst_test_control_source_new ();

  /* that property should exist and should be controllable */
  res = gst_object_set_control_source (GST_OBJECT (elem), "int",
      GST_CONTROL_SOURCE (cs));
  fail_unless (res == TRUE, NULL);

  gst_object_unref (cs);
  gst_object_unref (elem);
}

GST_END_TEST;

/* tests for an element with several controlled params */
GST_START_TEST (controller_new_okay2)
{
  GstElement *elem;
  GstTestControlSource *cs1, *cs2;
  gboolean res;

  elem = gst_element_factory_make ("testobj", NULL);
  cs1 = gst_test_control_source_new ();
  cs2 = gst_test_control_source_new ();

  /* these properties should exist and should be controllable */
  res = gst_object_set_control_source (GST_OBJECT (elem), "int",
      GST_CONTROL_SOURCE (cs1));
  fail_unless (res == TRUE, NULL);

  res = gst_object_set_control_source (GST_OBJECT (elem), "boolean",
      GST_CONTROL_SOURCE (cs2));
  fail_unless (res == TRUE, NULL);

  gst_object_unref (cs1);
  gst_object_unref (cs2);
  gst_object_unref (elem);
}

GST_END_TEST;

/* controlling a param twice should be handled */
GST_START_TEST (controller_param_twice)
{
  GstElement *elem;
  GstTestControlSource *cs;
  gboolean res;

  elem = gst_element_factory_make ("testobj", NULL);
  cs = gst_test_control_source_new ();

  /* that property should exist and should be controllable */
  res = gst_object_set_control_source (GST_OBJECT (elem), "int",
      GST_CONTROL_SOURCE (cs));
  fail_unless (res, NULL);

  /* setting it again will just unset the old and set it again
   * this might cause some trouble with binding the control source again
   */
  res = gst_object_set_control_source (GST_OBJECT (elem), "int",
      GST_CONTROL_SOURCE (cs));
  fail_unless (res, NULL);

  /* it should have been added at least once, let remove it */
  res = gst_object_set_control_source (GST_OBJECT (elem), "int", NULL);
  fail_unless (res, NULL);

  /* removing it again should not work */
  res = gst_object_set_control_source (GST_OBJECT (elem), "int", NULL);
  fail_unless (!res, NULL);

  gst_object_unref (cs);
  gst_object_unref (elem);
}

GST_END_TEST;

/* tests if we can run controller methods against any GObject */
GST_START_TEST (controller_any_gobject)
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

/* tests if we cleanup properly */
GST_START_TEST (controller_controlsource_refcounts)
{
  GstElement *elem;
  GstControlSource *csource, *test_csource;

  elem = gst_element_factory_make ("testobj", NULL);

  csource = (GstControlSource *) gst_test_control_source_new ();
  fail_unless (csource != NULL, NULL);

  fail_unless_equals_int (G_OBJECT (csource)->ref_count, 1);
  fail_unless (gst_object_set_control_source (GST_OBJECT (elem), "int",
          csource));
  fail_unless_equals_int (G_OBJECT (csource)->ref_count, 2);

  test_csource = gst_object_get_control_source (GST_OBJECT (elem), "int");
  fail_unless (test_csource != NULL, NULL);
  fail_unless (test_csource == csource);
  fail_unless_equals_int (G_OBJECT (csource)->ref_count, 3);
  gst_object_unref (test_csource);
  gst_object_unref (csource);

  gst_object_unref (elem);
}

GST_END_TEST;

/* tests if we can bnd a control source twice */
GST_START_TEST (controller_bind_twice)
{
  GstElement *elem;
  GstControlSource *csource;

  elem = gst_element_factory_make ("testobj", NULL);

  csource = (GstControlSource *) gst_test_control_source_new ();
  fail_unless (csource != NULL, NULL);

  fail_unless (gst_object_set_control_source (GST_OBJECT (elem), "int",
          csource));
  fail_unless (gst_object_set_control_source (GST_OBJECT (elem), "double",
          csource));

  gst_object_unref (csource);

  gst_object_unref (elem);
}

GST_END_TEST;

/* tests synching a value */
GST_START_TEST (controller_sync1)
{
  GstElement *elem;
  GstTestControlSource *csource;

  elem = gst_element_factory_make ("testobj", NULL);

  csource = gst_test_control_source_new ();
  fail_unless (csource != NULL, NULL);

  fail_unless (gst_object_set_control_source (GST_OBJECT (elem), "int",
          (GstControlSource *) csource));

  csource->value = 0.5;
  fail_unless (gst_object_sync_values (GST_OBJECT (elem), 0LL));
  fail_unless_equals_int (GST_TEST_OBJ (elem)->val_int, 50);

  gst_object_unref (csource);

  gst_object_unref (elem);
}

GST_END_TEST;

/* tests synching a value */
GST_START_TEST (controller_sync2)
{
  GstElement *elem;
  GstTestControlSource *csource;

  elem = gst_element_factory_make ("testobj", NULL);

  csource = gst_test_control_source_new ();
  fail_unless (csource != NULL, NULL);

  fail_unless (gst_object_set_control_source (GST_OBJECT (elem), "int",
          (GstControlSource *) csource));
  fail_unless (gst_object_set_control_source (GST_OBJECT (elem), "double",
          (GstControlSource *) csource));

  csource->value = 0.5;
  fail_unless (gst_object_sync_values (GST_OBJECT (elem), 0LL));
  fail_unless_equals_int (GST_TEST_OBJ (elem)->val_int, 50);
  fail_unless_equals_float (GST_TEST_OBJ (elem)->val_double, 50.0);

  gst_object_unref (csource);

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
  tcase_add_test (tc, controller_any_gobject);
  tcase_add_test (tc, controller_controlsource_refcounts);
  tcase_add_test (tc, controller_bind_twice);
  tcase_add_test (tc, controller_sync1);
  tcase_add_test (tc, controller_sync2);

  return s;
}

GST_CHECK_MAIN (gst_controller);
