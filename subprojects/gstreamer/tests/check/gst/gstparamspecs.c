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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

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

  gst_object_unref (obj);
}

GST_END_TEST
GST_START_TEST (test_param_spec_kind)
{
  GParamSpec *pspec;
  const GstStructure *kind;
  GstStructure *new_kind;
  const GValue *mimetypes;
  const GValue *extensions;
  GValue list = G_VALUE_INIT;
  GValue v = G_VALUE_INIT;
  gchar *str;

  pspec = g_param_spec_string ("str", "str", "str", NULL, G_PARAM_READWRITE);

  fail_unless (gst_param_spec_get_kind (pspec) == NULL);

  gst_param_spec_set_kind (pspec, gst_structure_new (GST_PROP_KIND_LOCATION,
          GST_PROP_KIND_FIELD_SUBTYPE, G_TYPE_STRING,
          GST_PROP_KIND_SUBTYPE_URI, NULL));
  kind = gst_param_spec_get_kind (pspec);
  fail_unless (kind != NULL);
  fail_unless (gst_structure_has_name (kind, GST_PROP_KIND_LOCATION));
  fail_unless_equals_string (gst_structure_get_string (kind,
          GST_PROP_KIND_FIELD_SUBTYPE), GST_PROP_KIND_SUBTYPE_URI);

  /* a kind is permanent: setting again is refused and keeps the first one.
   * A refused kind is not taken, so the caller still owns it. */
  new_kind = gst_structure_new_empty ("x-other");
  ASSERT_CRITICAL (gst_param_spec_set_kind (pspec, new_kind));
  gst_structure_free (new_kind);
  ASSERT_CRITICAL (gst_param_spec_set_kind (pspec, NULL));
  kind = gst_param_spec_get_kind (pspec);
  fail_unless (gst_structure_has_name (kind, GST_PROP_KIND_LOCATION));
  fail_unless_equals_string (gst_structure_get_string (kind,
          GST_PROP_KIND_FIELD_SUBTYPE), GST_PROP_KIND_SUBTYPE_URI);
  g_param_spec_unref (pspec);

  pspec = g_param_spec_string ("str", "str", "str", NULL, G_PARAM_READWRITE);
  new_kind = gst_structure_new (GST_PROP_KIND_LOCATION,
      GST_PROP_KIND_FIELD_SUBTYPE, G_TYPE_STRING, GST_PROP_KIND_SUBTYPE_PATH,
      NULL);
  g_value_init (&v, G_TYPE_STRING);
  g_value_init (&list, GST_TYPE_LIST);
  g_value_set_static_string (&v, "image/jpeg");
  gst_value_list_append_value (&list, &v);
  g_value_set_static_string (&v, "image/png");
  gst_value_list_append_value (&list, &v);
  gst_structure_set_value (new_kind, GST_PROP_KIND_FIELD_MIMETYPES, &list);
  g_value_unset (&list);
  g_value_unset (&v);
  gst_param_spec_set_kind (pspec, new_kind);
  kind = gst_param_spec_get_kind (pspec);
  fail_unless (gst_structure_has_name (kind, GST_PROP_KIND_LOCATION));
  fail_unless_equals_string (gst_structure_get_string (kind,
          GST_PROP_KIND_FIELD_SUBTYPE), GST_PROP_KIND_SUBTYPE_PATH);

  mimetypes = gst_structure_get_value (kind, GST_PROP_KIND_FIELD_MIMETYPES);
  fail_unless (GST_VALUE_HOLDS_LIST (mimetypes));
  fail_unless_equals_int (gst_value_list_get_size (mimetypes), 2);
  fail_unless_equals_string (g_value_get_string (gst_value_list_get_value
          (mimetypes, 0)), "image/jpeg");
  fail_unless_equals_string (g_value_get_string (gst_value_list_get_value
          (mimetypes, 1)), "image/png");

  str = gst_structure_to_string (kind);
  fail_unless_equals_string (str, "location, subtype=(string)path, "
      "mimetypes=(string){ image/jpeg, image/png };");
  g_free (str);
  g_param_spec_unref (pspec);

  pspec = g_param_spec_string ("str", "str", "str", NULL, G_PARAM_READWRITE);
  gst_param_spec_set_kind (pspec, gst_structure_new (GST_PROP_KIND_LOCATION,
          GST_PROP_KIND_FIELD_SUBTYPE, G_TYPE_STRING,
          GST_PROP_KIND_SUBTYPE_PATH, GST_PROP_KIND_FIELD_EXTENSIONS,
          G_TYPE_STRING, "onnx", NULL));
  fail_unless_equals_string (gst_structure_get_string (gst_param_spec_get_kind
          (pspec), GST_PROP_KIND_FIELD_EXTENSIONS), "onnx");
  g_param_spec_unref (pspec);

  /* from a string, a field is a plain string or a list depending on syntax */
  pspec = g_param_spec_string ("str", "str", "str", NULL, G_PARAM_READWRITE);
  gst_param_spec_set_kind (pspec,
      gst_structure_new_from_string (GST_PROP_KIND_LOCATION ", "
          GST_PROP_KIND_FIELD_SUBTYPE "=(string)" GST_PROP_KIND_SUBTYPE_PATH
          ", " GST_PROP_KIND_FIELD_MIMETYPES
          "=(string)application/vnd.iccprofile, "
          GST_PROP_KIND_FIELD_EXTENSIONS "=(string){icc,icm}"));
  kind = gst_param_spec_get_kind (pspec);
  fail_unless (kind != NULL);

  mimetypes = gst_structure_get_value (kind, GST_PROP_KIND_FIELD_MIMETYPES);
  fail_unless (G_VALUE_HOLDS_STRING (mimetypes));
  fail_unless_equals_string (g_value_get_string (mimetypes),
      "application/vnd.iccprofile");

  extensions = gst_structure_get_value (kind, GST_PROP_KIND_FIELD_EXTENSIONS);
  fail_unless (GST_VALUE_HOLDS_LIST (extensions));
  fail_unless_equals_int (gst_value_list_get_size (extensions), 2);
  fail_unless_equals_string (g_value_get_string (gst_value_list_get_value
          (extensions, 0)), "icc");
  fail_unless_equals_string (g_value_get_string (gst_value_list_get_value
          (extensions, 1)), "icm");
  /* a list field is not readable as a plain string */
  fail_unless (gst_structure_get_string (kind,
          GST_PROP_KIND_FIELD_EXTENSIONS) == NULL);
  g_param_spec_unref (pspec);

  /* a single entry in braces stays a list */
  pspec = g_param_spec_string ("str", "str", "str", NULL, G_PARAM_READWRITE);
  gst_param_spec_set_kind (pspec,
      gst_structure_new_from_string (GST_PROP_KIND_LOCATION ", "
          GST_PROP_KIND_FIELD_EXTENSIONS "=(string){onnx}"));
  extensions = gst_structure_get_value (gst_param_spec_get_kind (pspec),
      GST_PROP_KIND_FIELD_EXTENSIONS);
  fail_unless (GST_VALUE_HOLDS_LIST (extensions));
  fail_unless_equals_int (gst_value_list_get_size (extensions), 1);
  fail_unless_equals_string (g_value_get_string (gst_value_list_get_value
          (extensions, 0)), "onnx");

  /* the hint is not restricted to string properties */
  g_param_spec_unref (pspec);
  pspec = g_param_spec_uint ("num", "num", "num", 0, 100, 0, G_PARAM_READWRITE);
  gst_param_spec_set_kind (pspec, gst_structure_new_empty ("x-mycorp-thing"));
  fail_unless (gst_structure_has_name (gst_param_spec_get_kind (pspec),
          "x-mycorp-thing"));

  /* the kind is released with the pspec */
  g_param_spec_unref (pspec);
}

GST_END_TEST static Suite *
gst_param_spec_suite (void)
{
  Suite *s = suite_create ("GstParamSpec");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_param_spec_fraction);
  tcase_add_test (tc_chain, test_param_spec_kind);

  return s;
}

GST_CHECK_MAIN (gst_param_spec);
