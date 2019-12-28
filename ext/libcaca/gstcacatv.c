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
 * SECTION:element-cacatv
 * @see_also: #GstCacaSink
 *
 * Transforms video into color ascii art.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 videotestsrc ! cacatv ! videoconvert ! autovideosink
 * ]| This pipeline shows the effect of cacatv on a test stream.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "gstcacatv.h"

/* cacatv signals and args */
enum
{
  LAST_SIGNAL
};

#define GST_CACA_DEFAULT_FONT 0
#define GST_CACA_DEFAULT_SCREEN_WIDTH 80
#define GST_CACA_DEFAULT_SCREEN_HEIGHT 24
#define GST_CACA_DEFAULT_DITHER CACA_DITHERING_NONE
#define GST_CACA_DEFAULT_ANTIALIASING FALSE

enum
{
  PROP_0,
  PROP_CANVAS_WIDTH,
  PROP_CANVAS_HEIGHT,
  PROP_FONT,
  PROP_DITHER,
  PROP_ANTIALIASING
};

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE
        ("{ RGB, BGR, RGBx, xRGB, BGRx, xBGR, RGBA, RGB16, RGB15 }"))
    );
static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{ ARGB }"))
    );

static GstFlowReturn
gst_cacatv_transform_frame (GstVideoFilter * vfilter, GstVideoFrame * in_frame,
    GstVideoFrame * out_frame)
{
  GstCACATv *cacatv = GST_CACATV (vfilter);

  GST_OBJECT_LOCK (cacatv);
  caca_clear_canvas (cacatv->canvas);
  caca_dither_bitmap (cacatv->canvas, 0, 0,
      caca_get_canvas_width (cacatv->canvas),
      caca_get_canvas_height (cacatv->canvas), cacatv->dither,
      GST_VIDEO_FRAME_PLANE_DATA (in_frame, 0));
  /* libcaca always renders ARGB */
  caca_render_canvas (cacatv->canvas, cacatv->font,
      GST_VIDEO_FRAME_PLANE_DATA (out_frame, 0), cacatv->src_width,
      cacatv->src_height, 4 * cacatv->src_width);
  GST_OBJECT_UNLOCK (cacatv);

  return GST_FLOW_OK;
}

static void gst_cacatv_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_cacatv_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

#define gst_cacatv_parent_class parent_class
G_DEFINE_TYPE (GstCACATv, gst_cacatv, GST_TYPE_VIDEO_FILTER);

#define GST_TYPE_CACADITHER (gst_cacatv_dither_get_type())
static GType
gst_cacatv_dither_get_type (void)
{
  static GType dither_type = 0;

  static const GEnumValue dither_types[] = {
    {CACA_DITHERING_NONE, "No dither_mode", "none"},
    {CACA_DITHERING_ORDERED2, "Ordered 2x2 Bayer dither_mode", "2x2"},
    {CACA_DITHERING_ORDERED4, "Ordered 4x4 Bayer dither_mode", "4x4"},
    {CACA_DITHERING_ORDERED8, "Ordered 8x8 Bayer dither_mode", "8x8"},
    {CACA_DITHERING_RANDOM, "Random dither_mode", "random"},
    {0, NULL, NULL},
  };

  if (!dither_type) {
    dither_type = g_enum_register_static ("GstCACATvDithering", dither_types);
  }
  return dither_type;
}

static gboolean
gst_cacatv_setcaps (GstVideoFilter * filter, GstCaps * incaps,
    GstVideoInfo * in_info, GstCaps * outcaps, GstVideoInfo * out_info)
{
  GstCACATv *cacatv = GST_CACATV (filter);
  GstVideoInfo info;
  guint bpp, red_mask, green_mask, blue_mask, depth;

  if (!gst_video_info_from_caps (&info, incaps))
    goto caps_error;

  cacatv->sink_width = GST_VIDEO_INFO_WIDTH (&info);
  cacatv->sink_height = GST_VIDEO_INFO_HEIGHT (&info);

  switch (GST_VIDEO_INFO_FORMAT (&info)) {
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_BGR:
      bpp = 8 * info.finfo->pixel_stride[0];
      depth = 3;
      red_mask = 0xff << (8 * info.finfo->poffset[GST_VIDEO_COMP_R]);
      green_mask = 0xff << (8 * info.finfo->poffset[GST_VIDEO_COMP_G]);
      blue_mask = 0xff << (8 * info.finfo->poffset[GST_VIDEO_COMP_B]);
      break;
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_xBGR:
      bpp = 8 * info.finfo->pixel_stride[0];
      depth = 4;
      red_mask = 0xff << (8 * info.finfo->poffset[GST_VIDEO_COMP_R]);
      green_mask = 0xff << (8 * info.finfo->poffset[GST_VIDEO_COMP_G]);
      blue_mask = 0xff << (8 * info.finfo->poffset[GST_VIDEO_COMP_B]);
      break;
    case GST_VIDEO_FORMAT_RGB16:
      bpp = 16;
      depth = 2;
      red_mask = 0xf800;
      green_mask = 0x07e0;
      blue_mask = 0x001f;
      break;
    case GST_VIDEO_FORMAT_RGB15:
      bpp = 16;
      depth = 2;
      red_mask = 0x7c00;
      green_mask = 0x03e0;
      blue_mask = 0x001f;
      break;
    default:
      goto invalid_format;
  }

  /* free if already exists (there is no dither resize) */
  caca_free_dither (cacatv->dither);
  cacatv->dither =
      caca_create_dither (bpp, cacatv->sink_width, cacatv->sink_height,
      depth * cacatv->sink_width, red_mask, green_mask, blue_mask, 0x00000000);
  caca_set_canvas_size (cacatv->canvas, cacatv->canvas_width,
      cacatv->canvas_height);

  return TRUE;
  /* ERRORS */
caps_error:
  {
    GST_ERROR_OBJECT (cacatv, "error parsing caps");
    return FALSE;
  }
invalid_format:
  {
    GST_ERROR_OBJECT (cacatv, "invalid format");
    return FALSE;
  }
}

/* use a custom transform_caps */
static GstCaps *
gst_cacatv_transform_caps (GstBaseTransform * trans, GstPadDirection direction,
    GstCaps * caps, GstCaps * filter)
{
  GstCaps *ret;
  GstCACATv *cacatv = GST_CACATV (trans);
  GValue formats = G_VALUE_INIT;
  GValue value = G_VALUE_INIT;
  GValue src_width = G_VALUE_INIT;
  GValue src_height = G_VALUE_INIT;

  if (direction == GST_PAD_SINK) {

    ret = gst_caps_copy (caps);

    g_value_init (&src_width, G_TYPE_INT);
    g_value_init (&src_height, G_TYPE_INT);
    /* calculate output resolution from canvas size and font size */
    cacatv->src_width =
        cacatv->canvas_width * caca_get_font_width (cacatv->font);
    cacatv->src_height =
        cacatv->canvas_height * caca_get_font_height (cacatv->font);

    g_value_set_int (&src_width, cacatv->src_width);
    g_value_set_int (&src_height, cacatv->src_height);

    gst_caps_set_value (ret, "width", &src_width);
    gst_caps_set_value (ret, "height", &src_height);
    /* force ARGB output format */
    g_value_init (&formats, GST_TYPE_LIST);
    g_value_init (&value, G_TYPE_STRING);
    g_value_set_string (&value, "ARGB");
    gst_value_list_append_value (&formats, &value);

    gst_caps_set_value (ret, "format", &formats);
  } else {
    ret = gst_static_pad_template_get_caps (&sink_template);
  }

  return ret;
}

static void
gst_cacatv_finalize (GObject * object)
{
  GstCACATv *cacatv = GST_CACATV (object);
  caca_free_font (cacatv->font);
  caca_free_dither (cacatv->dither);
  caca_free_canvas (cacatv->canvas);
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_cacatv_class_init (GstCACATvClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstVideoFilterClass *videofilter_class;
  GstBaseTransformClass *transform_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  videofilter_class = (GstVideoFilterClass *) klass;
  transform_class = (GstBaseTransformClass *) klass;

  gobject_class->set_property = gst_cacatv_set_property;
  gobject_class->get_property = gst_cacatv_get_property;
  gobject_class->finalize = gst_cacatv_finalize;

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_CANVAS_WIDTH,
      g_param_spec_int ("canvas-width", "Canvas Width",
          "The width of the canvas in characters", 0, G_MAXINT,
          GST_CACA_DEFAULT_SCREEN_WIDTH,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_CANVAS_HEIGHT,
      g_param_spec_int ("canvas-height", "Canvas Height",
          "The height of the canvas in characters", 0, G_MAXINT,
          GST_CACA_DEFAULT_SCREEN_HEIGHT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_FONT,
      g_param_spec_int ("font", "Font", "selected libcaca font", 0, G_MAXINT,
          GST_CACA_DEFAULT_SCREEN_HEIGHT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_DITHER,
      g_param_spec_enum ("dither", "Dither Type", "Set type of Dither",
          GST_TYPE_CACADITHER, GST_CACA_DEFAULT_DITHER,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_ANTIALIASING,
      g_param_spec_boolean ("anti-aliasing", "Anti Aliasing",
          "Enables Anti-Aliasing", GST_CACA_DEFAULT_ANTIALIASING,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (gstelement_class,
      "CacaTV effect", "Filter/Effect/Video",
      "Colored ASCII art effect", "Eric Marks <bigmarkslp@gmail.com>");

  gst_element_class_add_static_pad_template (gstelement_class, &sink_template);
  gst_element_class_add_static_pad_template (gstelement_class, &src_template);

  videofilter_class->transform_frame =
      GST_DEBUG_FUNCPTR (gst_cacatv_transform_frame);
  videofilter_class->set_info = GST_DEBUG_FUNCPTR (gst_cacatv_setcaps);
  transform_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_cacatv_transform_caps);
}

static void
gst_cacatv_init (GstCACATv * cacatv)
{
  char const *const *fonts = caca_get_font_list ();
  cacatv->font_index = GST_CACA_DEFAULT_FONT;
  cacatv->font = caca_load_font (fonts[cacatv->font_index], 0);

  cacatv->canvas_width = GST_CACA_DEFAULT_SCREEN_WIDTH;
  cacatv->canvas_height = GST_CACA_DEFAULT_SCREEN_HEIGHT;
  cacatv->canvas =
      caca_create_canvas (cacatv->canvas_width, cacatv->canvas_height);

  cacatv->antialiasing = FALSE;
  caca_set_feature (CACA_ANTIALIASING_MIN);

  cacatv->dither_mode = 0;
  caca_set_dithering (CACA_DITHERING_NONE);
}

static void
gst_cacatv_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstCACATv *cacatv = GST_CACATV (object);

  switch (prop_id) {
    case PROP_DITHER:{
      cacatv->dither_mode = g_value_get_enum (value);
      caca_set_dithering (cacatv->dither_mode + CACA_DITHERING_NONE);
      break;
    }
    case PROP_ANTIALIASING:{
      cacatv->antialiasing = g_value_get_boolean (value);
      if (cacatv->antialiasing) {
        caca_set_feature (CACA_ANTIALIASING_MAX);
      } else {
        caca_set_feature (CACA_ANTIALIASING_MIN);
      }
      break;
    }
    case PROP_CANVAS_WIDTH:{
      cacatv->canvas_width = g_value_get_int (value);
      /* recalculate output resolution based on new width */
      gst_pad_mark_reconfigure (GST_BASE_TRANSFORM_SRC_PAD (object));
      break;
    }
    case PROP_CANVAS_HEIGHT:{
      cacatv->canvas_height = g_value_get_int (value);
      /* recalculate output resolution based on new height */
      gst_pad_mark_reconfigure (GST_BASE_TRANSFORM_SRC_PAD (object));
      break;
    }
    case PROP_FONT:{
      char const *const *fonts = caca_get_font_list ();
      cacatv->font_index = g_value_get_int (value);
      caca_free_font (cacatv->font);
      cacatv->font = caca_load_font (fonts[cacatv->font_index], 0);
      /* recalculate output resolution based on new font */
      gst_pad_mark_reconfigure (GST_BASE_TRANSFORM_SRC_PAD (object));
      break;
    }
    default:
      break;
  }
}

static void
gst_cacatv_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstCACATv *cacatv = GST_CACATV (object);

  switch (prop_id) {
    case PROP_CANVAS_WIDTH:{
      g_value_set_int (value, cacatv->canvas_width);
      break;
    }
    case PROP_CANVAS_HEIGHT:{
      g_value_set_int (value, cacatv->canvas_height);
      break;
    }
    case PROP_DITHER:{
      g_value_set_enum (value, cacatv->dither_mode);
      break;
    }
    case PROP_ANTIALIASING:{
      g_value_set_boolean (value, cacatv->antialiasing);
      break;
    }
    case PROP_FONT:{
      g_value_set_int (value, cacatv->font_index);
      break;
    }
    default:{
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
  }
}
