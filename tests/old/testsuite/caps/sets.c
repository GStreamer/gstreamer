/*
 * Copyright (C) 2004 Benjamin Otte <in7y118@public.uni-hamburg.de>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <gst/gst.h>
#include <string.h>

static const gchar *caps[] = {
  "video/x-raw-yuv, width=(int)[ 16, 4096 ], height=(int)[ 16, 4096 ], framerate=(double)[ 0, 1.7976931348623157e+308 ], format=(fourcc)I420; video/x-raw-yuv, width=(int)[ 16, 4096 ], height=(int)[ 16, 4096 ], framerate=(double)[ 0, 1.7976931348623157e+308 ], format=(fourcc)YUY2; video/x-raw-rgb, width=(int)[ 16, 4096 ], height=(int)[ 16, 4096 ], framerate=(double)[ 0, 1.7976931348623157e+308 ], bpp=(int)24, depth=(int)24, red_mask=(int)16711680, green_mask=(int)65280, blue_mask=(int)255, endianness=(int)4321; video/x-raw-rgb, width=(int)[ 16, 4096 ], height=(int)[ 16, 4096 ], framerate=(double)[ 0, 1.7976931348623157e+308 ], bpp=(int)24, depth=(int)24, red_mask=(int)255, green_mask=(int)65280, blue_mask=(int)16711680, endianness=(int)4321; video/x-raw-yuv, width=(int)[ 16, 4096 ], height=(int)[ 16, 4096 ], framerate=(double)[ 0, 1.7976931348623157e+308 ], format=(fourcc)Y42B; video/x-raw-rgb, width=(int)[ 16, 4096 ], height=(int)[ 16, 4096 ], framerate=(double)[ 0, 1.7976931348623157e+308 ], bpp=(int)32, depth=(int)24, red_mask=(int)65280, green_mask=(int)16711680, blue_mask=(int)-16777216, endianness=(int)4321; video/x-raw-yuv, width=(int)[ 16, 4096 ], height=(int)[ 16, 4096 ], framerate=(double)[ 0, 1.7976931348623157e+308 ], format=(fourcc)YUV9; video/x-raw-yuv, width=(int)[ 16, 4096 ], height=(int)[ 16, 4096 ], framerate=(double)[ 0, 1.7976931348623157e+308 ], format=(fourcc)Y41B; video/x-raw-rgb, width=(int)[ 16, 4096 ], height=(int)[ 16, 4096 ], framerate=(double)[ 0, 1.7976931348623157e+308 ], bpp=(int)16, depth=(int)16, red_mask=(int)63488, green_mask=(int)2016, blue_mask=(int)31, endianness=(int)1234; video/x-raw-rgb, width=(int)[ 16, 4096 ], height=(int)[ 16, 4096 ], framerate=(double)[ 0, 1.7976931348623157e+308 ], bpp=(int)16, depth=(int)15, red_mask=(int)31744, green_mask=(int)992, blue_mask=(int)31, endianness=(int)1234",
  "video/x-raw-yuv, format=(fourcc){ YUY2, I420 }, width=(int)[ 1, 2147483647 ], height=(int)[ 1, 2147483647 ]; video/x-jpeg, width=(int)[ 1, 2147483647 ], height=(int)[ 1, 2147483647 ]; video/x-divx, divxversion=(int)[ 3, 5 ], width=(int)[ 1, 2147483647 ], height=(int)[ 1, 2147483647 ]; video/x-xvid, width=(int)[ 1, 2147483647 ], height=(int)[ 1, 2147483647 ]; video/x-3ivx, width=(int)[ 1, 2147483647 ], height=(int)[ 1, 2147483647 ]; video/x-msmpeg, msmpegversion=(int)[ 41, 43 ], width=(int)[ 1, 2147483647 ], height=(int)[ 1, 2147483647 ]; video/mpeg, mpegversion=(int)1, systemstream=(boolean)false, width=(int)[ 1, 2147483647 ], height=(int)[ 1, 2147483647 ]; video/x-h263, width=(int)[ 1, 2147483647 ], height=(int)[ 1, 2147483647 ]; video/x-dv, systemstream=(boolean)false, width=(int)720, height=(int){ 576, 480 }; video/x-huffyuv, width=(int)[ 1, 2147483647 ], height=(int)[ 1, 2147483647 ]",
  "video/x-raw-yuv, format=(fourcc){ YUY2, I420 }, width=(int)[ 16, 4096 ], height=(int)[ 16, 4096 ]; image/jpeg, width=(int)[ 16, 4096 ], height=(int)[ 16, 4096 ]; video/x-divx, width=(int)[ 16, 4096 ], height=(int)[ 16, 4096 ], divxversion=(int)[ 3, 5 ]; video/x-xvid, width=(int)[ 16, 4096 ], height=(int)[ 16, 4096 ]; video/x-3ivx, width=(int)[ 16, 4096 ], height=(int)[ 16, 4096 ]; video/x-msmpeg, width=(int)[ 16, 4096 ], height=(int)[ 16, 4096 ], msmpegversion=(int)[ 41, 43 ]; video/mpeg, width=(int)[ 16, 4096 ], height=(int)[ 16, 4096 ], mpegversion=(int)1, systemstream=(boolean)false; video/x-h263, width=(int)[ 16, 4096 ], height=(int)[ 16, 4096 ]; video/x-dv, width=(int)720, height=(int){ 576, 480 }, systemstream=(boolean)false; video/x-huffyuv, width=(int)[ 16, 4096 ], height=(int)[ 16, 4096 ]",
  "video/x-raw-rgb, bpp=(int)32, depth=(int)24, endianness=(int)4321, red_mask=(int)65280, green_mask=(int)16711680, blue_mask=(int)-16777216, width=(int)[ 16, 4096 ], height=(int)[ 16, 4096 ], framerate=(double)[ 0, 1.7976931348623157e+308 ]; video/x-raw-rgb, bpp=(int)32, depth=(int)24, endianness=(int)4321, red_mask=(int)-16777216, green_mask=(int)16711680, blue_mask=(int)65280, width=(int)[ 16, 4096 ], height=(int)[ 16, 4096 ], framerate=(double)[ 0, 1.7976931348623157e+308 ]",
  "video/x-raw-rgb, bpp=(int)32, depth=(int)24, endianness=(int)4321, red_mask=(int)65280, green_mask=(int)16711680, blue_mask=(int)-16777216, width=(int)[ 16, 4096 ], height=(int)[ 16, 4096 ], framerate=(double)[ 0, 1.7976931348623157e+308 ]",
  "video/x-raw-yuv, format=(fourcc){ I420 }, width=(int)[ 16, 4096 ], height=(int)[ 16, 4096 ]",
  "ANY",
  "EMPTY"
};

static void
check_caps (const gchar * eins, const gchar * zwei)
{
  GstCaps *one, *two, *test, *test2, *test3, *test4;

  one = gst_caps_from_string (eins);
  two = gst_caps_from_string (zwei);
  g_print ("      A  =  %u\n", strlen (eins));
  g_print ("      B  =  %u\n", strlen (zwei));

  test = gst_caps_intersect (one, two);
  if (gst_caps_is_equal (one, two)) {
    g_print ("         EQUAL\n\n");
    g_assert (gst_caps_is_equal (one, test));
    g_assert (gst_caps_is_equal (two, test));
  } else if (!gst_caps_is_any (one) || gst_caps_is_empty (two)) {
    test2 = gst_caps_subtract (one, test);
    g_print ("  A - B  =  %u\n", strlen (gst_caps_to_string (test2)));
    /* test2 = one - (one A two) = one - two */
    test3 = gst_caps_intersect (test2, two);
    g_print ("  empty  =  %s\n", gst_caps_to_string (test3));
    g_assert (gst_caps_is_empty (test3));
    gst_caps_free (test3);
    test3 = gst_caps_union (test2, two);
    g_print ("  A + B  =  %u\n", strlen (gst_caps_to_string (test3)));
    /* test3 = one - two + two = one + two */
    g_print ("  A + B  =  %s\n", gst_caps_to_string (gst_caps_subtract (one,
                test3)));
    g_assert (gst_caps_is_subset (one, test3));
    test4 = gst_caps_union (one, two);
    g_assert (gst_caps_is_equal (test3, test4));
    g_print ("         NOT EQUAL\n\n");
    gst_caps_free (test2);
    gst_caps_free (test3);
    gst_caps_free (test4);
  } else {
    g_print ("         ANY CAPS\n\n");
  }
  gst_caps_free (test);
  gst_caps_free (two);
  gst_caps_free (one);
}

gint
main (gint argc, gchar ** argv)
{
  guint i, j;

  gst_init (&argc, &argv);

  for (i = 0; i < G_N_ELEMENTS (caps); i++) {
    for (j = 0; j < G_N_ELEMENTS (caps); j++) {
      g_print ("%u - %u\n", i, j);
      check_caps (caps[i], caps[j]);
    }
  }

  return 0;
}
