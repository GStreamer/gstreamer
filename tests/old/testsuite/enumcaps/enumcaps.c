/* GStreamer test
 * (c) 2004 Ronald Bultje <rbultje@ronald.bitfreak.net>
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
#include <config.h>
#endif

#include <gst/gst.h>

typedef enum
{
  TEST_YES,
  TEST_NO
}
TestBool;

#define TEST_BOOL_TYPE (test_bool_get_type ())
GType
test_bool_get_type (void)
{
  static GType etype = 0;

  if (etype == 0) {
    static const GEnumValue values[] = {
      {TEST_YES, "TEST_YES", "yes"},
      {TEST_NO, "TEST_NO", "no"},
      {0, NULL, NULL}
    };

    etype = g_enum_register_static ("TestBool", values);
  }
  return etype;
}

gint
main (gint argc, gchar * argv[])
{
  gchar *str;
  GstCaps *caps, *res_caps;
  GstStructure *strc;
  GValue value = { 0 };
  TestBool yes, no;

  /* register multichannel type */
  gst_init (&argc, &argv);
  test_bool_get_type ();

  /* test some caps */
  caps = gst_caps_new_simple ("application/x-gst-test", NULL);
  str = gst_caps_to_string (caps);
  g_assert (str);
  g_free (str);

  /* set enums in list */
  strc = gst_caps_get_structure (caps, 0);
  g_value_init (&value, TEST_BOOL_TYPE);
  g_value_set_enum (&value, TEST_YES);
  gst_structure_set_value (strc, "yes", &value);
  g_value_set_enum (&value, TEST_NO);
  gst_structure_set_value (strc, "no", &value);
  g_value_unset (&value);

  /* test to-/from-string conversions for enums */
  str = gst_caps_to_string (caps);
  g_assert (str);
  res_caps = gst_caps_from_string (str);
  g_free (str);

  /* see if all worked */
  strc = gst_caps_get_structure (res_caps, 0);
  yes = g_value_get_enum (gst_structure_get_value (strc, "yes"));
  no = g_value_get_enum (gst_structure_get_value (strc, "no"));
  g_assert (yes == TEST_YES && no == TEST_NO);
  gst_caps_free (caps);
  gst_caps_free (res_caps);

  /* yes */
  return 0;
}
