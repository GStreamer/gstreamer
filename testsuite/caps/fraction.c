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

void
test (void)
{
  GValue value1 = { 0 };
  GValue value2 = { 0 };
  GValue value3 = { 0 };

  //gboolean ret;

  /* comparing 2/3 with 3/4 */
  g_value_init (&value1, GST_TYPE_FRACTION);
  gst_value_set_fraction (&value1, 2, 3);
  g_value_init (&value2, GST_TYPE_FRACTION);
  gst_value_set_fraction (&value2, 3, 4);
  g_assert (gst_value_compare (&value1, &value2) == GST_VALUE_LESS_THAN);
  g_assert (gst_value_compare (&value2, &value1) == GST_VALUE_GREATER_THAN);
  g_assert (gst_value_compare (&value1, &value1) == GST_VALUE_EQUAL);
  g_value_unset (&value1);
  g_value_unset (&value2);

  /* comparing -4/5 with 2/-3 */
  g_value_init (&value1, GST_TYPE_FRACTION);
  gst_value_set_fraction (&value1, -4, 5);
  g_value_init (&value2, GST_TYPE_FRACTION);
  gst_value_set_fraction (&value2, 2, -3);
  g_assert (gst_value_compare (&value1, &value2) == GST_VALUE_LESS_THAN);
  g_assert (gst_value_compare (&value2, &value1) == GST_VALUE_GREATER_THAN);
  g_assert (gst_value_compare (&value1, &value1) == GST_VALUE_EQUAL);
  g_value_unset (&value1);
  g_value_unset (&value2);

  /* comparing 10/100 with 20/2000 */
  g_value_init (&value1, GST_TYPE_FRACTION);
  gst_value_set_fraction (&value1, 10, 100);
  g_value_init (&value2, GST_TYPE_FRACTION);
  gst_value_set_fraction (&value2, 200, 2000);
  g_assert (gst_value_compare (&value1, &value2) == GST_VALUE_EQUAL);
  g_value_unset (&value1);
  g_value_unset (&value2);

  /* comparing -4/5 with 2/-3 */
  g_value_init (&value1, GST_TYPE_FRACTION);
  gst_value_set_fraction (&value1, -4, 5);
  g_value_init (&value2, GST_TYPE_FRACTION);
  gst_value_set_fraction (&value2, 2, -3);
  g_assert (gst_value_compare (&value1, &value2) == GST_VALUE_LESS_THAN);
  g_assert (gst_value_compare (&value2, &value1) == GST_VALUE_GREATER_THAN);
  g_assert (gst_value_compare (&value1, &value1) == GST_VALUE_EQUAL);
  g_value_unset (&value1);
  g_value_unset (&value2);

  /* multiplying 4/5 with 3/-2 */
  g_value_init (&value1, GST_TYPE_FRACTION);
  gst_value_set_fraction (&value1, 4, 5);
  g_value_init (&value2, GST_TYPE_FRACTION);
  gst_value_set_fraction (&value2, 3, -2);
  g_value_init (&value3, GST_TYPE_FRACTION);
  g_assert (gst_value_fraction_multiply (&value3, &value1, &value2));
  g_assert (gst_value_get_fraction_nominator (&value3) == -6);
  g_assert (gst_value_get_fraction_denominator (&value3) == 5);
  g_value_unset (&value1);
  g_value_unset (&value2);
  g_value_unset (&value3);
}

int
main (int argc, char *argv[])
{

  gst_init (&argc, &argv);

  test ();

  return 0;
}
