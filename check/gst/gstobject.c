/* GStreamer
 *
 * unit test for GstObject
 *
 * Copyright (C) <2004> Thomas Vander Stichele <thomas at apestaart dot org>
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

#include <signal.h>
#include <string.h>
#include <stdlib.h>

#include <check.h>

#include <gst/gst.h>

/*
  Create a fake subclass
 */
typedef struct _GstFakeObjectClass GstFakeObjectClass;
typedef struct _GstFakeObject GstFakeObject;

struct _GstFakeObject
{
  GstObject object;
};

struct _GstFakeObjectClass
{
  GstObjectClass parent_class;
};

GType _gst_fake_object_type = 0;

//static GstObjectClass *parent_class = NULL;
//static guint gst_fake_object_signals[LAST_SIGNAL] = { 0 };

GType
gst_fake_object_get_type (void)
{
  if (!_gst_fake_object_type) {
    static const GTypeInfo fake_object_info = {
      sizeof (GstFakeObjectClass),
      NULL,                     //gst_fake_object_base_class_init,
      NULL,                     //gst_fake_object_base_class_finalize,
      NULL,                     //(GClassInitFunc) gst_fake_object_class_init,
      NULL,
      NULL,
      sizeof (GstFakeObject),
      0,
      NULL,                     //(GInstanceInitFunc) gst_fake_object_init,
      NULL
    };

    _gst_fake_object_type = g_type_register_static (GST_TYPE_OBJECT,
        "GstFakeObject", &fake_object_info, 0);
  }
  return _gst_fake_object_type;
}

/* g_object_new on abstract GstObject should fail */
START_TEST (test_fail_abstract_new)
{
  GstObject *object;

  object = g_object_new (gst_object_get_type (), NULL);
  /* this should assert and segfault, but we add fallbacks anyway */
  fail_unless (object == NULL, "Created an instance of abstract GstObject");
}

END_TEST
/* g_object_new on GstFakeObject should succeed */
START_TEST (test_fake_object_new)
{
  GstObject *object;

  object = g_object_new (gst_fake_object_get_type (), NULL);
  fail_if (object == NULL, "Failed to create instance of GstFakeObject");
  fail_unless (GST_IS_OBJECT (object),
      "GstFakeObject instance is not a GstObject");
}

END_TEST
/* GstFakeObject name tests */
START_TEST (test_fake_object_name)
{
  GstObject *object;
  gchar *name;
  gchar *name2;

  object = g_object_new (gst_fake_object_get_type (), NULL);

  name = gst_object_get_name (object);
  fail_if (name != NULL, "Newly created object has a name");

  /* give a random name by setting with NULL;
   * GstFakeObject class -> fakeobject%d */
  gst_object_set_name (object, NULL);
  name = gst_object_get_name (object);
  fail_if (name == NULL, "Random name was not assigned");
  fail_if (strncmp (name, "fakeobject", 10) != 0,
      "Random name %s does not start with Gst", name);
  g_free (name);

  gst_object_set_name (object, "fake");
  name = gst_object_get_name (object);
  fail_if (name == NULL, "Failed to get name of GstFakeObject");
  fail_if (strcmp (name, "fake") != 0, "Name of GstFakeObject is not 'fake'");

  /* change the gotten name to see that it's a copy and not the original */
  name[0] = 'm';
  name2 = gst_object_get_name (object);
  fail_if (strcmp (name2, "fake") != 0,
      "Copy of object name affected actual object name");
  g_free (name);
  g_free (name2);
}

END_TEST Suite *
gst_object_suite (void)
{
  Suite *s = suite_create ("GstObject");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test_raise_signal (tc_chain, test_fail_abstract_new, SIGSEGV);
  tcase_add_test (tc_chain, test_fake_object_new);
  tcase_add_test (tc_chain, test_fake_object_name);
  //tcase_add_checked_fixture (tc_chain, setup, teardown);
  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = gst_object_suite ();
  SRunner *sr = srunner_create (s);

  gst_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
