/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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
/* Element-Checklist-Version: 5 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>
#include <math.h>

#include <gst/gst.h>


static void
gst_audioscale_expand_value (GValue * dest, const GValue * src)
{
  int rate_min, rate_max;

  if (G_VALUE_TYPE (src) == G_TYPE_INT ||
      G_VALUE_TYPE (src) == GST_TYPE_INT_RANGE) {
    if (G_VALUE_TYPE (src) == G_TYPE_INT) {
      rate_min = g_value_get_int (src);
      rate_max = rate_min;
    } else {
      rate_min = gst_value_get_int_range_min (src);
      rate_max = gst_value_get_int_range_max (src);
    }

    rate_min /= 2;
    if (rate_min < 1)
      rate_min = 1;
    if (rate_max < G_MAXINT / 2) {
      rate_max *= 2;
    } else {
      rate_max = G_MAXINT;
    }

    g_value_init (dest, GST_TYPE_INT_RANGE);
    gst_value_set_int_range (dest, rate_min, rate_max);
    return;
  }

  if (G_VALUE_TYPE (src) == GST_TYPE_LIST) {
    int i;

    g_value_init (dest, GST_TYPE_LIST);
    for (i = 0; i < gst_value_list_get_size (src); i++) {
      const GValue *s = gst_value_list_get_value (src, i);
      GValue d = { 0 };
      int j;

      gst_audioscale_expand_value (&d, s);

      for (j = 0; j < gst_value_list_get_size (dest); j++) {
        const GValue *s2 = gst_value_list_get_value (dest, j);
        GValue d2 = { 0 };

        gst_value_union (&d2, &d, s2);
        if (G_VALUE_TYPE (&d2) == GST_TYPE_INT_RANGE) {
          g_value_unset ((GValue *) s2);
          gst_value_init_and_copy ((GValue *) s2, &d2);
          break;
        }
        g_value_unset (&d2);
      }
      if (j == gst_value_list_get_size (dest)) {
        gst_value_list_append_value (dest, &d);
      }
      g_value_unset (&d);
    }

    if (gst_value_list_get_size (dest) == 1) {
      const GValue *s = gst_value_list_get_value (dest, 0);
      GValue d = { 0 };

      gst_value_init_and_copy (&d, s);
      g_value_unset (dest);
      gst_value_init_and_copy (dest, &d);
      g_value_unset (&d);
    }

    return;
  }

  GST_ERROR ("unexpected value type");
}

static GstCaps *
gst_audioscale_getcaps (const GstCaps * othercaps)
{
  GstCaps *caps;
  int i;

  caps = gst_caps_copy (othercaps);

  /* we do this hack, because the audioscale lib doesn't handle
   * rate conversions larger than a factor of 2 */
  for (i = 0; i < gst_caps_get_size (caps); i++) {
    GstStructure *structure = gst_caps_get_structure (caps, i);
    const GValue *value;
    GValue dest = { 0 };

    value = gst_structure_get_value (structure, "rate");
    if (value == NULL) {
      GST_ERROR ("caps structure doesn't have required rate field");
      return NULL;
    }

    gst_audioscale_expand_value (&dest, value);

    gst_structure_set_value (structure, "rate", &dest);
  }

  return caps;
}


void
test_caps (const char *s)
{
  GstCaps *caps;
  GstCaps *caps2;
  char *s2;

  caps = gst_caps_from_string (s);
  caps2 = gst_audioscale_getcaps (caps);
  s2 = gst_caps_to_string (caps2);

  g_print ("original: %s\nfiltered: %s\n\n", s, s2);

  g_free (s2);
  gst_caps_free (caps);
  gst_caps_free (caps2);
}


int
main (int argc, char *argv[])
{

  gst_init (&argc, &argv);

  test_caps ("audio/x-raw-int, rate=(int)1");
  test_caps ("audio/x-raw-int, rate=(int)10");
  test_caps ("audio/x-raw-int, rate=(int)100");
  test_caps ("audio/x-raw-int, rate=(int)10000");
  test_caps ("audio/x-raw-int, rate=(int)2000000000");

  test_caps ("audio/x-raw-int, rate=(int)[1,100]");
  test_caps ("audio/x-raw-int, rate=(int)[1000,40000]");

  test_caps ("audio/x-raw-int, rate=(int){1,100}");
  test_caps ("audio/x-raw-int, rate=(int){100,200,300}");
  test_caps ("audio/x-raw-int, rate=(int){[100,200],1000}");

  return 0;
}
