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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */
/**
 * SECTION:element-cacasink
 * @title: cacasink
 * @see_also: #GstAASink
 *
 * Displays video as color ascii art.
 *
 * ## Example launch line
 * |[
 * CACA_GEOMETRY=160x60 CACA_FONT=5x7 gst-launch-1.0 filesrc location=test.avi ! decodebin ! videoconvert ! cacasink
 * ]| This pipeline renders a video to ascii art into a separate window using a
 * small font and specifying the ascii resolution.
 * |[
 * CACA_DRIVER=ncurses gst-launch-1.0 filesrc location=test.avi ! decodebin ! videoconvert ! cacasink
 * ]| This pipeline renders a video to ascii art into the current terminal.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include "gstcacasink.h"

//#define GST_CACA_DEFAULT_RED_MASK R_MASK_32_REVERSE_INT
//#define GST_CACA_DEFAULT_GREEN_MASK G_MASK_32_REVERSE_INT
//#define GST_CACA_DEFAULT_BLUE_MASK B_MASK_32_REVERSE_INT

/* cacasink signals and args */
enum
{
  LAST_SIGNAL
};

#define GST_CACA_DEFAULT_SCREEN_WIDTH 80
#define GST_CACA_DEFAULT_SCREEN_HEIGHT 25
#define GST_CACA_DEFAULT_DITHER CACA_DITHERING_NONE
#define GST_CACA_DEFAULT_ANTIALIASING TRUE

enum
{
  PROP_0,
  PROP_SCREEN_WIDTH,
  PROP_SCREEN_HEIGHT,
  PROP_DITHER,
  PROP_ANTIALIASING,
  PROP_DRIVER
};

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE
        ("{ RGB, BGR, RGBx, xRGB, BGRx, xBGR, RGB16, RGB15 }"))
    );

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

#define gst_cacasink_parent_class parent_class
G_DEFINE_TYPE (GstCACASink, gst_cacasink, GST_TYPE_BASE_SINK);
GST_ELEMENT_REGISTER_DEFINE (cacasink, "cacasink", GST_RANK_NONE,
    GST_TYPE_CACASINK);

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

#define GST_TYPE_CACADRIVER (gst_cacasink_driver_get_type())

/**
 * GstCACASinkDriver:
 *
 * Libcaca output driver.
 *
 * Since: 1.24
 */
static GType
gst_cacasink_driver_get_type (void)
{
  static GType driver_type = 0;

  if (g_once_init_enter (&driver_type)) {
    char const *const *list;
    gint i, n_drivers = 0;
    GEnumValue *driver;
    GType _driver_type;

    list = caca_get_display_driver_list ();

    /* Read number of available drivers */
    for (i = 0; list[i]; i += 2, ++n_drivers);

    driver = g_new0 (GEnumValue, n_drivers + 1);

    for (i = 0; i < n_drivers; i++) {
      driver[i].value = i;
      driver[i].value_nick = g_strdup (list[2 * i]);
      driver[i].value_name = g_strdup (list[2 * i + 1]);
    }
    driver[i].value = 0;
    driver[i].value_name = NULL;
    driver[i].value_nick = NULL;

    _driver_type = g_enum_register_static ("GstCACASinkDriver", driver);
    g_once_init_leave (&driver_type, _driver_type);
  }

  return driver_type;
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

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SCREEN_WIDTH,
      g_param_spec_int ("screen-width", "Screen Width",
          "The width of the screen", 0, G_MAXINT, GST_CACA_DEFAULT_SCREEN_WIDTH,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SCREEN_HEIGHT,
      g_param_spec_int ("screen-height", "Screen Height",
          "The height of the screen", 0, G_MAXINT,
          GST_CACA_DEFAULT_SCREEN_HEIGHT,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_DITHER,
      g_param_spec_enum ("dither", "Dither Type", "Set type of Dither",
          GST_TYPE_CACADITHER, GST_CACA_DEFAULT_DITHER,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_ANTIALIASING,
      g_param_spec_boolean ("anti-aliasing", "Anti Aliasing",
          "Enables Anti-Aliasing", GST_CACA_DEFAULT_ANTIALIASING,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstCACASink:driver:
   *
   * The libcaca output driver.
   *
   * Since: 1.24
   **/
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_DRIVER,
      g_param_spec_enum ("driver", "driver", "Output driver",
          GST_TYPE_CACADRIVER, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gstelement_class->change_state = gst_cacasink_change_state;

  gst_element_class_set_static_metadata (gstelement_class,
      "A colored ASCII art video sink", "Sink/Video",
      "A colored ASCII art videosink", "Zeeshan Ali <zak147@yahoo.com>");
  gst_element_class_add_static_pad_template (gstelement_class, &sink_template);

  gstbasesink_class->set_caps = GST_DEBUG_FUNCPTR (gst_cacasink_setcaps);
  gstbasesink_class->get_times = GST_DEBUG_FUNCPTR (gst_cacasink_get_times);
  gstbasesink_class->preroll = GST_DEBUG_FUNCPTR (gst_cacasink_render);
  gstbasesink_class->render = GST_DEBUG_FUNCPTR (gst_cacasink_render);

  gst_type_mark_as_plugin_api (GST_TYPE_CACADITHER, 0);
  gst_type_mark_as_plugin_api (GST_TYPE_CACADRIVER,
      GST_PLUGIN_API_FLAG_IGNORE_ENUM_MEMBERS);
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
  GstVideoInfo info;
  guint bpp, red_mask, green_mask, blue_mask;

  cacasink = GST_CACASINK (basesink);

  if (!gst_video_info_from_caps (&info, caps))
    goto caps_error;

  switch (GST_VIDEO_INFO_FORMAT (&info)) {
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_BGR:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_xBGR:
      bpp = 8 * info.finfo->pixel_stride[0];
      red_mask = 0xff << (8 * info.finfo->poffset[GST_VIDEO_COMP_R]);
      green_mask = 0xff << (8 * info.finfo->poffset[GST_VIDEO_COMP_G]);
      blue_mask = 0xff << (8 * info.finfo->poffset[GST_VIDEO_COMP_B]);
      break;
    case GST_VIDEO_FORMAT_RGB16:
      bpp = 16;
      red_mask = 0xf800;
      green_mask = 0x07e0;
      blue_mask = 0x001f;
      break;
    case GST_VIDEO_FORMAT_RGB15:
      bpp = 16;
      red_mask = 0x7c00;
      green_mask = 0x03e0;
      blue_mask = 0x001f;
      break;
    default:
      goto invalid_format;
  }

  if (cacasink->bitmap) {
    caca_free_bitmap (cacasink->bitmap);
  }
  cacasink->bitmap = caca_create_bitmap (bpp,
      GST_VIDEO_INFO_WIDTH (&info),
      GST_VIDEO_INFO_HEIGHT (&info),
      GST_ROUND_UP_4 (GST_VIDEO_INFO_WIDTH (&info) * bpp / 8),
      red_mask, green_mask, blue_mask, 0);
  if (!cacasink->bitmap)
    goto no_bitmap;

  cacasink->info = info;

  return TRUE;

  /* ERROS */
caps_error:
  {
    GST_ERROR_OBJECT (cacasink, "error parsing caps");
    return FALSE;
  }
invalid_format:
  {
    GST_ERROR_OBJECT (cacasink, "invalid format");
    return FALSE;
  }
no_bitmap:
  {
    GST_ERROR_OBJECT (cacasink, "could not create bitmap");
    return FALSE;
  }
}

static void
gst_cacasink_init (GstCACASink * cacasink)
{
  cacasink->screen_width = GST_CACA_DEFAULT_SCREEN_WIDTH;
  cacasink->screen_height = GST_CACA_DEFAULT_SCREEN_HEIGHT;

  cacasink->dither = GST_CACA_DEFAULT_DITHER;
  cacasink->antialiasing = GST_CACA_DEFAULT_ANTIALIASING;
  cacasink->driver = 0;
}

static GstFlowReturn
gst_cacasink_render (GstBaseSink * basesink, GstBuffer * buffer)
{
  GstCACASink *cacasink = GST_CACASINK (basesink);
  GstVideoFrame frame;

  GST_DEBUG ("render");

  if (!gst_video_frame_map (&frame, &cacasink->info, buffer, GST_MAP_READ))
    goto invalid_frame;

  caca_clear_canvas (cacasink->cv);
  caca_dither_bitmap (cacasink->cv, 0, 0, cacasink->screen_width - 1,
      cacasink->screen_height - 1, cacasink->bitmap,
      GST_VIDEO_FRAME_PLANE_DATA (&frame, 0));
  caca_refresh_display (cacasink->dp);

  gst_video_frame_unmap (&frame);

  return GST_FLOW_OK;

  /* ERRORS */
invalid_frame:
  {
    GST_ERROR_OBJECT (cacasink, "invalid frame received");
    return GST_FLOW_ERROR;
  }
}

static void
gst_cacasink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstCACASink *cacasink;

  g_return_if_fail (GST_IS_CACASINK (object));

  cacasink = GST_CACASINK (object);

  switch (prop_id) {
    case PROP_DITHER:{
      cacasink->dither = g_value_get_enum (value);
      caca_set_dithering (cacasink->dither + CACA_DITHERING_NONE);
      break;
    }
    case PROP_ANTIALIASING:{
      cacasink->antialiasing = g_value_get_boolean (value);
      if (cacasink->antialiasing) {
        caca_set_feature (CACA_ANTIALIASING_MAX);
      } else {
        caca_set_feature (CACA_ANTIALIASING_MIN);
      }
      break;
    }
    case PROP_DRIVER:{
      cacasink->driver = g_value_get_enum (value);
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
    case PROP_SCREEN_WIDTH:{
      g_value_set_int (value, cacasink->screen_width);
      break;
    }
    case PROP_SCREEN_HEIGHT:{
      g_value_set_int (value, cacasink->screen_height);
      break;
    }
    case PROP_DITHER:{
      g_value_set_enum (value, cacasink->dither);
      break;
    }
    case PROP_ANTIALIASING:{
      g_value_set_boolean (value, cacasink->antialiasing);
      break;
    }
    case PROP_DRIVER:{
      g_value_set_enum (value, cacasink->driver);
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
  GEnumClass *enum_class;
  GEnumValue *ev;

  cacasink->bitmap = NULL;

  if (!(cacasink->cv = caca_create_canvas (0, 0)))
    goto init_failed;

  enum_class = g_type_class_peek (GST_TYPE_CACADRIVER);
  ev = g_enum_get_value (enum_class, cacasink->driver);

  cacasink->dp = caca_create_display_with_driver (cacasink->cv, ev->value_nick);

  if (!cacasink->dp) {
    caca_free_canvas (cacasink->cv);
    return FALSE;
  }

  cacasink->screen_width = caca_get_canvas_width (cacasink->cv);
  cacasink->screen_height = caca_get_canvas_height (cacasink->cv);
  cacasink->antialiasing = TRUE;
  caca_set_feature (CACA_ANTIALIASING_MAX);
  cacasink->dither = 0;
  caca_set_dithering (CACA_DITHERING_NONE);

  return TRUE;

  /* ERRORS */
init_failed:
  {
    GST_ELEMENT_ERROR (cacasink, RESOURCE, OPEN_WRITE, (NULL),
        ("caca_init() failed"));
    return FALSE;
  }
}

static void
gst_cacasink_close (GstCACASink * cacasink)
{
  if (cacasink->bitmap) {
    caca_free_bitmap (cacasink->bitmap);
    cacasink->bitmap = NULL;
  }

  caca_free_display (cacasink->dp);
  cacasink->dp = NULL;
  caca_free_canvas (cacasink->cv);
  cacasink->cv = NULL;
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
