/* GStreamer GstParamSpec unit tests
 * Copyright (C) 2007 Tim-Philipp Müller <tim centricular net>
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
#include <gst/gst.h>
#include <string.h>

/* some minimal dummy object */
#define GST_TYPE_DUMMY_OBJ gst_dummy_obj_get_type()

typedef struct
{
  GstElement parent;
  guint num, denom;
} GstDummyObj;

typedef GstElementClass GstDummyObjClass;

GType gst_dummy_obj_get_type (void);
G_DEFINE_TYPE (GstDummyObj, gst_dummy_obj, GST_TYPE_ELEMENT);

static void
gst_dummy_obj_get_property (GObject * obj, guint prop_id, GValue * val,
    GParamSpec * pspec);
static void
gst_dummy_obj_set_property (GObject * obj, guint prop_id, const GValue * val,
    GParamSpec * pspec);

static void
gst_dummy_obj_class_init (GstDummyObjClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = gst_dummy_obj_get_property;
  gobject_class->set_property = gst_dummy_obj_set_property;

  ASSERT_CRITICAL (
      /* default value is out of bounds, should print a warning */
      g_object_class_install_property (gobject_class, 1,
          gst_param_spec_fraction ("ratio", "ratio", "ratio", 0, 1, 2, 1,
              16, 4, G_PARAM_READWRITE));
      );

  /* should be within bounds */
  g_object_class_install_property (gobject_class, 2,
      gst_param_spec_fraction ("other-ratio", "other ratio", "other ratio",
          0, 1, 2, 1, 16, 9, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, 3,
      g_param_spec_boolean ("foo", "foo", "foo", TRUE, G_PARAM_READWRITE));
}

static void
gst_dummy_obj_init (GstDummyObj * obj)
{
  /* nothing to do there */
}

static void
gst_dummy_obj_set_property (GObject * obj, guint prop_id, const GValue * val,
    GParamSpec * pspec)
{
  GstDummyObj *dobj = (GstDummyObj *) obj;

  fail_unless_equals_int (prop_id, 2);
  dobj->num = gst_value_get_fraction_numerator (val);
  dobj->denom = gst_value_get_fraction_denominator (val);
}

static void
gst_dummy_obj_get_property (GObject * obj, guint prop_id, GValue * val,
    GParamSpec * pspec)
{
  GstDummyObj *dobj = (GstDummyObj *) obj;

  fail_unless_equals_int (prop_id, 2);
  gst_value_set_fraction (val, dobj->num, dobj->denom);
}

GST_START_TEST (test_param_spec_fraction)
{
  GObject *obj;
  GValue val = { 0, };
  gint n = 0, d = 0;

  obj = g_object_new (GST_TYPE_DUMMY_OBJ, "other-ratio", 15, 8, NULL);

  g_value_init (&val, GST_TYPE_FRACTION);
  g_object_get_property (G_OBJECT (obj), "other-ratio", &val);
  fail_unless_equals_int (gst_value_get_fraction_numerator (&val), 15);
  fail_unless_equals_int (gst_value_get_fraction_denominator (&val), 8);
  g_value_unset (&val);

  g_object_get (obj, "other-ratio", &n, &d, NULL);
  fail_unless_equals_int (n, 15);
  fail_unless_equals_int (d, 8);

  g_object_unref (obj);
}

GST_END_TEST static Suite *
gst_param_spec_suite (void)
{
  Suite *s = suite_create ("GstParamSpec");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_param_spec_fraction);

  return s;
}

GST_CHECK_MAIN (gst_param_spec);
