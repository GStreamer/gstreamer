#include <glib.h>

/* defines an array of strings named caps_list, that contains a list of caps for
   general tests. So if you don't know what caps to use to write a test, just
   include this file */

static const gchar *caps_list[] = {
  "audio/x-adpcm, layout=(string)quicktime; audio/x-adpcm, layout=(string)quicktime; audio/x-adpcm, layout=(string)wav; audio/x-adpcm, layout=(string)wav; audio/x-adpcm, layout=(string)dk3; audio/x-adpcm, layout=(string)dk3; audio/x-adpcm, layout=(string)dk4; audio/x-adpcm, layout=(string)dk4; audio/x-adpcm, layout=(string)westwood; audio/x-adpcm, layout=(string)westwood; audio/x-adpcm, layout=(string)smjpeg; audio/x-adpcm, layout=(string)smjpeg; audio/x-adpcm, layout=(string)microsoft; audio/x-adpcm, layout=(string)microsoft; audio/x-adpcm, layout=(string)4xm; audio/x-adpcm, layout=(string)4xm; audio/x-adpcm, layout=(string)xa; audio/x-adpcm, layout=(string)xa; audio/x-adpcm, layout=(string)adx; audio/x-adpcm, layout=(string)adx; audio/x-adpcm, layout=(string)ea; audio/x-adpcm, layout=(string)ea; audio/x-adpcm, layout=(string)g726; audio/x-adpcm, layout=(string)g726",
  "video/x-raw, width=(int)[ 16, 4096 ], height=(int)[ 16, 4096 ], framerate=(double)[ 0, 1.7976931348623157e+308 ], format=(string)I420; video/x-raw, width=(int)[ 16, 4096 ], height=(int)[ 16, 4096 ], framerate=(double)[ 0, 1.7976931348623157e+308 ], format=(string)YUY2; video/x-raw, format=(string)RGB, width=(int)[ 16, 4096 ], height=(int)[ 16, 4096 ], framerate=(double)[ 0, 1.7976931348623157e+308 ]; video/x-raw, format=(string)BGR, width=(int)[ 16, 4096 ], height=(int)[ 16, 4096 ], framerate=(double)[ 0, 1.7976931348623157e+308 ]; video/x-raw, width=(int)[ 16, 4096 ], height=(int)[ 16, 4096 ], framerate=(double)[ 0, 1.7976931348623157e+308 ], format=(string)Y42B; video/x-raw, format=(string)BGRx, width=(int)[ 16, 4096 ], height=(int)[ 16, 4096 ], framerate=(double)[ 0, 1.7976931348623157e+308 ]; video/x-raw, width=(int)[ 16, 4096 ], height=(int)[ 16, 4096 ], framerate=(double)[ 0, 1.7976931348623157e+308 ], format=(string)YUV9; video/x-raw, width=(int)[ 16, 4096 ], height=(int)[ 16, 4096 ], framerate=(double)[ 0, 1.7976931348623157e+308 ], format=(string)Y41B; video/x-raw, format=(string)RGB16 ,width=(int)[ 16, 4096 ], height=(int)[ 16, 4096 ], framerate=(double)[ 0, 1.7976931348623157e+308 ]; video/x-raw, format=(string)RGB15, width=(int)[ 16, 4096 ], height=(int)[ 16, 4096 ], framerate=(double)[ 0, 1.7976931348623157e+308 ]",
  "video/x-raw, format=(string){ YUY2, I420 }, width=(int)[ 1, 2147483647 ], height=(int)[ 1, 2147483647 ], framerate=(double)[ 0, 1.7976931348623157e+308 ]; video/x-jpeg, width=(int)[ 1, 2147483647 ], height=(int)[ 1, 2147483647 ]; video/x-divx, divxversion=(int)[ 3, 5 ], width=(int)[ 1, 2147483647 ], height=(int)[ 1, 2147483647 ]; video/x-xvid, width=(int)[ 1, 2147483647 ], height=(int)[ 1, 2147483647 ]; video/x-3ivx, width=(int)[ 1, 2147483647 ], height=(int)[ 1, 2147483647 ]; video/x-msmpeg, msmpegversion=(int)[ 41, 43 ], width=(int)[ 1, 2147483647 ], height=(int)[ 1, 2147483647 ]; video/mpeg, mpegversion=(int)1, systemstream=(boolean)false, width=(int)[ 1, 2147483647 ], height=(int)[ 1, 2147483647 ]; video/x-h263, width=(int)[ 1, 2147483647 ], height=(int)[ 1, 2147483647 ]; video/x-dv, systemstream=(boolean)false, width=(int)720, height=(int){ 576, 480 }; video/x-huffyuv, width=(int)[ 1, 2147483647 ], height=(int)[ 1, 2147483647 ]",
  "video/x-raw, format=(string){ YUY2, I420 }, width=(int)[ 16, 4096 ], height=(int)[ 16, 4096 ], framerate=(double)[ 0, 1.7976931348623157e+308 ]; image/jpeg, width=(int)[ 16, 4096 ], height=(int)[ 16, 4096 ]; video/x-divx, width=(int)[ 16, 4096 ], height=(int)[ 16, 4096 ], divxversion=(int)[ 3, 5 ]; video/x-xvid, width=(int)[ 16, 4096 ], height=(int)[ 16, 4096 ]; video/x-3ivx, width=(int)[ 16, 4096 ], height=(int)[ 16, 4096 ]; video/x-msmpeg, width=(int)[ 16, 4096 ], height=(int)[ 16, 4096 ], msmpegversion=(int)[ 41, 43 ]; video/mpeg, width=(int)[ 16, 4096 ], height=(int)[ 16, 4096 ], mpegversion=(int)1, systemstream=(boolean)false; video/x-h263, width=(int)[ 16, 4096 ], height=(int)[ 16, 4096 ]; video/x-dv, width=(int)720, height=(int){ 576, 480 }, systemstream=(boolean)false; video/x-huffyuv, width=(int)[ 16, 4096 ], height=(int)[ 16, 4096 ]",
  "video/x-raw, format=(string)BGRx, width=(int)[ 16, 4096 ], height=(int)[ 16, 4096 ], framerate=(double)[ 0, 1.7976931348623157e+308 ]; video/x-raw, format=(string)RGBx, width=(int)[ 16, 4096 ], height=(int)[ 16, 4096 ], framerate=(double)[ 0, 1.7976931348623157e+308 ]",
  "video/x-raw, format=(string)BGRx, width=(int)[ 16, 4096 ], height=(int)[ 16, 4096 ], framerate=(double)[ 0, 1.7976931348623157e+308 ]",
  "video/x-raw, format=(string){ I420 }, width=(int)[ 16, 4096 ], height=(int)[ 16, 4096 ], framerate=(double)[ 0, 1.7976931348623157e+308 ]",
  "video/x-raw, format=(string) xBGR, framerate = (double) [ 0, max ]",
  "video/x-raw, format=(string) RGBx, framerate = (double) [ 0, max ]",
  "video/x-raw,\\ format=(string) { RGBA, RGBx, BGRx, BGRA, xRGB, xBGR, ARGB, ABGR }",
  /* Test fraction type */
  "test/gst-fraction, fraction = (fraction) 1/8",
  "test/gst-fraction, fraction = (fraction) MIN",
  "test/gst-fraction, fraction = (fraction) MAX",
  /* Test fraction range */
  "test/gst-fraction-range, fraction = (fraction) [ 1/4, 1/3 ]",
  "test/gst-fraction-range, fraction = (fraction) [ MIN, MAX ]",
  "test/gst-fraction-range, fraction = (fraction) [ 1/MAX, MAX ]",
  /* Test lists of fractions and fraction ranges */
  "test/gst-fraction-range, fraction = (fraction) { [ 1/4, 1/3 ], 1/8 }",
  "test/gst-fraction-range, fraction = (fraction) { [ 1/4, 1/3 ], [ 1/8, 2/8 ] }",
  /* FlagSet */
  "test/gst-flags,thingy=1f394:00ff8ff",
  "test/gst-flags,thingy=101ffff1f394:fff00ff00ff8ff",
  /* Some random checks */
  "video/x-raw, format = (string) { I420, Y42B, Y444 }, framerate = (fraction) [1/MAX, MAX], width = (int) [ 1, MAX ], height = (int) [ 1, MAX ]",

  "ANY",
  "EMPTY"
};

