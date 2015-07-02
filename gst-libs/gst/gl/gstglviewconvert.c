/*
 * GStreamer
 * Copyright (C) 2009 Julien Isorce <julien.isorce@mail.com>
 * Copyright (C) 2014 Jan Schmidt <jan@centricular.com>
 * Copyright (C) 2015 Matthew Waters <matthew@centricular.com>
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
 * SECTION:viewconvert
 *
 * Convert stereoscopic/multiview video using fragment shaders.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstglviewconvert.h"

#define USING_OPENGL(context) (gst_gl_context_check_gl_version (context, GST_GL_API_OPENGL, 1, 0))
#define USING_OPENGL3(context) (gst_gl_context_check_gl_version (context, GST_GL_API_OPENGL3, 3, 1))
#define USING_GLES(context) (gst_gl_context_check_gl_version (context, GST_GL_API_GLES, 1, 0))
#define USING_GLES2(context) (gst_gl_context_check_gl_version (context, GST_GL_API_GLES2, 2, 0))
#define USING_GLES3(context) (gst_gl_context_check_gl_version (context, GST_GL_API_GLES2, 3, 0))

static GstStaticCaps caps_template =
GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
    (GST_CAPS_FEATURE_MEMORY_GL_MEMORY, "RGBA"));

#define GST_CAT_DEFAULT gst_gl_view_convert_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

enum
{
  PROP_0,
  PROP_INPUT_LAYOUT,
  PROP_INPUT_FLAGS,
  PROP_OUTPUT_LAYOUT,
  PROP_OUTPUT_FLAGS,
  PROP_OUTPUT_DOWNMIX_MODE
};

#define DEFAULT_DOWNMIX GST_GL_STEREO_DOWNMIX_ANAGLYPH_GREEN_MAGENTA_DUBOIS

struct _GstGLViewConvertPrivate
{
  gboolean result;

  GstVideoMultiviewMode input_mode;
  GstVideoMultiviewFlags input_flags;
  GstVideoMultiviewMode output_mode;
  GstVideoMultiviewFlags output_flags;

  GstBuffer *primary_in;
  GstBuffer *auxilliary_in;

  GstBuffer *primary_out;
  GstBuffer *auxilliary_out;

  GstGLMemory *in_tex[GST_VIDEO_MAX_PLANES];
  GstGLMemory *out_tex[GST_VIDEO_MAX_PLANES];
  guint n_out_tex;

  GLuint vao;
  GLuint vertex_buffer;
  GLuint vbo_indices;
  GLuint attr_position;
  GLuint attr_texture;
};

#define GST_GL_VIEW_CONVERT_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
    GST_TYPE_GL_VIEW_CONVERT, GstGLViewConvertPrivate))

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_gl_view_convert_debug, "glviewconvert", 0, "glviewconvert object");

G_DEFINE_TYPE_WITH_CODE (GstGLViewConvert, gst_gl_view_convert,
    GST_TYPE_OBJECT, DEBUG_INIT);

static void gst_gl_view_convert_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_gl_view_convert_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_gl_view_convert_finalize (GObject * object);

static void _do_view_convert (GstGLContext * context,
    GstGLViewConvert * viewconvert);

GType
gst_gl_stereo_downmix_mode_get_type (void)
{
  static volatile gsize g_define_type_id__volatile = 0;
  if (g_once_init_enter (&g_define_type_id__volatile)) {
    static const GEnumValue values[] = {
      {GST_GL_STEREO_DOWNMIX_ANAGLYPH_GREEN_MAGENTA_DUBOIS,
          "Dubois optimised Green-Magenta anaglyph", "green-magenta-dubois"},
      {GST_GL_STEREO_DOWNMIX_ANAGLYPH_RED_CYAN_DUBOIS,
            "Dubois optimised Red-Cyan anaglyph",
          "red-cyan-dubois"},
      {GST_GL_STEREO_DOWNMIX_ANAGLYPH_AMBER_BLUE_DUBOIS,
          "Dubois optimised Amber-Blue anaglyph", "amber-blue-dubois"},
      {0, NULL, NULL}
    };
    GType g_define_type_id =
        g_enum_register_static ("GstGLStereoDownmix", values);
    g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
  }
  return g_define_type_id__volatile;
}

/* These match the order and number of DOWNMIX_ANAGLYPH_* modes */
static GLfloat downmix_matrices[][2][9] = {
  {                             /* Green-Magenta Dubois */
        {-0.062, 0.284, -0.015, -0.158, 0.668, -0.027, -0.039, 0.143, 0.021},
        {0.529, -0.016, 0.009, 0.705, -0.015, 0.075, 0.024, -0.065, 0.937}
      },
  {                             /* Red-Cyan Dubois */
        /* Source of this matrix: http://www.site.uottawa.ca/~edubois/anaglyph/LeastSquaresHowToPhotoshop.pdf */
        {0.437, -0.062, -0.048, 0.449, -0.062, -0.050, 0.164, -0.024, -0.017},
        {-0.011, 0.377, -0.026, -0.032, 0.761, -0.093, -0.007, 0.009, 1.234}
      },
  {                             /* Amber-blue Dubois */
        {1.062, -0.026, -0.038, -0.205, 0.908, -0.173, 0.299, 0.068, 0.022},
        {-0.016, 0.006, 0.094, -0.123, 0.062, 0.185, -0.017, -0.017, 0.911}
      }
};

/* *INDENT-OFF* */
static const gchar *fragment_source =
  "#ifdef GL_ES\n"
  "precision mediump float;\n"
  "#endif\n"
  "varying vec2 v_texcoord;\n"
  "uniform sampler2D tex_l;\n"
  "uniform sampler2D tex_r;\n"
  "uniform float width;\n"
  "uniform float height;\n"
  "uniform mat3 downmix[2];\n"
  "uniform vec2 tex_scale[2];\n"
  "uniform vec2 offsets[2];\n"
  "void main () {\n"
  "vec4 l, r;\n"
  /* input */
  "%s"
  /* now have left and right pixel into l and r */
  /* output */
  "%s"
  "}\n";

static const gchar *frag_input =
  "  vec2 l_tex = v_texcoord * tex_scale[0] + offsets[0];\n"
  "  vec2 r_tex = v_texcoord * tex_scale[1] + offsets[1];\n"
  "  l = texture2D(tex_l, l_tex).rgba;\n"
  "  r = texture2D(tex_r, r_tex).rgba;\n";

static const gchar *frag_output_downmix =
  "  vec3 lcol = l.rgb * l.a + vec3(1.0-l.a);\n"
  "  vec3 rcol = r.rgb * r.a + vec3(1.0-r.a);\n"
  "  if (l.a + r.a > 0.0) {\n"
  "    lcol = clamp (downmix[0] * lcol, 0.0, 1.0);\n"
  "    rcol = clamp (downmix[1] * rcol, 0.0, 1.0);\n"
  "    gl_FragColor = vec4 (lcol + rcol, 1.0);\n"
  "  } else {\n"
  "    gl_FragColor = vec4 (0.0);\n"
  "  }\n";

static const gchar *frag_output_left =
  "  gl_FragColor = l;\n";

static const gchar *frag_output_right =
  "  gl_FragColor = r;\n";

static const gchar *frag_output_side_by_side =
  "  if (v_texcoord.x < 0.5) {\n"
  "    gl_FragColor = l;\n"
  "  } else {\n"
  "    gl_FragColor = r;\n"
  "  };\n";

static const gchar *frag_output_top_bottom =
  "if (v_texcoord.y < 0.5) {\n"
  "  gl_FragColor = l;\n"
  "} else {\n"
  "  gl_FragColor = r;\n"
  "};\n";

static const gchar *frag_output_column_interleaved =
  "if (int(mod(l_tex.x * width, 2.0)) == 0) {\n"
  "  gl_FragColor = l;\n"
  "} else {\n"
  "  gl_FragColor = r;\n"
  "};\n";

static const gchar *frag_output_row_interleaved =
  "if (int(mod(l_tex.y * height, 2.0)) == 0) {\n"
  "  gl_FragColor = l;\n"
  "} else {\n"
  "  gl_FragColor = r;\n"
  "};\n";

static const gchar *frag_output_checkerboard =
  "if (int(mod(l_tex.x * width, 2.0)) == \n"
  "    int(mod(l_tex.y * height, 2.0))) {\n"
  "  gl_FragColor = l;\n"
  "} else {\n"
  "  gl_FragColor = r;\n"
  "};\n";

static const gchar *frag_output_separated =
  "gl_FragData[0] = l;\n"
  "gl_FragData[1] = r;\n";

static const gchar text_vertex_shader[] =
  "attribute vec4 a_position;   \n"
  "attribute vec2 a_texcoord;   \n"
  "varying vec2 v_texcoord;     \n"
  "void main()                  \n"
  "{                            \n"
  "   gl_Position = a_position; \n"
  "   v_texcoord = a_texcoord;  \n"
  "}                            \n";
/* *INDENT-ON* */

static const GLfloat vertices[] = {
  1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
  -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
  -1.0f, 1.0f, 0.0f, 0.0f, 1.0f,
  1.0f, 1.0f, 0.0f, 1.0f, 1.0f
};

static const GLushort indices[] = { 0, 1, 2, 0, 2, 3 };

static void
gst_gl_view_convert_class_init (GstGLViewConvertClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  g_type_class_add_private (klass, sizeof (GstGLViewConvertPrivate));

  gobject_class->set_property = gst_gl_view_convert_set_property;
  gobject_class->get_property = gst_gl_view_convert_get_property;
  gobject_class->finalize = gst_gl_view_convert_finalize;

  g_object_class_install_property (gobject_class, PROP_INPUT_LAYOUT,
      g_param_spec_enum ("input-mode-override",
          "Input Multiview Mode Override",
          "Override any input information about multiview layout",
          GST_TYPE_VIDEO_MULTIVIEW_MODE,
          GST_VIDEO_MULTIVIEW_MODE_NONE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_INPUT_FLAGS,
      g_param_spec_flags ("input-flags-override",
          "Input Multiview Flags Override",
          "Override any input information about multiview layout flags",
          GST_TYPE_VIDEO_MULTIVIEW_FLAGS, GST_VIDEO_MULTIVIEW_FLAGS_NONE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_OUTPUT_LAYOUT,
      g_param_spec_enum ("output-mode-override",
          "Output Multiview Mode Override",
          "Override automatic output mode selection for multiview layout",
          GST_TYPE_VIDEO_MULTIVIEW_MODE, GST_VIDEO_MULTIVIEW_MODE_NONE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_OUTPUT_FLAGS,
      g_param_spec_flags ("output-flags-override",
          "Output Multiview Flags Override",
          "Override automatic negotiation for output multiview layout flags",
          GST_TYPE_VIDEO_MULTIVIEW_FLAGS, GST_VIDEO_MULTIVIEW_FLAGS_NONE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_OUTPUT_DOWNMIX_MODE,
      g_param_spec_enum ("downmix-mode", "Mode for mono downmixed output",
          "Output anaglyph type to generate when downmixing to mono",
          GST_TYPE_GL_STEREO_DOWNMIX_MODE_TYPE, DEFAULT_DOWNMIX,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_gl_view_convert_init (GstGLViewConvert * convert)
{
  convert->priv = GST_GL_VIEW_CONVERT_GET_PRIVATE (convert);

  convert->shader = NULL;
  convert->downmix_mode = DEFAULT_DOWNMIX;
  convert->priv->input_mode = GST_VIDEO_MULTIVIEW_MODE_NONE;
  convert->priv->input_flags = GST_VIDEO_MULTIVIEW_FLAGS_NONE;
  convert->priv->output_mode = GST_VIDEO_MULTIVIEW_MODE_NONE;
  convert->priv->output_flags = GST_VIDEO_MULTIVIEW_FLAGS_NONE;

  convert->input_mode_override = GST_VIDEO_MULTIVIEW_MODE_NONE;
  convert->input_flags_override = GST_VIDEO_MULTIVIEW_FLAGS_NONE;
  convert->output_mode_override = GST_VIDEO_MULTIVIEW_MODE_NONE;
  convert->output_flags_override = GST_VIDEO_MULTIVIEW_FLAGS_NONE;

  gst_video_info_init (&convert->in_info);
  gst_video_info_init (&convert->out_info);
}

static void
gst_gl_view_convert_finalize (GObject * object)
{
  GstGLViewConvert *viewconvert;

  viewconvert = GST_GL_VIEW_CONVERT (object);

  gst_gl_view_convert_reset (viewconvert);

  gst_buffer_replace (&viewconvert->priv->primary_in, NULL);
  gst_buffer_replace (&viewconvert->priv->auxilliary_in, NULL);
  gst_buffer_replace (&viewconvert->priv->primary_out, NULL);
  gst_buffer_replace (&viewconvert->priv->auxilliary_out, NULL);

  if (viewconvert->context) {
    gst_object_unref (viewconvert->context);
    viewconvert->context = NULL;
  }

  G_OBJECT_CLASS (gst_gl_view_convert_parent_class)->finalize (object);
}

GstGLViewConvert *
gst_gl_view_convert_new (void)
{
  return g_object_new (GST_TYPE_GL_VIEW_CONVERT, NULL);
}

void
gst_gl_view_convert_set_context (GstGLViewConvert * viewconvert,
    GstGLContext * context)
{
  g_return_if_fail (GST_IS_GL_VIEW_CONVERT (viewconvert));

  if (gst_object_replace ((GstObject **) & viewconvert->context,
          GST_OBJECT (context)))
    gst_gl_view_convert_reset (viewconvert);
}

gboolean
gst_gl_view_convert_set_format (GstGLViewConvert * viewconvert,
    GstVideoInfo * in_info, GstVideoInfo * out_info)
{
  g_return_val_if_fail (GST_IS_GL_VIEW_CONVERT (viewconvert), FALSE);

  if (gst_video_info_is_equal (in_info, &viewconvert->in_info) &&
      gst_video_info_is_equal (out_info, &viewconvert->out_info))
    return TRUE;

  if (GST_VIDEO_INFO_FORMAT (in_info) != GST_VIDEO_FORMAT_RGBA ||
      GST_VIDEO_INFO_FORMAT (out_info) != GST_VIDEO_FORMAT_RGBA) {
    GST_ERROR_OBJECT (viewconvert,
        "Multiview conversion can currently only be performed on RGBA textures");
    return FALSE;
  }

  /* FIXME: Compare what changed and decide if we need a full reset or not */
  GST_OBJECT_LOCK (viewconvert);
  gst_gl_view_convert_reset (viewconvert);

  viewconvert->in_info = *in_info;
  viewconvert->out_info = *out_info;

  gst_buffer_replace (&viewconvert->priv->primary_in, NULL);
  gst_buffer_replace (&viewconvert->priv->auxilliary_in, NULL);
  gst_buffer_replace (&viewconvert->priv->primary_out, NULL);
  gst_buffer_replace (&viewconvert->priv->auxilliary_out, NULL);
  GST_OBJECT_UNLOCK (viewconvert);

  return TRUE;
}

/**
 * gst_gl_view_convert_set_caps:
 * @viewconvert: a #GstGLViewConvert
 * @in_caps: input #GstCaps
 * @out_caps: output #GstCaps
 *
 * Initializes @viewconvert with the information required for conversion.
 */
gboolean
gst_gl_view_convert_set_caps (GstGLViewConvert * viewconvert,
    GstCaps * in_caps, GstCaps * out_caps)
{
  GstVideoInfo in_info, out_info;
  GstCapsFeatures *in_features, *out_features;

  g_return_val_if_fail (GST_IS_GL_VIEW_CONVERT (viewconvert), FALSE);
  g_return_val_if_fail (GST_IS_CAPS (in_caps), FALSE);
  g_return_val_if_fail (GST_IS_CAPS (out_caps), FALSE);

  GST_INFO_OBJECT (viewconvert,
      "Configuring multiview conversion from caps %" GST_PTR_FORMAT
      " to %" GST_PTR_FORMAT, in_caps, out_caps);

  in_features = gst_caps_get_features (in_caps, 0);
  out_features = gst_caps_get_features (out_caps, 0);

  if (!gst_caps_features_contains (in_features,
          GST_CAPS_FEATURE_MEMORY_GL_MEMORY))
    return FALSE;
  if (!gst_caps_features_contains (out_features,
          GST_CAPS_FEATURE_MEMORY_GL_MEMORY))
    return FALSE;

  if (!gst_video_info_from_caps (&in_info, in_caps))
    return FALSE;
  if (!gst_video_info_from_caps (&out_info, out_caps))
    return FALSE;

  return gst_gl_view_convert_set_format (viewconvert, &in_info, &out_info);
}

/* Function that can halve the value
 * of ints, fractions, int/fraction ranges and lists of ints/fractions */
static gboolean
_halve_value (GValue * out, const GValue * in_value)
{
  /* Fundamental fixed types first */
  if (G_VALUE_HOLDS_INT (in_value)) {
    g_value_init (out, G_TYPE_INT);
    g_value_set_int (out, MAX (g_value_get_int (in_value) / 2, 1));
  } else if (GST_VALUE_HOLDS_FRACTION (in_value)) {
    gint num, den;
    num = gst_value_get_fraction_numerator (in_value);
    den = gst_value_get_fraction_denominator (in_value);
    g_value_init (out, GST_TYPE_FRACTION);
    /* Don't adjust 'infinite' fractions */
    if ((num != 1 || den != 2147483647) && (num != 2147483647 || den != 1)) {
      /* FIXME - could do better approximation when den > G_MAXINT/2? */
      den = den > G_MAXINT / 2 ? G_MAXINT : den * 2;
    }
    gst_value_set_fraction (out, num, den);
  } else if (GST_VALUE_HOLDS_INT_RANGE (in_value)) {
    gint range_min = gst_value_get_int_range_min (in_value);
    gint range_max = gst_value_get_int_range_max (in_value);
    gint range_step = gst_value_get_int_range_step (in_value);
    g_value_init (out, GST_TYPE_INT_RANGE);
    if (range_min != 1)
      range_min = MAX (1, range_min / 2);
    if (range_max != G_MAXINT)
      range_max = MAX (1, range_max / 2);
    gst_value_set_int_range_step (out, range_min,
        range_max, MAX (1, range_step / 2));
  } else if (GST_VALUE_HOLDS_FRACTION_RANGE (in_value)) {
    GValue min_out = G_VALUE_INIT;
    GValue max_out = G_VALUE_INIT;
    const GValue *range_min = gst_value_get_fraction_range_min (in_value);
    const GValue *range_max = gst_value_get_fraction_range_max (in_value);
    _halve_value (&min_out, range_min);
    _halve_value (&max_out, range_max);
    g_value_init (out, GST_TYPE_FRACTION_RANGE);
    gst_value_set_fraction_range (out, &min_out, &max_out);
    g_value_unset (&min_out);
    g_value_unset (&max_out);
  } else if (GST_VALUE_HOLDS_LIST (in_value)) {
    gint i;
    g_value_init (out, GST_TYPE_LIST);
    for (i = 0; i < gst_value_list_get_size (in_value); i++) {
      const GValue *entry;
      GValue tmp = G_VALUE_INIT;

      entry = gst_value_list_get_value (in_value, i);
      /* Random list values might not be the right type */
      if (!_halve_value (&tmp, entry))
        goto fail;
      gst_value_list_append_and_take_value (out, &tmp);
    }
  } else {
    return FALSE;
  }

  return TRUE;
fail:
  g_value_unset (out);
  return FALSE;
}

static GstStructure *
_halve_structure_field (const GstStructure * in, const gchar * field_name)
{
  GstStructure *out;
  const GValue *in_value = gst_structure_get_value (in, field_name);
  GValue tmp = G_VALUE_INIT;

  if (G_UNLIKELY (in_value == NULL))
    return gst_structure_copy (in);     /* Field doesn't exist, leave it as is */

  if (!_halve_value (&tmp, in_value))
    return NULL;

  out = gst_structure_copy (in);
  gst_structure_set_value (out, field_name, &tmp);
  g_value_unset (&tmp);

  return out;
}

/* Function that can double the value
 * of ints, fractions, int/fraction ranges and lists of ints/fractions */
static gboolean
_double_value (GValue * out, const GValue * in_value)
{
  /* Fundamental fixed types first */
  if (G_VALUE_HOLDS_INT (in_value)) {
    gint n = g_value_get_int (in_value);
    g_value_init (out, G_TYPE_INT);
    if (n <= G_MAXINT / 2)
      g_value_set_int (out, n * 2);
    else
      g_value_set_int (out, G_MAXINT);
  } else if (GST_VALUE_HOLDS_FRACTION (in_value)) {
    gint num, den;
    num = gst_value_get_fraction_numerator (in_value);
    den = gst_value_get_fraction_denominator (in_value);
    g_value_init (out, GST_TYPE_FRACTION);
    /* Don't adjust 'infinite' fractions */
    if ((num != 1 || den != 2147483647) && (num != 2147483647 || den != 1)) {
      /* FIXME - could do better approximation when num > G_MAXINT/2? */
      num = num > G_MAXINT / 2 ? G_MAXINT : num * 2;
    }
    gst_value_set_fraction (out, num, den);
  } else if (GST_VALUE_HOLDS_INT_RANGE (in_value)) {
    gint range_min = gst_value_get_int_range_min (in_value);
    gint range_max = gst_value_get_int_range_max (in_value);
    gint range_step = gst_value_get_int_range_step (in_value);
    if (range_min != 1) {
      range_min = MIN (G_MAXINT / 2, range_min);
      range_min *= 2;
    }
    if (range_max != G_MAXINT) {
      range_max = MIN (G_MAXINT / 2, range_max);
      range_max *= 2;
    }
    range_step = MIN (G_MAXINT / 2, range_step);
    g_value_init (out, GST_TYPE_INT_RANGE);
    gst_value_set_int_range_step (out, range_min, range_max, range_step);
  } else if (GST_VALUE_HOLDS_FRACTION_RANGE (in_value)) {
    GValue min_out = G_VALUE_INIT;
    GValue max_out = G_VALUE_INIT;
    const GValue *range_min = gst_value_get_fraction_range_min (in_value);
    const GValue *range_max = gst_value_get_fraction_range_max (in_value);
    _double_value (&min_out, range_min);
    _double_value (&max_out, range_max);
    g_value_init (out, GST_TYPE_FRACTION_RANGE);
    gst_value_set_fraction_range (out, &min_out, &max_out);
    g_value_unset (&min_out);
    g_value_unset (&max_out);
  } else if (GST_VALUE_HOLDS_LIST (in_value)) {
    gint i;
    g_value_init (out, GST_TYPE_LIST);
    for (i = 0; i < gst_value_list_get_size (in_value); i++) {
      const GValue *entry;
      GValue tmp = G_VALUE_INIT;

      entry = gst_value_list_get_value (in_value, i);
      /* Random list values might not be the right type */
      if (!_double_value (&tmp, entry))
        goto fail;
      gst_value_list_append_and_take_value (out, &tmp);
    }
  } else {
    return FALSE;
  }

  return TRUE;
fail:
  g_value_unset (out);
  return FALSE;
}

static GstStructure *
_double_structure_field (const GstStructure * in, const gchar * field_name)
{
  GstStructure *out;
  const GValue *in_value = gst_structure_get_value (in, field_name);
  GValue tmp = G_VALUE_INIT;

  if (G_UNLIKELY (in_value == NULL))
    return gst_structure_copy (in);     /* Field doesn't exist, leave it as is */

  if (!_double_value (&tmp, in_value))
    return NULL;

  out = gst_structure_copy (in);
  gst_structure_set_value (out, field_name, &tmp);
  g_value_unset (&tmp);

  return out;
}

/* Return a copy of the caps with the requested field halved in value/range */
#if 0
static GstCaps *
_halve_caps_field (const GstCaps * in, const gchar * field_name)
{
  gint i;
  GstCaps *out = gst_caps_new_empty ();

  for (i = 0; i < gst_caps_get_size (in); i++) {
    const GstStructure *cur = gst_caps_get_structure (in, i);
    GstCapsFeatures *f = gst_caps_get_features (in, i);

    GstStructure *res = _halve_structure_field (cur, field_name);
    out =
        gst_caps_merge_structure_full (out, res,
        f ? gst_caps_features_copy (f) : NULL);
  }

  return out;
}
#endif

/* Return a copy of the caps with the requested field doubled in value/range */
static GstCaps *
_double_caps_field (const GstCaps * in, const gchar * field_name)
{
  gint i;
  GstCaps *out = gst_caps_new_empty ();

  for (i = 0; i < gst_caps_get_size (in); i++) {
    const GstStructure *cur = gst_caps_get_structure (in, i);
    GstCapsFeatures *f = gst_caps_get_features (in, i);

    GstStructure *res = _double_structure_field (cur, field_name);
    out =
        gst_caps_merge_structure_full (out, res,
        f ? gst_caps_features_copy (f) : NULL);
  }

  return out;
}

/* Takes ownership of the input caps  */
static GstCaps *
_expand_par_for_half_aspect (GstCaps * in, gboolean vertical_half_aspect)
{

  guint mview_flags, mview_flags_mask;
  GstCaps *out;
  GstStructure *tmp;

  out = gst_caps_new_empty ();

  while (gst_caps_get_size (in) > 0) {
    GstStructure *s;
    GstCapsFeatures *features;

    features = gst_caps_get_features (in, 0);
    if (features)
      features = gst_caps_features_copy (features);

    s = gst_caps_steal_structure (in, 0);

    if (!gst_structure_get_flagset (s, "multiview-flags", &mview_flags,
            &mview_flags_mask)) {
      gst_caps_append_structure_full (out, s, features);
      continue;
    }
    /* If the input doesn't care about the half-aspect flag, allow current PAR in either variant */
    if ((mview_flags_mask & GST_VIDEO_MULTIVIEW_FLAGS_HALF_ASPECT) == 0) {
      gst_caps_append_structure_full (out, s, features);
      continue;
    }
    if (!gst_structure_has_field (s, "pixel-aspect-ratio")) {
      /* No par field, dont-care the half-aspect flag */
      gst_structure_set (s, "multiview-flags",
          GST_TYPE_VIDEO_MULTIVIEW_FLAGSET,
          mview_flags & ~GST_VIDEO_MULTIVIEW_FLAGS_HALF_ASPECT,
          mview_flags_mask & ~GST_VIDEO_MULTIVIEW_FLAGS_HALF_ASPECT, NULL);
      gst_caps_append_structure_full (out, s, features);
      continue;
    }

    /* Halve or double PAR base on inputs input specified. */

    /* Append a copy with the half-aspect flag as-is */
    tmp = gst_structure_copy (s);
    out = gst_caps_merge_structure_full (out, tmp,
        features ? gst_caps_features_copy (features) : NULL);

    /* and then a copy inverted */
    if (mview_flags & GST_VIDEO_MULTIVIEW_FLAGS_HALF_ASPECT) {
      /* Input is half-aspect. Double/halve the PAR, clear the flag */
      if (vertical_half_aspect)
        tmp = _halve_structure_field (s, "pixel-aspect-ratio");
      else
        tmp = _double_structure_field (s, "pixel-aspect-ratio");
      /* Clear the flag */
      gst_structure_set (tmp, "multiview-flags",
          GST_TYPE_VIDEO_MULTIVIEW_FLAGSET,
          mview_flags & ~GST_VIDEO_MULTIVIEW_FLAGS_HALF_ASPECT,
          mview_flags_mask | GST_VIDEO_MULTIVIEW_FLAGS_HALF_ASPECT, NULL);
    } else {
      if (vertical_half_aspect)
        tmp = _double_structure_field (s, "pixel-aspect-ratio");
      else
        tmp = _halve_structure_field (s, "pixel-aspect-ratio");
      /* Set the flag */
      gst_structure_set (tmp, "multiview-flags",
          GST_TYPE_VIDEO_MULTIVIEW_FLAGSET,
          mview_flags | GST_VIDEO_MULTIVIEW_FLAGS_HALF_ASPECT,
          mview_flags_mask | GST_VIDEO_MULTIVIEW_FLAGS_HALF_ASPECT, NULL);
    }

    out = gst_caps_merge_structure_full (out, tmp,
        features ? gst_caps_features_copy (features) : NULL);

    gst_structure_free (s);
    if (features)
      gst_caps_features_free (features);
  }

  gst_caps_unref (in);

  return out;
}

/* If input supports top-bottom or row-interleaved, we may halve height to mono frames.
 * If input supports left-right, checkerboard, quincunx or column-interleaved,
 * we may halve width to mono frames.
 * For output of top-bottom or row-interleaved, we may double the mono height
 * For output of left-right, checkerboard, quincunx or column-interleaved,
 * we may double the mono width.
 * In all cases, if input has half-aspect and output does not, we may double the PAR
 * And if input does *not* have half-aspect flag and output does not, we may halve the PAR
 */
static GstCaps *
_expand_structure (GstGLViewConvert * viewconvert,
    GstCaps * out_caps, GstStructure * structure, GstCapsFeatures * features)
{
  GstCaps *expanded_caps, *tmp;
  GstCaps *mono_caps;
  const gchar *default_mview_mode_str = NULL;
  guint mview_flags, mview_flags_mask;
  const GValue *in_modes;
  gint i;

  /* Empty caps to accumulate into */
  expanded_caps = gst_caps_new_empty ();

  /* First, set defaults if multiview flags are missing */
  default_mview_mode_str =
      gst_video_multiview_mode_to_caps_string (GST_VIDEO_MULTIVIEW_MODE_MONO);

  mview_flags = GST_VIDEO_MULTIVIEW_FLAGS_NONE;
  mview_flags_mask = GST_FLAG_SET_MASK_EXACT;

  if (!gst_structure_has_field (structure, "multiview-mode")) {
    gst_structure_set (structure,
        "multiview-mode", G_TYPE_STRING, default_mview_mode_str, NULL);
  }
  if (!gst_structure_has_field (structure, "multiview-flags")) {
    gst_structure_set (structure,
        "multiview-flags", GST_TYPE_VIDEO_MULTIVIEW_FLAGSET, mview_flags,
        mview_flags_mask, NULL);
  } else {
    gst_structure_get_flagset (structure, "multiview-flags",
        &mview_flags, &mview_flags_mask);
  }

  in_modes = gst_structure_get_value (structure, "multiview-mode");
  mono_caps = gst_caps_new_empty ();
  if (gst_value_intersect (NULL, in_modes,
          gst_video_multiview_get_mono_modes ())) {
    GstStructure *new_struct = gst_structure_copy (structure);
    gst_structure_set_value (new_struct, "multiview-mode",
        gst_video_multiview_get_mono_modes ());
    /* Half-aspect makes no sense for mono or unpacked, get rid of it */
    if (mview_flags & GST_VIDEO_MULTIVIEW_FLAGS_HALF_ASPECT) {
      gst_structure_set (new_struct, "multiview-flags",
          GST_TYPE_VIDEO_MULTIVIEW_FLAGSET,
          mview_flags & ~GST_VIDEO_MULTIVIEW_FLAGS_HALF_ASPECT,
          mview_flags_mask & ~GST_VIDEO_MULTIVIEW_FLAGS_HALF_ASPECT, NULL);
    }
    gst_caps_append_structure_full (mono_caps, new_struct,
        features ? gst_caps_features_copy (features) : NULL);
  }
  if (gst_value_intersect (NULL, in_modes,
          gst_video_multiview_get_unpacked_modes ())) {
    GstStructure *new_struct = gst_structure_copy (structure);

    gst_structure_set_value (new_struct, "multiview-mode",
        gst_video_multiview_get_mono_modes ());

    /* Half-aspect makes no sense for mono or unpacked, get rid of it */
    if (mview_flags & GST_VIDEO_MULTIVIEW_FLAGS_HALF_ASPECT) {
      gst_structure_set (new_struct, "multiview-flags",
          GST_TYPE_VIDEO_MULTIVIEW_FLAGSET,
          mview_flags & ~GST_VIDEO_MULTIVIEW_FLAGS_HALF_ASPECT,
          mview_flags_mask & ~GST_VIDEO_MULTIVIEW_FLAGS_HALF_ASPECT, NULL);
    }
    gst_caps_append_structure_full (mono_caps, new_struct,
        features ? gst_caps_features_copy (features) : NULL);
  }

  if (gst_value_intersect (NULL, in_modes,
          gst_video_multiview_get_doubled_height_modes ())) {
    /* Append mono formats with height halved */
    GstStructure *new_struct = _halve_structure_field (structure, "height");
    gst_structure_set_value (new_struct, "multiview-mode",
        gst_video_multiview_get_mono_modes ());
    /* Normalise the half-aspect flag away */
    if (mview_flags & GST_VIDEO_MULTIVIEW_FLAGS_HALF_ASPECT) {
      GstStructure *s =
          _halve_structure_field (new_struct, "pixel-aspect-ratio");
      gst_structure_set (structure, "multiview-flags",
          GST_TYPE_VIDEO_MULTIVIEW_FLAGSET,
          mview_flags & ~GST_VIDEO_MULTIVIEW_FLAGS_HALF_ASPECT,
          mview_flags_mask | GST_VIDEO_MULTIVIEW_FLAGS_HALF_ASPECT, NULL);
      gst_structure_free (new_struct);
      new_struct = s;
    }
    mono_caps = gst_caps_merge_structure_full (mono_caps, new_struct,
        features ? gst_caps_features_copy (features) : NULL);
  }
  if (gst_value_intersect (NULL, in_modes,
          gst_video_multiview_get_doubled_width_modes ())) {
    /* Append mono formats with width halved */
    GstStructure *new_struct = _halve_structure_field (structure, "width");
    gst_structure_set_value (new_struct, "multiview-mode",
        gst_video_multiview_get_mono_modes ());
    /* Normalise the half-aspect flag away */
    if (mview_flags & GST_VIDEO_MULTIVIEW_FLAGS_HALF_ASPECT) {
      GstStructure *s =
          _double_structure_field (new_struct, "pixel-aspect-ratio");
      gst_structure_set (structure, "multiview-flags",
          GST_TYPE_VIDEO_MULTIVIEW_FLAGSET,
          mview_flags & ~GST_VIDEO_MULTIVIEW_FLAGS_HALF_ASPECT,
          mview_flags_mask | GST_VIDEO_MULTIVIEW_FLAGS_HALF_ASPECT, NULL);
      gst_structure_free (new_struct);
      new_struct = s;
    }
    mono_caps = gst_caps_merge_structure_full (mono_caps, new_struct,
        features ? gst_caps_features_copy (features) : NULL);
  }
  if (gst_value_intersect (NULL, in_modes,
          gst_video_multiview_get_doubled_size_modes ())) {
    /* Append checkerboard/doubled size formats with width & height halved */
    GstStructure *new_struct_w = _halve_structure_field (structure, "width");
    GstStructure *new_struct_wh =
        _halve_structure_field (new_struct_w, "height");
    gst_structure_free (new_struct_w);
    gst_structure_set_value (new_struct_wh, "multiview-mode",
        gst_video_multiview_get_mono_modes ());
    mono_caps = gst_caps_merge_structure_full (mono_caps, new_struct_wh,
        features ? gst_caps_features_copy (features) : NULL);
  }

  /* Everything is normalised now, unset the flags we can change */
  /* Remove the views field, as these are all 'mono' modes
   * Need to do this before we expand caps back out to frame packed modes */
  for (i = 0; i < gst_caps_get_size (mono_caps); i++) {
    GstStructure *s = gst_caps_get_structure (mono_caps, i);
    gst_structure_remove_fields (s, "views", NULL);
    if (gst_structure_get_flagset (s, "multiview-flags", &mview_flags,
            &mview_flags_mask)) {
      /* Preserve only the half-aspect and mixed-mono flags, for now.
       * The rest we can change */
      mview_flags_mask &=
          (GST_VIDEO_MULTIVIEW_FLAGS_HALF_ASPECT |
          GST_VIDEO_MULTIVIEW_FLAGS_MIXED_MONO);
      gst_structure_set (s, "multiview-flags", GST_TYPE_VIDEO_MULTIVIEW_FLAGSET,
          mview_flags, mview_flags_mask, NULL);
    }
  }

  GST_TRACE_OBJECT (viewconvert,
      "Collected single-view caps %" GST_PTR_FORMAT, mono_caps);
  /* Put unpacked and mono modes first. We don't care about flags. Clear them */
  tmp = gst_caps_copy (mono_caps);
  for (i = 0; i < gst_caps_get_size (tmp); i++) {
    GstStructure *s = gst_caps_get_structure (tmp, i);
    gst_structure_remove_fields (s, "views", NULL);
    if (gst_structure_get_flagset (s, "multiview-flags", &mview_flags,
            &mview_flags_mask)) {
      /* We can change any flags for mono modes - half-aspect and mixed-mono have no meaning */
      mview_flags_mask = 0;
      gst_structure_set (s, "multiview-flags", GST_TYPE_VIDEO_MULTIVIEW_FLAGSET,
          mview_flags, mview_flags_mask, NULL);
    }
  }
  expanded_caps = gst_caps_merge (expanded_caps, tmp);

  /* Unpacked output modes have 2 views, for now */
  tmp = gst_caps_copy (mono_caps);
  gst_caps_set_value (tmp, "multiview-mode",
      gst_video_multiview_get_unpacked_modes ());
  for (i = 0; i < gst_caps_get_size (tmp); i++) {
    GstStructure *s = gst_caps_get_structure (tmp, i);
    gst_structure_set (s, "views", G_TYPE_INT, 2, NULL);
    if (gst_structure_get_flagset (s, "multiview-flags", &mview_flags,
            &mview_flags_mask)) {
      /* We can change any flags for unpacked modes - half-aspect and mixed-mono have no meaning */
      mview_flags_mask = 0;
      gst_structure_set (s, "multiview-flags", GST_TYPE_VIDEO_MULTIVIEW_FLAGSET,
          mview_flags, mview_flags_mask, NULL);
    }
  }
  expanded_caps = gst_caps_merge (expanded_caps, tmp);

  /* Double height output modes */
  tmp = _double_caps_field (mono_caps, "height");
  gst_caps_set_value (tmp, "multiview-mode",
      gst_video_multiview_get_doubled_height_modes ());
  tmp = _expand_par_for_half_aspect (tmp, TRUE);

  expanded_caps = gst_caps_merge (expanded_caps, tmp);

  /* Double width output modes */
  tmp = _double_caps_field (mono_caps, "width");
  gst_caps_set_value (tmp, "multiview-mode",
      gst_video_multiview_get_doubled_width_modes ());
  tmp = _expand_par_for_half_aspect (tmp, FALSE);

  expanded_caps = gst_caps_merge (expanded_caps, tmp);

  /* Double size output modes */
  {
    GstCaps *tmp_w = _double_caps_field (mono_caps, "width");
    tmp = _double_caps_field (tmp_w, "height");
    gst_caps_unref (tmp_w);
    gst_caps_set_value (tmp, "multiview-mode",
        gst_video_multiview_get_doubled_size_modes ());
    expanded_caps = gst_caps_merge (expanded_caps, tmp);
  }

  /* We're done with the mono caps now */
  gst_caps_unref (mono_caps);

  GST_TRACE_OBJECT (viewconvert,
      "expanded transform caps now %" GST_PTR_FORMAT, expanded_caps);

  if (gst_caps_is_empty (expanded_caps)) {
    gst_caps_unref (expanded_caps);
    return out_caps;
  }
  /* Really, we can rescale - so at this point we can append full-range
   * height/width/PAR as an unpreferred final option. */
  tmp = gst_caps_copy (expanded_caps);
  gst_caps_set_simple (tmp, "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
      "height", GST_TYPE_INT_RANGE, 1, G_MAXINT, NULL);

  out_caps = gst_caps_merge (out_caps, expanded_caps);
  out_caps = gst_caps_merge (out_caps, tmp);
  return out_caps;
}

static GstCaps *
_intersect_with_mview_mode (GstCaps * caps,
    GstVideoMultiviewMode mode, GstVideoMultiviewFlags flags)
{
  GstCaps *filter, *result;
  const gchar *caps_str;

  caps_str = gst_video_multiview_mode_to_caps_string (mode);

  filter = gst_caps_new_simple ("video/x-raw",
      "multiview-mode", G_TYPE_STRING,
      caps_str, "multiview-flags", GST_TYPE_VIDEO_MULTIVIEW_FLAGSET, flags,
      GST_FLAG_SET_MASK_EXACT, NULL);

  if (mode == GST_VIDEO_MULTIVIEW_MODE_SEPARATED ||
      mode == GST_VIDEO_MULTIVIEW_MODE_FRAME_BY_FRAME)
    gst_caps_set_simple (filter, "views", G_TYPE_INT, 2, NULL);

  gst_caps_set_features (filter, 0, gst_caps_features_new_any ());

  GST_DEBUG ("Intersecting target caps %" GST_PTR_FORMAT
      " with caps %" GST_PTR_FORMAT, caps, filter);

  result = gst_caps_intersect_full (caps, filter, GST_CAPS_INTERSECT_FIRST);
  gst_caps_unref (filter);
  return result;
}

static GstCaps *
_intersect_with_mview_modes (GstCaps * caps, const GValue * modes)
{
  GstCaps *filter, *result;

  filter = gst_caps_new_empty_simple ("video/x-raw");

  gst_caps_set_value (filter, "multiview-mode", modes);
  gst_caps_set_features (filter, 0, gst_caps_features_new_any ());

  GST_DEBUG ("Intersecting target caps %" GST_PTR_FORMAT
      " with caps %" GST_PTR_FORMAT, caps, filter);

  result = gst_caps_intersect_full (caps, filter, GST_CAPS_INTERSECT_FIRST);
  gst_caps_unref (filter);
  return result;
}


GstCaps *
gst_gl_view_convert_transform_caps (GstGLViewConvert * viewconvert,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  gint i;
  GstCaps *base_caps = gst_static_caps_get (&caps_template);
  GstCaps *out_caps, *tmp_caps;

  g_return_val_if_fail (GST_IS_GL_VIEW_CONVERT (viewconvert), NULL);

  GST_DEBUG_OBJECT (viewconvert, "Direction %s "
      "input caps %" GST_PTR_FORMAT " filter %" GST_PTR_FORMAT,
      direction == GST_PAD_SINK ? "sink" : "src", caps, filter);

  /* We can only process GLmemory RGBA caps, start from that */
  caps = gst_caps_intersect (caps, base_caps);
  gst_caps_unref (base_caps);

  /* Change input/output to the formats we can convert to/from,
   * but keep the original caps at the start - we will always prefer
   * passthrough */
  if (direction == GST_PAD_SINK) {
    out_caps = gst_caps_copy (caps);
    if (viewconvert->input_mode_override != GST_VIDEO_MULTIVIEW_MODE_NONE) {
      GstVideoMultiviewMode mode = viewconvert->input_mode_override;
      GstVideoMultiviewFlags flags = viewconvert->input_flags_override;

      const gchar *caps_str = gst_video_multiview_mode_to_caps_string (mode);
      /* Coerce the input caps before transforming, so the sizes come out right */
      gst_caps_set_simple (out_caps, "multiview-mode", G_TYPE_STRING,
          caps_str, "multiview-flags", GST_TYPE_VIDEO_MULTIVIEW_FLAGSET, flags,
          GST_FLAG_SET_MASK_EXACT, NULL);
    }
  } else {
    out_caps = gst_caps_new_empty ();
  }

  for (i = 0; i < gst_caps_get_size (caps); i++) {
    GstStructure *structure = gst_caps_get_structure (caps, i);
    GstCapsFeatures *features = gst_caps_get_features (caps, i);
    out_caps = _expand_structure (viewconvert, out_caps, structure, features);
  }

  if (gst_caps_is_empty (out_caps))
    goto out;

  /* If we have an output mode override, limit things to that */
  if (direction == GST_PAD_SINK &&
      viewconvert->output_mode_override != GST_VIDEO_MULTIVIEW_MODE_NONE) {

    tmp_caps = _intersect_with_mview_mode (out_caps,
        viewconvert->output_mode_override, viewconvert->output_flags_override);

    gst_caps_unref (out_caps);
    out_caps = tmp_caps;
  } else if (viewconvert->input_mode_override != GST_VIDEO_MULTIVIEW_MODE_NONE) {
    /* Prepend a copy of our preferred input caps in case the peer
     * can handle them */
    tmp_caps = _intersect_with_mview_mode (out_caps,
        viewconvert->input_mode_override, viewconvert->input_flags_override);
    out_caps = gst_caps_merge (out_caps, tmp_caps);
  }
  if (direction == GST_PAD_SRC) {
    GstStructure *s;
    /* When generating input caps, we also need a copy of the mono caps
     * without multiview-mode or flags for backwards compat, at the end */
    tmp_caps = _intersect_with_mview_mode (caps,
        GST_VIDEO_MULTIVIEW_MODE_MONO, GST_VIDEO_MULTIVIEW_FLAGS_NONE);
    if (!gst_caps_is_empty (tmp_caps)) {
      s = gst_caps_get_structure (tmp_caps, 0);
      gst_structure_remove_fields (s, "multiview-mode", "multiview-flags",
          NULL);
      out_caps = gst_caps_merge (out_caps, tmp_caps);
    } else
      gst_caps_unref (tmp_caps);
  }
out:
  gst_caps_unref (caps);

  GST_DEBUG_OBJECT (viewconvert, "Returning caps %" GST_PTR_FORMAT, out_caps);
  return out_caps;
}

GstCaps *
gst_gl_view_convert_fixate_caps (GstGLViewConvert * viewconvert,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
{
  GstVideoMultiviewMode mode = viewconvert->output_mode_override;
  GstVideoMultiviewFlags flags = viewconvert->output_flags_override;
  GstCaps *tmp;

  g_return_val_if_fail (GST_IS_GL_VIEW_CONVERT (viewconvert), NULL);

  othercaps = gst_caps_make_writable (othercaps);
  GST_LOG_OBJECT (viewconvert, "dir %s fixating %" GST_PTR_FORMAT
      " against caps %" GST_PTR_FORMAT,
      direction == GST_PAD_SINK ? "sink" : "src", othercaps, caps);

  if (direction == GST_PAD_SINK) {
    if (mode != GST_VIDEO_MULTIVIEW_MODE_NONE) {
      /* We have a requested output mode and are fixating source caps, try and enforce it */
      tmp = _intersect_with_mview_mode (othercaps, mode, flags);
      gst_caps_unref (othercaps);
      othercaps = tmp;
    } else {
      /* See if we can do passthrough */
      GstVideoInfo info;

      if (gst_video_info_from_caps (&info, caps)) {
        GstVideoMultiviewMode mode = GST_VIDEO_INFO_MULTIVIEW_MODE (&info);
        GstVideoMultiviewFlags flags = GST_VIDEO_INFO_MULTIVIEW_FLAGS (&info);

        if (viewconvert->input_mode_override != GST_VIDEO_MULTIVIEW_MODE_NONE) {
          mode = viewconvert->input_mode_override;
          flags = viewconvert->input_flags_override;
        }

        tmp = _intersect_with_mview_mode (othercaps, mode, flags);
        if (gst_caps_is_empty (tmp)) {
          /* Nope, we can't pass our input caps downstream */
          gst_caps_unref (tmp);
        } else {
          gst_caps_unref (othercaps);
          othercaps = tmp;
          goto done;
        }
      }

      /* Prefer an unpacked mode for output */
      tmp =
          _intersect_with_mview_modes (othercaps,
          gst_video_multiview_get_unpacked_modes ());
      if (!gst_caps_is_empty (tmp)) {
        gst_caps_unref (othercaps);
        othercaps = tmp;
      } else {
        gst_caps_unref (tmp);
      }
    }
  } else if (viewconvert->input_mode_override != GST_VIDEO_MULTIVIEW_MODE_NONE) {
    /* See if we can coerce the caps into matching input mode/flags,
     * in case it doesn't care at all, but allow it not to too */
    mode = viewconvert->input_mode_override;
    flags = viewconvert->input_flags_override;
    tmp = _intersect_with_mview_mode (othercaps, mode, flags);
    if (gst_caps_is_empty (tmp)) {
      /* Nope, we can pass our input caps downstream */
      gst_caps_unref (tmp);
    } else {
      gst_caps_unref (othercaps);
      othercaps = tmp;
    }
  }

done:
  GST_DEBUG_OBJECT (viewconvert, "dir %s fixated to %" GST_PTR_FORMAT
      " against caps %" GST_PTR_FORMAT,
      direction == GST_PAD_SINK ? "sink" : "src", othercaps, caps);
  return othercaps;
}

void
gst_gl_view_convert_reset (GstGLViewConvert * viewconvert)
{
  g_return_if_fail (GST_IS_GL_VIEW_CONVERT (viewconvert));
  if (viewconvert->shader)
    gst_gl_context_del_shader (viewconvert->context, viewconvert->shader);
  viewconvert->shader = NULL;
  viewconvert->initted = FALSE;
  viewconvert->reconfigure = FALSE;
}

static void
gst_gl_view_convert_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGLViewConvert *convert = GST_GL_VIEW_CONVERT (object);
  switch (prop_id) {
    case PROP_INPUT_LAYOUT:
      convert->input_mode_override = g_value_get_enum (value);
      break;
    case PROP_INPUT_FLAGS:
      convert->input_flags_override = g_value_get_flags (value);
      break;
    case PROP_OUTPUT_LAYOUT:
      convert->output_mode_override = g_value_get_enum (value);
      break;
    case PROP_OUTPUT_FLAGS:
      convert->output_flags_override = g_value_get_flags (value);
      break;
    case PROP_OUTPUT_DOWNMIX_MODE:
      convert->downmix_mode = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_LOCK (convert);
  convert->reconfigure = TRUE;
  GST_OBJECT_UNLOCK (convert);
}

static void
gst_gl_view_convert_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstGLViewConvert *convert = GST_GL_VIEW_CONVERT (object);
  switch (prop_id) {
    case PROP_INPUT_LAYOUT:
      g_value_set_enum (value, convert->input_mode_override);
      break;
    case PROP_INPUT_FLAGS:
      g_value_set_flags (value, convert->input_flags_override);
      break;
    case PROP_OUTPUT_LAYOUT:
      g_value_set_enum (value, convert->output_mode_override);
      break;
    case PROP_OUTPUT_FLAGS:
      g_value_set_flags (value, convert->output_flags_override);
      break;
    case PROP_OUTPUT_DOWNMIX_MODE:
      g_value_set_enum (value, convert->downmix_mode);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

GstBuffer *
gst_gl_view_convert_perform (GstGLViewConvert * viewconvert, GstBuffer * inbuf)
{
  GstBuffer *out;

  if (gst_gl_view_convert_submit_input_buffer (viewconvert,
          GST_BUFFER_IS_DISCONT (inbuf), gst_buffer_ref (inbuf)) != GST_FLOW_OK)
    return NULL;
  if (gst_gl_view_convert_get_output (viewconvert, &out) != GST_FLOW_OK)
    return NULL;

  return out;
}

/* called by _init_convert (in the gl thread) */
static gboolean
_init_view_convert_fbo (GstGLViewConvert * viewconvert)
{
  GstGLFuncs *gl;
  guint out_width, out_height;
  GLuint fake_texture = 0;      /* a FBO must hava texture to init */
  GLenum internal_format;

  gl = viewconvert->context->gl_vtable;
  out_width = GST_VIDEO_INFO_WIDTH (&viewconvert->out_info);
  out_height = GST_VIDEO_INFO_HEIGHT (&viewconvert->out_info);
  if (!gl->GenFramebuffers) {
    /* turn off the pipeline because Frame buffer object is a not present */
    gst_gl_context_set_error (viewconvert->context,
        "Frambuffer objects unsupported");
    return FALSE;
  }

  /* setup FBO */
  gl->GenFramebuffers (1, &viewconvert->fbo);
  gl->BindFramebuffer (GL_FRAMEBUFFER, viewconvert->fbo);
  /* setup the render buffer for depth */
  gl->GenRenderbuffers (1, &viewconvert->depth_buffer);
  gl->BindRenderbuffer (GL_RENDERBUFFER, viewconvert->depth_buffer);
  if (USING_OPENGL (viewconvert->context)
      || USING_OPENGL3 (viewconvert->context)) {
    gl->RenderbufferStorage (GL_RENDERBUFFER, GL_DEPTH_COMPONENT, out_width,
        out_height);
    gl->RenderbufferStorage (GL_RENDERBUFFER, GL_DEPTH24_STENCIL8,
        out_width, out_height);
  }
  if (USING_GLES2 (viewconvert->context)) {
    gl->RenderbufferStorage (GL_RENDERBUFFER, GL_DEPTH_COMPONENT16,
        out_width, out_height);
  }

  /* a fake texture is attached to the convert FBO (cannot init without it) */
  gl->GenTextures (1, &fake_texture);
  gl->BindTexture (GL_TEXTURE_2D, fake_texture);
  internal_format =
      gst_gl_sized_gl_format_from_gl_format_type (viewconvert->context, GL_RGBA,
      GL_UNSIGNED_BYTE);
  gl->TexImage2D (GL_TEXTURE_2D, 0, internal_format, out_width, out_height,
      0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  gl->TexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  gl->TexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  gl->TexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  gl->TexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  /* attach the texture to the FBO to renderer to */
  gl->FramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
      GL_TEXTURE_2D, fake_texture, 0);
  /* attach the depth render buffer to the FBO */
  gl->FramebufferRenderbuffer (GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
      GL_RENDERBUFFER, viewconvert->depth_buffer);
  if (USING_OPENGL (viewconvert->context)) {
    gl->FramebufferRenderbuffer (GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT,
        GL_RENDERBUFFER, viewconvert->depth_buffer);
  }

  if (!gst_gl_context_check_framebuffer_status (viewconvert->context)) {
    gst_gl_context_set_error (viewconvert->context,
        "GL framebuffer status incomplete");
    gl->DeleteTextures (1, &fake_texture);
    return FALSE;
  }

  /* unbind the FBO */
  gl->BindFramebuffer (GL_FRAMEBUFFER, 0);
  gl->DeleteTextures (1, &fake_texture);
  return TRUE;
}

/* free after use */
static gchar *
_get_shader_string (GstGLViewConvert * viewconvert,
    GstVideoMultiviewMode in_mode, GstVideoMultiviewMode out_mode)
{
  const gchar *input_str, *output_str;
  gboolean mono_input = FALSE;
  switch (in_mode) {
    case GST_VIDEO_MULTIVIEW_MODE_NONE:
    case GST_VIDEO_MULTIVIEW_MODE_MONO:
    case GST_VIDEO_MULTIVIEW_MODE_LEFT:
    case GST_VIDEO_MULTIVIEW_MODE_RIGHT:
      mono_input = TRUE;
      /* Fall through */
    default:
      input_str = frag_input;
      break;
  }

  switch (out_mode) {
    case GST_VIDEO_MULTIVIEW_MODE_LEFT:
      output_str = frag_output_left;
      break;
    case GST_VIDEO_MULTIVIEW_MODE_RIGHT:
      output_str = frag_output_right;
      break;
    case GST_VIDEO_MULTIVIEW_MODE_SIDE_BY_SIDE_QUINCUNX:
      /* FIXME: implement properly with sub-sampling */
    case GST_VIDEO_MULTIVIEW_MODE_SIDE_BY_SIDE:
      output_str = frag_output_side_by_side;
      break;
    case GST_VIDEO_MULTIVIEW_MODE_TOP_BOTTOM:
      output_str = frag_output_top_bottom;
      break;
    case GST_VIDEO_MULTIVIEW_MODE_COLUMN_INTERLEAVED:
      output_str = frag_output_column_interleaved;
      break;
    case GST_VIDEO_MULTIVIEW_MODE_ROW_INTERLEAVED:
      output_str = frag_output_row_interleaved;
      break;
    case GST_VIDEO_MULTIVIEW_MODE_SEPARATED:
    case GST_VIDEO_MULTIVIEW_MODE_FRAME_BY_FRAME:
      output_str = frag_output_separated;
      break;
    case GST_VIDEO_MULTIVIEW_MODE_CHECKERBOARD:
      output_str = frag_output_checkerboard;
      break;
    case GST_VIDEO_MULTIVIEW_MODE_NONE:
    case GST_VIDEO_MULTIVIEW_MODE_MONO:
    default:
      if (mono_input)
        output_str = frag_output_left;
      else
        output_str = frag_output_downmix;
      break;
  }

  return g_strdup_printf (fragment_source, input_str, output_str);
}

static void
_bind_buffer (GstGLViewConvert * viewconvert)
{
  const GstGLFuncs *gl = viewconvert->context->gl_vtable;
  gl->BindBuffer (GL_ELEMENT_ARRAY_BUFFER, viewconvert->priv->vbo_indices);
  gl->BindBuffer (GL_ARRAY_BUFFER, viewconvert->priv->vertex_buffer);
  /* Load the vertex position */
  gl->VertexAttribPointer (viewconvert->priv->attr_position, 3, GL_FLOAT,
      GL_FALSE, 5 * sizeof (GLfloat), (void *) 0);
  /* Load the texture coordinate */
  gl->VertexAttribPointer (viewconvert->priv->attr_texture, 2, GL_FLOAT,
      GL_FALSE, 5 * sizeof (GLfloat), (void *) (3 * sizeof (GLfloat)));
  gl->EnableVertexAttribArray (viewconvert->priv->attr_position);
  gl->EnableVertexAttribArray (viewconvert->priv->attr_texture);
}

static void
_unbind_buffer (GstGLViewConvert * viewconvert)
{
  const GstGLFuncs *gl = viewconvert->context->gl_vtable;
  gl->BindBuffer (GL_ELEMENT_ARRAY_BUFFER, 0);
  gl->BindBuffer (GL_ARRAY_BUFFER, 0);
  gl->DisableVertexAttribArray (viewconvert->priv->attr_position);
  gl->DisableVertexAttribArray (viewconvert->priv->attr_texture);
}

/* Called in the gl thread */
static gboolean
_init_view_convert (GstGLViewConvert * viewconvert)
{
  GstGLViewConvertPrivate *priv = viewconvert->priv;
  GstVideoMultiviewMode in_mode = priv->input_mode;
  GstVideoMultiviewMode out_mode = priv->output_mode;
  GstVideoMultiviewFlags in_flags = priv->input_flags;
  GstVideoMultiviewFlags out_flags = priv->output_flags;
  gfloat tex_scale[2][2] = {
    {1., 1.},
    {1., 1.}
  };
  gfloat offsets[2][2] = {
    {0., 0.},
    {0., 0.}
  };
  gchar *fragment_source_str;
  GstGLFuncs *gl;
  gboolean res;
  gint l_index, r_index;

  gl = viewconvert->context->gl_vtable;
  if (viewconvert->reconfigure)
    gst_gl_view_convert_reset (viewconvert);
  if (viewconvert->initted)
    return TRUE;

  GST_LOG_OBJECT (viewconvert,
      "Initializing multiview conversion from %s mode %d flags 0x%x w %u h %u to "
      "%s mode %d flags 0x%x w %u h %u",
      gst_video_format_to_string (GST_VIDEO_INFO_FORMAT
          (&viewconvert->in_info)), in_mode, in_flags,
      viewconvert->in_info.width, viewconvert->in_info.height,
      gst_video_format_to_string (GST_VIDEO_INFO_FORMAT
          (&viewconvert->out_info)), out_mode, out_flags,
      viewconvert->out_info.width, viewconvert->out_info.height);

  if (!gl->CreateProgramObject && !gl->CreateProgram) {
    gst_gl_context_set_error (viewconvert->context,
        "Cannot perform multiview conversion without OpenGL shaders");
    goto error;
  }
  if ((in_flags & GST_VIDEO_MULTIVIEW_FLAGS_RIGHT_VIEW_FIRST) ==
      (out_flags & GST_VIDEO_MULTIVIEW_FLAGS_RIGHT_VIEW_FIRST)) {
    l_index = 0;
    r_index = 1;
  } else {
    GST_LOG_OBJECT (viewconvert, "Switching left/right views");
    /* Swap the views */
    l_index = 1;
    r_index = 0;
  }

  if (in_mode < GST_VIDEO_MULTIVIEW_MODE_SIDE_BY_SIDE) {        /* unknown/mono/left/right single image */
  } else if (in_mode == GST_VIDEO_MULTIVIEW_MODE_SIDE_BY_SIDE ||
      in_mode == GST_VIDEO_MULTIVIEW_MODE_SIDE_BY_SIDE_QUINCUNX) {
    /* Side-by-side input */
    offsets[r_index][0] += 0.5 * tex_scale[r_index][0];
    tex_scale[0][0] *= 0.5f;    /* Half horizontal scale */
    tex_scale[1][0] *= 0.5f;
  } else if (in_mode == GST_VIDEO_MULTIVIEW_MODE_TOP_BOTTOM) {  /* top-bottom */
    offsets[r_index][1] += 0.5 * tex_scale[r_index][1];
    tex_scale[0][1] *= 0.5f;    /* Half vertical scale */
    tex_scale[1][1] *= 0.5f;
  }

  /* Flipped is vertical, flopped is horizontal.
   * Adjust and offset per-view scaling. This needs to be done
   * after the input scaling already splits the views, before
   * adding any output scaling. */
  if ((in_flags & GST_VIDEO_MULTIVIEW_FLAGS_LEFT_FLIPPED) !=
      (out_flags & GST_VIDEO_MULTIVIEW_FLAGS_LEFT_FLIPPED)) {
    offsets[l_index][1] += tex_scale[l_index][1];
    tex_scale[l_index][1] *= -1.0;
  }
  if ((in_flags & GST_VIDEO_MULTIVIEW_FLAGS_LEFT_FLOPPED) !=
      (out_flags & GST_VIDEO_MULTIVIEW_FLAGS_LEFT_FLOPPED)) {
    offsets[l_index][0] += tex_scale[l_index][0];
    tex_scale[l_index][0] *= -1.0;
  }
  if ((in_flags & GST_VIDEO_MULTIVIEW_FLAGS_RIGHT_FLIPPED) !=
      (out_flags & GST_VIDEO_MULTIVIEW_FLAGS_RIGHT_FLIPPED)) {
    offsets[r_index][1] += tex_scale[r_index][1];
    tex_scale[r_index][1] *= -1.0;
  }
  if ((in_flags & GST_VIDEO_MULTIVIEW_FLAGS_RIGHT_FLOPPED) !=
      (out_flags & GST_VIDEO_MULTIVIEW_FLAGS_RIGHT_FLOPPED)) {
    offsets[r_index][0] += tex_scale[r_index][0];
    tex_scale[r_index][0] *= -1.0;
  }

  if (out_mode == GST_VIDEO_MULTIVIEW_MODE_SIDE_BY_SIDE ||
      out_mode == GST_VIDEO_MULTIVIEW_MODE_SIDE_BY_SIDE_QUINCUNX) {
    /* Side-by-Side */
    offsets[1][0] -= tex_scale[1][0];
    tex_scale[0][0] *= 2.0f;
    tex_scale[1][0] *= 2.0f;
  } else if (out_mode == GST_VIDEO_MULTIVIEW_MODE_TOP_BOTTOM) {
    offsets[1][1] -= tex_scale[1][1];
    tex_scale[0][1] *= 2.0f;
    tex_scale[1][1] *= 2.0f;
  }

  GST_DEBUG_OBJECT (viewconvert,
      "Scaling matrix [ %f, %f ] [ %f %f]. Offsets [ %f, %f ] [ %f, %f ]",
      tex_scale[0][0], tex_scale[0][1],
      tex_scale[1][0], tex_scale[1][1],
      offsets[0][0], offsets[0][1], offsets[1][0], offsets[1][1]);
  fragment_source_str = _get_shader_string (viewconvert, in_mode, out_mode);
// g_print ("%s\n", fragment_source_str);
  res = gst_gl_context_gen_shader (viewconvert->context, text_vertex_shader,
      fragment_source_str, &viewconvert->shader);
  g_free (fragment_source_str);
  if (!res)
    goto error;
  viewconvert->priv->attr_position =
      gst_gl_shader_get_attribute_location (viewconvert->shader, "a_position");
  viewconvert->priv->attr_texture =
      gst_gl_shader_get_attribute_location (viewconvert->shader, "a_texcoord");
  gst_gl_shader_use (viewconvert->shader);
  gst_gl_shader_set_uniform_2fv (viewconvert->shader, "tex_scale",
      2, tex_scale[0]);
  gst_gl_shader_set_uniform_2fv (viewconvert->shader, "offsets", 2, offsets[0]);
  gst_gl_shader_set_uniform_1f (viewconvert->shader, "width",
      GST_VIDEO_INFO_WIDTH (&viewconvert->out_info));
  gst_gl_shader_set_uniform_1f (viewconvert->shader, "height",
      GST_VIDEO_INFO_HEIGHT (&viewconvert->out_info));
  gst_gl_shader_set_uniform_matrix_3fv (viewconvert->shader, "downmix",
      2, FALSE, &downmix_matrices[viewconvert->downmix_mode][0][0]);
  if (in_mode == GST_VIDEO_MULTIVIEW_MODE_SEPARATED ||
      in_mode == GST_VIDEO_MULTIVIEW_MODE_FRAME_BY_FRAME) {
    gst_gl_shader_set_uniform_1i (viewconvert->shader, "tex_l", l_index);
    gst_gl_shader_set_uniform_1i (viewconvert->shader, "tex_r", r_index);
  } else {
    gst_gl_shader_set_uniform_1i (viewconvert->shader, "tex_l", 0);
    gst_gl_shader_set_uniform_1i (viewconvert->shader, "tex_r", 0);
  }
  gst_gl_context_clear_shader (viewconvert->context);
  if (!_init_view_convert_fbo (viewconvert)) {
    goto error;
  }

  if (!viewconvert->priv->vertex_buffer) {
    if (gl->GenVertexArrays) {
      gl->GenVertexArrays (1, &viewconvert->priv->vao);
      gl->BindVertexArray (viewconvert->priv->vao);
    }

    gl->GenBuffers (1, &viewconvert->priv->vertex_buffer);
    gl->BindBuffer (GL_ARRAY_BUFFER, viewconvert->priv->vertex_buffer);
    gl->BufferData (GL_ARRAY_BUFFER, 4 * 5 * sizeof (GLfloat), vertices,
        GL_STATIC_DRAW);
    gl->GenBuffers (1, &viewconvert->priv->vbo_indices);
    gl->BindBuffer (GL_ELEMENT_ARRAY_BUFFER, viewconvert->priv->vbo_indices);
    gl->BufferData (GL_ELEMENT_ARRAY_BUFFER, sizeof (indices), indices,
        GL_STATIC_DRAW);
    if (gl->GenVertexArrays) {
      _bind_buffer (viewconvert);
      gl->BindVertexArray (0);
    }

    gl->BindBuffer (GL_ARRAY_BUFFER, 0);
    gl->BindBuffer (GL_ELEMENT_ARRAY_BUFFER, 0);
  }

  gl->BindTexture (GL_TEXTURE_2D, 0);
  viewconvert->initted = TRUE;
  return TRUE;
error:
  return FALSE;
}

static gboolean
_do_view_convert_draw (GstGLContext * context, GstGLViewConvert * viewconvert)
{
  GstGLViewConvertPrivate *priv = viewconvert->priv;
  GstGLFuncs *gl;
  guint out_width, out_height;
  gint out_views, i;
  GLint viewport_dim[4] = { 0 };
  GLenum multipleRT[] = {
    GL_COLOR_ATTACHMENT0,
    GL_COLOR_ATTACHMENT1,
    GL_COLOR_ATTACHMENT2
  };
  GstVideoMultiviewMode in_mode = priv->input_mode;
  GstVideoMultiviewMode out_mode = priv->output_mode;

  gl = context->gl_vtable;
  out_width = GST_VIDEO_INFO_WIDTH (&viewconvert->out_info);
  out_height = GST_VIDEO_INFO_HEIGHT (&viewconvert->out_info);
  gl->BindFramebuffer (GL_FRAMEBUFFER, viewconvert->fbo);
  if (out_mode == GST_VIDEO_MULTIVIEW_MODE_SEPARATED ||
      out_mode == GST_VIDEO_MULTIVIEW_MODE_FRAME_BY_FRAME) {
    out_views = viewconvert->out_info.views;
  } else {
    out_views = 1;
  }
  /* attach the texture to the FBO to renderer to */
  for (i = 0; i < out_views; i++) {
    /* needed? */
    gl->BindTexture (GL_TEXTURE_2D, priv->out_tex[i]->tex_id);
    gl->FramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i,
        GL_TEXTURE_2D, priv->out_tex[i]->tex_id, 0);
  }

  if (gl->DrawBuffers)
    gl->DrawBuffers (out_views, multipleRT);
  else if (gl->DrawBuffer)
    gl->DrawBuffer (GL_COLOR_ATTACHMENT0);
  gl->GetIntegerv (GL_VIEWPORT, viewport_dim);
  gl->Viewport (0, 0, out_width, out_height);
  gst_gl_shader_use (viewconvert->shader);
  if (gl->BindVertexArray)
    gl->BindVertexArray (priv->vao);
  else
    _bind_buffer (viewconvert);
  if (in_mode == GST_VIDEO_MULTIVIEW_MODE_SEPARATED ||
      in_mode == GST_VIDEO_MULTIVIEW_MODE_FRAME_BY_FRAME) {
    if (priv->in_tex[1] == NULL) {
      GST_ERROR_OBJECT (viewconvert,
          "No 2nd view available during conversion!");
      return FALSE;
    }
    gl->ActiveTexture (GL_TEXTURE1);
    gl->BindTexture (GL_TEXTURE_2D, priv->in_tex[1]->tex_id);
  }

  gl->ActiveTexture (GL_TEXTURE0);
  gl->BindTexture (GL_TEXTURE_2D, priv->in_tex[0]->tex_id);
  gl->DrawElements (GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, NULL);
  if (gl->BindVertexArray)
    gl->BindVertexArray (0);
  else
    _unbind_buffer (viewconvert);
  if (gl->DrawBuffer)
    gl->DrawBuffer (GL_NONE);
  /* we are done with the shader */
  gst_gl_context_clear_shader (context);
  gl->Viewport (viewport_dim[0], viewport_dim[1], viewport_dim[2],
      viewport_dim[3]);
  gst_gl_context_check_framebuffer_status (context);
  gl->BindFramebuffer (GL_FRAMEBUFFER, 0);
  return TRUE;
}

static gboolean
_gen_buffer (GstGLViewConvert * viewconvert, GstBuffer ** target)
{
  *target = gst_buffer_new ();
  if (!gst_gl_memory_setup_buffer (viewconvert->context, NULL,
          &viewconvert->out_info, NULL, *target)) {
    return FALSE;
  }
  gst_buffer_add_video_meta_full (*target, 0,
      GST_VIDEO_INFO_FORMAT (&viewconvert->out_info),
      GST_VIDEO_INFO_WIDTH (&viewconvert->out_info),
      GST_VIDEO_INFO_HEIGHT (&viewconvert->out_info),
      GST_VIDEO_INFO_N_PLANES (&viewconvert->out_info),
      viewconvert->out_info.offset, viewconvert->out_info.stride);

  return TRUE;
}

static void
_do_view_convert (GstGLContext * context, GstGLViewConvert * viewconvert)
{
  GstGLViewConvertPrivate *priv = viewconvert->priv;
  guint in_width, in_height, out_width, out_height;
  GstMapInfo out_info[GST_VIDEO_MAX_PLANES], in_info[GST_VIDEO_MAX_PLANES];
  GstGLMemory *dest_tex[GST_VIDEO_MAX_PLANES];
  gboolean res = TRUE;
  gint i = 0, j = 0;
  gint in_views, out_views;
  GstVideoMultiviewMode in_mode;
  GstVideoMultiviewMode out_mode;

  out_width = GST_VIDEO_INFO_WIDTH (&viewconvert->out_info);
  out_height = GST_VIDEO_INFO_HEIGHT (&viewconvert->out_info);
  in_width = GST_VIDEO_INFO_WIDTH (&viewconvert->in_info);
  in_height = GST_VIDEO_INFO_HEIGHT (&viewconvert->in_info);

  g_return_if_fail (priv->primary_out == NULL);
  g_return_if_fail (priv->auxilliary_out == NULL);

  in_mode = priv->input_mode;
  out_mode = priv->output_mode;

  if (in_mode == GST_VIDEO_MULTIVIEW_MODE_SEPARATED ||
      in_mode == GST_VIDEO_MULTIVIEW_MODE_FRAME_BY_FRAME)
    in_views = viewconvert->in_info.views;
  else
    in_views = 1;

  if (out_mode == GST_VIDEO_MULTIVIEW_MODE_SEPARATED ||
      out_mode == GST_VIDEO_MULTIVIEW_MODE_FRAME_BY_FRAME)
    out_views = viewconvert->out_info.views;
  else
    out_views = 1;

  if (!_init_view_convert (viewconvert)) {
    priv->result = FALSE;
    return;
  }

  if (!_gen_buffer (viewconvert, &priv->primary_out)) {
    GST_ERROR_OBJECT (viewconvert,
        "Failed to setup memory for primary output buffer");
    priv->result = FALSE;
    return;
  }

  if (out_mode == GST_VIDEO_MULTIVIEW_MODE_FRAME_BY_FRAME) {
    if (!_gen_buffer (viewconvert, &priv->auxilliary_out)) {
      GST_ERROR_OBJECT (viewconvert,
          "Failed to setup memory for second view output buffer");
      priv->result = FALSE;
      return;
    }
  }

  for (i = 0; i < in_views; i++) {
    if (in_mode == GST_VIDEO_MULTIVIEW_MODE_FRAME_BY_FRAME && i > 0) {
      priv->in_tex[i] =
          (GstGLMemory *) gst_buffer_peek_memory (priv->auxilliary_in, 0);
    } else {
      priv->in_tex[i] =
          (GstGLMemory *) gst_buffer_peek_memory (priv->primary_in, i);
    }
    if (!gst_is_gl_memory ((GstMemory *) priv->in_tex[i])) {
      GST_ERROR_OBJECT (viewconvert, "input must be GstGLMemory");
      res = FALSE;
      goto out;
    }
    if (!gst_memory_map ((GstMemory *) priv->in_tex[i],
            &in_info[i], GST_MAP_READ | GST_MAP_GL)) {
      GST_ERROR_OBJECT (viewconvert, "failed to map input memory %p",
          priv->in_tex[i]);
      res = FALSE;
      goto out;
    }
  }

  for (j = 0; j < out_views; j++) {
    GstGLMemory *out_tex;
    guint width, height;
    GstVideoInfo temp_info;

    if (j > 0 && out_mode == GST_VIDEO_MULTIVIEW_MODE_FRAME_BY_FRAME) {
      dest_tex[j] = out_tex =
          (GstGLMemory *) gst_buffer_peek_memory (priv->auxilliary_out, 0);
    } else {
      dest_tex[j] = out_tex =
          (GstGLMemory *) gst_buffer_peek_memory (priv->primary_out, j);
    }

    if (!gst_is_gl_memory ((GstMemory *) out_tex)) {
      GST_ERROR_OBJECT (viewconvert, "output must be GstGLMemory");
      res = FALSE;
      goto out;
    }

    width = gst_gl_memory_get_texture_width (out_tex);
    height = gst_gl_memory_get_texture_height (out_tex);
    gst_video_info_set_format (&temp_info, GST_VIDEO_FORMAT_RGBA, width,
        height);
    if (out_tex->tex_type == GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE
        || out_tex->tex_type == GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE_ALPHA
        || out_width != width || out_height != height) {
      /* Luminance formats are not color renderable */
      /* renderering to a framebuffer only renders the intersection of all
       * the attachments i.e. the smallest attachment size */
      if (!priv->out_tex[j])
        priv->out_tex[j] =
            (GstGLMemory *) gst_gl_memory_alloc (context, NULL, &temp_info, 0,
            NULL);
    } else {
      priv->out_tex[j] = out_tex;
    }

    if (!gst_memory_map ((GstMemory *) priv->out_tex[j],
            &out_info[j], GST_MAP_WRITE | GST_MAP_GL)) {
      GST_ERROR_OBJECT (viewconvert, "failed to map output memory %p",
          priv->out_tex[i]);
      res = FALSE;
      goto out;
    }
  }
  priv->n_out_tex = out_views;

  GST_LOG_OBJECT (viewconvert, "multiview splitting to textures:%p,%p,%p,%p "
      "dimensions:%ux%u, from textures:%p,%p,%p,%p dimensions:%ux%u",
      priv->out_tex[0], priv->out_tex[1],
      priv->out_tex[2], priv->out_tex[3],
      out_width, out_height, priv->in_tex[0],
      priv->in_tex[1], priv->in_tex[2], priv->in_tex[3], in_width, in_height);

  if (!_do_view_convert_draw (context, viewconvert))
    res = FALSE;
out:
  for (j--; j >= 0; j--) {
    GstGLMemory *out_tex;
    guint width, height;

    out_tex = dest_tex[j];

    width = gst_gl_memory_get_texture_width (out_tex);
    height = gst_gl_memory_get_texture_height (out_tex);

    gst_memory_unmap ((GstMemory *) priv->out_tex[j], &out_info[j]);
    if (out_tex != priv->out_tex[j]) {
      GstMapInfo to_info, from_info;
      if (!gst_memory_map ((GstMemory *) priv->out_tex[j],
              &from_info, GST_MAP_READ | GST_MAP_GL)) {
        gst_gl_context_set_error (viewconvert->context,
            "Failed to map " "intermediate memory");
        res = FALSE;
        continue;
      }
      if (!gst_memory_map ((GstMemory *) out_tex, &to_info,
              GST_MAP_WRITE | GST_MAP_GL)) {
        gst_gl_context_set_error (viewconvert->context, "Failed to map "
            "intermediate memory");
        res = FALSE;
        continue;
      }
      gst_gl_memory_copy_into_texture (priv->out_tex[j],
          out_tex->tex_id, out_tex->tex_type, width, height,
          GST_VIDEO_INFO_PLANE_STRIDE (&out_tex->info, out_tex->plane), FALSE);
      gst_memory_unmap ((GstMemory *) out_tex, &to_info);
    }

    priv->out_tex[j] = NULL;
  }

  for (i--; i >= 0; i--) {
    gst_memory_unmap ((GstMemory *) priv->in_tex[i], &in_info[i]);
  }

  if (!res) {
    gst_buffer_replace (&priv->primary_out, NULL);
    gst_buffer_replace (&priv->auxilliary_out, NULL);
  }

  priv->result = res;
  return;
}

GstFlowReturn
gst_gl_view_convert_submit_input_buffer (GstGLViewConvert * viewconvert,
    gboolean is_discont, GstBuffer * input)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstVideoMultiviewMode mode;
  GstBuffer **target;

  if (is_discont) {
    gst_buffer_replace (&viewconvert->priv->primary_in, NULL);
    gst_buffer_replace (&viewconvert->priv->auxilliary_in, NULL);
  }

  mode = viewconvert->input_mode_override;
  if (mode == GST_VIDEO_MULTIVIEW_MODE_NONE)
    mode = GST_VIDEO_INFO_MULTIVIEW_MODE (&viewconvert->in_info);

  target = &viewconvert->priv->primary_in;

  /* For frame-by-frame mode, we need to collect the 2nd eye into
   * our auxilliary buffer */
  if (mode == GST_VIDEO_MULTIVIEW_MODE_FRAME_BY_FRAME) {
    if (!GST_BUFFER_FLAG_IS_SET (input, GST_VIDEO_BUFFER_FLAG_FIRST_IN_BUNDLE))
      target = &viewconvert->priv->auxilliary_in;
  }

  if (*target)
    gst_buffer_unref (*target);
  *target = input;

  return ret;
}

GstFlowReturn
gst_gl_view_convert_get_output (GstGLViewConvert * viewconvert,
    GstBuffer ** outbuf_ptr)
{
  GstGLViewConvertPrivate *priv = viewconvert->priv;
  GstBuffer *outbuf = NULL;
  GstFlowReturn ret = GST_FLOW_OK;
  GstVideoMultiviewMode in_mode, out_mode;
  GstVideoMultiviewFlags in_flags, out_flags;

  g_return_val_if_fail (GST_IS_GL_VIEW_CONVERT (viewconvert), GST_FLOW_ERROR);
  g_return_val_if_fail (GST_GL_IS_CONTEXT (viewconvert->context),
      GST_FLOW_ERROR);

  GST_OBJECT_LOCK (viewconvert);

  /* See if a buffer is available already */
  if (priv->primary_out) {
    outbuf = viewconvert->priv->primary_out;
    priv->primary_out = NULL;
    goto done;
  }
  if (viewconvert->priv->auxilliary_out) {
    outbuf = priv->auxilliary_out;
    priv->auxilliary_out = NULL;
    goto done;
  }

  /* Check prereqs before processing a new input buffer */
  if (priv->primary_in == NULL)
    goto done;

  in_mode = viewconvert->input_mode_override;
  in_flags = viewconvert->input_flags_override;
  if (in_mode == GST_VIDEO_MULTIVIEW_MODE_NONE) {
    in_mode = GST_VIDEO_INFO_MULTIVIEW_MODE (&viewconvert->in_info);
    in_flags = GST_VIDEO_INFO_MULTIVIEW_FLAGS (&viewconvert->in_info);
  }

  /* Configured output mode already takes any override
   * into account */
  out_mode = GST_VIDEO_INFO_MULTIVIEW_MODE (&viewconvert->out_info);
  out_flags = GST_VIDEO_INFO_MULTIVIEW_FLAGS (&viewconvert->out_info);

  if (in_mode == GST_VIDEO_MULTIVIEW_MODE_FRAME_BY_FRAME) {
    /* For frame-by-frame, we need 2 input buffers */
    if (priv->auxilliary_in == NULL) {
      GST_LOG_OBJECT (viewconvert,
          "Can't generate output yet - frame-by-frame mode");
      goto done;
    }
  }

  /* Store the current conversion in the priv vars */
  priv->input_mode = in_mode;
  priv->input_flags = in_flags;
  priv->output_mode = out_mode;
  priv->output_flags = out_flags;

  if (priv->input_mode == priv->output_mode &&
      priv->input_flags == priv->output_flags &&
      viewconvert->in_info.width == viewconvert->out_info.width &&
      viewconvert->in_info.height == viewconvert->out_info.height) {
    /* passthrough - just pass input buffers */
    outbuf = gst_buffer_ref (priv->primary_in);
    if (in_mode == GST_VIDEO_MULTIVIEW_MODE_FRAME_BY_FRAME)
      priv->auxilliary_out = gst_buffer_ref (priv->auxilliary_in);
    goto done_clear_input;
  }

  /* Generate new output buffer(s) */
  gst_gl_context_thread_add (viewconvert->context,
      (GstGLContextThreadFunc) _do_view_convert, viewconvert);

  if (!priv->result) {
    if (priv->primary_out)
      gst_object_unref (priv->primary_out);
    if (priv->auxilliary_out)
      gst_object_unref (priv->auxilliary_out);
    priv->primary_out = NULL;
    priv->auxilliary_out = NULL;
    ret = GST_FLOW_ERROR;
    goto done_clear_input;
  }

  outbuf = priv->primary_out;
  gst_buffer_copy_into (outbuf, priv->primary_in,
      GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS, 0, -1);
  GST_BUFFER_FLAG_SET (outbuf,
      GST_VIDEO_BUFFER_FLAG_FIRST_IN_BUNDLE |
      GST_VIDEO_BUFFER_FLAG_MULTIPLE_VIEW);

  if (priv->auxilliary_out) {
    gst_buffer_copy_into (priv->auxilliary_out,
        priv->primary_out, GST_BUFFER_COPY_FLAGS, 0, -1);
    GST_BUFFER_FLAG_UNSET (priv->auxilliary_out,
        GST_VIDEO_BUFFER_FLAG_FIRST_IN_BUNDLE);
  }
  priv->primary_out = NULL;

done_clear_input:
  /* Invalidate input buffers now they've been used */
  gst_buffer_replace (&priv->primary_in, NULL);
  gst_buffer_replace (&priv->auxilliary_in, NULL);

done:
  GST_OBJECT_UNLOCK (viewconvert);
  *outbuf_ptr = outbuf;
  return ret;
}
