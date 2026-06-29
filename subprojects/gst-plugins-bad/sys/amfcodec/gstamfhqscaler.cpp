/* GStreamer
 * Copyright (C) 2026 Azat Nurgaliev <azat.nurg@gmail.com>
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
 * SECTION:element-amfhqscaler
 * @title: amfhqscaler
 * @short_description: AMD AMF based high-quality video upscaler
 *
 * Wraps AMF's `AMFHQScaler` component (AMD Video Super Resolution,
 * FSR-style bicubic, …). The element is intended for *upscaling* and
 * refuses configurations where any output dimension is smaller than
 * the input, mirroring the behavior of ffmpeg's `sr_amf` filter.
 *
 * Since: 1.30
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstamfhqscaler.h"
#include "gstamfbasefilter.h"
#include "gstamfutils.h"

#include <components/HQScaler.h>
#include <core/Data.h>
#include <core/Factory.h>

#include <string>

GST_DEBUG_CATEGORY_STATIC (gst_amf_hq_scaler_debug);
#define GST_CAT_DEFAULT gst_amf_hq_scaler_debug

/* *INDENT-OFF* */
using namespace amf;
/* *INDENT-ON* */

typedef struct _GstAmfHQScalerClassData
{
  GstCaps *sink_caps;
  GstCaps *src_caps;
  gint64 adapter_luid;
  guint device_index;
} GstAmfHQScalerClassData;

enum
{
  PROP_0,
  PROP_ALGORITHM,
  PROP_SHARPNESS,
  PROP_ADD_BORDERS,
  PROP_BORDER_COLOR,
};

#define DEFAULT_ALGORITHM           AMF_HQ_SCALER_ALGORITHM_BILINEAR
#define DEFAULT_SHARPNESS           0.5f
#define DEFAULT_ADD_BORDERS         TRUE
#define DEFAULT_BORDER_COLOR        0x00000000  /* opaque black, packed AMFColor */

typedef struct _GstAmfHQScaler GstAmfHQScaler;
typedef struct _GstAmfHQScalerClass GstAmfHQScalerClass;

struct _GstAmfHQScaler
{
  GstAmfBaseFilter parent;

  gint algorithm;
  gfloat sharpness;
  gboolean add_borders;
  guint border_color;
};

struct _GstAmfHQScalerClass
{
  GstAmfBaseFilterClass parent_class;

  gint64 adapter_luid;
  guint device_index;
};

#define GST_AMF_HQS(object) ((GstAmfHQScaler *) (object))
#define GST_AMF_HQS_GET_CLASS(object) \
    (G_TYPE_INSTANCE_GET_CLASS ((object),G_TYPE_FROM_INSTANCE (object),GstAmfHQScalerClass))

static void gst_amf_hq_scaler_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_amf_hq_scaler_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstCaps *gst_amf_hq_scaler_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static GstCaps *gst_amf_hq_scaler_fixate_size (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps);
static GstCaps *gst_amf_hq_scaler_fixate_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps);

static const wchar_t *gst_amf_hq_scaler_get_component_id (GstAmfBaseFilter *
    self);
static gboolean gst_amf_hq_scaler_configure_component (GstAmfBaseFilter * self,
    AMFComponent * comp, const GstVideoInfo * in_info,
    const GstVideoInfo * out_info);
static gboolean gst_amf_hq_scaler_validate_caps (GstAmfBaseFilter * self,
    const GstVideoInfo * in_info, const GstVideoInfo * out_info);

#define GST_TYPE_AMF_HQS_ALGORITHM (gst_amf_hqs_algorithm_get_type())
static GType
gst_amf_hqs_algorithm_get_type (void)
{
  static GType type = 0;
  static const GEnumValue values[] = {
    {AMF_HQ_SCALER_ALGORITHM_BILINEAR, "Bilinear", "bilinear"},
    {AMF_HQ_SCALER_ALGORITHM_BICUBIC, "Bicubic", "bicubic"},
    {AMF_HQ_SCALER_ALGORITHM_VIDEOSR1_0, "Video SR 1.0", "videosr1-0"},
    {AMF_HQ_SCALER_ALGORITHM_POINT, "Point (nearest)", "point"},
    {AMF_HQ_SCALER_ALGORITHM_VIDEOSR1_1, "Video SR 1.1", "videosr1-1"},
    {0, nullptr, nullptr}
  };

  if (g_once_init_enter (&type)) {
    GType t = g_enum_register_static ("GstAmfHQScalerAlgorithm", values);
    g_once_init_leave (&type, t);
  }
  return type;
}

static void
gst_amf_hq_scaler_class_init (GstAmfHQScalerClass * klass, gpointer data)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *trans_class = GST_BASE_TRANSFORM_CLASS (klass);
  GstAmfBaseFilterClass *base_class = GST_AMF_BASE_FILTER_CLASS (klass);
  GstAmfHQScalerClassData *cdata = (GstAmfHQScalerClassData *) data;

  gobject_class->set_property = gst_amf_hq_scaler_set_property;
  gobject_class->get_property = gst_amf_hq_scaler_get_property;

  g_object_class_install_property (gobject_class, PROP_ALGORITHM,
      g_param_spec_enum ("algorithm", "Algorithm",
          "High-quality scaling algorithm to use",
          GST_TYPE_AMF_HQS_ALGORITHM, DEFAULT_ALGORITHM,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
              G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_SHARPNESS,
      g_param_spec_float ("sharpness", "Sharpness",
          "Sharpness factor (0.0 - 2.0)",
          0.0f, 2.0f, DEFAULT_SHARPNESS,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
              G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_ADD_BORDERS,
      g_param_spec_boolean ("add-borders", "Add Borders",
          "Keep the original aspect ratio when scaling and fill padding "
          "with border-color",
          DEFAULT_ADD_BORDERS,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
              G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_BORDER_COLOR,
      g_param_spec_uint ("border-color", "Border color",
          "Border color when add-borders is enabled (RGBA, 0xAARRGGBB)",
          0, G_MAXUINT, DEFAULT_BORDER_COLOR,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
              G_PARAM_STATIC_STRINGS)));

  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
          cdata->sink_caps));
  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
          cdata->src_caps));

  gst_element_class_set_static_metadata (element_class,
      "AMD AMF High-Quality Video Scaler",
      "Filter/Converter/Video/Scaler/Hardware",
      "High-quality video upscaling using AMD AMF (VSR / FSR / Bicubic)",
      "GStreamer AMF contributors");

  trans_class->passthrough_on_same_caps = FALSE;
  trans_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_amf_hq_scaler_transform_caps);
  trans_class->fixate_caps = GST_DEBUG_FUNCPTR (gst_amf_hq_scaler_fixate_caps);

  base_class->get_component_id =
      GST_DEBUG_FUNCPTR (gst_amf_hq_scaler_get_component_id);
  base_class->configure_component =
      GST_DEBUG_FUNCPTR (gst_amf_hq_scaler_configure_component);
  base_class->validate_caps =
      GST_DEBUG_FUNCPTR (gst_amf_hq_scaler_validate_caps);

  klass->adapter_luid = cdata->adapter_luid;
  klass->device_index = cdata->device_index;

  gst_type_mark_as_plugin_api (GST_TYPE_AMF_HQS_ALGORITHM,
      (GstPluginAPIFlags) 0);
}

static void
gst_amf_hq_scaler_init (GstAmfHQScaler * self)
{
  GstAmfHQScalerClass *klass = GST_AMF_HQS_GET_CLASS (self);

  gst_amf_base_filter_set_subclass_data (GST_AMF_BASE_FILTER (self),
      klass->adapter_luid, klass->device_index);

  self->algorithm = DEFAULT_ALGORITHM;
  self->sharpness = DEFAULT_SHARPNESS;
  self->add_borders = DEFAULT_ADD_BORDERS;
  self->border_color = DEFAULT_BORDER_COLOR;
}

static void
gst_amf_hq_scaler_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAmfHQScaler *self = GST_AMF_HQS (object);

  switch (prop_id) {
    case PROP_ALGORITHM:
      self->algorithm = g_value_get_enum (value);
      break;
    case PROP_SHARPNESS:
      self->sharpness = g_value_get_float (value);
      break;
    case PROP_ADD_BORDERS:
      self->add_borders = g_value_get_boolean (value);
      break;
    case PROP_BORDER_COLOR:
      self->border_color = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_amf_hq_scaler_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstAmfHQScaler *self = GST_AMF_HQS (object);

  switch (prop_id) {
    case PROP_ALGORITHM:
      g_value_set_enum (value, self->algorithm);
      break;
    case PROP_SHARPNESS:
      g_value_set_float (value, self->sharpness);
      break;
    case PROP_ADD_BORDERS:
      g_value_set_boolean (value, self->add_borders);
      break;
    case PROP_BORDER_COLOR:
      g_value_set_uint (value, self->border_color);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_amf_hq_scaler_is_supported_caps_features (const GstCapsFeatures * features)
{
  guint n_features;

  if (!features || gst_caps_features_is_any (features))
    return FALSE;

  n_features = gst_caps_features_get_size (features);
  for (guint i = 0; i < n_features; i++) {
    const gchar *feature = gst_caps_features_get_nth (features, i);

    if (!g_strcmp0 (feature, GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY))
      continue;
    if (!g_strcmp0 (feature, GST_CAPS_FEATURE_FORMAT_INTERLACED))
      continue;
    if (!g_strcmp0 (feature,
            GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION))
      continue;
#ifdef G_OS_WIN32
    if (!g_strcmp0 (feature, GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY))
      continue;
#endif

    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_amf_hq_scaler_get_dim_range (GstStructure * st, const gchar * field,
    gint * min, gint * max)
{
  const GValue *val = gst_structure_get_value (st, field);

  if (!val)
    return FALSE;
  if (G_VALUE_HOLDS_INT (val)) {
    *min = *max = g_value_get_int (val);
    return TRUE;
  }
  if (GST_VALUE_HOLDS_INT_RANGE (val)) {
    *min = gst_value_get_int_range_min (val);
    *max = gst_value_get_int_range_max (val);
    return TRUE;
  }
  return FALSE;
}

static gboolean
gst_amf_hq_scaler_get_template_dim_range (GstBaseTransform * trans,
    GstPadDirection direction, const gchar * field, gint * min, gint * max)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (trans);
  const gchar *other_pad = (direction == GST_PAD_SINK) ? "src" : "sink";
  GstPadTemplate *templ;
  GstCaps *caps;
  GstStructure *st;

  templ = gst_element_class_get_pad_template (klass, other_pad);
  if (!templ)
    return FALSE;

  caps = gst_pad_template_get_caps (templ);
  if (!caps || gst_caps_is_empty (caps)) {
    if (caps)
      gst_caps_unref (caps);
    return FALSE;
  }

  st = gst_caps_get_structure (caps, 0);
  if (!gst_amf_hq_scaler_get_dim_range (st, field, min, max)) {
    gst_caps_unref (caps);
    return FALSE;
  }

  gst_caps_unref (caps);
  return TRUE;
}

static void
gst_amf_hq_scaler_set_upscale_dim (GstStructure * st,
    GstPadDirection direction, const gchar * field, gint template_min,
    gint template_max)
{
  gint dim_min, dim_max;
  gint out_min, out_max;

  if (gst_amf_hq_scaler_get_dim_range (st, field, &dim_min, &dim_max)) {
    if (direction == GST_PAD_SRC) {
      /* Output caps -> possible input: input <= output. */
      out_min = template_min;
      out_max = CLAMP (dim_max, template_min, template_max);
    } else {
      /* Input caps -> possible output: output >= input. */
      out_min = CLAMP (dim_min, template_min, template_max);
      out_max = template_max;
    }
  } else {
    out_min = template_min;
    out_max = template_max;
  }

  if (out_min > out_max)
    out_max = out_min;

  gst_structure_set (st, field, GST_TYPE_INT_RANGE, out_min, out_max, NULL);
}

static GstCaps *
gst_amf_hq_scaler_transform_size_info (GstBaseTransform * trans,
    GstCaps * caps, GstPadDirection direction)
{
  GstStructure *st;
  GstCaps *res;
  gint i, n;
  gint template_min_w = 1, template_max_w = G_MAXINT;
  gint template_min_h = 1, template_max_h = G_MAXINT;

  gst_amf_hq_scaler_get_template_dim_range (trans, direction, "width",
      &template_min_w, &template_max_w);
  gst_amf_hq_scaler_get_template_dim_range (trans, direction, "height",
      &template_min_h, &template_max_h);

  res = gst_caps_new_empty ();
  n = gst_caps_get_size (caps);

  for (i = 0; i < n; i++) {
    GstCapsFeatures *feat = gst_caps_get_features (caps, i);

    st = gst_structure_copy (gst_caps_get_structure (caps, i));

    if (!gst_amf_hq_scaler_is_supported_caps_features (feat)) {
      gst_caps_append_structure_full (res, st, gst_caps_features_copy (feat));
      continue;
    }

    gst_amf_hq_scaler_set_upscale_dim (st, direction, "width", template_min_w,
        template_max_w);
    gst_amf_hq_scaler_set_upscale_dim (st, direction, "height", template_min_h,
        template_max_h);
    if (gst_structure_has_field (st, "pixel-aspect-ratio")) {
      gst_structure_set (st, "pixel-aspect-ratio", GST_TYPE_FRACTION_RANGE,
          1, G_MAXINT, G_MAXINT, 1, NULL);
    }
#ifdef G_OS_WIN32
    if (feat
        && gst_caps_features_contains (feat,
            GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY)) {
      gst_caps_append_structure_full (res, st,
          gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY, NULL));
    } else {
#endif
      gst_caps_append_structure_full (res, st,
          gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY, NULL));
#ifdef G_OS_WIN32
    }
#endif
  }

  return res;
}

static GstCaps *
gst_amf_hq_scaler_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstCaps *tmp, *result;

  /* The HQ scaler does not change pixel format, only width/height,
   * so we keep the format/colorimetry untouched. */
  tmp = gst_amf_hq_scaler_transform_size_info (trans, caps, direction);

  if (filter) {
    result = gst_caps_intersect_full (filter, tmp, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (tmp);
  } else {
    result = tmp;
  }

  GST_DEBUG_OBJECT (trans, "transformed (%s) %" GST_PTR_FORMAT " into %"
      GST_PTR_FORMAT, direction == GST_PAD_SINK ? "sink" : "src", caps, result);

  return result;
}

static GstCaps *
gst_amf_hq_scaler_fixate_size (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
{
  GstStructure *ins, *outs;
  const GValue *from_par, *to_par;
  GValue fpar = G_VALUE_INIT, tpar = G_VALUE_INIT;

  othercaps = gst_caps_truncate (othercaps);
  othercaps = gst_caps_make_writable (othercaps);
  ins = gst_caps_get_structure (caps, 0);
  outs = gst_caps_get_structure (othercaps, 0);

  from_par = gst_structure_get_value (ins, "pixel-aspect-ratio");
  to_par = gst_structure_get_value (outs, "pixel-aspect-ratio");

  /* If we're fixating from the sinkpad we always set the PAR and
   * assume that missing PAR on the sinkpad means 1/1 and
   * missing PAR on the srcpad means undefined
   */
  if (direction == GST_PAD_SINK) {
    if (!from_par) {
      g_value_init (&fpar, GST_TYPE_FRACTION);
      gst_value_set_fraction (&fpar, 1, 1);
      from_par = &fpar;
    }
    if (!to_par) {
      g_value_init (&tpar, GST_TYPE_FRACTION_RANGE);
      gst_value_set_fraction_range_full (&tpar, 1, G_MAXINT, G_MAXINT, 1);
      to_par = &tpar;
    }
  } else {
    gint from_par_n, from_par_d;

    if (!from_par) {
      g_value_init (&fpar, GST_TYPE_FRACTION);
      gst_value_set_fraction (&fpar, 1, 1);
      from_par = &fpar;

      from_par_n = from_par_d = 1;
    } else {
      from_par_n = gst_value_get_fraction_numerator (from_par);
      from_par_d = gst_value_get_fraction_denominator (from_par);
    }

    if (!to_par) {
      g_value_init (&tpar, GST_TYPE_FRACTION);
      gst_value_set_fraction (&tpar, from_par_n, from_par_d);
      to_par = &tpar;

      gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
          from_par_n, from_par_d, NULL);
    }
  }

  /* we have both PAR but they might not be fixated */
  {
    gint from_w, from_h, from_par_n, from_par_d, to_par_n, to_par_d;
    gint w = 0, h = 0;
    gint from_dar_n, from_dar_d;
    gint num, den;

    /* from_par should be fixed */
    g_return_val_if_fail (gst_value_is_fixed (from_par), othercaps);

    from_par_n = gst_value_get_fraction_numerator (from_par);
    from_par_d = gst_value_get_fraction_denominator (from_par);

    gst_structure_get_int (ins, "width", &from_w);
    gst_structure_get_int (ins, "height", &from_h);

    gst_structure_get_int (outs, "width", &w);
    gst_structure_get_int (outs, "height", &h);

    /* if both width and height are already fixed, we can't do anything
     * about it anymore */
    if (w && h) {
      guint n, d;

      GST_DEBUG_OBJECT (base, "dimensions already set to %dx%d, not fixating",
          w, h);
      if (!gst_value_is_fixed (to_par)) {
        if (gst_video_calculate_display_ratio (&n, &d, from_w, from_h,
                from_par_n, from_par_d, w, h)) {
          GST_DEBUG_OBJECT (base, "fixating to_par to %dx%d", n, d);
          if (gst_structure_has_field (outs, "pixel-aspect-ratio"))
            gst_structure_fixate_field_nearest_fraction (outs,
                "pixel-aspect-ratio", n, d);
          else if (n != d)
            gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
                n, d, NULL);
        }
      }
      goto done;
    }

    /* Calculate input DAR */
    if (!gst_util_fraction_multiply (from_w, from_h, from_par_n, from_par_d,
            &from_dar_n, &from_dar_d)) {
      GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (NULL),
          ("Error calculating the output scaled size - integer overflow"));
      goto done;
    }

    GST_DEBUG_OBJECT (base, "Input DAR is %d/%d", from_dar_n, from_dar_d);

    /* If either width or height are fixed there's not much we
     * can do either except choosing a height or width and PAR
     * that matches the DAR as good as possible
     */
    if (h) {
      GstStructure *tmp;
      gint set_w, set_par_n, set_par_d;

      GST_DEBUG_OBJECT (base, "height is fixed (%d)", h);

      /* If the PAR is fixed too, there's not much to do
       * except choosing the width that is nearest to the
       * width with the same DAR */
      if (gst_value_is_fixed (to_par)) {
        to_par_n = gst_value_get_fraction_numerator (to_par);
        to_par_d = gst_value_get_fraction_denominator (to_par);

        GST_DEBUG_OBJECT (base, "PAR is fixed %d/%d", to_par_n, to_par_d);

        if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, to_par_d,
                to_par_n, &num, &den)) {
          GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (NULL),
              ("Error calculating the output scaled size - integer overflow"));
          goto done;
        }

        w = (guint) gst_util_uint64_scale_int_round (h, num, den);
        gst_structure_fixate_field_nearest_int (outs, "width", w);

        goto done;
      }

      /* The PAR is not fixed and it's quite likely that we can set
       * an arbitrary PAR. */

      /* Check if we can keep the input width */
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "width", from_w);
      gst_structure_get_int (tmp, "width", &set_w);

      /* Might have failed but try to keep the DAR nonetheless by
       * adjusting the PAR */
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, h, set_w,
              &to_par_n, &to_par_d)) {
        GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (NULL),
            ("Error calculating the output scaled size - integer overflow"));
        gst_structure_free (tmp);
        goto done;
      }

      if (!gst_structure_has_field (tmp, "pixel-aspect-ratio"))
        gst_structure_set_value (tmp, "pixel-aspect-ratio", to_par);
      gst_structure_fixate_field_nearest_fraction (tmp, "pixel-aspect-ratio",
          to_par_n, to_par_d);
      gst_structure_get_fraction (tmp, "pixel-aspect-ratio", &set_par_n,
          &set_par_d);
      gst_structure_free (tmp);

      /* Check if the adjusted PAR is accepted */
      if (set_par_n == to_par_n && set_par_d == to_par_d) {
        if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
            set_par_n != set_par_d)
          gst_structure_set (outs, "width", G_TYPE_INT, set_w,
              "pixel-aspect-ratio", GST_TYPE_FRACTION, set_par_n, set_par_d,
              NULL);
        goto done;
      }

      /* Otherwise scale the width to the new PAR and check if the
       * adjusted with is accepted. If all that fails we can't keep
       * the DAR */
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, set_par_d,
              set_par_n, &num, &den)) {
        GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (NULL),
            ("Error calculating the output scaled size - integer overflow"));
        goto done;
      }

      w = (guint) gst_util_uint64_scale_int_round (h, num, den);
      gst_structure_fixate_field_nearest_int (outs, "width", w);
      if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
          set_par_n != set_par_d)
        gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
            set_par_n, set_par_d, NULL);

      goto done;
    } else if (w) {
      GstStructure *tmp;
      gint set_h, set_par_n, set_par_d;

      GST_DEBUG_OBJECT (base, "width is fixed (%d)", w);

      /* If the PAR is fixed too, there's not much to do
       * except choosing the height that is nearest to the
       * height with the same DAR */
      if (gst_value_is_fixed (to_par)) {
        to_par_n = gst_value_get_fraction_numerator (to_par);
        to_par_d = gst_value_get_fraction_denominator (to_par);

        GST_DEBUG_OBJECT (base, "PAR is fixed %d/%d", to_par_n, to_par_d);

        if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, to_par_d,
                to_par_n, &num, &den)) {
          GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (NULL),
              ("Error calculating the output scaled size - integer overflow"));
          goto done;
        }

        h = (guint) gst_util_uint64_scale_int_round (w, den, num);
        gst_structure_fixate_field_nearest_int (outs, "height", h);

        goto done;
      }

      /* The PAR is not fixed and it's quite likely that we can set
       * an arbitrary PAR. */

      /* Check if we can keep the input height */
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "height", from_h);
      gst_structure_get_int (tmp, "height", &set_h);

      /* Might have failed but try to keep the DAR nonetheless by
       * adjusting the PAR */
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, set_h, w,
              &to_par_n, &to_par_d)) {
        GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (NULL),
            ("Error calculating the output scaled size - integer overflow"));
        gst_structure_free (tmp);
        goto done;
      }
      if (!gst_structure_has_field (tmp, "pixel-aspect-ratio"))
        gst_structure_set_value (tmp, "pixel-aspect-ratio", to_par);
      gst_structure_fixate_field_nearest_fraction (tmp, "pixel-aspect-ratio",
          to_par_n, to_par_d);
      gst_structure_get_fraction (tmp, "pixel-aspect-ratio", &set_par_n,
          &set_par_d);
      gst_structure_free (tmp);

      /* Check if the adjusted PAR is accepted */
      if (set_par_n == to_par_n && set_par_d == to_par_d) {
        if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
            set_par_n != set_par_d)
          gst_structure_set (outs, "height", G_TYPE_INT, set_h,
              "pixel-aspect-ratio", GST_TYPE_FRACTION, set_par_n, set_par_d,
              NULL);
        goto done;
      }

      /* Otherwise scale the height to the new PAR and check if the
       * adjusted with is accepted. If all that fails we can't keep
       * the DAR */
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, set_par_d,
              set_par_n, &num, &den)) {
        GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (NULL),
            ("Error calculating the output scale sized - integer overflow"));
        goto done;
      }

      h = (guint) gst_util_uint64_scale_int_round (w, den, num);
      gst_structure_fixate_field_nearest_int (outs, "height", h);
      if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
          set_par_n != set_par_d)
        gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
            set_par_n, set_par_d, NULL);

      goto done;
    } else if (gst_value_is_fixed (to_par)) {
      GstStructure *tmp;
      gint set_h, set_w, f_h, f_w;

      to_par_n = gst_value_get_fraction_numerator (to_par);
      to_par_d = gst_value_get_fraction_denominator (to_par);

      /* Calculate scale factor for the PAR change */
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, to_par_n,
              to_par_d, &num, &den)) {
        GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (NULL),
            ("Error calculating the output scaled size - integer overflow"));
        goto done;
      }

      /* Try to keep the input height (because of interlacing) */
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "height", from_h);
      gst_structure_get_int (tmp, "height", &set_h);

      /* This might have failed but try to scale the width
       * to keep the DAR nonetheless */
      w = (guint) gst_util_uint64_scale_int_round (set_h, num, den);
      gst_structure_fixate_field_nearest_int (tmp, "width", w);
      gst_structure_get_int (tmp, "width", &set_w);
      gst_structure_free (tmp);

      /* We kept the DAR and the height is nearest to the original height */
      if (set_w == w) {
        gst_structure_set (outs, "width", G_TYPE_INT, set_w, "height",
            G_TYPE_INT, set_h, NULL);
        goto done;
      }

      f_h = set_h;
      f_w = set_w;

      /* If the former failed, try to keep the input width at least */
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "width", from_w);
      gst_structure_get_int (tmp, "width", &set_w);

      /* This might have failed but try to scale the width
       * to keep the DAR nonetheless */
      h = (guint) gst_util_uint64_scale_int_round (set_w, den, num);
      gst_structure_fixate_field_nearest_int (tmp, "height", h);
      gst_structure_get_int (tmp, "height", &set_h);
      gst_structure_free (tmp);

      /* We kept the DAR and the width is nearest to the original width */
      if (set_h == h) {
        gst_structure_set (outs, "width", G_TYPE_INT, set_w, "height",
            G_TYPE_INT, set_h, NULL);
        goto done;
      }

      /* If all this failed, keep the dimensions with the DAR that was closest
       * to the correct DAR. This changes the DAR but there's not much else to
       * do here.
       */
      if (set_w * ABS (set_h - h) < ABS (f_w - w) * f_h) {
        f_h = set_h;
        f_w = set_w;
      }
      gst_structure_set (outs, "width", G_TYPE_INT, f_w, "height", G_TYPE_INT,
          f_h, NULL);
      goto done;
    } else {
      GstStructure *tmp;
      gint set_h, set_w, set_par_n, set_par_d, tmp2;

      /* width, height and PAR are not fixed but passthrough is not possible */

      /* First try to keep the height and width as good as possible
       * and scale PAR */
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "height", from_h);
      gst_structure_get_int (tmp, "height", &set_h);
      gst_structure_fixate_field_nearest_int (tmp, "width", from_w);
      gst_structure_get_int (tmp, "width", &set_w);

      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, set_h, set_w,
              &to_par_n, &to_par_d)) {
        GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (NULL),
            ("Error calculating the output scaled size - integer overflow"));
        gst_structure_free (tmp);
        goto done;
      }

      if (!gst_structure_has_field (tmp, "pixel-aspect-ratio"))
        gst_structure_set_value (tmp, "pixel-aspect-ratio", to_par);
      gst_structure_fixate_field_nearest_fraction (tmp, "pixel-aspect-ratio",
          to_par_n, to_par_d);
      gst_structure_get_fraction (tmp, "pixel-aspect-ratio", &set_par_n,
          &set_par_d);
      gst_structure_free (tmp);

      if (set_par_n == to_par_n && set_par_d == to_par_d) {
        gst_structure_set (outs, "width", G_TYPE_INT, set_w, "height",
            G_TYPE_INT, set_h, NULL);

        if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
            set_par_n != set_par_d)
          gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
              set_par_n, set_par_d, NULL);
        goto done;
      }

      /* Otherwise try to scale width to keep the DAR with the set
       * PAR and height */
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, set_par_d,
              set_par_n, &num, &den)) {
        GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (NULL),
            ("Error calculating the output scaled size - integer overflow"));
        goto done;
      }

      w = (guint) gst_util_uint64_scale_int_round (set_h, num, den);
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "width", w);
      gst_structure_get_int (tmp, "width", &tmp2);
      gst_structure_free (tmp);

      if (tmp2 == w) {
        gst_structure_set (outs, "width", G_TYPE_INT, tmp2, "height",
            G_TYPE_INT, set_h, NULL);
        if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
            set_par_n != set_par_d)
          gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
              set_par_n, set_par_d, NULL);
        goto done;
      }

      /* ... or try the same with the height */
      h = (guint) gst_util_uint64_scale_int_round (set_w, den, num);
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "height", h);
      gst_structure_get_int (tmp, "height", &tmp2);
      gst_structure_free (tmp);

      if (tmp2 == h) {
        gst_structure_set (outs, "width", G_TYPE_INT, set_w, "height",
            G_TYPE_INT, tmp2, NULL);
        if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
            set_par_n != set_par_d)
          gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
              set_par_n, set_par_d, NULL);
        goto done;
      }

      /* If all fails we can't keep the DAR and take the nearest values
       * for everything from the first try */
      gst_structure_set (outs, "width", G_TYPE_INT, set_w, "height",
          G_TYPE_INT, set_h, NULL);
      if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
          set_par_n != set_par_d)
        gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
            set_par_n, set_par_d, NULL);
    }
  }

done:
  if (from_par == &fpar)
    g_value_unset (&fpar);
  if (to_par == &tpar)
    g_value_unset (&tpar);

  return othercaps;
}

static GstCaps *
gst_amf_hq_scaler_fixate_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
{
  GST_DEBUG_OBJECT (trans,
      "trying to fixate othercaps %" GST_PTR_FORMAT " based on caps %"
      GST_PTR_FORMAT, othercaps, caps);

  othercaps = gst_amf_hq_scaler_fixate_size (trans, direction, caps, othercaps);

  GST_DEBUG_OBJECT (trans, "fixated othercaps to %" GST_PTR_FORMAT, othercaps);

  return othercaps;
}

static const wchar_t *
gst_amf_hq_scaler_get_component_id (GstAmfBaseFilter * self)
{
  return AMFHQScaler;
}

static gboolean
gst_amf_hq_scaler_validate_caps (GstAmfBaseFilter * filter,
    const GstVideoInfo * in_info, const GstVideoInfo * out_info)
{
  /* HQ scaler is upscaling-only, like ffmpeg's vf_sr_amf. */
  if (GST_VIDEO_INFO_WIDTH (in_info) > GST_VIDEO_INFO_WIDTH (out_info)
      || GST_VIDEO_INFO_HEIGHT (in_info) > GST_VIDEO_INFO_HEIGHT (out_info)) {
    GST_ERROR_OBJECT (filter,
        "AMF HQ scaler only supports upscaling: input %dx%d, output %dx%d",
        GST_VIDEO_INFO_WIDTH (in_info), GST_VIDEO_INFO_HEIGHT (in_info),
        GST_VIDEO_INFO_WIDTH (out_info), GST_VIDEO_INFO_HEIGHT (out_info));
    return FALSE;
  }

  return TRUE;
}

static gint
gst_amf_hq_scaler_resolve_algorithm (GstAmfHQScaler * self,
    const GstVideoInfo * in_info)
{
  gint algorithm = self->algorithm;

#ifdef G_OS_WIN32
  /* AMF HQ Scaler API: VideoSR 1.1 is not supported for NV12/P010. */
  GstVideoFormat fmt = GST_VIDEO_INFO_FORMAT (in_info);
  if ((fmt == GST_VIDEO_FORMAT_NV12 || fmt == GST_VIDEO_FORMAT_P010_10LE)
      && algorithm == AMF_HQ_SCALER_ALGORITHM_VIDEOSR1_1) {
    GST_WARNING_OBJECT (self,
        "Video SR 1.1 is not supported for %s; using Video SR 1.0",
        gst_video_format_to_string (fmt));
#else
  if (algorithm == AMF_HQ_SCALER_ALGORITHM_VIDEOSR1_1) {
    GST_WARNING_OBJECT (self,
        "Video SR 1.1 is supported on Windows DX11/DX12 only; using Video SR 1.0");
#endif
    algorithm = AMF_HQ_SCALER_ALGORITHM_VIDEOSR1_0;
  }

  return algorithm;
}

static gboolean
gst_amf_hq_scaler_from_srgb_from_info (const GstVideoInfo * in_info)
{
  if (!GST_VIDEO_INFO_IS_RGB (in_info))
    return FALSE;

  return in_info->colorimetry.transfer == GST_VIDEO_TRANSFER_SRGB;
}

static gboolean
gst_amf_hq_scaler_configure_component (GstAmfBaseFilter * filter,
    AMFComponent * comp, const GstVideoInfo * in_info,
    const GstVideoInfo * out_info)
{
  GstAmfHQScaler *self = GST_AMF_HQS (filter);
  AMFSize out_size;
  AMF_RESULT result;
  gint algorithm;
  gboolean from_srgb;

  algorithm = gst_amf_hq_scaler_resolve_algorithm (self, in_info);
  from_srgb = gst_amf_hq_scaler_from_srgb_from_info (in_info);

  if (gst_amf_base_filter_get_device (filter)) {
    result = comp->SetProperty (AMF_HQ_SCALER_ENGINE_TYPE,
#ifdef G_OS_WIN32
        (amf_int64) amf::AMF_MEMORY_DX11);
#else
        (amf_int64) amf::AMF_MEMORY_VULKAN);
#endif
    if (result != AMF_OK)
      GST_WARNING_OBJECT (self, "Failed to set HQ scaler engine type");
  }

  out_size.width = GST_VIDEO_INFO_WIDTH (out_info);
  out_size.height = GST_VIDEO_INFO_HEIGHT (out_info);
  result = comp->SetProperty (AMF_HQ_SCALER_OUTPUT_SIZE, out_size);
  if (result != AMF_OK)
    GST_WARNING_OBJECT (self, "Failed to set output size");

  result = comp->SetProperty (AMF_HQ_SCALER_ALGORITHM, (amf_int64) algorithm);
  if (result != AMF_OK)
    GST_WARNING_OBJECT (self, "Failed to set algorithm");

  result = comp->SetProperty (AMF_HQ_SCALER_SHARPNESS,
      (amf_double) self->sharpness);
  if (result != AMF_OK)
    GST_WARNING_OBJECT (self, "Failed to set sharpness");

  result = comp->SetProperty (AMF_HQ_SCALER_KEEP_ASPECT_RATIO,
      (amf_bool) self->add_borders);
  if (result != AMF_OK)
    GST_WARNING_OBJECT (self, "Failed to set keep-aspect-ratio");

  result = comp->SetProperty (AMF_HQ_SCALER_FILL, (amf_bool) self->add_borders);
  if (result != AMF_OK)
    GST_WARNING_OBJECT (self, "Failed to set fill");

  if (self->add_borders) {
    AMFColor color;
    color.r = (self->border_color >> 16) & 0xff;
    color.g = (self->border_color >> 8) & 0xff;
    color.b = (self->border_color >> 0) & 0xff;
    color.a = (self->border_color >> 24) & 0xff;
    result = comp->SetProperty (AMF_HQ_SCALER_FILL_COLOR, color);
    if (result != AMF_OK)
      GST_WARNING_OBJECT (self, "Failed to set border-color");
  }

  result = comp->SetProperty (AMF_HQ_SCALER_FROM_SRGB, (amf_bool) from_srgb);
  if (result != AMF_OK)
    GST_WARNING_OBJECT (self, "Failed to set FromSRGB");

  return TRUE;
}

static GstCaps *
gst_amf_hq_scaler_build_template_caps (AMFComponent * comp, gboolean is_input)
{
  GstCaps *caps;
  AMFCapsPtr amf_caps;
  AMFIOCapsPtr io_caps;
  AMF_RESULT result;
  amf_int32 min_w, max_w;
  amf_int32 min_h, max_h;
  std::string formats;

  result = comp->GetCaps (&amf_caps);
  if (result != AMF_OK)
    return nullptr;

  if (is_input)
    result = amf_caps->GetInputCaps (&io_caps);
  else
    result = amf_caps->GetOutputCaps (&io_caps);
  if (result != AMF_OK)
    return nullptr;

  io_caps->GetWidthRange (&min_w, &max_w);
  io_caps->GetHeightRange (&min_h, &max_h);

  if (min_w <= 0 || min_h <= 0 || max_w <= 0 || max_h <= 0)
    return nullptr;

  {
    amf_int32 num_fmt = io_caps->GetNumOfFormats ();
    gboolean first = TRUE;
    formats = "{ ";
    for (amf_int32 i = 0; i < num_fmt; i++) {
      AMF_SURFACE_FORMAT fmt;
      amf_bool native;
      const char *name = nullptr;

      if (io_caps->GetFormatAt (i, &fmt, &native) != AMF_OK)
        continue;

      switch (fmt) {
        case AMF_SURFACE_NV12:
          name = "NV12";
          break;
        case AMF_SURFACE_P010:
          name = "P010_10LE";
          break;
        case AMF_SURFACE_BGRA:
          name = "BGRA";
          break;
        case AMF_SURFACE_RGBA:
          name = "RGBA";
          break;
        default:
          break;
      }
      if (!name)
        continue;
      if (!first)
        formats += ", ";
      formats += name;
      first = FALSE;
    }
    if (first)
      return nullptr;
    formats += " }";
  }

  {
    gchar *caps_str = g_strdup_printf ("video/x-raw, format = %s, "
        "width = (int) [ %d, %d ], height = (int) [ %d, %d ]",
        formats.c_str (), min_w, max_w, min_h, max_h);
    caps = gst_caps_from_string (caps_str);
    g_free (caps_str);
  }
  if (!caps)
    return nullptr;

#ifdef G_OS_WIN32
  {
    GstCaps *d3d11_caps = gst_caps_copy (caps);
    for (guint j = 0; j < gst_caps_get_size (d3d11_caps); j++) {
      gst_caps_set_features (d3d11_caps, j,
          gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY, NULL));
    }
    gst_caps_append (d3d11_caps, caps);
    return d3d11_caps;
  }
#else
  return caps;
#endif
}

void
gst_amf_hq_scaler_register (GstPlugin * plugin, GstObject * device,
    gpointer context, guint rank)
{
  AMFContext *amf_context = (AMFContext *) context;
  AMFFactory *factory = (AMFFactory *) gst_amf_get_factory ();
  AMFComponentPtr comp;
  AMF_RESULT result;
  GstAmfHQScalerClassData *cdata;

  GST_DEBUG_CATEGORY_INIT (gst_amf_hq_scaler_debug, "amfhqscaler", 0,
      "amfhqscaler");

  if (!factory)
    return;

  result = factory->CreateComponent (amf_context, AMFHQScaler, &comp);
  if (result != AMF_OK) {
    GST_WARNING_OBJECT (device,
        "Failed to create AMFHQScaler for registration probe, result %"
        GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
    return;
  }
#ifndef G_OS_WIN32
  /* AMF defaults HQ scaler engine to D3D11; Linux AMF contexts are Vulkan-only.
   * Hint the engine immediately so subsequent capability queries / Init in real
   * use line up with the underlying Vulkan AMF context. */
  result = comp->SetProperty (AMF_HQ_SCALER_ENGINE_TYPE,
      (amf_int64) AMF_MEMORY_VULKAN);
  if (result != AMF_OK) {
    GST_WARNING_OBJECT (device,
        "Failed to hint AMF_HQ_SCALER_ENGINE_TYPE = Vulkan on probe component, result %"
        GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
  }
#endif

  cdata = g_new0 (GstAmfHQScalerClassData, 1);
#ifdef G_OS_WIN32
  if (GST_IS_D3D11_DEVICE (device)) {
    GstD3D11Device *d3ddev = GST_D3D11_DEVICE (device);
    g_object_get (d3ddev, "adapter-luid", &cdata->adapter_luid, nullptr);
  }
#endif

  cdata->sink_caps =
      gst_amf_hq_scaler_build_template_caps (comp.GetPtr (), TRUE);
  cdata->src_caps =
      gst_amf_hq_scaler_build_template_caps (comp.GetPtr (), FALSE);

  if (!cdata->sink_caps || !cdata->src_caps) {
    GST_WARNING_OBJECT (device, "Failed to build HQ scaler template caps");
    if (cdata->sink_caps)
      gst_caps_unref (cdata->sink_caps);
    if (cdata->src_caps)
      gst_caps_unref (cdata->src_caps);
    g_free (cdata);
    return;
  }

  GST_MINI_OBJECT_FLAG_SET (cdata->sink_caps,
      GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);
  GST_MINI_OBJECT_FLAG_SET (cdata->src_caps,
      GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);

  GType type;
  gchar *type_name = g_strdup ("GstAmfHQScaler");
  gchar *feature_name = g_strdup ("amfhqscaler");
  gint index = 0;

  GTypeInfo type_info = {
    sizeof (GstAmfHQScalerClass),
    nullptr,
    nullptr,
    (GClassInitFunc) gst_amf_hq_scaler_class_init,
    nullptr,
    cdata,
    sizeof (GstAmfHQScaler),
    0,
    (GInstanceInitFunc) gst_amf_hq_scaler_init,
  };

  while (g_type_from_name (type_name)) {
    index++;
    g_free (type_name);
    g_free (feature_name);
    type_name = g_strdup_printf ("GstAmfDevice%dHQScaler", index);
    feature_name = g_strdup_printf ("amfdevice%dhqscaler", index);
  }

  type = g_type_register_static (GST_TYPE_AMF_BASE_FILTER, type_name,
      &type_info, (GTypeFlags) 0);

  if (rank > 0 && index != 0)
    rank--;

  if (index != 0)
    gst_element_type_set_skip_documentation (type);

  if (!gst_element_register (plugin, feature_name, rank, type))
    GST_WARNING_OBJECT (device, "Failed to register element '%s'",
        feature_name);

  g_free (type_name);
  g_free (feature_name);
}
