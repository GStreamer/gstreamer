/* GStreamer
 * Copyright (C) <2016> Matthew Waters <matthew@centricular.com>
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
/*
 * This file was modified from videobalance and converted to OpenGL
 */

/**
 * SECTION:element-glcolorbalance
 * @title: glcolorbalance
 *
 * Adjusts brightness, contrast, hue, saturation on a video stream.
 *
 * ## Example launch line
 * |[
 * gst-launch-1.0 videotestsrc ! glupload ! glcolorbalance saturation=0.0 ! glcolorconvert ! gldownload ! ximagesink
 * ]| This pipeline converts the image to black and white by setting the
 * saturation to 0.0.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/math-compat.h>

#include "gstglcolorbalance.h"
#include <string.h>

#include <gst/video/colorbalance.h>

GST_DEBUG_CATEGORY_STATIC (glcolorbalance_debug);
#define GST_CAT_DEFAULT glcolorbalance_debug

/* GstGLColorBalance properties */
#define DEFAULT_PROP_CONTRAST       1.0
#define DEFAULT_PROP_BRIGHTNESS	    0.0
#define DEFAULT_PROP_HUE            0.0
#define DEFAULT_PROP_SATURATION	    1.0

/* *INDENT-OFF* */
static const gchar *color_balance_frag =
  "#ifdef GL_ES\n"
  "precision mediump float;\n"
  "#endif\n"
  "uniform float brightness;\n"
  "uniform float contrast;\n"
  "uniform float saturation;\n"
  "uniform float hue;\n"
  "varying vec2 v_texcoord;\n"
  "uniform sampler2D tex;\n"
  "#define from_yuv_bt601_offset vec3(-0.0625, -0.5, -0.5)\n"
  "#define from_yuv_bt601_rcoeff vec3(1.164, 0.000, 1.596)\n"
  "#define from_yuv_bt601_gcoeff vec3(1.164,-0.391,-0.813)\n"
  "#define from_yuv_bt601_bcoeff vec3(1.164, 2.018, 0.000)\n"
  "#define from_rgb_bt601_offset vec3(0.0625, 0.5, 0.5)\n"
  "#define from_rgb_bt601_ycoeff vec3(0.256816, 0.504154, 0.0979137)\n"
  "#define from_rgb_bt601_ucoeff vec3(-0.148246, -0.29102, 0.439266)\n"
  "#define from_rgb_bt601_vcoeff vec3(0.439271, -0.367833, -0.071438)\n"
  "#define PI 3.14159265\n"
  "\n"
  "vec3 yuv_to_rgb (vec3 val) {\n"
  "  vec3 rgb;\n"
  "  val += from_yuv_bt601_offset;\n"
  "  rgb.r = dot(val, from_yuv_bt601_rcoeff);\n"
  "  rgb.g = dot(val, from_yuv_bt601_gcoeff);\n"
  "  rgb.b = dot(val, from_yuv_bt601_bcoeff);\n"
  "  return rgb;\n"
  "}\n"
  "vec3 rgb_to_yuv (vec3 val) {\n"
  "  vec3 yuv;\n"
  "  yuv.r = dot(val.rgb, from_rgb_bt601_ycoeff);\n"
  "  yuv.g = dot(val.rgb, from_rgb_bt601_ucoeff);\n"
  "  yuv.b = dot(val.rgb, from_rgb_bt601_vcoeff);\n"
  "  yuv += from_rgb_bt601_offset;\n"
  "  return yuv;\n"
  "}\n"
  /* 224 = 256 - (256 - 240) - 16*/
  "float luma_to_narrow (float luma) {\n"
  "  return (luma + 16.0 / 256.0) * 219.0 / 256.0;"
  "}\n"
  "float luma_to_full (float luma) {\n"
  "  return (luma * 256.0 / 219.0) - 16.0 / 256.0;"
  "}\n"
  "void main () {\n"
  "  vec3 yuv;\n"
  /* operations translated from videobalanceand tested with glvideomixer
   * with one pad's paremeters blend-equation-rgb={subtract,reverse-subtract},
   * blend-function-src-rgb=src-color and blend-function-dst-rgb=dst-color */
  "  float hue_cos = cos (PI * hue);\n"
  "  float hue_sin = sin (PI * hue);\n"
  "  vec4 rgba = texture2D (tex, v_texcoord);\n"
  "  yuv = rgb_to_yuv (rgba.rgb);\n"
  "  yuv.x = clamp (luma_to_narrow (luma_to_full(yuv.x) * contrast) + brightness, 0.0, 1.0);\n"
  "  vec2 uv = yuv.yz;\n"
  "  yuv.y = clamp (0.5 + (((uv.x - 0.5) * hue_cos + (uv.y - 0.5) * hue_sin) * saturation), 0.0, 1.0);\n"
  "  yuv.z = clamp (0.5 + (((0.5 - uv.x) * hue_sin + (uv.y - 0.5) * hue_cos) * saturation), 0.0, 1.0);\n"
  "  rgba.rgb = yuv_to_rgb (yuv);\n"
  "  gl_FragColor = rgba;\n"
  "}\n";
/* *INDENT-ON* */

enum
{
  PROP_0,
  PROP_CONTRAST,
  PROP_BRIGHTNESS,
  PROP_HUE,
  PROP_SATURATION
};

static void gst_gl_color_balance_colorbalance_init (GstColorBalanceInterface *
    iface);

static void gst_gl_color_balance_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gl_color_balance_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

#define gst_gl_color_balance_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstGLColorBalance, gst_gl_color_balance,
    GST_TYPE_GL_FILTER,
    G_IMPLEMENT_INTERFACE (GST_TYPE_COLOR_BALANCE,
        gst_gl_color_balance_colorbalance_init));

static gboolean
gst_gl_color_balance_is_passthrough (GstGLColorBalance * glcolorbalance)
{
  return glcolorbalance->contrast == 1.0 &&
      glcolorbalance->brightness == 0.0 &&
      glcolorbalance->hue == 0.0 && glcolorbalance->saturation == 1.0;
}

static void
gst_gl_color_balance_update_properties (GstGLColorBalance * glcolorbalance)
{
  gboolean current_passthrough, passthrough;
  GstBaseTransform *base = GST_BASE_TRANSFORM (glcolorbalance);

  GST_OBJECT_LOCK (glcolorbalance);
  passthrough = gst_gl_color_balance_is_passthrough (glcolorbalance);
  GST_OBJECT_UNLOCK (glcolorbalance);
  current_passthrough = gst_base_transform_is_passthrough (base);

  gst_base_transform_set_passthrough (base, passthrough);
  if (current_passthrough != passthrough)
    gst_base_transform_reconfigure_src (base);
}

static gboolean
_create_shader (GstGLColorBalance * balance)
{
  GstGLBaseFilter *base_filter = GST_GL_BASE_FILTER (balance);
  GstGLFilter *filter = GST_GL_FILTER (balance);
  GError *error = NULL;

  if (balance->shader)
    gst_object_unref (balance->shader);

  if (!(balance->shader =
          gst_gl_shader_new_link_with_stages (base_filter->context, &error,
              gst_glsl_stage_new_default_vertex (base_filter->context),
              gst_glsl_stage_new_with_string (base_filter->context,
                  GL_FRAGMENT_SHADER, GST_GLSL_VERSION_NONE,
                  GST_GLSL_PROFILE_ES | GST_GLSL_PROFILE_COMPATIBILITY,
                  color_balance_frag), NULL))) {
    GST_ELEMENT_ERROR (balance, RESOURCE, NOT_FOUND, ("%s",
            "Failed to initialize colorbalance shader"), ("%s",
            error ? error->message : "Unknown error"));
    return FALSE;
  }

  filter->draw_attr_position_loc =
      gst_gl_shader_get_attribute_location (balance->shader, "a_position");
  filter->draw_attr_texture_loc =
      gst_gl_shader_get_attribute_location (balance->shader, "a_texcoord");

  return TRUE;
}

static gboolean
gst_gl_color_balance_gl_start (GstGLBaseFilter * base_filter)
{
  GstGLColorBalance *balance = GST_GL_COLOR_BALANCE (base_filter);

  if (!_create_shader (balance))
    return FALSE;

  return GST_GL_BASE_FILTER_CLASS (parent_class)->gl_start (base_filter);
}

static void
gst_gl_color_balance_gl_stop (GstGLBaseFilter * base_filter)
{
  GstGLColorBalance *balance = GST_GL_COLOR_BALANCE (base_filter);

  if (balance->shader)
    gst_object_unref (balance->shader);
  balance->shader = NULL;

  GST_GL_BASE_FILTER_CLASS (parent_class)->gl_stop (base_filter);
}

static void
gst_gl_color_balance_before_transform (GstBaseTransform * base, GstBuffer * buf)
{
  GstGLColorBalance *balance = GST_GL_COLOR_BALANCE (base);
  GstClockTime timestamp, stream_time;

  timestamp = GST_BUFFER_TIMESTAMP (buf);
  stream_time =
      gst_segment_to_stream_time (&base->segment, GST_FORMAT_TIME, timestamp);

  GST_DEBUG_OBJECT (balance, "sync to %" GST_TIME_FORMAT,
      GST_TIME_ARGS (timestamp));

  if (GST_CLOCK_TIME_IS_VALID (stream_time))
    gst_object_sync_values (GST_OBJECT (balance), stream_time);
}

static gboolean
gst_gl_color_balance_filter_texture (GstGLFilter * filter, GstGLMemory * in_tex,
    GstGLMemory * out_tex)
{
  GstGLColorBalance *balance = GST_GL_COLOR_BALANCE (filter);

  if (!balance->shader)
    _create_shader (balance);

  gst_gl_shader_use (balance->shader);
  GST_OBJECT_LOCK (balance);
  gst_gl_shader_set_uniform_1f (balance->shader, "brightness",
      balance->brightness);
  gst_gl_shader_set_uniform_1f (balance->shader, "contrast", balance->contrast);
  gst_gl_shader_set_uniform_1f (balance->shader, "saturation",
      balance->saturation);
  gst_gl_shader_set_uniform_1f (balance->shader, "hue", balance->hue);
  GST_OBJECT_UNLOCK (balance);

  gst_gl_filter_render_to_target_with_shader (filter, in_tex, out_tex,
      balance->shader);

  return TRUE;
}

static void
gst_gl_color_balance_finalize (GObject * object)
{
  GstGLColorBalance *balance = GST_GL_COLOR_BALANCE (object);
  GList *channels = NULL;

  channels = balance->channels;
  while (channels) {
    GstColorBalanceChannel *channel = channels->data;

    g_object_unref (channel);
    channels->data = NULL;
    channels = g_list_next (channels);
  }

  if (balance->channels)
    g_list_free (balance->channels);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_gl_color_balance_class_init (GstGLColorBalanceClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;
  GstBaseTransformClass *trans_class = (GstBaseTransformClass *) klass;
  GstGLBaseFilterClass *base_filter_class = (GstGLBaseFilterClass *) klass;
  GstGLFilterClass *filter_class = (GstGLFilterClass *) klass;

  GST_DEBUG_CATEGORY_INIT (glcolorbalance_debug, "glcolorbalance", 0,
      "glcolorbalance");

  gobject_class->finalize = gst_gl_color_balance_finalize;
  gobject_class->set_property = gst_gl_color_balance_set_property;
  gobject_class->get_property = gst_gl_color_balance_get_property;

  g_object_class_install_property (gobject_class, PROP_CONTRAST,
      g_param_spec_double ("contrast", "Contrast", "contrast",
          0.0, 2.0, DEFAULT_PROP_CONTRAST,
          GST_PARAM_CONTROLLABLE | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_BRIGHTNESS,
      g_param_spec_double ("brightness", "Brightness", "brightness", -1.0, 1.0,
          DEFAULT_PROP_BRIGHTNESS,
          GST_PARAM_CONTROLLABLE | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_HUE,
      g_param_spec_double ("hue", "Hue", "hue", -1.0, 1.0, DEFAULT_PROP_HUE,
          GST_PARAM_CONTROLLABLE | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_SATURATION,
      g_param_spec_double ("saturation", "Saturation", "saturation", 0.0, 2.0,
          DEFAULT_PROP_SATURATION,
          GST_PARAM_CONTROLLABLE | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (gstelement_class, "Video balance",
      "Filter/Effect/Video",
      "Adjusts brightness, contrast, hue, saturation on a video stream",
      "Matthew Waters <matthew@centricular.com>");

  trans_class->before_transform =
      GST_DEBUG_FUNCPTR (gst_gl_color_balance_before_transform);
  trans_class->transform_ip_on_passthrough = FALSE;

  base_filter_class->gl_start =
      GST_DEBUG_FUNCPTR (gst_gl_color_balance_gl_start);
  base_filter_class->gl_stop = GST_DEBUG_FUNCPTR (gst_gl_color_balance_gl_stop);

  filter_class->filter_texture =
      GST_DEBUG_FUNCPTR (gst_gl_color_balance_filter_texture);
}

static void
gst_gl_color_balance_init (GstGLColorBalance * glcolorbalance)
{
  const gchar *channels[4] = { "HUE", "SATURATION",
    "BRIGHTNESS", "CONTRAST"
  };
  gint i;

  /* Initialize propertiews */
  glcolorbalance->contrast = DEFAULT_PROP_CONTRAST;
  glcolorbalance->brightness = DEFAULT_PROP_BRIGHTNESS;
  glcolorbalance->hue = DEFAULT_PROP_HUE;
  glcolorbalance->saturation = DEFAULT_PROP_SATURATION;

  gst_gl_color_balance_update_properties (glcolorbalance);

  /* Generate the channels list */
  for (i = 0; i < G_N_ELEMENTS (channels); i++) {
    GstColorBalanceChannel *channel;

    channel = g_object_new (GST_TYPE_COLOR_BALANCE_CHANNEL, NULL);
    channel->label = g_strdup (channels[i]);
    channel->min_value = -1000;
    channel->max_value = 1000;

    glcolorbalance->channels =
        g_list_append (glcolorbalance->channels, channel);
  }
}

static const GList *
gst_gl_color_balance_colorbalance_list_channels (GstColorBalance * balance)
{
  GstGLColorBalance *glcolorbalance = GST_GL_COLOR_BALANCE (balance);

  g_return_val_if_fail (glcolorbalance != NULL, NULL);
  g_return_val_if_fail (GST_IS_GL_COLOR_BALANCE (glcolorbalance), NULL);

  return glcolorbalance->channels;
}

static void
gst_gl_color_balance_colorbalance_set_value (GstColorBalance * balance,
    GstColorBalanceChannel * channel, gint value)
{
  GstGLColorBalance *vb = GST_GL_COLOR_BALANCE (balance);
  gdouble new_val;
  gboolean changed = FALSE;

  g_return_if_fail (vb != NULL);
  g_return_if_fail (GST_IS_GL_COLOR_BALANCE (vb));
  g_return_if_fail (channel->label != NULL);

  GST_OBJECT_LOCK (vb);
  if (!g_ascii_strcasecmp (channel->label, "HUE")) {
    new_val = (value + 1000.0) * 2.0 / 2000.0 - 1.0;
    changed = new_val != vb->hue;
    vb->hue = new_val;
  } else if (!g_ascii_strcasecmp (channel->label, "SATURATION")) {
    new_val = (value + 1000.0) * 2.0 / 2000.0;
    changed = new_val != vb->saturation;
    vb->saturation = new_val;
  } else if (!g_ascii_strcasecmp (channel->label, "BRIGHTNESS")) {
    new_val = (value + 1000.0) * 2.0 / 2000.0 - 1.0;
    changed = new_val != vb->brightness;
    vb->brightness = new_val;
  } else if (!g_ascii_strcasecmp (channel->label, "CONTRAST")) {
    new_val = (value + 1000.0) * 2.0 / 2000.0;
    changed = new_val != vb->contrast;
    vb->contrast = new_val;
  }
  GST_OBJECT_UNLOCK (vb);

  if (changed)
    gst_gl_color_balance_update_properties (vb);

  if (changed) {
    gst_color_balance_value_changed (balance, channel,
        gst_color_balance_get_value (balance, channel));
  }
}

static gint
gst_gl_color_balance_colorbalance_get_value (GstColorBalance * balance,
    GstColorBalanceChannel * channel)
{
  GstGLColorBalance *vb = GST_GL_COLOR_BALANCE (balance);
  gint value = 0;

  g_return_val_if_fail (vb != NULL, 0);
  g_return_val_if_fail (GST_IS_GL_COLOR_BALANCE (vb), 0);
  g_return_val_if_fail (channel->label != NULL, 0);

  if (!g_ascii_strcasecmp (channel->label, "HUE")) {
    value = (vb->hue + 1) * 2000.0 / 2.0 - 1000.0;
  } else if (!g_ascii_strcasecmp (channel->label, "SATURATION")) {
    value = vb->saturation * 2000.0 / 2.0 - 1000.0;
  } else if (!g_ascii_strcasecmp (channel->label, "BRIGHTNESS")) {
    value = (vb->brightness + 1) * 2000.0 / 2.0 - 1000.0;
  } else if (!g_ascii_strcasecmp (channel->label, "CONTRAST")) {
    value = vb->contrast * 2000.0 / 2.0 - 1000.0;
  }

  return value;
}

static GstColorBalanceType
gst_gl_color_balance_colorbalance_get_balance_type (GstColorBalance * balance)
{
  return GST_COLOR_BALANCE_HARDWARE;
}

static void
gst_gl_color_balance_colorbalance_init (GstColorBalanceInterface * iface)
{
  iface->list_channels = gst_gl_color_balance_colorbalance_list_channels;
  iface->set_value = gst_gl_color_balance_colorbalance_set_value;
  iface->get_value = gst_gl_color_balance_colorbalance_get_value;
  iface->get_balance_type = gst_gl_color_balance_colorbalance_get_balance_type;
}

static GstColorBalanceChannel *
gst_gl_color_balance_find_channel (GstGLColorBalance * balance,
    const gchar * label)
{
  GList *l;

  for (l = balance->channels; l; l = l->next) {
    GstColorBalanceChannel *channel = l->data;

    if (g_ascii_strcasecmp (channel->label, label) == 0)
      return channel;
  }
  return NULL;
}

static void
gst_gl_color_balance_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGLColorBalance *balance = GST_GL_COLOR_BALANCE (object);
  gdouble d;
  const gchar *label = NULL;

  GST_OBJECT_LOCK (balance);
  switch (prop_id) {
    case PROP_CONTRAST:
      d = g_value_get_double (value);
      GST_DEBUG_OBJECT (balance, "Changing contrast from %lf to %lf",
          balance->contrast, d);
      if (d != balance->contrast)
        label = "CONTRAST";
      balance->contrast = d;
      break;
    case PROP_BRIGHTNESS:
      d = g_value_get_double (value);
      GST_DEBUG_OBJECT (balance, "Changing brightness from %lf to %lf",
          balance->brightness, d);
      if (d != balance->brightness)
        label = "BRIGHTNESS";
      balance->brightness = d;
      break;
    case PROP_HUE:
      d = g_value_get_double (value);
      GST_DEBUG_OBJECT (balance, "Changing hue from %lf to %lf", balance->hue,
          d);
      if (d != balance->hue)
        label = "HUE";
      balance->hue = d;
      break;
    case PROP_SATURATION:
      d = g_value_get_double (value);
      GST_DEBUG_OBJECT (balance, "Changing saturation from %lf to %lf",
          balance->saturation, d);
      if (d != balance->saturation)
        label = "SATURATION";
      balance->saturation = d;
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_OBJECT_UNLOCK (balance);
  gst_gl_color_balance_update_properties (balance);

  if (label) {
    GstColorBalanceChannel *channel =
        gst_gl_color_balance_find_channel (balance, label);
    gst_color_balance_value_changed (GST_COLOR_BALANCE (balance), channel,
        gst_color_balance_get_value (GST_COLOR_BALANCE (balance), channel));
  }
}

static void
gst_gl_color_balance_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstGLColorBalance *balance = GST_GL_COLOR_BALANCE (object);

  switch (prop_id) {
    case PROP_CONTRAST:
      g_value_set_double (value, balance->contrast);
      break;
    case PROP_BRIGHTNESS:
      g_value_set_double (value, balance->brightness);
      break;
    case PROP_HUE:
      g_value_set_double (value, balance->hue);
      break;
    case PROP_SATURATION:
      g_value_set_double (value, balance->saturation);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
