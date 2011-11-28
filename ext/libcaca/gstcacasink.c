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
/**
 * SECTION:element-cacasink
 * @see_also: #GstAASink
 *
 * Displays video as color ascii art.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * CACA_GEOMETRY=160x60 CACA_FONT=5x7 gst-launch filesrc location=test.avi ! decodebin ! ffmpegcolorspace ! cacasink
 * ]| This pipeline renders a video to ascii art into a separate window using a
 * small font and specifying the ascii resolution.
 * |[
 * CACA_DRIVER=ncurses gst-launch filesrc location=test.avi ! decodebin ! ffmpegcolorspace ! cacasink
 * ]| This pipeline renders a video to ascii art into the current terminal.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <sys/time.h>

#include "gstcacasink.h"

#define GST_CACA_DEFAULT_SCREEN_WIDTH 80
#define GST_CACA_DEFAULT_SCREEN_HEIGHT 25
#define GST_CACA_DEFAULT_BPP 24
#define GST_CACA_DEFAULT_RED_MASK GST_VIDEO_BYTE1_MASK_32_INT
#define GST_CACA_DEFAULT_GREEN_MASK GST_VIDEO_BYTE2_MASK_32_INT
#define GST_CACA_DEFAULT_BLUE_MASK GST_VIDEO_BYTE3_MASK_32_INT

//#define GST_CACA_DEFAULT_RED_MASK R_MASK_32_REVERSE_INT
//#define GST_CACA_DEFAULT_GREEN_MASK G_MASK_32_REVERSE_INT
//#define GST_CACA_DEFAULT_BLUE_MASK B_MASK_32_REVERSE_INT

/* cacasink signals and args */
enum
{
  LAST_SIGNAL
};


enum
{
  ARG_0,
  ARG_SCREEN_WIDTH,
  ARG_SCREEN_HEIGHT,
  ARG_DITHER,
  ARG_ANTIALIASING
};

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_RGB ";" GST_VIDEO_CAPS_RGBx ";"
        GST_VIDEO_CAPS_RGB_16 ";" GST_VIDEO_CAPS_RGB_15)
    );

static void gst_cacasink_base_init (gpointer g_class);
static void gst_cacasink_class_init (GstCACASinkClass * klass);
static void gst_cacasink_init (GstCACASink * cacasink);

static gboolean gst_cacasink_setcaps (GstBaseSink * pad, GstCaps * caps);
static void gst_cacasink_get_times (GstBaseSink * sink, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end);
static GstFlowReturn gst_cacasink_render (GstBaseSink * basesink,
    GstBuffer * buffer);

static void gst_cacasink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_cacasink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstStateChangeReturn gst_cacasink_change_state (GstElement * element,
    GstStateChange transition);

static GstElementClass *parent_class = NULL;

GType
gst_cacasink_get_type (void)
{
  static GType cacasink_type = 0;

  if (!cacasink_type) {
    static const GTypeInfo cacasink_info = {
      sizeof (GstCACASinkClass),
      gst_cacasink_base_init,
      NULL,
      (GClassInitFunc) gst_cacasink_class_init,
      NULL,
      NULL,
      sizeof (GstCACASink),
      0,
      (GInstanceInitFunc) gst_cacasink_init,
    };

    cacasink_type =
        g_type_register_static (GST_TYPE_BASE_SINK, "GstCACASink",
        &cacasink_info, 0);
  }
  return cacasink_type;
}

#define GST_TYPE_CACADITHER (gst_cacasink_dither_get_type())
static GType
gst_cacasink_dither_get_type (void)
{
  static GType dither_type = 0;

  static const GEnumValue dither_types[] = {
    {CACA_DITHERING_NONE, "No dithering", "none"},
    {CACA_DITHERING_ORDERED2, "Ordered 2x2 Bayer dithering", "2x2"},
    {CACA_DITHERING_ORDERED4, "Ordered 4x4 Bayer dithering", "4x4"},
    {CACA_DITHERING_ORDERED8, "Ordered 8x8 Bayer dithering", "8x8"},
    {CACA_DITHERING_RANDOM, "Random dithering", "random"},
    {0, NULL, NULL},
  };

  if (!dither_type) {
    dither_type = g_enum_register_static ("GstCACASinkDithering", dither_types);
  }
  return dither_type;
}

static void
gst_cacasink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class,
      "A colored ASCII art video sink", "Sink/Video",
      "A colored ASCII art videosink", "Zeeshan Ali <zak147@yahoo.com>");
  gst_element_class_add_static_pad_template (element_class,
      &sink_template);
}

static void
gst_cacasink_class_init (GstCACASinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstbasesink_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = gst_cacasink_set_property;
  gobject_class->get_property = gst_cacasink_get_property;
  gstelement_class->change_state = gst_cacasink_change_state;

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_SCREEN_WIDTH,
      g_param_spec_int ("screen-width", "Screen Width",
          "The width of the screen", 0, G_MAXINT, GST_CACA_DEFAULT_SCREEN_WIDTH,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_SCREEN_HEIGHT,
      g_param_spec_int ("screen-height", "Screen Height",
          "The height of the screen", 0, G_MAXINT,
          GST_CACA_DEFAULT_SCREEN_HEIGHT,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_DITHER,
      g_param_spec_enum ("dither", "Dither Type", "Set type of Dither",
          GST_TYPE_CACADITHER, CACA_DITHERING_NONE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_ANTIALIASING,
      g_param_spec_boolean ("anti-aliasing", "Anti Aliasing",
          "Enables Anti-Aliasing", TRUE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gstbasesink_class->set_caps = GST_DEBUG_FUNCPTR (gst_cacasink_setcaps);
  gstbasesink_class->get_times = GST_DEBUG_FUNCPTR (gst_cacasink_get_times);
  gstbasesink_class->preroll = GST_DEBUG_FUNCPTR (gst_cacasink_render);
  gstbasesink_class->render = GST_DEBUG_FUNCPTR (gst_cacasink_render);
}

static void
gst_cacasink_get_times (GstBaseSink * sink, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end)
{
  *start = GST_BUFFER_TIMESTAMP (buffer);
  *end = *start + GST_BUFFER_DURATION (buffer);
}

static gboolean
gst_cacasink_setcaps (GstBaseSink * basesink, GstCaps * caps)
{
  GstCACASink *cacasink;
  GstStructure *structure;
  gint endianness;

  cacasink = GST_CACASINK (basesink);

  structure = gst_caps_get_structure (caps, 0);
  gst_structure_get_int (structure, "width", &(cacasink->width));
  gst_structure_get_int (structure, "height", &(cacasink->height));
  gst_structure_get_int (structure, "endianness", &endianness);
  gst_structure_get_int (structure, "bpp", (int *) &cacasink->bpp);
  gst_structure_get_int (structure, "red_mask", (int *) &cacasink->red_mask);
  gst_structure_get_int (structure, "green_mask",
      (int *) &cacasink->green_mask);
  gst_structure_get_int (structure, "blue_mask", (int *) &cacasink->blue_mask);

  if (cacasink->bpp == 24) {
    cacasink->red_mask = GUINT32_FROM_BE (cacasink->red_mask) >> 8;
    cacasink->green_mask = GUINT32_FROM_BE (cacasink->green_mask) >> 8;
    cacasink->blue_mask = GUINT32_FROM_BE (cacasink->blue_mask) >> 8;
  }

  else if (cacasink->bpp == 32) {
    cacasink->red_mask = GUINT32_FROM_BE (cacasink->red_mask);
    cacasink->green_mask = GUINT32_FROM_BE (cacasink->green_mask);
    cacasink->blue_mask = GUINT32_FROM_BE (cacasink->blue_mask);
  }

  else if (cacasink->bpp == 16) {
    if (endianness == G_BIG_ENDIAN) {
      cacasink->red_mask = GUINT16_FROM_BE (cacasink->red_mask);
      cacasink->green_mask = GUINT16_FROM_BE (cacasink->green_mask);
      cacasink->blue_mask = GUINT16_FROM_BE (cacasink->blue_mask);
    } else {
      cacasink->red_mask = GUINT16_FROM_LE (cacasink->red_mask);
      cacasink->green_mask = GUINT16_FROM_LE (cacasink->green_mask);
      cacasink->blue_mask = GUINT16_FROM_LE (cacasink->blue_mask);
    }
  }

  if (cacasink->bitmap) {
    caca_free_bitmap (cacasink->bitmap);
  }

  cacasink->bitmap = caca_create_bitmap (cacasink->bpp,
      cacasink->width,
      cacasink->height,
      GST_ROUND_UP_4 (cacasink->width * cacasink->bpp / 8),
      cacasink->red_mask, cacasink->green_mask, cacasink->blue_mask, 0);

  if (!cacasink->bitmap) {
    return FALSE;
  }

  return TRUE;
}

static void
gst_cacasink_init (GstCACASink * cacasink)
{
  cacasink->screen_width = GST_CACA_DEFAULT_SCREEN_WIDTH;
  cacasink->screen_height = GST_CACA_DEFAULT_SCREEN_HEIGHT;
  cacasink->bpp = GST_CACA_DEFAULT_BPP;
  cacasink->red_mask = GST_CACA_DEFAULT_RED_MASK;
  cacasink->green_mask = GST_CACA_DEFAULT_GREEN_MASK;
  cacasink->blue_mask = GST_CACA_DEFAULT_BLUE_MASK;
}

static GstFlowReturn
gst_cacasink_render (GstBaseSink * basesink, GstBuffer * buffer)
{
  GstCACASink *cacasink = GST_CACASINK (basesink);

  GST_DEBUG ("render");

  caca_clear ();
  caca_draw_bitmap (0, 0, cacasink->screen_width - 1,
      cacasink->screen_height - 1, cacasink->bitmap, GST_BUFFER_DATA (buffer));
  caca_refresh ();

  return GST_FLOW_OK;
}

static void
gst_cacasink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstCACASink *cacasink;

  g_return_if_fail (GST_IS_CACASINK (object));

  cacasink = GST_CACASINK (object);

  switch (prop_id) {
    case ARG_DITHER:{
      cacasink->dither = g_value_get_enum (value);
      caca_set_dithering (cacasink->dither + CACA_DITHERING_NONE);
      break;
    }
    case ARG_ANTIALIASING:{
      cacasink->antialiasing = g_value_get_boolean (value);
      if (cacasink->antialiasing) {
        caca_set_feature (CACA_ANTIALIASING_MAX);
      } else {
        caca_set_feature (CACA_ANTIALIASING_MIN);
      }
      break;
    }
    default:
      break;
  }
}

static void
gst_cacasink_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstCACASink *cacasink;

  cacasink = GST_CACASINK (object);

  switch (prop_id) {
    case ARG_SCREEN_WIDTH:{
      g_value_set_int (value, cacasink->screen_width);
      break;
    }
    case ARG_SCREEN_HEIGHT:{
      g_value_set_int (value, cacasink->screen_height);
      break;
    }
    case ARG_DITHER:{
      g_value_set_enum (value, cacasink->dither);
      break;
    }
    case ARG_ANTIALIASING:{
      g_value_set_boolean (value, cacasink->antialiasing);
      break;
    }
    default:{
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
  }
}

static gboolean
gst_cacasink_open (GstCACASink * cacasink)
{
  cacasink->bitmap = NULL;

  if (caca_init () < 0) {
    GST_ELEMENT_ERROR (cacasink, RESOURCE, OPEN_WRITE, (NULL),
        ("caca_init() failed"));
    return FALSE;
  }

  cacasink->screen_width = caca_get_width ();
  cacasink->screen_height = caca_get_height ();
  cacasink->antialiasing = TRUE;
  caca_set_feature (CACA_ANTIALIASING_MAX);
  cacasink->dither = 0;
  caca_set_dithering (CACA_DITHERING_NONE);

  return TRUE;
}

static void
gst_cacasink_close (GstCACASink * cacasink)
{
  if (cacasink->bitmap) {
    caca_free_bitmap (cacasink->bitmap);
    cacasink->bitmap = NULL;
  }
  caca_end ();
}

static GstStateChangeReturn
gst_cacasink_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (!gst_cacasink_open (GST_CACASINK (element)))
        return GST_STATE_CHANGE_FAILURE;
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_cacasink_close (GST_CACASINK (element));
      break;
    default:
      break;
  }
  return ret;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "cacasink", GST_RANK_NONE,
          GST_TYPE_CACASINK))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "cacasink",
    "Colored ASCII Art video sink",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
