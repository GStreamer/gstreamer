/* GStreamer
 *
 * fraction.c: test for all GstFraction operations
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

#include <gst/gst.h>
#include <glib.h>

static void
check_multiplication (int num1, int den1, int num2, int den2, int num_result,
    int den_result)
{
  GValue value1 = { 0 };
  GValue value2 = { 0 };
  GValue value3 = { 0 };

  g_value_init (&value1, GST_TYPE_FRACTION);
  g_value_init (&value2, GST_TYPE_FRACTION);
  g_value_init (&value3, GST_TYPE_FRACTION);

  gst_value_set_fraction (&value1, num1, den1);
  gst_value_set_fraction (&value2, num2, den2);
  g_print ("%d/%d * %d/%d = ", num1, den1, num2, den2);
  gst_value_fraction_multiply (&value3, &value1, &value2);
  g_print ("%d/%d (should be %d/%d)\n",
      gst_value_get_fraction_numerator (&value3),
      gst_value_get_fraction_denominator (&value3), num_result, den_result);
  g_assert (gst_value_get_fraction_numerator (&value3) == num_result);
  g_assert (gst_value_get_fraction_denominator (&value3) == den_result);

  g_value_unset (&value1);
  g_value_unset (&value2);
  g_value_unset (&value3);
}

static void
check_equal (int num1, int den1, int num2, int den2)
{
  GValue value1 = { 0 };
  GValue value2 = { 0 };

  g_value_init (&value1, GST_TYPE_FRACTION);
  g_value_init (&value2, GST_TYPE_FRACTION);

  gst_value_set_fraction (&value1, num1, den1);
  gst_value_set_fraction (&value2, num2, den2);
  g_print ("%d/%d == %d/%d ? ", num1, den1, num2, den2);
  g_assert (gst_value_compare (&value1, &value2) == GST_VALUE_EQUAL);
  g_print ("yes\n");

  g_value_unset (&value1);
  g_value_unset (&value2);
}

static void
zero_test (void)
{
  GValue value1 = { 0 };

  g_value_init (&value1, GST_TYPE_FRACTION);

  /* fractions are initialized at 0 */
  g_assert (gst_value_get_fraction_numerator (&value1) == 0);
  g_assert (gst_value_get_fraction_denominator (&value1) == 1);

  /* every zero value is set to 0/1 */
  gst_value_set_fraction (&value1, 0, 235);
  g_assert (gst_value_get_fraction_numerator (&value1) == 0);
  g_assert (gst_value_get_fraction_denominator (&value1) == 1);
  gst_value_set_fraction (&value1, 0, -G_MAXINT);
  g_assert (gst_value_get_fraction_numerator (&value1) == 0);
  g_assert (gst_value_get_fraction_denominator (&value1) == 1);

  g_value_unset (&value1);
}

int
main (int argc, char *argv[])
{
  GValue value1 = { 0 };
  GValue value2 = { 0 };
  GValue value3 = { 0 };

  gst_init (&argc, &argv);

  g_value_init (&value1, GST_TYPE_FRACTION);
  g_value_init (&value2, GST_TYPE_FRACTION);
  g_value_init (&value3, GST_TYPE_FRACTION);

  /*** zeroes ***/

  /* basic zero tests */
  zero_test ();

  /* check all zeroes are zeroes */
  check_equal (0, 1, 0, 12345);
  check_equal (0, 1, 0, -1);

  /* check multiplying with zeroes results in zeroes */
  check_multiplication (0, 1, 17, 18, 0, 1);
  check_multiplication (0, -13, -G_MAXINT, 2736, 0, 1);

  /*** large numbers ***/

  /* check multiplying large numbers works */
  check_multiplication (G_MAXINT, 1, G_MAXINT - 1, G_MAXINT, G_MAXINT - 1, 1);
  check_multiplication (-G_MAXINT, 1, -G_MAXINT + 1, -G_MAXINT, -G_MAXINT + 1,
      1);
  check_multiplication (G_MAXINT / 28, 459, -28, -G_MAXINT / 459,
      G_MAXINT / 28 * 28, G_MAXINT / 459 * 459);
  check_multiplication (3117 * 13, -17, 3117 * 17, 13, -3117 * 3117, 1);

  return 0;
}
