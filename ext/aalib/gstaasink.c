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
 * SECTION:element-aasink
 * @see_also: #GstCACASink
 *
 * Displays video as b/w ascii art.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch filesrc location=test.avi ! decodebin ! ffmpegcolorspace ! aasink
 * ]| This pipeline renders a video to ascii art into a separate window.
 * |[
 * gst-launch filesrc location=test.avi ! decodebin ! ffmpegcolorspace ! aasink driver=curses
 * ]| This pipeline renders a video to ascii art into the current terminal.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <sys/time.h>

#include "gstaasink.h"
#include <gst/video/video.h>

/* aasink signals and args */
enum
{
  SIGNAL_FRAME_DISPLAYED,
  SIGNAL_HAVE_SIZE,
  LAST_SIGNAL
};


enum
{
  ARG_0,
  ARG_WIDTH,
  ARG_HEIGHT,
  ARG_DRIVER,
  ARG_DITHER,
  ARG_BRIGHTNESS,
  ARG_CONTRAST,
  ARG_GAMMA,
  ARG_INVERSION,
  ARG_RANDOMVAL,
  ARG_FRAMES_DISPLAYED,
  ARG_FRAME_TIME
};

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("I420"))
    );

static void gst_aasink_base_init (gpointer g_class);
static void gst_aasink_class_init (GstAASinkClass * klass);
static void gst_aasink_init (GstAASink * aasink);

static gboolean gst_aasink_setcaps (GstBaseSink * pad, GstCaps * caps);
static void gst_aasink_get_times (GstBaseSink * sink, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end);
static GstFlowReturn gst_aasink_render (GstBaseSink * basesink,
    GstBuffer * buffer);

static void gst_aasink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_aasink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstStateChangeReturn gst_aasink_change_state (GstElement * element,
    GstStateChange transition);

static GstElementClass *parent_class = NULL;
static guint gst_aasink_signals[LAST_SIGNAL] = { 0 };

GType
gst_aasink_get_type (void)
{
  static GType aasink_type = 0;

  if (!aasink_type) {
    static const GTypeInfo aasink_info = {
      sizeof (GstAASinkClass),
      gst_aasink_base_init,
      NULL,
      (GClassInitFunc) gst_aasink_class_init,
      NULL,
      NULL,
      sizeof (GstAASink),
      0,
      (GInstanceInitFunc) gst_aasink_init,
    };

    aasink_type =
        g_type_register_static (GST_TYPE_BASE_SINK, "GstAASink", &aasink_info,
        0);
  }
  return aasink_type;
}

#define GST_TYPE_AADRIVERS (gst_aasink_drivers_get_type())
static GType
gst_aasink_drivers_get_type (void)
{
  static GType driver_type = 0;

  if (!driver_type) {
    GEnumValue *drivers;
    const struct aa_driver *driver;
    gint n_drivers;
    gint i;

    for (n_drivers = 0; aa_drivers[n_drivers]; n_drivers++) {
      /* count number of drivers */
    }

    drivers = g_new0 (GEnumValue, n_drivers + 1);

    for (i = 0; i < n_drivers; i++) {
      driver = aa_drivers[i];
      drivers[i].value = i;
      drivers[i].value_name = g_strdup (driver->name);
      drivers[i].value_nick = g_utf8_strdown (driver->shortname, -1);
    }
    drivers[i].value = 0;
    drivers[i].value_name = NULL;
    drivers[i].value_nick = NULL;

    driver_type = g_enum_register_static ("GstAASinkDrivers", drivers);
  }
  return driver_type;
}

#define GST_TYPE_AADITHER (gst_aasink_dither_get_type())
static GType
gst_aasink_dither_get_type (void)
{
  static GType dither_type = 0;

  if (!dither_type) {
    GEnumValue *ditherers;
    gint n_ditherers;
    gint i;

    for (n_ditherers = 0; aa_dithernames[n_ditherers]; n_ditherers++) {
      /* count number of ditherers */
    }

    ditherers = g_new0 (GEnumValue, n_ditherers + 1);

    for (i = 0; i < n_ditherers; i++) {
      ditherers[i].value = i;
      ditherers[i].value_name = g_strdup (aa_dithernames[i]);
      ditherers[i].value_nick =
          g_strdelimit (g_strdup (aa_dithernames[i]), " _", '-');
    }
    ditherers[i].value = 0;
    ditherers[i].value_name = NULL;
    ditherers[i].value_nick = NULL;

    dither_type = g_enum_register_static ("GstAASinkDitherers", ditherers);
  }
  return dither_type;
}

static void
gst_aasink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class,
      &sink_template);
  gst_element_class_set_details_simple (element_class, "ASCII art video sink",
      "Sink/Video",
      "An ASCII art videosink", "Wim Taymans <wim.taymans@chello.be>");
}

static void
gst_aasink_class_init (GstAASinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstbasesink_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = gst_aasink_set_property;
  gobject_class->get_property = gst_aasink_get_property;

  /* FIXME: add long property descriptions */
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_WIDTH,
      g_param_spec_int ("width", "width", "width", G_MININT, G_MAXINT, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_HEIGHT,
      g_param_spec_int ("height", "height", "height", G_MININT, G_MAXINT, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_DRIVER,
      g_param_spec_enum ("driver", "driver", "driver", GST_TYPE_AADRIVERS, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_DITHER,
      g_param_spec_enum ("dither", "dither", "dither", GST_TYPE_AADITHER, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BRIGHTNESS,
      g_param_spec_int ("brightness", "brightness", "brightness", G_MININT,
          G_MAXINT, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_CONTRAST,
      g_param_spec_int ("contrast", "contrast", "contrast", G_MININT, G_MAXINT,
          0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_GAMMA,
      g_param_spec_float ("gamma", "gamma", "gamma", 0.0, 5.0, 1.0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_INVERSION,
      g_param_spec_boolean ("inversion", "inversion", "inversion", TRUE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_RANDOMVAL,
      g_param_spec_int ("randomval", "randomval", "randomval", G_MININT,
          G_MAXINT, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_FRAMES_DISPLAYED,
      g_param_spec_int ("frames-displayed", "frames displayed",
          "frames displayed", G_MININT, G_MAXINT, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_FRAME_TIME,
      g_param_spec_int ("frame-time", "frame time", "frame time", G_MININT,
          G_MAXINT, 0, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  gst_aasink_signals[SIGNAL_FRAME_DISPLAYED] =
      g_signal_new ("frame-displayed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstAASinkClass, frame_displayed),
      NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
  gst_aasink_signals[SIGNAL_HAVE_SIZE] =
      g_signal_new ("have-size", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstAASinkClass, have_size), NULL, NULL,
      gst_marshal_VOID__INT_INT, G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);

  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_aasink_change_state);

  gstbasesink_class->set_caps = GST_DEBUG_FUNCPTR (gst_aasink_setcaps);
  gstbasesink_class->get_times = GST_DEBUG_FUNCPTR (gst_aasink_get_times);
  gstbasesink_class->preroll = GST_DEBUG_FUNCPTR (gst_aasink_render);
  gstbasesink_class->render = GST_DEBUG_FUNCPTR (gst_aasink_render);
}

static void
gst_aasink_fixate (GstPad * pad, GstCaps * caps)
{
  GstStructure *structure;

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_fixate_field_nearest_int (structure, "width", 320);
  gst_structure_fixate_field_nearest_int (structure, "height", 240);
  gst_structure_fixate_field_nearest_fraction (structure, "framerate", 30, 1);
}

static gboolean
gst_aasink_setcaps (GstBaseSink * basesink, GstCaps * caps)
{
  GstAASink *aasink;
  GstStructure *structure;

  aasink = GST_AASINK (basesink);

  structure = gst_caps_get_structure (caps, 0);
  gst_structure_get_int (structure, "width", &aasink->width);
  gst_structure_get_int (structure, "height", &aasink->height);

  /* FIXME aasink->format is never set */
  g_print ("%d %d\n", aasink->width, aasink->height);

  GST_DEBUG ("aasink: setting %08lx (%" GST_FOURCC_FORMAT ")",
      aasink->format, GST_FOURCC_ARGS (aasink->format));

  g_signal_emit (G_OBJECT (aasink), gst_aasink_signals[SIGNAL_HAVE_SIZE], 0,
      aasink->width, aasink->height);

  return TRUE;
}

static void
gst_aasink_init (GstAASink * aasink)
{
  GstPad *pad;

  pad = GST_BASE_SINK_PAD (aasink);
  gst_pad_set_fixatecaps_function (pad, gst_aasink_fixate);

  memcpy (&aasink->ascii_surf, &aa_defparams,
      sizeof (struct aa_hardware_params));
  aasink->ascii_parms.bright = 0;
  aasink->ascii_parms.contrast = 16;
  aasink->ascii_parms.gamma = 1.0;
  aasink->ascii_parms.dither = 0;
  aasink->ascii_parms.inversion = 0;
  aasink->ascii_parms.randomval = 0;
  aasink->aa_driver = 0;

  aasink->width = -1;
  aasink->height = -1;

}

static void
gst_aasink_scale (GstAASink * aasink, guchar * src, guchar * dest,
    gint sw, gint sh, gint dw, gint dh)
{
  gint ypos, yinc, y;
  gint xpos, xinc, x;

  g_return_if_fail ((dw != 0) && (dh != 0));

  ypos = 0x10000;
  yinc = (sh << 16) / dh;
  xinc = (sw << 16) / dw;

  for (y = dh; y; y--) {
    while (ypos > 0x10000) {
      ypos -= 0x10000;
      src += sw;
    }
    xpos = 0x10000;
    {
      guchar *destp = dest;
      guchar *srcp = src;

      for (x = dw; x; x--) {
        while (xpos >= 0x10000L) {
          srcp++;
          xpos -= 0x10000L;
        }
        *destp++ = *srcp;
        xpos += xinc;
      }
    }
    dest += dw;
    ypos += yinc;
  }
}

static void
gst_aasink_get_times (GstBaseSink * sink, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end)
{
  *start = GST_BUFFER_TIMESTAMP (buffer);
  *end = *start + GST_BUFFER_DURATION (buffer);
}

static GstFlowReturn
gst_aasink_render (GstBaseSink * basesink, GstBuffer * buffer)
{
  GstAASink *aasink;

  aasink = GST_AASINK (basesink);

  GST_DEBUG ("render");

  gst_aasink_scale (aasink, GST_BUFFER_DATA (buffer),   /* src */
      aa_image (aasink->context),       /* dest */
      aasink->width,            /* sw */
      aasink->height,           /* sh */
      aa_imgwidth (aasink->context),    /* dw */
      aa_imgheight (aasink->context));  /* dh */

  aa_render (aasink->context, &aasink->ascii_parms,
      0, 0, aa_imgwidth (aasink->context), aa_imgheight (aasink->context));
  aa_flush (aasink->context);
  aa_getevent (aasink->context, FALSE);

  return GST_FLOW_OK;
}


static void
gst_aasink_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstAASink *aasink;

  aasink = GST_AASINK (object);

  switch (prop_id) {
    case ARG_WIDTH:
      aasink->ascii_surf.width = g_value_get_int (value);
      break;
    case ARG_HEIGHT:
      aasink->ascii_surf.height = g_value_get_int (value);
      break;
    case ARG_DRIVER:{
      aasink->aa_driver = g_value_get_enum (value);
      break;
    }
    case ARG_DITHER:{
      aasink->ascii_parms.dither = g_value_get_enum (value);
      break;
    }
    case ARG_BRIGHTNESS:{
      aasink->ascii_parms.bright = g_value_get_int (value);
      break;
    }
    case ARG_CONTRAST:{
      aasink->ascii_parms.contrast = g_value_get_int (value);
      break;
    }
    case ARG_GAMMA:{
      aasink->ascii_parms.gamma = g_value_get_float (value);
      break;
    }
    case ARG_INVERSION:{
      aasink->ascii_parms.inversion = g_value_get_boolean (value);
      break;
    }
    case ARG_RANDOMVAL:{
      aasink->ascii_parms.randomval = g_value_get_int (value);
      break;
    }
    default:
      break;
  }
}

static void
gst_aasink_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstAASink *aasink;

  aasink = GST_AASINK (object);

  switch (prop_id) {
    case ARG_WIDTH:{
      g_value_set_int (value, aasink->ascii_surf.width);
      break;
    }
    case ARG_HEIGHT:{
      g_value_set_int (value, aasink->ascii_surf.height);
      break;
    }
    case ARG_DRIVER:{
      g_value_set_enum (value, aasink->aa_driver);
      break;
    }
    case ARG_DITHER:{
      g_value_set_enum (value, aasink->ascii_parms.dither);
      break;
    }
    case ARG_BRIGHTNESS:{
      g_value_set_int (value, aasink->ascii_parms.bright);
      break;
    }
    case ARG_CONTRAST:{
      g_value_set_int (value, aasink->ascii_parms.contrast);
      break;
    }
    case ARG_GAMMA:{
      g_value_set_float (value, aasink->ascii_parms.gamma);
      break;
    }
    case ARG_INVERSION:{
      g_value_set_boolean (value, aasink->ascii_parms.inversion);
      break;
    }
    case ARG_RANDOMVAL:{
      g_value_set_int (value, aasink->ascii_parms.randomval);
      break;
    }
    case ARG_FRAMES_DISPLAYED:{
      g_value_set_int (value, aasink->frames_displayed);
      break;
    }
    case ARG_FRAME_TIME:{
      g_value_set_int (value, aasink->frame_time / 1000000);
      break;
    }
    default:{
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
  }
}

static gboolean
gst_aasink_open (GstAASink * aasink)
{
  if (!aasink->context) {
    aa_recommendhidisplay (aa_drivers[aasink->aa_driver]->shortname);

    aasink->context = aa_autoinit (&aasink->ascii_surf);
    if (aasink->context == NULL) {
      GST_ELEMENT_ERROR (GST_ELEMENT (aasink), LIBRARY, TOO_LAZY, (NULL),
          ("error opening aalib context"));
      return FALSE;
    }
    aa_autoinitkbd (aasink->context, 0);
    aa_resizehandler (aasink->context, (void *) aa_resize);
  }
  return TRUE;
}

static gboolean
gst_aasink_close (GstAASink * aasink)
{
  aa_close (aasink->context);
  aasink->context = NULL;

  return TRUE;
}

static GstStateChangeReturn
gst_aasink_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;


  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (!gst_aasink_open (GST_AASINK (element)))
        goto open_failed;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_aasink_close (GST_AASINK (element));
      break;
    default:
      break;
  }

  return ret;

open_failed:
  {
    return GST_STATE_CHANGE_FAILURE;
  }
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "aasink", GST_RANK_NONE, GST_TYPE_AASINK))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "aasink",
    "ASCII Art video sink",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
