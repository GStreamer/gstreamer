/* GStreamer
 * Copyright (C) <2019> Eric Marks <bigmarkslp@gmail.com>
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
 * SECTION:element-aatv
 * @see_also: #GstAASink
 *
 * Transforms video into ascii art.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 videotestsrc ! aatv ! videoconvert ! autovideosink
 * ]| This pipeline shows the effect of aatv on a test stream.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstaatv.h"
#include <string.h>
#include <stdlib.h>

#define PROP_AATV_COLOR_TEXT_DEFAULT        0xffffffff  /* White */
#define PROP_AATV_COLOR_BACKGROUND_DEFAULT  0xff000000  /* Black */
#define PROP_AATV_COLOR_RAIN_DEFAULT        0xff00ff00  /* Green */
#define PROP_AATV_RAIN_MODE_DEFAULT         GST_RAIN_OFF
#define PROP_BRIGHTNESS_TARGET_MIN_DEFAULT  0.3
#define PROP_BRIGHTNESS_TARGET_MAX_DEFAULT  0.4
#define PROP_RAIN_SPAWN_DEFAULT             0.2
#define PROP_RAIN_DELAY_MIN_DEFAULT         0
#define PROP_RAIN_DELAY_MAX_DEFAULT         3
#define PROP_RAIN_LENGTH_MIN_DEFAULT        4
#define PROP_RAIN_LENGTH_MAX_DEFAULT        30

/* aatv signals and args */
enum
{
  LAST_SIGNAL
};

#define CHECK_BIT(var,pos) ((var) & (1<<(pos)))

enum
{
  PROP_0,
  PROP_WIDTH,
  PROP_HEIGHT,
  PROP_DITHER,
  PROP_FONT,
  PROP_CONTRAST,
  PROP_GAMMA,
  PROP_RANDOMVAL,
  PROP_BRIGHTNESS_AUTO,
  PROP_BRIGHTNESS_ACTUAL,
  PROP_BRIGHTNESS,
  PROP_BRIGHTNESS_TARGET_MIN,
  PROP_BRIGHTNESS_TARGET_MAX,
  PROP_COLOR_BACKGROUND,
  PROP_COLOR_TEXT,
  PROP_COLOR_TEXT_BOLD,
  PROP_COLOR_TEXT_NORMAL,
  PROP_COLOR_TEXT_DIM,
  PROP_COLOR_RAIN,
  PROP_COLOR_RAIN_BOLD,
  PROP_COLOR_RAIN_NORMAL,
  PROP_COLOR_RAIN_DIM,
  PROP_RAIN_MODE,
  PROP_RAIN_SPAWN_RATE,
  PROP_RAIN_DELAY_MIN,
  PROP_RAIN_DELAY_MAX,
  PROP_RAIN_LENGTH_MIN,
  PROP_RAIN_LENGTH_MAX
};

static GstStaticPadTemplate sink_template_tv = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{ I420 }"))
    );
static GstStaticPadTemplate src_template_tv = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{ RGBA }"))
    );

static void gst_aatv_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_aatv_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

#define GST_TYPE_AATV_RAIN_MODE (gst_aatv_rain_mode_get_type())

static GType
gst_aatv_rain_mode_get_type (void)
{
  static GType rain_mode = 0;

  static const GEnumValue rain_modes[] = {
    {GST_RAIN_OFF, "No Rain", "none"},
    {GST_RAIN_DOWN, "Rain Down", "down"},
    {GST_RAIN_UP, "Rain Up", "up"},
    {GST_RAIN_LEFT, "Rain Left", "left"},
    {GST_RAIN_RIGHT, "Rain Right", "right"},
    {0, NULL, NULL},
  };

  if (!rain_mode) {
    rain_mode = g_enum_register_static ("GstAATvRainModes", rain_modes);
  }
  return rain_mode;
}

#define gst_aatv_parent_class parent_class
G_DEFINE_TYPE (GstAATv, gst_aatv, GST_TYPE_VIDEO_FILTER);

static void
gst_aatv_scale (GstAATv * aatv, guchar * src, guchar * dest,
    gint sw, gint sh, gint ss, gint dw, gint dh)
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
      src += ss;
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
gst_aatv_rain (GstAATv * aatv)
{
  gint i;
  gboolean obstructed;

  GstAATvDroplet *raindrops = aatv->raindrops;

  for (i = 0; i < aatv->rain_width; i++) {
    if (raindrops[i].enabled == FALSE) {
      if (g_random_double () < aatv->rain_spawn_rate) {

        obstructed = FALSE;

        /* Don't let adjacent lines be enabled at the same time. */
        if (i > 0)
          if (raindrops[i - 1].enabled == TRUE)
            if (raindrops[i - 1].location - raindrops[i - 1].length <
                aatv->rain_height / 4)
              obstructed = TRUE;

        if (i < aatv->rain_width)
          if (raindrops[i + 1].enabled == TRUE)
            if (raindrops[i + 1].location - raindrops[i + 1].length <
                aatv->rain_height / 4)
              obstructed = TRUE;

        if (obstructed == FALSE) {
          raindrops[i].location = 0;
          raindrops[i].length =
              g_random_int_range (aatv->rain_length_min, aatv->rain_length_max);
          raindrops[i].delay =
              g_random_int_range (aatv->rain_delay_min, aatv->rain_delay_max);
          raindrops[i].delay_counter = 0;
          raindrops[i].enabled = TRUE;
        }
      }
    } else {
      raindrops[i].delay_counter++;
      if (raindrops[i].delay_counter > raindrops[i].delay) {
        raindrops[i].delay_counter = 0;
        raindrops[i].location++;
      }
      if (raindrops[i].location - raindrops[i].length > aatv->rain_height) {
        raindrops[i].enabled = FALSE;
      }
    }
  }
}

static void
gst_aatv_render (GstAATv * aatv, gint32 * dest)
{
  gint x, y;
  guint font_x, font_y;
  guint background_pixels = 0;
  guint foreground_pixels = 0;
  guint char_index = 0;
  guint dest_index = 0;

  gchar input_letter, input_glyph, attribute;
  gboolean rain_pixel;

  GstAATvDroplet *raindrops = aatv->raindrops;

  const guchar *font_base_address = aa_currentfont (aatv->context)->data;
  guint font_height = aa_currentfont (aatv->context)->height;

  /* loop through the canvas height */
  for (y = 0; y < aa_scrheight (aatv->context); y++) {
    /* loop through the height of a character's font */
    for (font_y = 0; font_y < font_height; font_y++) {
      /* loop through the canvas width */
      for (x = 0; x < aa_scrwidth (aatv->context); x++) {

        /* which char are we working on */
        char_index = x + y * aa_scrwidth (aatv->context);
        /* lookup what character we need to render */
        input_letter = aa_text (aatv->context)[char_index];
        /* check for special attributes like bold or dimmed */
        attribute = aa_attrs (aatv->context)[char_index];
        /* look that character up in the font glyph table */
        input_glyph = font_base_address[input_letter * font_height + font_y];

        /* check if we need to re-color this character for rain effect */
        rain_pixel = FALSE;

        if (aatv->rain_mode == GST_RAIN_DOWN) {
          if (raindrops[x].enabled)
            if (y <= raindrops[x].location)
              if (y >= raindrops[x].location - raindrops[x].length)
                rain_pixel = TRUE;
        } else if (aatv->rain_mode == GST_RAIN_UP) {
          if (raindrops[x].enabled)
            if (aatv->rain_height - y <= raindrops[x].location)
              if (aatv->rain_height - y >=
                  raindrops[x].location - raindrops[x].length)
                rain_pixel = TRUE;
        } else if (aatv->rain_mode == GST_RAIN_LEFT) {
          if (raindrops[y].enabled)
            if (x <= raindrops[y].location)
              if (x >= raindrops[y].location - raindrops[y].length)
                rain_pixel = TRUE;
        } else if (aatv->rain_mode == GST_RAIN_RIGHT) {
          if (raindrops[y].enabled)
            if (aatv->rain_height - x <= raindrops[y].location)
              if (aatv->rain_height - x >=
                  raindrops[y].location - raindrops[y].length)
                rain_pixel = TRUE;
        }
        /* loop through the width of a character's font (always 8 pixels wide) */
        for (font_x = 0; font_x < 8; font_x++) {
          guint32 *pixel_argb;
          if (CHECK_BIT (input_glyph, font_x)) {
            if (attribute == AA_DIM) {
              if (rain_pixel)
                pixel_argb = &aatv->color_rain_dim;
              else
                pixel_argb = &aatv->color_text_dim;
            } else if (attribute == AA_BOLD) {
              if (rain_pixel)
                pixel_argb = &aatv->color_rain_bold;
              else
                pixel_argb = &aatv->color_text_bold;
            } else {
              if (rain_pixel)
                pixel_argb = &aatv->color_rain_normal;
              else
                pixel_argb = &aatv->color_text_normal;
            }
            foreground_pixels++;
          } else {
            pixel_argb = &aatv->color_background;
            background_pixels++;
          }
          dest[dest_index++] = *pixel_argb;
        }
      }
    }
  }

  aatv->lit_percentage =
      0.2 * (aatv->lit_percentage) +
      0.8 * (float) foreground_pixels / background_pixels;

  if (aatv->auto_brightness) {
    if (aatv->lit_percentage > aatv->brightness_target_max)
      if (aatv->ascii_parms.bright > -254)
        aatv->ascii_parms.bright--;
    if (aatv->lit_percentage < aatv->brightness_target_min)
      if (aatv->ascii_parms.bright < 254)
        aatv->ascii_parms.bright++;
  }
}

static GstFlowReturn
gst_aatv_transform_frame (GstVideoFilter * vfilter, GstVideoFrame * in_frame,
    GstVideoFrame * out_frame)
{
  GstAATv *aatv = GST_AATV (vfilter);

  if (aatv->rain_mode != GST_RAIN_OFF)
    gst_aatv_rain (aatv);

  GST_OBJECT_LOCK (aatv);

  gst_aatv_scale (aatv, GST_VIDEO_FRAME_PLANE_DATA (in_frame, 0),       /* src */
      aa_image (aatv->context), /* dest */
      GST_VIDEO_FRAME_WIDTH (in_frame), /* sw */
      GST_VIDEO_FRAME_HEIGHT (in_frame),        /* sh */
      GST_VIDEO_FRAME_PLANE_STRIDE (in_frame, 0),       /* ss */
      aa_imgwidth (aatv->context),      /* dw */
      aa_imgheight (aatv->context));    /* dh */

  aa_render (aatv->context, &aatv->ascii_parms, 0, 0,
      aa_imgwidth (aatv->context), aa_imgheight (aatv->context));
  gst_aatv_render (aatv, GST_VIDEO_FRAME_PLANE_DATA (out_frame, 0));

  GST_OBJECT_UNLOCK (aatv);

  return GST_FLOW_OK;
}


#define GST_TYPE_AADITHER (gst_aatv_dither_get_type())
static GType
gst_aatv_dither_get_type (void)
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

    dither_type = g_enum_register_static ("GstAATvDitherers", ditherers);
  }
  return dither_type;
}

#define GST_TYPE_AAFONT (gst_aatv_font_get_type())
static GType
gst_aatv_font_get_type (void)
{
  static GType font_type = 0;

  if (!font_type) {
    GEnumValue *fonts;
    gint n_fonts;
    gint i;

    for (n_fonts = 0; aa_fonts[n_fonts]; n_fonts++) {
      /* count number of fonts  */
    }

    fonts = g_new0 (GEnumValue, n_fonts + 1);

    for (i = 0; i < n_fonts; i++) {
      fonts[i].value = i;
      fonts[i].value_name = g_strdup (aa_fonts[i]->shortname);
      fonts[i].value_nick =
          g_strdelimit (g_strdup (aa_fonts[i]->name), " _", '-');
    }
    fonts[i].value = 0;
    fonts[i].value_name = NULL;
    fonts[i].value_nick = NULL;

    font_type = g_enum_register_static ("GstAATvFonts", fonts);
  }
  return font_type;
}

/* use a custom transform_caps */
static GstCaps *
gst_aatv_transform_caps (GstBaseTransform * trans, GstPadDirection direction,
    GstCaps * caps, GstCaps * filter)
{
  GstCaps *ret;
  GstAATv *aatv = GST_AATV (trans);
  GValue formats = G_VALUE_INIT;
  GValue value = G_VALUE_INIT;
  GValue src_width = G_VALUE_INIT;
  GValue src_height = G_VALUE_INIT;

  if (direction == GST_PAD_SINK) {

    ret = gst_caps_copy (caps);

    g_value_init (&src_width, G_TYPE_INT);
    g_value_init (&src_height, G_TYPE_INT);
    /* calculate output resolution from canvas size and font size */

    g_value_set_int (&src_width, aa_defparams.width * 8);
    g_value_set_int (&src_height,
        aa_defparams.height * aa_currentfont (aatv->context)->height);

    gst_caps_set_value (ret, "width", &src_width);
    gst_caps_set_value (ret, "height", &src_height);
    /* force RGBA output format */
    g_value_init (&formats, GST_TYPE_LIST);
    g_value_init (&value, G_TYPE_STRING);
    g_value_set_string (&value, "RGBA");
    gst_value_list_append_value (&formats, &value);

    gst_caps_set_value (ret, "format", &formats);

  } else {
    ret = gst_static_pad_template_get_caps (&sink_template_tv);
  }

  return ret;
}


static void
gst_aatv_finalize (GObject * object)
{
  GstAATv *aatv = GST_AATV (object);
  free (aatv->raindrops);
  if (aatv->context != NULL)
    aa_close (aatv->context);
  G_OBJECT_CLASS (parent_class)->finalize (object);
}


static void
gst_aatv_class_init (GstAATvClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstVideoFilterClass *videofilter_class;
  GstBaseTransformClass *transform_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  videofilter_class = (GstVideoFilterClass *) klass;
  transform_class = (GstBaseTransformClass *) klass;

  gobject_class->set_property = gst_aatv_set_property;
  gobject_class->get_property = gst_aatv_get_property;
  gobject_class->finalize = gst_aatv_finalize;

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_WIDTH,
      g_param_spec_int ("width", "width", "Width of the ASCII canvas", 0,
          G_MAXINT, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_HEIGHT,
      g_param_spec_int ("height", "height", "Height of the ASCII canvas", 0,
          G_MAXINT, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_DITHER,
      g_param_spec_enum ("dither", "dither",
          "Add noise to more closely approximate gray levels.",
          GST_TYPE_AADITHER, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_FONT,
      g_param_spec_enum ("font", "font", "AAlib Font", GST_TYPE_AAFONT, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_COLOR_TEXT,
      g_param_spec_uint ("color-text", "color-text",
          "Automatically sets color-test-bold, color-text-normal, and color-text-dim with progressively dimmer values (big-endian ARGB).",
          0, G_MAXUINT32, 0,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_COLOR_TEXT_BOLD,
      g_param_spec_uint ("color-text-bold", "color-text-bold",
          "Sets the brightest color to use for foreground ASCII text (big-endian ARGB).",
          0, G_MAXUINT32, 0,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_COLOR_TEXT_NORMAL, g_param_spec_uint ("color-text-normal",
          "color-text-normal",
          "Sets the normal brightness color to use for foreground ASCII text (big-endian ARGB).",
          0, G_MAXUINT32, 0,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_COLOR_TEXT_DIM,
      g_param_spec_uint ("color-text-dim", "color-text-dim",
          "Sets the dimmest brightness color to use for foreground ASCII text (big-endian ARGB).",
          0, G_MAXUINT32, 0,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_COLOR_BACKGROUND, g_param_spec_uint ("color-background",
          "color-background",
          "Color to use as the background for the ASCII text (big-endian ARGB).",
          0, G_MAXUINT32, 0,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_BRIGHTNESS,
      g_param_spec_int ("brightness", "brightness", "Brightness", -255,
          255, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_BRIGHTNESS_AUTO,
      g_param_spec_boolean ("brightness-auto", "brightness-auto",
          "Automatically adjust brightness based on the previous frame's foreground pixel fill percentage",
          TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_BRIGHTNESS_ACTUAL, g_param_spec_float ("brightness-actual",
          "brightness-actual",
          "Actual calculated foreground pixel fill percentage", 0.0, 1.0, 0.0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_BRIGHTNESS_TARGET_MIN, g_param_spec_float ("brightness-min",
          "brightness-min",
          "Minimum target foreground pixel fill percentage for automatic brightness control",
          0.0, 1.0, 0.0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_RAIN_SPAWN_RATE,
      g_param_spec_float ("rain-spawn-rate", "rain-spawn-rate",
          "Percentage chance for a raindrop to spawn", 0.0, 1.0, 0.0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_BRIGHTNESS_TARGET_MAX, g_param_spec_float ("brightness-max",
          "brightness-max",
          "Maximum target foreground pixel fill percentage for automatic brightness control",
          0.0, 1.0, 0.0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_CONTRAST,
      g_param_spec_int ("contrast", "contrast", "Contrast", 0, G_MAXUINT8,
          0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_GAMMA,
      g_param_spec_float ("gamma", "gamma", "Gamma correction", 0.0, 5.0, 1.0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_RANDOMVAL,
      g_param_spec_int ("randomval", "randomval",
          "Adds a random value in the range (-randomval/2,ranomval/2) to each pixel during rendering",
          0, 255, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_RAIN_DELAY_MIN,
      g_param_spec_int ("rain-delay-min", "rain-delay-min",
          "Minimum frame delay between rain motion", 0, G_MAXINT, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_RAIN_DELAY_MAX,
      g_param_spec_int ("rain-delay-max", "rain-delay-max",
          "Maximum frame delay between rain motion", 0, G_MAXINT, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_RAIN_LENGTH_MIN,
      g_param_spec_int ("rain-length-min", "rain-length-min",
          "Minimum length of a rain", 0, G_MAXINT, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_RAIN_LENGTH_MAX,
      g_param_spec_int ("rain-length-max", "rain-length-max",
          "Maximum length of a rain", 0, G_MAXINT, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_RAIN_MODE,
      g_param_spec_enum ("rain-mode", "rain-mode",
          "Set the direction of raindrops", GST_TYPE_AATV_RAIN_MODE, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_COLOR_RAIN,
      g_param_spec_uint ("color-rain", "color-rain",
          "Automatically sets color-rain-bold, color-rain-normal, and color-rain-dim with progressively dimmer values (big-endian ARGB).",
          0, G_MAXUINT32, 0,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_COLOR_RAIN_BOLD,
      g_param_spec_uint ("color-rain-bold", "color-rain-bold",
          "Sets the brightest color to use for foreground ASCII text rain overlays (big-endian ARGB).",
          0, G_MAXUINT32, 0,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_COLOR_RAIN_NORMAL, g_param_spec_uint ("color-rain-normal",
          "color-rain-normal",
          "Sets the normal brightness color to use for foreground ASCII text rain overlays (big-endian ARGB).",
          0, G_MAXUINT32, 0,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_COLOR_RAIN_DIM,
      g_param_spec_uint ("color-rain-dim", "color-rain-dim",
          "Sets the dimmest brightness color to use for foreground ASCII text rain overlays (big-endian ARGB).",
          0, G_MAXUINT32, 0,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_static_pad_template (gstelement_class,
      &sink_template_tv);
  gst_element_class_add_static_pad_template (gstelement_class,
      &src_template_tv);

  gst_element_class_set_static_metadata (gstelement_class,
      "aaTV effect", "Filter/Effect/Video",
      "ASCII art effect", "Eric Marks <bigmarkslp@gmail.com>");

  transform_class->transform_caps = GST_DEBUG_FUNCPTR (gst_aatv_transform_caps);
  videofilter_class->transform_frame =
      GST_DEBUG_FUNCPTR (gst_aatv_transform_frame);
}

static void
gst_aatv_rain_init (GstAATv * aatv)
{
  switch (aatv->rain_mode) {
    case GST_RAIN_DOWN:
    case GST_RAIN_UP:
      aatv->rain_width = aa_defparams.width;
      aatv->rain_height = aa_defparams.height;
      break;
    case GST_RAIN_LEFT:
    case GST_RAIN_RIGHT:
      aatv->rain_width = aa_defparams.height;
      aatv->rain_height = aa_defparams.width;
      break;
    case GST_RAIN_OFF:
      aatv->rain_width = 0;
      aatv->rain_height = 0;
  }

  if (aatv->context != NULL)
    aa_close (aatv->context);
  aatv->context = aa_init (&mem_d, &aa_defparams, NULL);
  aa_setfont (aatv->context, aa_fonts[0]);

  aatv->raindrops =
      realloc (aatv->raindrops,
      aatv->rain_width * sizeof (struct _GstAATvDroplet));
  for (gint i = 0; i < aatv->rain_width; i++)
    aatv->raindrops[i].enabled = FALSE;

}

static guint32
gst_aatv_set_color (guint32 input_color, guint8 dim)
{
  guint8 a = ((input_color >> 24) & 0xff);
  guint8 b = ((input_color >> 16) & 0xff) >> dim;
  guint8 g = ((input_color >> 8) & 0xff) >> dim;
  guint8 r = ((input_color >> 0) & 0xff) >> dim;

  return ((a << 24) | (b << 16) | (g << 8) | (r << 0));
}

static void
gst_aatv_set_color_rain (GstAATv * aatv, guint input_color)
{
  aatv->color_rain = input_color;
  aatv->color_rain_bold = gst_aatv_set_color (input_color, 0);
  aatv->color_rain_normal = gst_aatv_set_color (aatv->color_rain_bold, 1);
  aatv->color_rain_dim = gst_aatv_set_color (aatv->color_rain_normal, 1);
}

static void
gst_aatv_set_color_text (GstAATv * aatv, guint input_color)
{
  aatv->color_text = input_color;
  aatv->color_text_bold = gst_aatv_set_color (input_color, 0);
  aatv->color_text_normal = gst_aatv_set_color (aatv->color_text_bold, 1);
  aatv->color_text_dim = gst_aatv_set_color (aatv->color_text_normal, 1);
}

static void
gst_aatv_init (GstAATv * aatv)
{
  aa_defparams.width = 80;
  aa_defparams.height = 24;

  aatv->ascii_parms.bright = 0;
  aatv->ascii_parms.contrast = 0;
  aatv->ascii_parms.gamma = 1.0;
  aatv->ascii_parms.dither = 0;
  aatv->ascii_parms.inversion = 0;
  aatv->ascii_parms.randomval = 0;

  aatv->color_background =
      gst_aatv_set_color (PROP_AATV_COLOR_BACKGROUND_DEFAULT, 0);
  gst_aatv_set_color_rain (aatv, PROP_AATV_COLOR_RAIN_DEFAULT);
  gst_aatv_set_color_text (aatv, PROP_AATV_COLOR_TEXT_DEFAULT);

  aatv->rain_mode = PROP_AATV_RAIN_MODE_DEFAULT;

  gst_aatv_rain_init (aatv);

  aatv->rain_spawn_rate = PROP_RAIN_SPAWN_DEFAULT;

  aatv->auto_brightness = TRUE;
  aatv->brightness_target_min = PROP_BRIGHTNESS_TARGET_MIN_DEFAULT;
  aatv->brightness_target_max = PROP_BRIGHTNESS_TARGET_MAX_DEFAULT;
  aatv->lit_percentage =
      (PROP_BRIGHTNESS_TARGET_MIN_DEFAULT +
      PROP_BRIGHTNESS_TARGET_MAX_DEFAULT) / 2;

  aatv->rain_length_min = PROP_RAIN_LENGTH_MIN_DEFAULT;
  aatv->rain_length_max = PROP_RAIN_LENGTH_MAX_DEFAULT;

  aatv->rain_delay_min = PROP_RAIN_DELAY_MIN_DEFAULT;
  aatv->rain_delay_max = PROP_RAIN_DELAY_MAX_DEFAULT;
}

static void
gst_aatv_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstAATv *aatv = GST_AATV (object);

  switch (prop_id) {
    case PROP_WIDTH:{
      aa_defparams.width = g_value_get_int (value);
      /* recalculate output resolution based on new width */
      gst_aatv_rain_init (aatv);
      gst_pad_mark_reconfigure (GST_BASE_TRANSFORM_SRC_PAD (object));
      break;
    }
    case PROP_HEIGHT:{
      aa_defparams.height = g_value_get_int (value);
      /* recalculate output resolution based on new height */
      gst_aatv_rain_init (aatv);
      gst_pad_mark_reconfigure (GST_BASE_TRANSFORM_SRC_PAD (object));
      break;
    }
    case PROP_DITHER:{
      aatv->ascii_parms.dither = g_value_get_enum (value);
      break;
    }
    case PROP_FONT:{
      aa_setfont (aatv->context, aa_fonts[g_value_get_enum (value)]);
      /* recalculate output resolution based on new font */
      gst_pad_mark_reconfigure (GST_BASE_TRANSFORM_SRC_PAD (object));
      break;
    }
    case PROP_BRIGHTNESS:{
      aatv->ascii_parms.bright = g_value_get_int (value);
      break;
    }
    case PROP_CONTRAST:{
      aatv->ascii_parms.contrast = g_value_get_int (value);
      break;
    }
    case PROP_GAMMA:{
      aatv->ascii_parms.gamma = g_value_get_float (value);
      break;
    }
    case PROP_BRIGHTNESS_TARGET_MIN:{
      if (g_value_get_float (value) <= aatv->brightness_target_max)
        aatv->brightness_target_min = g_value_get_float (value);
      break;
    }
    case PROP_BRIGHTNESS_TARGET_MAX:{
      if (g_value_get_float (value) >= aatv->brightness_target_min)
        aatv->brightness_target_max = g_value_get_float (value);
      break;
    }
    case PROP_RAIN_SPAWN_RATE:{
      aatv->rain_spawn_rate = g_value_get_float (value);
      break;
    }
    case PROP_COLOR_TEXT:{
      aatv->color_text = g_value_get_uint (value);
      gst_aatv_set_color_text (aatv, aatv->color_text);
      break;
    }
    case PROP_COLOR_TEXT_BOLD:{
      aatv->color_text_bold = gst_aatv_set_color (g_value_get_uint (value), 0);
      break;
    }
    case PROP_COLOR_TEXT_NORMAL:{
      aatv->color_text_normal =
          gst_aatv_set_color (g_value_get_uint (value), 0);
      break;
    }
    case PROP_COLOR_TEXT_DIM:{
      aatv->color_text_dim = gst_aatv_set_color (g_value_get_uint (value), 0);
      break;
    }
    case PROP_COLOR_BACKGROUND:{
      aatv->color_background = gst_aatv_set_color (g_value_get_uint (value), 0);
      break;
    }
    case PROP_COLOR_RAIN:{
      aatv->color_rain = g_value_get_uint (value);
      gst_aatv_set_color_rain (aatv, aatv->color_rain);
      break;
    }
    case PROP_COLOR_RAIN_BOLD:{
      aatv->color_rain_bold = gst_aatv_set_color (g_value_get_uint (value), 0);
      break;
    }
    case PROP_COLOR_RAIN_NORMAL:{
      aatv->color_rain_normal =
          gst_aatv_set_color (g_value_get_uint (value), 0);
      break;
    }
    case PROP_COLOR_RAIN_DIM:{
      aatv->color_rain_dim = gst_aatv_set_color (g_value_get_uint (value), 0);
      break;
    }
    case PROP_BRIGHTNESS_AUTO:{
      aatv->auto_brightness = g_value_get_boolean (value);
      break;
    }
    case PROP_RANDOMVAL:{
      aatv->ascii_parms.randomval = g_value_get_int (value);
      break;
    }
    case PROP_RAIN_DELAY_MIN:{
      if (g_value_get_float (value) <= aatv->rain_delay_max)
        aatv->rain_delay_min = g_value_get_int (value);
      break;
    }
    case PROP_RAIN_DELAY_MAX:{
      if (g_value_get_float (value) >= aatv->rain_delay_min)
        aatv->rain_delay_max = g_value_get_int (value);
      break;
    }
    case PROP_RAIN_LENGTH_MIN:{
      if (g_value_get_float (value) <= aatv->rain_length_max)
        aatv->rain_length_min = g_value_get_int (value);
      break;
    }
    case PROP_RAIN_LENGTH_MAX:{
      if (g_value_get_float (value) >= aatv->rain_length_min)
        aatv->rain_length_max = g_value_get_int (value);
      break;
    }
    case PROP_RAIN_MODE:{
      aatv->rain_mode = g_value_get_enum (value);
      break;
    }
    default:
      break;
  }
}

static void
gst_aatv_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstAATv *aatv = GST_AATV (object);

  switch (prop_id) {
    case PROP_BRIGHTNESS_ACTUAL:{
      g_value_set_float (value, aatv->lit_percentage);
      break;
    }
    case PROP_WIDTH:{
      g_value_set_int (value, aa_defparams.width);
      break;
    }
    case PROP_HEIGHT:{
      g_value_set_int (value, aa_defparams.height);

      break;
    }
    case PROP_DITHER:{
      g_value_set_enum (value, aatv->ascii_parms.dither);
      break;
    }
    case PROP_FONT:{
      g_value_set_enum (value, aatv->ascii_parms.dither);
      break;
    }
    case PROP_BRIGHTNESS:{
      g_value_set_int (value, aatv->ascii_parms.bright);
      break;
    }
    case PROP_BRIGHTNESS_AUTO:{
      g_value_set_boolean (value, aatv->auto_brightness);
      break;
    }
    case PROP_CONTRAST:{
      g_value_set_int (value, aatv->ascii_parms.contrast);
      break;
    }
    case PROP_GAMMA:{
      g_value_set_float (value, aatv->ascii_parms.gamma);
      break;
    }
    case PROP_RAIN_SPAWN_RATE:{
      g_value_set_float (value, aatv->rain_spawn_rate);
      break;
    }
    case PROP_BRIGHTNESS_TARGET_MIN:{
      g_value_set_float (value, aatv->brightness_target_min);
      break;
    }
    case PROP_BRIGHTNESS_TARGET_MAX:{
      g_value_set_float (value, aatv->brightness_target_max);
      break;
    }
    case PROP_COLOR_TEXT:{
      g_value_set_uint (value, aatv->color_text);
      break;
    }
    case PROP_COLOR_TEXT_BOLD:{
      g_value_set_uint (value, aatv->color_text_bold);
      break;
    }
    case PROP_COLOR_TEXT_NORMAL:{
      g_value_set_uint (value, aatv->color_text_normal);
      break;
    }
    case PROP_COLOR_TEXT_DIM:{
      g_value_set_uint (value, aatv->color_text_dim);
      break;
    }
    case PROP_COLOR_BACKGROUND:{
      g_value_set_uint (value, aatv->color_background);
      break;
    }
    case PROP_COLOR_RAIN:{
      g_value_set_uint (value, aatv->color_rain);
      break;
    }
    case PROP_COLOR_RAIN_BOLD:{
      g_value_set_uint (value, aatv->color_rain_bold);
      break;
    }
    case PROP_COLOR_RAIN_NORMAL:{
      g_value_set_uint (value, aatv->color_rain_normal);
      break;
    }
    case PROP_COLOR_RAIN_DIM:{
      g_value_set_uint (value, aatv->color_rain_dim);
      break;
    }
    case PROP_RANDOMVAL:{
      g_value_set_int (value, aatv->ascii_parms.randomval);
      break;
    }
    case PROP_RAIN_MODE:{
      g_value_set_enum (value, aatv->rain_mode);
      break;
    }
    case PROP_RAIN_DELAY_MIN:{
      g_value_set_int (value, aatv->rain_delay_min);
      break;
    }
    case PROP_RAIN_DELAY_MAX:{
      g_value_set_int (value, aatv->rain_delay_max);
      break;
    }
    case PROP_RAIN_LENGTH_MIN:{
      g_value_set_int (value, aatv->rain_length_min);
      break;
    }
    case PROP_RAIN_LENGTH_MAX:{
      g_value_set_int (value, aatv->rain_length_max);
      break;
    }
    default:{
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
  }
}
