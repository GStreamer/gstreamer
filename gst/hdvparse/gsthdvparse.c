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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
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

static gchar *aperture_table[] = {
  "???",
  "cls",
  "1.0",
  "1.2",
  "1.4",
  "1.6",
  "1.7",
  "1.8",
  "2.0",
  "2.2",
  "2.4",
  "2.6",
  "2.8",
  "3.1",
  "3.4",
  "3.7",
  "4.0",
  "4.4",
  "4.8",
  "5.2",
  "5.6",
  "6.2",
  "6.8",
  "7.3",
  "8.0",
  "8.7",
  "9.6",
  "10",
  "11",
  "12",
  "14",
  "14",
  "16",
  "17",
  "18",
  "6.7"
};

/* Observations from my HDV Camera (Canon HV20 Pal)
 * FIXME : replace with with code once we've figured out the algorithm.
 * Shutter speed	0x4f	0x50
 * ------------------------------------
 * 1/6			F3	95
 * 1/8			90	91
 * 1/12			FA	8A
 * 1/15			C8	88
 * 1/24			7D	85
 * 1/30			64	84
 * 1/48			BE	82
 * 1/60			32	82
 * 1/100		51	81
 * 1/250		87	80
 * 1/500		43	80
 * 1/1000		22	80
 * 1/2000		11	80
 */
typedef struct
{
  guint vala, valb, shutter;
} Shutter_t;

static Shutter_t shutter_table[] = {
  {0xf3, 0x95, 6},
  {0x90, 0x91, 8},
  {0xfa, 0x8a, 12},
  {0xc8, 0x88, 15},
  {0x7d, 0x85, 24},
  {0x64, 0x84, 30},
  {0xbe, 0x82, 48},
  {0x32, 0x82, 60},
  {0x51, 0x81, 100},
  {0x87, 0x80, 250},
  {0x43, 0x80, 500},
  {0x22, 0x80, 1000},
  {0x11, 0x80, 2000}
};

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
    GST_STATIC_CAPS ("private/hdv-a1")
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("private/hdv-a1,parsed=(boolean)True")
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

/* GObject vmethod implementations */

static void
gst_hdvparse_base_init (gpointer klass)
{
  static GstElementDetails element_details = {
    "HDVParser",
    "Data/Parser",
    "HDV private stream Parser",
    "Edward Hervey <bilboed@bilboed.com>"
  };
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_set_details (element_class, &element_details);
}

/* initialize the HDVParse's class */
static void
gst_hdvparse_class_init (GstHDVParseClass * klass)
{
  GST_BASE_TRANSFORM_CLASS (klass)->transform_ip =
      GST_DEBUG_FUNCPTR (gst_hdvparse_transform_ip);
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

static guint
get_shutter_speed (guint8 vala, guint8 valb)
{
  guint i;

  for (i = 0; i < G_N_ELEMENTS (shutter_table); i++)
    if (shutter_table[i].vala == vala && shutter_table[i].valb == valb)
      return shutter_table[i].shutter;
  GST_WARNING ("Unknown shutter speed ! vala:0x%02x, valb:0x%02x", vala, valb);
  return 0;
}

static void
gst_hdvparse_parse (GstHDVParse * filter, GstBuffer * buf)
{
  guint8 *data = GST_BUFFER_DATA (buf);
  guint apertured, shutter;
  gfloat gain;
  gboolean dst = FALSE;
  GstStructure *str;
  GstMessage *msg;

  GST_MEMDUMP_OBJECT (filter, "BUFFER", data, GST_BUFFER_SIZE (buf));

  str = gst_structure_empty_new ("HDV");

  /* 0x1f - 0x23 : TimeCode */

  if (data[0x1f] != 0xff) {
    guint8 tframe, tsec, tmin, thour;
    gchar *timecode = NULL;
    tframe = BCD (data[0x1f] & 0x3f);
    tsec = BCD (data[0x20] & 0x7f);
    tmin = BCD (data[0x21] & 0x7f);
    thour = BCD (data[0x22] & 0x3f);

    timecode =
        g_strdup_printf ("%01d:%02d:%02d.%02d", thour, tmin, tsec, tframe);
    gst_structure_set (str, "timecode", G_TYPE_STRING, timecode, NULL);
    g_free (timecode);
    GST_LOG_OBJECT (filter, timecode);
  }

  /* 0x23 : Timezone / Dailight Saving Time */
  /* 0x24 - 0x2a : Original time */
  if (data[0x23] != 0xff) {
    GDate *date = NULL;
    guint tzone = 0;
    guint day, month, year, hour, min, sec;
    gchar *datetime;

    tzone = data[0x23];
    dst = !(tzone & 0x80);
    tzone =
        BCD (tzone & 0x1f) > 12 ? BCD (tzone & 0x1f) - 12 : BCD (tzone & 0x1f);
    GST_LOG_OBJECT (filter, "TimeZone : %d, DST : %d", tzone, dst);

    day = BCD_M (data[0x24], 0x3f);
    month = BCD_M (data[0x25], 0x1f);
    year = BCD (data[0x26]);
    if (year > 90)
      year += 1900;
    else
      year += 2000;
    /* 0x27: ??? */
    sec = BCD_M (data[0x28], 0x7f);
    min = BCD_M (data[0x29], 0x7f);
    hour = BCD_M (data[0x2a], 0x3f);

    /* FIXME : we need a date/time object ! */
    date = g_date_new_dmy (day, month, year);
    datetime =
        g_strdup_printf ("%02d/%02d/%02d %02d:%02d:%02d", day, month, year,
        hour, min, sec);
    gst_structure_set (str, "date", GST_TYPE_DATE, date, "recording-time",
        G_TYPE_STRING, datetime, NULL);
    g_free (datetime);
    GST_LOG_OBJECT (filter, datetime);
  }

  /* 0x2b : Various flags, including scene-change */
  if (!((data[0x2b] & 0x20) >> 5)) {
    GST_LOG_OBJECT (filter, "Scene change !");
    gst_structure_set (str, "scene-change", G_TYPE_BOOLEAN, TRUE, NULL);
  }

  /* Check for partials */
  if (GST_BUFFER_SIZE (buf) < 0x50) {
    goto beach;
  }

  /* 0x43 : Aperture */
  apertured = data[0x43] & 0x3f;
  if (apertured < 35) {
    GST_LOG_OBJECT (filter, "Aperture : F%s", aperture_table[apertured]);
    gst_structure_set (str, "aperture", G_TYPE_STRING,
        aperture_table[apertured], NULL);
  } else {
    GST_LOG_OBJECT (filter, "Aperture : %d", apertured);
  }

  /* 0x44 : Gain */
  gain = ((data[0x44] & 0xf) - 1) * 1.5;
  GST_LOG_OBJECT (filter, "Gain : %03f db", gain);
  gst_structure_set (str, "gain", G_TYPE_FLOAT, gain, NULL);

  /* 0x4f - 0x50 : Shutter */
  shutter = get_shutter_speed (data[0x4f], data[0x50]);
  GST_LOG_OBJECT (filter, "Shutter speed : 1/%d", shutter);
  if (shutter)
    gst_structure_set (str, "shutter-speed", GST_TYPE_FRACTION, 1, shutter,
        NULL);

beach:
  msg = gst_message_new_element (GST_OBJECT (filter), str);
  gst_element_post_message (GST_ELEMENT (filter), msg);
  return;
}

/* GstBaseTransform vmethod implementations */

static GstFlowReturn
gst_hdvparse_transform_ip (GstBaseTransform * base, GstBuffer * outbuf)
{
  GstHDVParse *filter = GST_HDVPARSE (base);

  gst_hdvparse_parse (filter, outbuf);

  return GST_FLOW_OK;
}


/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
HDVParse_init (GstPlugin * HDVParse)
{
  return gst_element_register (HDVParse, "hdvparse", GST_RANK_PRIMARY,
      GST_TYPE_HDVPARSE);
}

/* gstreamer looks for this structure to register HDVParses
 *
 * exchange the string 'Template HDVParse' with you HDVParse description
 */
GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "hdvparse",
    "HDV private stream parser",
    HDVParse_init, VERSION, "LGPL", "GStreamer", "http://gstreamer.net/")
