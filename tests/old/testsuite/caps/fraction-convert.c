/* GStreamer
 *
 * fraction-convert.c: test for GstFraction transform
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

#include <math.h>
#include <gst/gst.h>
#include <glib.h>

static void
check_from_double_convert (gdouble value, gint num, gint denom, gdouble prec)
{
  GValue value1 = { 0 };
  GValue value2 = { 0 };
  gdouble check;
  gint res_num, res_denom;

  g_value_init (&value1, G_TYPE_DOUBLE);
  g_value_init (&value2, GST_TYPE_FRACTION);

  g_value_set_double (&value1, value);
  g_value_transform (&value1, &value2);
  g_print ("%s = %s ? (expected: %d/%d )\n",
      gst_value_serialize (&value1), gst_value_serialize (&value2), num, denom);

  res_num = gst_value_get_fraction_numerator (&value2);
  res_denom = gst_value_get_fraction_denominator (&value2);
  if (res_num == num && res_denom == denom) {
    g_print ("best conversion.\n");
  } else {
    if (fabs (value - res_num / (gdouble) res_denom) <= prec) {
      g_print ("acceptable suboptimal conversion.\n");
    } else {
      g_print ("unacceptable suboptimal conversion.\n");
      g_assert_not_reached ();
    }
  }
  g_value_transform (&value2, &value1);
  g_print ("%s = %s\n",
      gst_value_serialize (&value2), gst_value_serialize (&value1));
  check = g_value_get_double (&value1);
  g_assert (fabs (value - check) <= prec);
}

static void
check_from_fraction_convert (gint num, gint denom, gdouble prec)
{
  GValue value1 = { 0 };
  GValue value2 = { 0 };
  gdouble value;
  gint res_num, res_denom;

  g_value_init (&value1, GST_TYPE_FRACTION);
  g_value_init (&value2, G_TYPE_DOUBLE);

  gst_value_set_fraction (&value1, num, denom);
  g_value_transform (&value1, &value2);

  value = g_value_get_double (&value2);
  g_assert (fabs (value - ((gdouble) num) / denom) < prec);

  g_print ("%s = %s, %2.50lf as double\n",
      gst_value_serialize (&value1), gst_value_serialize (&value2), value);

  g_value_transform (&value2, &value1);
  g_print ("%s = %s ? (expected: %d/%d )\n",
      gst_value_serialize (&value2), gst_value_serialize (&value1), num, denom);
  value = g_value_get_double (&value2);

  res_num = gst_value_get_fraction_numerator (&value1);
  res_denom = gst_value_get_fraction_denominator (&value1);
  if (res_num == num && res_denom == denom) {
    g_print ("best conversion.\n");
  } else {
    if (fabs (value - res_num / (gdouble) res_denom) <= prec) {
      g_print ("acceptable suboptimal conversion.\n");
    } else {
      g_print ("unacceptable suboptimal conversion.\n");
      g_assert_not_reached ();
    }
  }

  g_value_unset (&value2);
  g_value_unset (&value1);
}

static void
transform_test (void)
{
  check_from_fraction_convert (30000, 1001, 1.0e-9);
  check_from_fraction_convert (1, G_MAXINT, 1.0e-9);
  check_from_fraction_convert (G_MAXINT, 1, 1.0e-9);

  check_from_double_convert (0.0, 0, 1, 1.0e-9);
  check_from_double_convert (1.0, 1, 1, 1.0e-9);
  check_from_double_convert (-1.0, -1, 1, 1.0e-9);
  check_from_double_convert (M_PI, 1881244168, 598818617, 1.0e-9);
  check_from_double_convert (-M_PI, -1881244168, 598818617, 1.0e-9);

  check_from_double_convert (G_MAXDOUBLE, G_MAXINT, 1, G_MAXDOUBLE);
  check_from_double_convert (G_MINDOUBLE, 0, 1, G_MAXDOUBLE);
  check_from_double_convert (-G_MAXDOUBLE, -G_MAXINT, 1, G_MAXDOUBLE);
  check_from_double_convert (-G_MINDOUBLE, 0, 1, G_MAXDOUBLE);

  check_from_double_convert (((gdouble) G_MAXINT) + 1, G_MAXINT, 1,
      G_MAXDOUBLE);
  check_from_double_convert (((gdouble) G_MININT) - 1, G_MININT + 1, 1,
      G_MAXDOUBLE);

  check_from_double_convert (G_MAXINT - 1, G_MAXINT - 1, 1, 0);
  check_from_double_convert (G_MININT + 1, G_MININT + 1, 1, 0);
}

int
main (int argc, char *argv[])
{
  gst_init (&argc, &argv);

  transform_test ();

  return 0;
}
