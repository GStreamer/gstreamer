/*
 * GStreamer
 * Copyright (C) 2009 Edward Hervey <bilboed@bilboed.com>
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

/**
 * SECTION:element-HDVParse
 *
 * <refsect2>
 * <title>Example launch line</title>
 * <para>
 * <programlisting>
 * gst-launch -v -m filesrc ! mpegtsdemux ! hdvparse ! fakesink silent=TRUE
 * </programlisting>
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <math.h>

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>

#include "gsthdvparse.h"

GST_DEBUG_CATEGORY_STATIC (gst_hdvparse_debug);
#define GST_CAT_DEFAULT gst_hdvparse_debug

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
};



#define CLOCK_BASE 9LL
#define CLOCK_FREQ (CLOCK_BASE * 10000)

#define MPEGTIME_TO_GSTTIME(time) (gst_util_uint64_scale ((time), \
            GST_MSECOND/10, CLOCK_BASE))
#define GSTTIME_TO_MPEGTIME(time) (gst_util_uint64_scale ((time), \
            CLOCK_BASE, GST_MSECOND/10))

/* If set to 1, then extra validation will be applied to check
 * for complete spec compliance wherever applicable. */
#define VALIDATE 0

/* Binary-coded decimal reading macro */
#define BCD(c) ( ((((c) >> 4) & 0x0f) * 10) + ((c) & 0x0f) )
/* Same as before, but with a mask */
#define BCD_M(c, mask) (BCD ((c) & (mask)))

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */
static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("hdv/aux-v;hdv/aux-a")
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS
    ("hdv/aux-v,parsed=(boolean)True;hdv/aux-a,parsed=(boolean)True")
    );

/* debug category for fltering log messages
 *
 * exchange the string 'Template HDVParse' with your description
 */
#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_hdvparse_debug, "hdvparse", 0, "HDV private stream parser");

GST_BOILERPLATE_FULL (GstHDVParse, gst_hdvparse, GstBaseTransform,
    GST_TYPE_BASE_TRANSFORM, DEBUG_INIT);

static GstFlowReturn gst_hdvparse_transform_ip (GstBaseTransform * base,
    GstBuffer * outbuf);
static GstCaps *gst_hdvparse_transform_caps (GstBaseTransform * trans,
    GstPadDirection dir, GstCaps * incaps);

/* GObject vmethod implementations */

static void
gst_hdvparse_base_init (gpointer klass)
{

  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_set_static_metadata (element_class, "HDVParser",
      "Data/Parser",
      "HDV private stream Parser", "Edward Hervey <bilboed@bilboed.com>");
}

/* initialize the HDVParse's class */
static void
gst_hdvparse_class_init (GstHDVParseClass * klass)
{
  GST_BASE_TRANSFORM_CLASS (klass)->transform_ip =
      GST_DEBUG_FUNCPTR (gst_hdvparse_transform_ip);
  GST_BASE_TRANSFORM_CLASS (klass)->transform_caps =
      GST_DEBUG_FUNCPTR (gst_hdvparse_transform_caps);
}

/* initialize the new element
 * initialize instance structure
 */
static void
gst_hdvparse_init (GstHDVParse * filter, GstHDVParseClass * klass)
{
  GstBaseTransform *transform = GST_BASE_TRANSFORM (filter);

  gst_base_transform_set_in_place (transform, TRUE);
  gst_base_transform_set_passthrough (transform, TRUE);
}

static GstCaps *
gst_hdvparse_transform_caps (GstBaseTransform * trans, GstPadDirection dir,
    GstCaps * incaps)
{
  GstCaps *res = NULL;
  GstStructure *st = gst_caps_get_structure (incaps, 0);

  GST_WARNING_OBJECT (trans, "dir:%d, incaps:%" GST_PTR_FORMAT, dir, incaps);

  if (dir == GST_PAD_SINK) {
    res = gst_caps_new_simple (gst_structure_get_name (st),
        "parsed", G_TYPE_BOOLEAN, TRUE, NULL);
  } else {
    res = gst_caps_new_simple (gst_structure_get_name (st), NULL);
  }

  return res;
}


static inline const gchar *
sfr_to_framerate (guint8 sfr)
{
  switch (sfr) {
    case 4:
      return "30000/1001";
    case 3:
      return "25/1";
    case 1:
      return "24000/1001";
    default:
      return "RESERVED";
  }
}

static GstFlowReturn
parse_dv_multi_pack (GstHDVParse * filter, guint8 * data, guint64 size,
    GstStructure * st)
{
  guint64 offs = 1;

  while (size / 5) {
    GST_LOG ("DV pack 0x%x", data[offs]);
    switch (data[offs]) {
      case 0x70:{
        guint8 irispos, ae, agc, wbmode, whitebal, focusmode, focuspos;

        irispos = data[offs + 1] & 0x3f;
        ae = data[offs + 2] >> 4;
        agc = data[offs + 2] & 0xf;
        wbmode = data[offs + 3] >> 5;
        whitebal = data[offs + 3] & 0x1f;
        focusmode = data[offs + 4] >> 7;
        focuspos = data[offs + 4] & 0x7f;

        GST_LOG (" Consumer Camera 1");

        GST_LOG ("  Iris position %d (0x%x)", irispos, irispos);
        /* Iris position = 2 ^ (IP/8) (for 0 < IP < 0x3C) */
        if (irispos < 0x3c) {
          GST_LOG ("   IRIS F%0.2f", powf (2.0, (((float) irispos) / 8.0)));
          gst_structure_set (st, "aperture-fnumber", G_TYPE_FLOAT,
              powf (2.0, (((float) irispos) / 8.0)), NULL);
        } else if (irispos == 0x3d) {
          GST_LOG ("   IRIS < 1.0");
        } else if (irispos == 0x3e) {
          GST_LOG ("    IRIS closed");
        }

        /* AE Mode:
         * 0 : Full automatic
         * 1 : Gain Priority mode
         * 2 : Shutter Priority mode
         * 3 : Iris priority mode
         * 4 : Manual
         * ..: Reserved
         * F : No information */
        GST_LOG ("  AE Mode: %d (0x%x)", ae, ae);

        GST_LOG ("  AGC: %d (0x%x)", agc, agc);
        if (agc < 0xd) {
          /* This is what the spec says.. but I'm not seeing the same on my camera :( */
          GST_LOG ("   Gain:%02.2fdB", (agc * 3.0) - 3.0);
          gst_structure_set (st, "gain", G_TYPE_FLOAT, (agc * 3.0) - 3.0, NULL);
        }
        /* White balance mode
         * 0 : Automatic
         * 1 : hold
         * 2 : one push
         * 3 : pre-set
         * 7 : no-information */
        if (wbmode != 7)
          GST_LOG ("  White balance mode : %d (0x%x)", wbmode, wbmode);
        /* White balance
         * 0 : Candle
         * 1 : Incandescent lamp
         * 2 : low color temperature fluorescent lamp
         * 3 : high color temperature fluorescent lamp
         * 4 : sunlight
         * 5 : cloudy weather
         * F : No information
         */
        if (whitebal != 0xf)
          GST_LOG ("  White balance : %d (0x%x)", whitebal, whitebal);
        if (focuspos != 0x7f) {
          GST_LOG ("  Focus mode : %s", focusmode ? "MANUAL" : "AUTOMATIC");
          GST_LOG ("  Focus position: %d (0x%x)", focuspos, focuspos);
        }
      }
        break;
      case 0x71:{
        guint8 v_pan, h_pan, focal_length, e_zoom;
        gboolean is, zen;

        v_pan = data[offs + 1] & 0x3f;
        is = data[offs + 2] >> 7;
        h_pan = data[offs + 2] & 0x7f;
        focal_length = data[offs + 3];
        zen = data[offs + 4] >> 7;
        e_zoom = data[offs + 4] & 0x7f;

        GST_LOG (" Consumer Camera 2");
        if (v_pan != 0x3f)
          GST_LOG ("  Vertical Panning : %d (0x%d)", v_pan, v_pan);
        if (h_pan != 0x7f)
          GST_LOG ("  Horizontal Panning : %d (0x%d)", h_pan, h_pan);
        GST_LOG ("  Stabilizer : %s", is ? "OFF" : "ON");
        if (focal_length != 0xff)
          GST_LOG ("  Focal Length : %f mm",
              (focal_length & 0x7f) * pow (10, focal_length & 0x80));
        if (zen == 0)
          GST_LOG ("  Electric Zoom %02dd.%03d", e_zoom >> 5, e_zoom & 0x1f);
      }
        break;
      case 0x7f:{
        guint16 speed;
        guint16 speedint;

        GST_LOG (" Shutter");
        if (data[offs + 1] != 0xff)
          GST_LOG (" Shutter Speed (1) : %d, 0x%x",
              data[offs + 1], data[offs + 1]);
        if (data[offs + 2] != 0xff)
          GST_LOG (" Shutter Speed (1) : %d, 0x%x",
              data[offs + 2], data[offs + 2]);

        speed = data[offs + 3] | (data[offs + 4] & 0x7f) << 8;

        /* The shutter speed is 1/(CSS * horizontal scanning period) */
        /* FIXME : 34000 is a value interpolated by observations */
        speedint = (int) (34000.0 / (float) speed);
        /* Only the highest two decimal digits are valid */
        if (speedint > 100)
          speedint = speedint / 10 * 10;

        GST_LOG (" Shutter speed : 1/%d", speedint);
        gst_structure_set (st, "shutter-speed", GST_TYPE_FRACTION,
            1, speedint, NULL);
      }
        break;
      default:
        GST_MEMDUMP ("Unknown pack", data + offs, 5);
        break;
    }
    size -= 5;
    offs += 5;
  }
  return GST_FLOW_OK;
}

static GstFlowReturn
parse_video_frame (GstHDVParse * filter, guint8 * data, guint64 size,
    GstStructure * st)
{
  guint32 etn, bitrate;
  guint8 nbframes, data_h, hdr_size, sfr, sdm;
  guint8 aspect, framerate, profile, level, format, chroma;
  guint8 gop_n, gop_m, cgms, recst, abst;
  guint16 vbv_delay, width, height, vbv_buffer;
  guint64 dts;
  gboolean pf, tf, rf;

  GST_LOG_OBJECT (filter, "Video Frame Pack");

  /* Byte | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
   *      ---------------------------------
   *  0   |          Size (0x39)          |
   *      ---------------------------------
   *  1   |                               |
   *  2   |            ETN                |
   *  3   |                               |
   *      ---------------------------------
   */

  if (data[0] != 0x39) {
    GST_WARNING ("Invalid size for Video frame");
    return GST_FLOW_ERROR;
  }
  etn = data[3] << 16 | data[2] << 8 | data[1];

  GST_LOG_OBJECT (filter, " ETN : %" G_GUINT32_FORMAT, etn);

  /* Pack-V Information
   *      ---------------------------------
   *  4   |    Number of Video Frames     |
   *      ---------------------------------
   *  5   | 0 | 0 | 0 | 0 |   DATA-H      |
   *      ---------------------------------
   *  6   |            VBV                |
   *  7   |           DELAY               |
   *      ---------------------------------
   *  8   |         HEADER SIZE           |
   *      ---------------------------------
   *  9   |                               |
   * 10   |           DTS                 |
   * 11   |                               |
   * 12   |                               |
   *      -----------------------------   |
   * 13   | 0 | 0 | 0 | 0 | 0 | 0 | 0 |   |
   *      ---------------------------------
   * 14   |PF |TF |RF | 0 |      SFR      |
   *      ---------------------------------
   */

  nbframes = data[4];

  if (VALIDATE && (data[5] >> 4))
    return GST_FLOW_ERROR;
  data_h = data[5] & 0xf;

  vbv_delay = data[6] | data[7] << 8;

  hdr_size = data[8];

  dts = data[9] | data[10] << 8 | data[11] << 16 | data[12] << 24;
  dts |= (guint64) (data[13] & 0x1) << 32;
  if (G_UNLIKELY (VALIDATE && (data[13] & 0xfe))) {
    return GST_FLOW_ERROR;
  }

  pf = data[14] & 0x80;
  tf = data[14] & 0x40;
  rf = data[14] & 0x20;
  if (G_UNLIKELY (VALIDATE && (data[14] & 0x10)))
    return GST_FLOW_ERROR;

  sfr = data[14] & 0x07;

  GST_LOG_OBJECT (filter, " Pack-V Information");
  GST_LOG_OBJECT (filter, "  Number of Video Frames : %d", nbframes);
  GST_LOG_OBJECT (filter, "  Leading PES-V picture type %s (0x%x)",
      (data_h == 0x1) ? "I-picture" : "other", data_h);
  GST_LOG_OBJECT (filter, "  VBV Delay of first frame: %" G_GUINT32_FORMAT,
      vbv_delay);
  GST_LOG_OBJECT (filter, "  Header Size:%d", hdr_size);
  GST_LOG_OBJECT (filter, "  DTS: %" GST_TIME_FORMAT " (%" G_GUINT64_FORMAT ")",
      GST_TIME_ARGS (MPEGTIME_TO_GSTTIME (dts)), dts);
  GST_LOG_OBJECT (filter, "  Video source : %s %s %s (0x%x 0x%x 0x%x)",
      pf ? "Progressive" : "Interlaced",
      tf ? "TFF" : "", rf ? "RFF" : "", pf, tf, rf);
  GST_LOG_OBJECT (filter, "  Source Frame Rate : %s (0x%x)",
      sfr_to_framerate (sfr), sfr);

  gst_structure_set (st, "DTS", G_TYPE_UINT64, MPEGTIME_TO_GSTTIME (dts),
      "interlaced", G_TYPE_BOOLEAN, !pf, NULL);

  /* Search Data Mode
   *      ---------------------------------
   * 15   |    Search Data Mode           |
   *      ---------------------------------
   */
  sdm = data[15];
  GST_LOG_OBJECT (filter, " Search Data Mode : 0x%x", sdm);
  GST_LOG_OBJECT (filter, "  %s %s %s",
      sdm & 0x2 ? "8x-Base" : "",
      sdm & 0x4 ? "8x-Helper" : "", sdm & 0x10 ? "24x" : "");

  /* Video Mode
   *      ---------------------------------
   * 16   |    Horizontal size            |
   *      -----------------               |
   * 17   | 0 | 0 | 0 | 0 |               |
   *      ---------------------------------
   * 18   |    Vertical   size            |
   *      -----------------               |
   * 19   | 0 | 0 | 0 | 0 |               |
   *      ---------------------------------
   * 20   | Aspect ratio  | Frame Rate    |
   *      ---------------------------------
   * 21   |                               |
   * 22   |            bitrate            |
   *      -------------------------       |
   * 23   | 0 | 0 | 0 | 0 | 0 | 0 |       |
   *      ---------------------------------
   * 24   |          VBV Buffer size      |
   *      -------------------------       |
   * 25   | 0 | 0 | 0 | 0 | 0 | 0 |       |
   *      ---------------------------------
   * 26   | 0 | Profile   |   Level       |
   *      ---------------------------------
   * 27   | 0 | Format    |Chroma | 0 | 0 |
   *      ---------------------------------
   * 28   |      GOP N        |  GOP M    |
   *      ---------------------------------
   */
  width = data[16] | (data[17] & 0xf) << 8;
  height = data[18] | (data[19] & 0xf) << 8;
  if (VALIDATE && ((data[17] & 0xf0) || data[19] & 0xf0))
    return GST_FLOW_ERROR;
  aspect = data[20] >> 4;
  framerate = data[20] & 0xf;
  bitrate = data[21] | data[22] << 8 | (data[23] & 0x3) << 16;
  if (VALIDATE && (data[23] & 0xfc))
    return GST_FLOW_ERROR;
  vbv_buffer = data[24] | (data[25] & 0x3) << 8;
  if (VALIDATE && (data[25] & 0xfc))
    return GST_FLOW_ERROR;
  profile = (data[26] >> 4) & 0x7;
  level = data[26] & 0xf;
  format = (data[27] >> 4) & 0x7;
  chroma = (data[27] >> 2) & 0x3;
  gop_n = data[28] >> 3;
  gop_m = data[28] & 0x7;

  GST_LOG_OBJECT (filter, " Video Mode");
  GST_LOG_OBJECT (filter, "  width:%d, height:%d", width, height);
  GST_LOG_OBJECT (filter, "  Aspect Ratio : %s (0x%x)",
      (aspect == 0x3) ? "16/9" : "RESERVED", aspect);
  GST_LOG_OBJECT (filter, "  Framerate: %s (0x%x)",
      sfr_to_framerate (framerate), framerate);
  GST_LOG_OBJECT (filter, "  Bitrate: %d bit/s", bitrate * 400);
  GST_LOG_OBJECT (filter, "  VBV buffer Size : %d bits",
      vbv_buffer * 16 * 1024);
  GST_LOG_OBJECT (filter, "  MPEG Profile : %s (0x%x)",
      (profile == 0x4) ? "Main" : "RESERVED", profile);
  GST_LOG_OBJECT (filter, "  MPEG Level : %s (0x%x)",
      (level == 0x6) ? "High-1440" : "RESERVED", level);
  GST_LOG_OBJECT (filter, "  Video format : %s (0x%x)",
      (format == 0) ? "Component" : "Reserved", format);
  GST_LOG_OBJECT (filter, "  Chroma : %s (0x%x)",
      (chroma == 0x1) ? "4:2:0" : "RESERVED", chroma);
  GST_LOG_OBJECT (filter, "  GOP N/M : %d / %d", gop_n, gop_m);

  /* data availability
   *      ---------------------------------
   * 29   | 0 | 0 | 0 | 0 | 0 |PE2|PE1|PE0|
   *      ---------------------------------
   * PE0 : HD2 TTC is valid
   * PE1 : REC DATE is valid
   * PE2 : REC TIME is valid
   */
  if (data[29] & 0x1) {
    guint8 fr, sec, min, hr;
    gboolean bf, df;
    gchar *ttcs;

    /* HD2 TTC
     *      ---------------------------------
     * 30   |BF |DF |Tens Fr|Units of Frames|
     *      ---------------------------------
     * 31   | 1 |Tens second|Units of Second|
     *      ---------------------------------
     * 32   | 1 |Tens minute|Units of Minute|
     *      ---------------------------------
     * 33   | 1 | 1 |Tens Hr|Units of Hours |
     *      ---------------------------------
     */
    bf = data[30] >> 7;
    df = (data[30] >> 6) & 0x1;
    fr = BCD (data[30] & 0x3f);
    sec = BCD (data[31] & 0x7f);
    min = BCD (data[32] & 0x7f);
    hr = BCD (data[33] & 0x3f);
    GST_LOG_OBJECT (filter, " HD2 Title Time Code");
    GST_LOG_OBJECT (filter, "  BF:%d, Drop Frame:%d", bf, df);
    ttcs = g_strdup_printf ("%02d:%02d:%02d.%02d", hr, min, sec, fr);
    GST_LOG_OBJECT (filter, "  Timecode %s", ttcs);
    /* FIXME : Use framerate information from above to convert to GstClockTime */
    gst_structure_set (st, "title-time-code", G_TYPE_STRING, ttcs, NULL);
    g_free (ttcs);

  }

  if (data[29] & 0x2) {
    gboolean ds, tm;
    guint8 tz, day, dow, month, year;
    GDate *date;

    /* REC DATE
     *      ---------------------------------
     * 34   |DS |TM |Tens TZ|Units of TimeZn|
     *      ---------------------------------
     * 35   | 1 | 1 |Tens dy| Units of Days |
     *      ---------------------------------
     * 36   |   Week    |TMN|Units of Months|
     *      ---------------------------------
     * 37   | Tens of Years |Units of Years |
     *      ---------------------------------
     */
    ds = data[34] >> 7;
    tm = (data[34] >> 6) & 0x1;
    tz = BCD (data[34] & 0x3f);
    day = BCD (data[35] & 0x3f);
    dow = data[36] >> 5;
    month = BCD (data[36] & 0x1f);
    year = BCD (data[37]);

    GST_LOG_OBJECT (filter, " REC DATE");
    GST_LOG_OBJECT (filter, "  ds:%d, tm:%d", ds, tm);
    GST_LOG_OBJECT (filter, "  Timezone: %d", tz);
    GST_LOG_OBJECT (filter, "  Date: %d %02d/%02d/%04d", dow, day, month, year);
    date = g_date_new_dmy (day, month, year);
    gst_structure_set (st, "date", GST_TYPE_DATE, date,
        "timezone", G_TYPE_INT, tz,
        "daylight-saving", G_TYPE_BOOLEAN, ds, NULL);
    g_date_free (date);
  }

  if (data[29] & 0x4) {
    guint8 fr, sec, min, hr;
    gchar *times;

    /* REC TIME
     *      ---------------------------------
     * 38   | 1 | 1 |Tens Fr|Units of Frames|
     *      ---------------------------------
     * 39   | 1 |Tens second|Units of Second|
     *      ---------------------------------
     * 40   | 1 |Tens minute|Units of Minute|
     *      ---------------------------------
     * 41   | 1 | 1 |Tens Hr|Units of Hours |
     *      ---------------------------------
     */
    fr = BCD (data[38] & 0x3f);
    sec = BCD (data[39] & 0x7f);
    min = BCD (data[40] & 0x7f);
    hr = BCD (data[41] & 0x3f);
    times = g_strdup_printf ("%02d:%02d:%02d", hr, min, sec);
    GST_LOG_OBJECT (filter, " REC TIME %02d:%02d:%02d.%02d", hr, min, sec, fr);
    gst_structure_set (st, "time", G_TYPE_STRING, times, NULL);
    g_free (times);
  }

  /* MISC
   *      ---------------------------------
   * 42   | CGMS  |REC|ABS| 0 | 0 | 0 | 0 |
   *      ---------------------------------
   */
  cgms = data[42] >> 6;
  recst = (data[42] >> 5) & 0x1;
  abst = (data[42] >> 4) & 0x1;

  GST_LOG_OBJECT (filter, " CGMS:0x%x", cgms);
  GST_LOG_OBJECT (filter, " Recording Start Point : %s",
      (recst == 0) ? "PRESENT" : "ABSENT");
  GST_LOG_OBJECT (filter, " ABST : %s",
      (abst == 0) ? "DISCONTINUITY" : "NO DISCONTINUITY");

  gst_structure_set (st, "recording-start-point", G_TYPE_BOOLEAN, !recst, NULL);

  /* Extended DV Pack #1
   * 43 - 47
   */
  GST_LOG_OBJECT (filter, " Extended DV Pack #1 : 0x%x", data[43]);

  /* Extended DV Pack #1
   * 48 - 52
   */
  GST_LOG_OBJECT (filter, " Extended DV Pack #2 : 0x%x", data[48]);

  /* Extended DV Pack #1
   * 53 - 57
   */
  GST_LOG_OBJECT (filter, " Extended DV Pack #3 : 0x%x", data[53]);

  return GST_FLOW_OK;

}

static GstFlowReturn
parse_audio_frame (GstHDVParse * filter, guint8 * data, guint64 size,
    GstStructure * st)
{
  guint32 etn;
  guint8 nbmute, nbaau;
  guint64 pts;
  guint16 audio_comp;
  guint8 bitrate, fs, compress, channel;
  guint8 option, cgms;
  gboolean acly, recst;

  GST_LOG_OBJECT (filter, "Audio Frame Pack");

  /* Byte | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
   *      ---------------------------------
   *  0   |          Size (0x0f)          |
   *      ---------------------------------
   *  1   |                               |
   *  2   |            ETN                |
   *  3   |                               |
   *      ---------------------------------
   *  4   |Nb Audio Mute | Number of AAU  |
   *      ---------------------------------
   */

  if (data[0] != 0x0f) {
    GST_WARNING ("Invalid size for audio frame");
    return GST_FLOW_ERROR;
  }
  etn = data[3] << 16 | data[2] << 8 | data[1];

  GST_LOG_OBJECT (filter, " ETN : %" G_GUINT32_FORMAT, etn);

  /* Pack-A Information
   *      ---------------------------------
   *  4   |Nb Audio Mute | Number of AAU  |
   *      ---------------------------------
   *  5   |                               |
   *  6   |            PTS                |
   *  7   |                               |
   *  8   |                               |
   *      -----------------------------   |
   *  9   | 0 | 0 | 0 | 0 | 0 | 0 | 0 |   |
   *      ---------------------------------
   * 10   |           Audio               |
   * 11   |         Compensation          |
   *      ---------------------------------
   */

  /* Number of Audio Mute Frames */
  nbmute = data[4] >> 4;
  /* Number of AAU */
  nbaau = data[4] & 0x0f;
  /* PTS of the first AAU immediatly following */
  pts = (data[5] | data[6] << 8 | data[7] << 16 | data[8] << 24);
  pts |= (guint64) (data[9] & 0x1) << 32;
  if (G_UNLIKELY (VALIDATE && (data[9] & 0xfe))) {
    return GST_FLOW_ERROR;
  }

  /* Amount of compensation */
  audio_comp = data[10] | data[11] << 8;

  GST_LOG_OBJECT (filter, " Pack-A Information");
  GST_LOG_OBJECT (filter, "  Nb Audio Mute Frames : %d", nbmute);
  GST_LOG_OBJECT (filter, "  Nb AAU : %d", nbaau);
  GST_LOG_OBJECT (filter,
      "  PTS : %" GST_TIME_FORMAT " (%" G_GUINT64_FORMAT ")",
      GST_TIME_ARGS (MPEGTIME_TO_GSTTIME (pts)), pts);
  GST_LOG_OBJECT (filter, "  Audio Compensation : %" G_GUINT32_FORMAT,
      audio_comp);

  /* Audio Mode
   *      ---------------------------------
   * 12   | Bitrate Index | 0 |Samplerate |
   *      ---------------------------------
   * 13   | Compression   |   Channels    |
   *      ---------------------------------
   * 14   | X |     Anciliary Option      |
   *      ---------------------------------
   *
   * X : Anciliary data present
   */

  bitrate = data[12] >> 4;
  fs = data[12] & 0x7;
  if (G_UNLIKELY (VALIDATE && (data[12] & 0x08)))
    return GST_FLOW_ERROR;

  compress = data[13] >> 4;
  channel = data[13] & 0xf;
  acly = data[14] & 0x80;
  option = data[14] & 0x7f;

  GST_LOG_OBJECT (filter, " Audio Mode");
  GST_LOG_OBJECT (filter, "  Bitrate : %s (0x%x)",
      (bitrate == 0xe) ? "384kbps" : "RESERVED", bitrate);
  GST_LOG_OBJECT (filter, "  Samplerate : %s (0x%x)",
      (fs == 0x1) ? "48 kHz" : "RESERVED", fs);
  GST_LOG_OBJECT (filter, "  Compression : %s (0x%x)",
      (compress == 0x2) ? "MPEG-1 Layer II" : "RESERVED", compress);
  GST_LOG_OBJECT (filter, "  Channels : %s (0x%x)",
      (channel == 0) ? "Stereo" : "RESERVED", channel);
  GST_LOG_OBJECT (filter, "  Anciliary data %s %s (0x%x)",
      acly ? "PRESENT" : "ABSENT",
      (option == 0xc) ? "IEC 13818-3" : "ABSENT/RESERVED", option);
  /* 
   *      ---------------------------------
   * 15   | CGMS  | R | 0 | 0 | 0 | 0 | 0 |
   *      ---------------------------------
   *
   * R : Recording Start Point
   */

  cgms = data[15] & 0xc0;
  recst = data[15] & 0x20;

  GST_LOG_OBJECT (filter, " Misc");
  GST_LOG_OBJECT (filter, "  CGMS : 0x%x", cgms);
  GST_LOG_OBJECT (filter, "  Recording Start Point %s",
      (recst) ? "ABSENT" : "PRESENT");

  gst_structure_set (st, "PTS", G_TYPE_UINT64, MPEGTIME_TO_GSTTIME (pts),
      "recording-start-point", G_TYPE_BOOLEAN, !recst, NULL);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_hdvparse_parse (GstHDVParse * filter, GstBuffer * buf)
{
  GstFlowReturn res = GST_FLOW_OK;
  guint8 *data = GST_BUFFER_DATA (buf);
  guint64 offs = 0;
  guint64 insize = GST_BUFFER_SIZE (buf);
  GstStructure *st;

  /* Byte | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
   *      ---------------------------------
   *  0   | 0 |      KEYWORD              |
   * (1)  |         LENGTH                |
   *                 ....
   *
   * KEYWORD :
   *   0x00 - 0x3F : Constant length (5 bytes)
   *   0x40 - 0x7F : Variable length (LENGTH + 1)
   *
   * LENGTH : if present, size of fields 1-N
   *
   * Known keyword values:
   * 0x00-0x07 : AUX-V
   * 0x08-0x3E : RESERVED
   * 0x3F      : AUX-N NO-INFO
   * 0x40-0x43 : AUX-A
   * 0x44-0x47 : AUX-V
   * 0x48-0x4F : AUX-N
   * 0x50-0x53 : AUX-SYS
   * 0x54-0x7E : RESERVED
   * 0x7F      : AUX-N NULL PACK
   */

  st = gst_structure_empty_new ("hdv-aux");

  while (res == GST_FLOW_OK && (offs < insize)) {
    guint8 kw = data[offs] & 0x7f;
    guint8 size;

    /* Variable size packs */
    if (kw >= 0x40) {
      size = data[offs + 1];
    } else
      size = 4;

    /* Size validation */
    GST_DEBUG ("kw:0x%x, insize:%" G_GUINT64_FORMAT ", offs:%" G_GUINT64_FORMAT
        ", size:%d", kw, insize, offs, size);
    if (insize < offs + size) {
      res = GST_FLOW_ERROR;
      goto beach;
    }

    switch (kw) {
      case 0x01:
        GST_LOG ("BINARY GROUP");
        offs += size + 1;
        break;
      case 0x07:
        GST_LOG ("ETN pack");
        break;
      case 0x40:
        GST_LOG ("Audio frame pack");
        res = parse_audio_frame (filter, data + offs + 1, size, st);
        offs += size + 2;
        break;
      case 0x3f:
        GST_LOG ("NO INFO pack");
        offs += size + 1;
        break;
      case 0x44:
        GST_LOG ("Video frame pack");
        res = parse_video_frame (filter, data + offs + 1, size, st);
        offs += size + 2;
        break;
      case 0x48:
      case 0x49:
      case 0x4A:
      case 0x4B:
        GST_LOG ("DV multi-pack");
        res = parse_dv_multi_pack (filter, data + offs + 1, size, st);
        offs += size + 2;
        break;
      default:
        GST_WARNING_OBJECT (filter, "Unknown AUX pack data of type 0x%x", kw);
        res = GST_FLOW_ERROR;
    }
  }

beach:
  if (gst_structure_n_fields (st)) {
    GstMessage *msg;
    /* Emit the element message */
    msg = gst_message_new_element (GST_OBJECT (filter), st);
    gst_element_post_message (GST_ELEMENT (filter), msg);
  } else
    gst_structure_free (st);

  return res;

}

/* GstBaseTransform vmethod implementations */

static GstFlowReturn
gst_hdvparse_transform_ip (GstBaseTransform * base, GstBuffer * outbuf)
{
  GstHDVParse *filter = GST_HDVPARSE (base);

  return gst_hdvparse_parse (filter, outbuf);
}


/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
HDVParse_init (GstPlugin * HDVParse)
{
  return gst_element_register (HDVParse, "hdvparse", GST_RANK_NONE,
      GST_TYPE_HDVPARSE);
}

/* gstreamer looks for this structure to register HDVParses
 *
 * exchange the string 'Template HDVParse' with you HDVParse description
 */
GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    hdvparse,
    "HDV private stream parser",
    HDVParse_init, VERSION, "LGPL", "GStreamer", "http://gstreamer.net/")
