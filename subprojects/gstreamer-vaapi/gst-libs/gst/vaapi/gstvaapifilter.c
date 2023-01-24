/*
 *  gstvaapifilter.c - Video processing abstraction
 *
 *  Copyright (C) 2013-2014 Intel Corporation
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

#include "config.h"

#include "sysdeps.h"
#include "gstvaapicompat.h"
#include "gstvaapifilter.h"
#include "gstvaapiutils.h"
#include "gstvaapivalue.h"
#include "gstvaapiminiobject.h"
#include "gstvaapidisplay_priv.h"
#include "gstvaapisurface_priv.h"
#include "gstvaapiutils_core.h"

#define GST_VAAPI_FILTER_CAST(obj) \
    ((GstVaapiFilter *)(obj))

typedef struct _GstVaapiFilterOpData GstVaapiFilterOpData;
struct _GstVaapiFilterOpData
{
  GstVaapiFilterOp op;
  GParamSpec *pspec;
  gint ref_count;
  guint va_type;
  guint va_subtype;
  gpointer va_caps;
  guint va_num_caps;
  guint va_cap_size;
  VABufferID va_buffer;
  guint va_buffer_size;
  guint is_enabled:1;
};

struct _GstVaapiFilter
{
  /*< private > */
  GstObject parent_instance;

  GstVaapiDisplay *display;
  VADisplay va_display;
  VAConfigID va_config;
  VAContextID va_context;
  GPtrArray *operations;
  GstVideoFormat format;
  GstVaapiScaleMethod scale_method;
  GstVideoOrientationMethod video_direction;
  GstVaapiConfigSurfaceAttributes *attribs;
  GArray *forward_references;
  GArray *backward_references;
  GstVaapiRectangle crop_rect;
  GstVaapiRectangle target_rect;
  guint use_crop_rect:1;
  guint use_target_rect:1;
  guint32 mirror_flags;
  guint32 rotation_flags;

  GstVideoColorimetry input_colorimetry;
  GstVideoColorimetry output_colorimetry;

#if VA_CHECK_VERSION(1,4,0)
  VAHdrMetaDataHDR10 hdr_meta;
#endif
};

typedef struct _GstVaapiFilterClass GstVaapiFilterClass;
struct _GstVaapiFilterClass
{
  /*< private > */
  GstObjectClass parent_class;
};

/* Debug category for VaapiFilter */
GST_DEBUG_CATEGORY (gst_debug_vaapi_filter);
#define GST_CAT_DEFAULT gst_debug_vaapi_filter

#define _do_init                                                       \
    GST_DEBUG_CATEGORY_INIT (gst_debug_vaapi_filter, "vaapifilter", 0, \
    "VA-API Filter");

G_DEFINE_TYPE_WITH_CODE (GstVaapiFilter, gst_vaapi_filter, GST_TYPE_OBJECT,
    _do_init);

/* ------------------------------------------------------------------------- */
/* --- VPP Types                                                         --- */
/* ------------------------------------------------------------------------- */

static GType
gst_vaapi_scale_method_get_type (void)
{
  static gsize g_type = 0;

  static const GEnumValue enum_values[] = {
    {GST_VAAPI_SCALE_METHOD_DEFAULT,
        "Default scaling mode", "default"},
    {GST_VAAPI_SCALE_METHOD_FAST,
        "Fast scaling mode", "fast"},
    {GST_VAAPI_SCALE_METHOD_HQ,
        "High quality scaling mode", "hq"},
    {0, NULL, NULL},
  };

  if (g_once_init_enter (&g_type)) {
    const GType type =
        g_enum_register_static ("GstVaapiScaleMethod", enum_values);
    g_once_init_leave (&g_type, type);

    gst_type_mark_as_plugin_api (type, 0);
  }
  return g_type;
}

GType
gst_vaapi_deinterlace_method_get_type (void)
{
  static gsize g_type = 0;

  static const GEnumValue enum_values[] = {
    {GST_VAAPI_DEINTERLACE_METHOD_NONE,
        "Disable deinterlacing", "none"},
    {GST_VAAPI_DEINTERLACE_METHOD_BOB,
        "Bob deinterlacing", "bob"},
    {GST_VAAPI_DEINTERLACE_METHOD_WEAVE,
        "Weave deinterlacing", "weave"},
    {GST_VAAPI_DEINTERLACE_METHOD_MOTION_ADAPTIVE,
        "Motion adaptive deinterlacing", "motion-adaptive"},
    {GST_VAAPI_DEINTERLACE_METHOD_MOTION_COMPENSATED,
        "Motion compensated deinterlacing", "motion-compensated"},
    {0, NULL, NULL},
  };

  if (g_once_init_enter (&g_type)) {
    const GType type =
        g_enum_register_static ("GstVaapiDeinterlaceMethod", enum_values);
    gst_type_mark_as_plugin_api (type, 0);
    g_once_init_leave (&g_type, type);
  }
  return g_type;
}

GType
gst_vaapi_deinterlace_flags_get_type (void)
{
  static gsize g_type = 0;

  static const GEnumValue enum_values[] = {
    {GST_VAAPI_DEINTERLACE_FLAG_TFF,
        "Top-field first", "top-field-first"},
    {GST_VAAPI_DEINTERLACE_FLAG_ONEFIELD,
        "One field", "one-field"},
    {GST_VAAPI_DEINTERLACE_FLAG_TOPFIELD,
        "Top field", "top-field"},
    {0, NULL, NULL}
  };

  if (g_once_init_enter (&g_type)) {
    const GType type =
        g_enum_register_static ("GstVaapiDeinterlaceFlags", enum_values);
    gst_type_mark_as_plugin_api (type, 0);
    g_once_init_leave (&g_type, type);
  }
  return g_type;
}

/* ------------------------------------------------------------------------- */
/* --- VPP Helpers                                                       --- */
/* ------------------------------------------------------------------------- */

static VAProcFilterType *
vpp_get_filters_unlocked (GstVaapiFilter * filter, guint * num_filters_ptr)
{
  VAProcFilterType *filters = NULL;
  guint num_filters = 0;
  VAStatus va_status;

  num_filters = VAProcFilterCount;
  filters = g_malloc_n (num_filters, sizeof (*filters));
  if (!filters)
    goto error;

  va_status = vaQueryVideoProcFilters (filter->va_display, filter->va_context,
      filters, &num_filters);

  // Try to reallocate to the expected number of filters
  if (va_status == VA_STATUS_ERROR_MAX_NUM_EXCEEDED) {
    VAProcFilterType *const new_filters =
        g_try_realloc_n (filters, num_filters, sizeof (*new_filters));
    if (!new_filters)
      goto error;
    filters = new_filters;

    va_status = vaQueryVideoProcFilters (filter->va_display,
        filter->va_context, filters, &num_filters);
  }
  if (!vaapi_check_status (va_status, "vaQueryVideoProcFilters()"))
    goto error;

  *num_filters_ptr = num_filters;
  return filters;

  /* ERRORS */
error:
  {
    g_free (filters);
    return NULL;
  }
}

static VAProcFilterType *
vpp_get_filters (GstVaapiFilter * filter, guint * num_filters_ptr)
{
  VAProcFilterType *filters;

  GST_VAAPI_DISPLAY_LOCK (filter->display);
  filters = vpp_get_filters_unlocked (filter, num_filters_ptr);
  GST_VAAPI_DISPLAY_UNLOCK (filter->display);
  return filters;
}

static gpointer
vpp_get_filter_caps_unlocked (GstVaapiFilter * filter, VAProcFilterType type,
    guint cap_size, guint * num_caps_ptr)
{
  gpointer caps;
  guint num_caps = 1;
  VAStatus va_status;

  caps = g_malloc (cap_size);
  if (!caps)
    goto error;

  va_status = vaQueryVideoProcFilterCaps (filter->va_display,
      filter->va_context, type, caps, &num_caps);

  // Try to reallocate to the expected number of filters
  if (va_status == VA_STATUS_ERROR_MAX_NUM_EXCEEDED) {
    gpointer const new_caps = g_try_realloc_n (caps, num_caps, cap_size);
    if (!new_caps)
      goto error;
    caps = new_caps;

    va_status = vaQueryVideoProcFilterCaps (filter->va_display,
        filter->va_context, type, caps, &num_caps);
  }
  if (!vaapi_check_status (va_status, "vaQueryVideoProcFilterCaps()"))
    goto error;

  *num_caps_ptr = num_caps;
  return caps;

  /* ERRORS */
error:
  {
    g_free (caps);
    return NULL;
  }
}

static gpointer
vpp_get_filter_caps (GstVaapiFilter * filter, VAProcFilterType type,
    guint cap_size, guint * num_caps_ptr)
{
  gpointer caps;

  GST_VAAPI_DISPLAY_LOCK (filter->display);
  caps = vpp_get_filter_caps_unlocked (filter, type, cap_size, num_caps_ptr);
  GST_VAAPI_DISPLAY_UNLOCK (filter->display);
  return caps;
}

static void
vpp_get_pipeline_caps_unlocked (GstVaapiFilter * filter)
{
#if VA_CHECK_VERSION(1,1,0)
  VAProcPipelineCaps pipeline_caps = { 0, };

  VAStatus va_status = vaQueryVideoProcPipelineCaps (filter->va_display,
      filter->va_context, NULL, 0, &pipeline_caps);

  if (vaapi_check_status (va_status, "vaQueryVideoProcPipelineCaps()")) {
    filter->mirror_flags = pipeline_caps.mirror_flags;
    filter->rotation_flags = pipeline_caps.rotation_flags;
    return;
  }
#endif

  filter->mirror_flags = 0;
  filter->rotation_flags = 0;
}

static void
vpp_get_pipeline_caps (GstVaapiFilter * filter)
{
  GST_VAAPI_DISPLAY_LOCK (filter->display);
  vpp_get_pipeline_caps_unlocked (filter);
  GST_VAAPI_DISPLAY_UNLOCK (filter->display);
}

/* ------------------------------------------------------------------------- */
/* --- VPP Operations                                                   --- */
/* ------------------------------------------------------------------------- */

#define DEFAULT_FORMAT  GST_VIDEO_FORMAT_UNKNOWN

#define OP_DATA_DEFAULT_VALUE(type, op_data) \
    g_value_get_##type (g_param_spec_get_default_value (op_data->pspec))

#define OP_RET_DEFAULT_VALUE(type, filter, op) \
    do { \
      g_return_val_if_fail (filter != NULL, FALSE); \
      return OP_DATA_DEFAULT_VALUE (type, find_operation (filter, op)); \
    } while (0)

enum
{
  PROP_DISPLAY = 1,
};

enum
{
  PROP_0,

  PROP_FORMAT = GST_VAAPI_FILTER_OP_FORMAT,
  PROP_CROP = GST_VAAPI_FILTER_OP_CROP,
  PROP_DENOISE = GST_VAAPI_FILTER_OP_DENOISE,
  PROP_SHARPEN = GST_VAAPI_FILTER_OP_SHARPEN,
  PROP_HUE = GST_VAAPI_FILTER_OP_HUE,
  PROP_SATURATION = GST_VAAPI_FILTER_OP_SATURATION,
  PROP_BRIGHTNESS = GST_VAAPI_FILTER_OP_BRIGHTNESS,
  PROP_CONTRAST = GST_VAAPI_FILTER_OP_CONTRAST,
  PROP_DEINTERLACING = GST_VAAPI_FILTER_OP_DEINTERLACING,
  PROP_SCALING = GST_VAAPI_FILTER_OP_SCALING,
  PROP_VIDEO_DIRECTION = GST_VAAPI_FILTER_OP_VIDEO_DIRECTION,
  PROP_HDR_TONE_MAP = GST_VAAPI_FILTER_OP_HDR_TONE_MAP,
#ifndef GST_REMOVE_DEPRECATED
  PROP_SKINTONE = GST_VAAPI_FILTER_OP_SKINTONE,
#endif
  PROP_SKINTONE_LEVEL = GST_VAAPI_FILTER_OP_SKINTONE_LEVEL,

  N_PROPERTIES
};

static GParamSpec *g_properties[N_PROPERTIES] = { NULL, };

static gsize g_properties_initialized = FALSE;

static void
init_properties (void)
{
  /**
   * GstVaapiFilter:format:
   *
   * The forced output pixel format, expressed as a #GstVideoFormat.
   */
  g_properties[PROP_FORMAT] = g_param_spec_enum ("format",
      "Format",
      "The forced output pixel format",
      GST_TYPE_VIDEO_FORMAT,
      DEFAULT_FORMAT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  /**
   * GstVaapiFilter:crop-rect:
   *
   * The cropping rectangle, expressed as a #GstVaapiRectangle.
   */
  g_properties[PROP_CROP] = g_param_spec_boxed ("crop-rect",
      "Cropping Rectangle",
      "The cropping rectangle",
      GST_VAAPI_TYPE_RECTANGLE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  /**
   * GstVaapiFilter:denoise:
   *
   * The level of noise reduction to apply.
   */
  g_properties[PROP_DENOISE] = g_param_spec_float ("denoise",
      "Denoising Level",
      "The level of denoising to apply",
      0.0, 1.0, 0.0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  /**
   * GstVaapiFilter:sharpen:
   *
   * The level of sharpening to apply for positive values, or the
   * level of blurring for negative values.
   */
  g_properties[PROP_SHARPEN] = g_param_spec_float ("sharpen",
      "Sharpening Level",
      "The level of sharpening/blurring to apply",
      -1.0, 1.0, 0.0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  /**
   * GstVaapiFilter:hue:
   *
   * The color hue, expressed as a float value. Range is -180.0 to
   * 180.0. Default value is 0.0 and represents no modification.
   */
  g_properties[PROP_HUE] = g_param_spec_float ("hue",
      "Hue",
      "The color hue value",
      -180.0, 180.0, 0.0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  /**
   * GstVaapiFilter:saturation:
   *
   * The color saturation, expressed as a float value. Range is 0.0 to
   * 2.0. Default value is 1.0 and represents no modification.
   */
  g_properties[PROP_SATURATION] = g_param_spec_float ("saturation",
      "Saturation",
      "The color saturation value",
      0.0, 2.0, 1.0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  /**
   * GstVaapiFilter:brightness:
   *
   * The color brightness, expressed as a float value. Range is -1.0
   * to 1.0. Default value is 0.0 and represents no modification.
   */
  g_properties[PROP_BRIGHTNESS] = g_param_spec_float ("brightness",
      "Brightness",
      "The color brightness value",
      -1.0, 1.0, 0.0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  /**
   * GstVaapiFilter:contrast:
   *
   * The color contrast, expressed as a float value. Range is 0.0 to
   * 2.0. Default value is 1.0 and represents no modification.
   */
  g_properties[PROP_CONTRAST] = g_param_spec_float ("contrast",
      "Contrast",
      "The color contrast value",
      0.0, 2.0, 1.0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  /**
   * GstVaapiFilter:deinterlace-method:
   *
   * The deinterlacing algorithm to apply, expressed a an enum
   * value. See #GstVaapiDeinterlaceMethod.
   */
  g_properties[PROP_DEINTERLACING] = g_param_spec_enum ("deinterlace",
      "Deinterlacing Method",
      "Deinterlacing method to apply",
      GST_VAAPI_TYPE_DEINTERLACE_METHOD,
      GST_VAAPI_DEINTERLACE_METHOD_NONE,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  /**
   * GstVaapiFilter:scale-method:
   *
   * The scaling method to use, expressed as an enum value. See
   * #GstVaapiScaleMethod.
   */
  g_properties[PROP_SCALING] = g_param_spec_enum ("scale-method",
      "Scaling Method",
      "Scaling method to use",
      GST_VAAPI_TYPE_SCALE_METHOD,
      GST_VAAPI_SCALE_METHOD_DEFAULT,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  /**
   * GstVaapiFilter:video-direction:
   *
   * The video-direction to use, expressed as an enum value. See
   * #GstVideoOrientationMethod.
   */
  g_properties[PROP_VIDEO_DIRECTION] = g_param_spec_enum ("video-direction",
      "Video Direction",
      "Video direction: rotation and flipping",
      GST_TYPE_VIDEO_ORIENTATION_METHOD,
      GST_VIDEO_ORIENTATION_IDENTITY,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  /**
   * GstVaapiFilter:tone-map:
   *
   * Apply HDR tone mapping
   **/
  g_properties[PROP_HDR_TONE_MAP] = g_param_spec_boolean ("hdr-tone-map",
      "HDR Tone Mapping",
      "Apply HDR tone mapping",
      FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

#ifndef GST_REMOVE_DEPRECATED
  /**
   * GstVaapiFilter:skin-tone-enhancement:
   *
   * Apply the skin tone enhancement algorithm.
   */
  g_properties[PROP_SKINTONE] = g_param_spec_boolean ("skin-tone-enhancement",
      "Skin tone enhancement",
      "Apply the skin tone enhancement algorithm",
      FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
#endif

  /**
   * GstVaapiFilter:skin-tone-enhancement-level:
   *
   * Apply the skin tone enhancement algorithm with specified value.
   */
  g_properties[PROP_SKINTONE_LEVEL] =
      g_param_spec_uint ("skin-tone-enhancement-level",
      "Skin tone enhancement level",
      "Apply the skin tone enhancement algorithm with specified level", 0, 9, 3,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
}

static void
ensure_properties (void)
{
  if (g_once_init_enter (&g_properties_initialized)) {
    init_properties ();
    g_once_init_leave (&g_properties_initialized, TRUE);
  }
}

static void
op_data_free (GstVaapiFilterOpData * op_data)
{
  g_free (op_data->va_caps);
  g_free (op_data);
}

static inline gpointer
op_data_new (GstVaapiFilterOp op, GParamSpec * pspec)
{
  GstVaapiFilterOpData *op_data;

  op_data = g_new0 (GstVaapiFilterOpData, 1);
  if (!op_data)
    return NULL;

  op_data->op = op;
  op_data->pspec = pspec;
  g_atomic_int_set (&op_data->ref_count, 1);
  op_data->va_buffer = VA_INVALID_ID;

  switch (op) {
    case GST_VAAPI_FILTER_OP_HDR_TONE_MAP:
#if VA_CHECK_VERSION(1,4,0)
      /* Only HDR10 tone mapping is supported */
      op_data->va_type = VAProcFilterHighDynamicRangeToneMapping;
      op_data->va_subtype = VAProcHighDynamicRangeMetadataHDR10;
      op_data->va_cap_size = sizeof (VAProcFilterCapHighDynamicRange);
      op_data->va_buffer_size =
          sizeof (VAProcFilterParameterBufferHDRToneMapping);
      break;
#else
      /* fall-through */
#endif
    case GST_VAAPI_FILTER_OP_FORMAT:
    case GST_VAAPI_FILTER_OP_CROP:
    case GST_VAAPI_FILTER_OP_SCALING:
    case GST_VAAPI_FILTER_OP_VIDEO_DIRECTION:
      op_data->va_type = VAProcFilterNone;
      break;
    case GST_VAAPI_FILTER_OP_DENOISE:
      op_data->va_type = VAProcFilterNoiseReduction;
      op_data->va_cap_size = sizeof (VAProcFilterCap);
      op_data->va_buffer_size = sizeof (VAProcFilterParameterBuffer);
      break;
    case GST_VAAPI_FILTER_OP_SHARPEN:
      op_data->va_type = VAProcFilterSharpening;
      op_data->va_cap_size = sizeof (VAProcFilterCap);
      op_data->va_buffer_size = sizeof (VAProcFilterParameterBuffer);
      break;
#ifndef GST_REMOVE_DEPRECATED
    case GST_VAAPI_FILTER_OP_SKINTONE:
#endif
    case GST_VAAPI_FILTER_OP_SKINTONE_LEVEL:
      op_data->va_type = VAProcFilterSkinToneEnhancement;
      op_data->va_buffer_size = sizeof (VAProcFilterParameterBuffer);
      break;
    case GST_VAAPI_FILTER_OP_HUE:
      op_data->va_subtype = VAProcColorBalanceHue;
      goto op_colorbalance;
    case GST_VAAPI_FILTER_OP_SATURATION:
      op_data->va_subtype = VAProcColorBalanceSaturation;
      goto op_colorbalance;
    case GST_VAAPI_FILTER_OP_BRIGHTNESS:
      op_data->va_subtype = VAProcColorBalanceBrightness;
      goto op_colorbalance;
    case GST_VAAPI_FILTER_OP_CONTRAST:
      op_data->va_subtype = VAProcColorBalanceContrast;
    op_colorbalance:
      op_data->va_type = VAProcFilterColorBalance;
      op_data->va_cap_size = sizeof (VAProcFilterCapColorBalance);
      op_data->va_buffer_size =
          sizeof (VAProcFilterParameterBufferColorBalance);
      break;
    case GST_VAAPI_FILTER_OP_DEINTERLACING:
      op_data->va_type = VAProcFilterDeinterlacing;
      op_data->va_cap_size = sizeof (VAProcFilterCapDeinterlacing);
      op_data->va_buffer_size =
          sizeof (VAProcFilterParameterBufferDeinterlacing);
      break;
    default:
      g_assert (0 && "unsupported operation");
      goto error;
  }
  return op_data;

  /* ERRORS */
error:
  {
    op_data_free (op_data);
    return NULL;
  }
}

static inline gpointer
op_data_ref (gpointer data)
{
  GstVaapiFilterOpData *const op_data = data;

  g_return_val_if_fail (op_data != NULL, NULL);

  g_atomic_int_inc (&op_data->ref_count);
  return op_data;
}

static void
op_data_unref (gpointer data)
{
  GstVaapiFilterOpData *const op_data = data;

  g_return_if_fail (op_data != NULL);
  g_return_if_fail (op_data->ref_count > 0);

  if (g_atomic_int_dec_and_test (&op_data->ref_count))
    op_data_free (op_data);
}

/* Ensure capability info is set up for the VA filter we are interested in */
static gboolean
op_data_ensure_caps (GstVaapiFilterOpData * op_data, gpointer filter_caps,
    guint num_filter_caps)
{
  guchar *filter_cap = filter_caps;
  guint i, va_num_caps = num_filter_caps;

  // Find the VA filter cap matching the op info sub-type
  if (op_data->va_subtype) {
    for (i = 0; i < num_filter_caps; i++) {
      /* XXX: sub-type shall always be the first field */
      if (op_data->va_subtype == *(guint *) filter_cap) {
        va_num_caps = 1;
        break;
      }
      filter_cap += op_data->va_cap_size;
    }
    if (i == num_filter_caps)
      return FALSE;
  }

  op_data->va_caps = g_memdup2 (filter_cap, op_data->va_cap_size * va_num_caps);
  if (!op_data->va_caps)
    return FALSE;

  op_data->va_num_caps = va_num_caps;
  return TRUE;
}

/* Scale the filter value wrt. library spec and VA driver spec */
static gboolean
op_data_get_value_float (GstVaapiFilterOpData * op_data,
    const VAProcFilterValueRange * range, gfloat value, gfloat * out_value_ptr)
{
  GParamSpecFloat *const pspec = G_PARAM_SPEC_FLOAT (op_data->pspec);
  gfloat out_value;

  g_return_val_if_fail (range != NULL, FALSE);
  g_return_val_if_fail (out_value_ptr != NULL, FALSE);

  if (value < pspec->minimum || value > pspec->maximum)
    return FALSE;

  // Scale wrt. the medium ("default") value
  out_value = range->default_value;
  if (value > pspec->default_value)
    out_value += ((value - pspec->default_value) /
        (pspec->maximum - pspec->default_value) *
        (range->max_value - range->default_value));
  else if (value < pspec->default_value)
    out_value -= ((pspec->default_value - value) /
        (pspec->default_value - pspec->minimum) *
        (range->default_value - range->min_value));

  *out_value_ptr = out_value;
  return TRUE;
}

/* Get default list of operations supported by the library */
static GPtrArray *
get_operations_default (void)
{
  GPtrArray *ops;
  guint i;

  ops = g_ptr_array_new_full (N_PROPERTIES, op_data_unref);
  if (!ops)
    return NULL;

  ensure_properties ();

  for (i = 0; i < N_PROPERTIES; i++) {
    GstVaapiFilterOpData *op_data;
    GParamSpec *const pspec = g_properties[i];
    if (!pspec)
      continue;

    op_data = op_data_new (i, pspec);
    if (!op_data)
      goto error;
    g_ptr_array_add (ops, op_data);
  }
  return ops;

  /* ERRORS */
error:
  {
    g_ptr_array_unref (ops);
    return NULL;
  }
}

/* Get the ordered list of operations, based on VA/VPP queries */
static GPtrArray *
get_operations_ordered (GstVaapiFilter * filter, GPtrArray * default_ops)
{
  GPtrArray *ops;
  VAProcFilterType *filters;
  gpointer filter_caps = NULL;
  guint i, j, num_filters, num_filter_caps = 0;

  ops = g_ptr_array_new_full (default_ops->len, op_data_unref);
  if (!ops)
    return NULL;

  filters = vpp_get_filters (filter, &num_filters);
  if (!filters)
    goto error;

  // Append virtual ops first, i.e. those without an associated VA filter
  for (i = 0; i < default_ops->len; i++) {
    GstVaapiFilterOpData *const op_data = g_ptr_array_index (default_ops, i);
    if (op_data->va_type == VAProcFilterNone)
      g_ptr_array_add (ops, op_data_ref (op_data));
  }

  // Append ops, while preserving the VA filters ordering
  for (i = 0; i < num_filters; i++) {
    const VAProcFilterType va_type = filters[i];
    if (va_type == VAProcFilterNone)
      continue;

    for (j = 0; j < default_ops->len; j++) {
      GstVaapiFilterOpData *const op_data = g_ptr_array_index (default_ops, j);
      if (op_data->va_type != va_type)
        continue;

      if (op_data->va_cap_size == 0) {  /* no caps, like skintone */
        g_ptr_array_add (ops, op_data_ref (op_data));
        continue;
      }

      if (!filter_caps) {
        filter_caps = vpp_get_filter_caps (filter, va_type,
            op_data->va_cap_size, &num_filter_caps);
        if (!filter_caps)
          continue;
      }

      if (!op_data_ensure_caps (op_data, filter_caps, num_filter_caps))
        continue;

      g_ptr_array_add (ops, op_data_ref (op_data));
    }
    free (filter_caps);
    filter_caps = NULL;
  }

  vpp_get_pipeline_caps (filter);

  if (filter->operations)
    g_ptr_array_unref (filter->operations);
  filter->operations = g_ptr_array_ref (ops);

  g_free (filters);
  g_ptr_array_unref (default_ops);
  return ops;

  /* ERRORS */
error:
  {
    g_free (filter_caps);
    g_free (filters);
    g_ptr_array_unref (ops);
    g_ptr_array_unref (default_ops);
    return NULL;
  }
}

/* Determine the set of supported VPP operations by the specific
   filter, or known to this library if filter is NULL */
static GPtrArray *
get_operations (GstVaapiFilter * filter)
{
  GPtrArray *ops;

  if (filter && filter->operations)
    return g_ptr_array_ref (filter->operations);

  ops = get_operations_default ();
  if (!ops)
    return NULL;
  return filter ? get_operations_ordered (filter, ops) : ops;
}

/* Ensure the set of supported VPP operations is cached into the
   GstVaapiFilter::operations member */
static inline gboolean
ensure_operations (GstVaapiFilter * filter)
{
  GPtrArray *ops;

  if (!filter)
    return FALSE;

  if (filter->operations)
    return TRUE;

  ops = get_operations (filter);
  if (!ops)
    return FALSE;

  g_ptr_array_unref (ops);
  return TRUE;
}

/* Find whether the VPP operation is supported or not */
static GstVaapiFilterOpData *
find_operation (GstVaapiFilter * filter, GstVaapiFilterOp op)
{
  guint i;

  if (!ensure_operations (filter))
    return NULL;

  for (i = 0; i < filter->operations->len; i++) {
    GstVaapiFilterOpData *const op_data =
        g_ptr_array_index (filter->operations, i);
    if (op_data->op == op)
      return op_data;
  }
  return NULL;
}

/* Ensure the operation's VA buffer is allocated */
static inline gboolean
op_ensure_n_elements_buffer (GstVaapiFilter * filter,
    GstVaapiFilterOpData * op_data, gint op_num)
{
  if (G_LIKELY (op_data->va_buffer != VA_INVALID_ID))
    return TRUE;
  return vaapi_create_n_elements_buffer (filter->va_display, filter->va_context,
      VAProcFilterParameterBufferType, op_data->va_buffer_size, NULL,
      &op_data->va_buffer, NULL, op_num);
}

static inline gboolean
op_ensure_buffer (GstVaapiFilter * filter, GstVaapiFilterOpData * op_data)
{
  return op_ensure_n_elements_buffer (filter, op_data, 1);
}

/* Update a generic filter (float value) */
static gboolean
op_set_generic_unlocked (GstVaapiFilter * filter,
    GstVaapiFilterOpData * op_data, gfloat value)
{
  VAProcFilterParameterBuffer *buf;
  VAProcFilterCap *filter_cap;
  gfloat va_value;

  if (!op_data || !op_ensure_buffer (filter, op_data))
    return FALSE;

  op_data->is_enabled = (value != OP_DATA_DEFAULT_VALUE (float, op_data));
  if (!op_data->is_enabled)
    return TRUE;

  filter_cap = op_data->va_caps;
  if (!op_data_get_value_float (op_data, &filter_cap->range, value, &va_value))
    return FALSE;

  buf = vaapi_map_buffer (filter->va_display, op_data->va_buffer);
  if (!buf)
    return FALSE;

  buf->type = op_data->va_type;
  buf->value = va_value;
  vaapi_unmap_buffer (filter->va_display, op_data->va_buffer, NULL);
  return TRUE;
}

static inline gboolean
op_set_generic (GstVaapiFilter * filter, GstVaapiFilterOpData * op_data,
    gfloat value)
{
  gboolean success = FALSE;

  GST_VAAPI_DISPLAY_LOCK (filter->display);
  success = op_set_generic_unlocked (filter, op_data, value);
  GST_VAAPI_DISPLAY_UNLOCK (filter->display);
  return success;
}

/* Update the color balance filter */
#define COLOR_BALANCE_NUM \
    GST_VAAPI_FILTER_OP_CONTRAST - GST_VAAPI_FILTER_OP_HUE + 1

static gboolean
op_set_color_balance_unlocked (GstVaapiFilter * filter,
    GstVaapiFilterOpData * op_data, gfloat value)
{
  VAProcFilterParameterBufferColorBalance *buf;
  VAProcFilterCapColorBalance *filter_cap;
  gfloat va_value;
  gint i;
  GstVaapiFilterOpData *color_data[COLOR_BALANCE_NUM];
  GstVaapiFilterOpData *enabled_data = NULL;
  gboolean ret = TRUE;

  if (!op_data)
    return FALSE;

  /* collect all the Color Balance operators and find the first
   * enabled one */
  for (i = 0; i < COLOR_BALANCE_NUM; i++) {
    color_data[i] = find_operation (filter, GST_VAAPI_FILTER_OP_HUE + i);
    if (!color_data[i])
      return FALSE;

    if (!enabled_data && color_data[i]->is_enabled)
      enabled_data = color_data[i];
  }

  /* If there's no enabled operators let's enable this one.
   *
   * HACK: This operator will be the only one with an allocated buffer
   * which will store all the color balance operators.
   */
  if (!enabled_data) {
    /* *INDENT-OFF* */
    if (value == OP_DATA_DEFAULT_VALUE (float, op_data))
      return TRUE;
    /* *INDENT-ON* */

    if (!op_ensure_n_elements_buffer (filter, op_data, COLOR_BALANCE_NUM))
      return FALSE;

    enabled_data = op_data;

    buf = vaapi_map_buffer (filter->va_display, enabled_data->va_buffer);
    if (!buf)
      return FALSE;

    /* Write all the color balance operator values in the buffer. --
     * Use the default value for all the operators except the set
     * one. */
    for (i = 0; i < COLOR_BALANCE_NUM; i++) {
      buf[i].type = color_data[i]->va_type;
      buf[i].attrib = color_data[i]->va_subtype;

      va_value = OP_DATA_DEFAULT_VALUE (float, color_data[i]);
      if (color_data[i]->op == op_data->op) {
        filter_cap = color_data[i]->va_caps;
        /* fail but ignore current value and set default one */
        if (!op_data_get_value_float (color_data[i], &filter_cap->range, value,
                &va_value))
          ret = FALSE;
      }

      buf[i].value = va_value;
    }

    enabled_data->is_enabled = 1;
  } else {
    /* There's already one operator enabled, *in theory* with a
     * buffer associated. */
    if (G_UNLIKELY (enabled_data->va_buffer == VA_INVALID_ID))
      return FALSE;

    filter_cap = op_data->va_caps;
    if (!op_data_get_value_float (op_data, &filter_cap->range, value,
            &va_value))
      return FALSE;

    buf = vaapi_map_buffer (filter->va_display, enabled_data->va_buffer);
    if (!buf)
      return FALSE;

    buf[op_data->op - GST_VAAPI_FILTER_OP_HUE].value = va_value;
  }

  vaapi_unmap_buffer (filter->va_display, enabled_data->va_buffer, NULL);

  return ret;
}

static inline gboolean
op_set_color_balance (GstVaapiFilter * filter, GstVaapiFilterOpData * op_data,
    gfloat value)
{
  gboolean success = FALSE;

  GST_VAAPI_DISPLAY_LOCK (filter->display);
  success = op_set_color_balance_unlocked (filter, op_data, value);
  GST_VAAPI_DISPLAY_UNLOCK (filter->display);
  return success;
}

/* Update deinterlace filter */
static gboolean
op_set_deinterlace_unlocked (GstVaapiFilter * filter,
    GstVaapiFilterOpData * op_data, GstVaapiDeinterlaceMethod method,
    guint flags)
{
  VAProcFilterParameterBufferDeinterlacing *buf;
  const VAProcFilterCapDeinterlacing *filter_caps;
  VAProcDeinterlacingType algorithm;
  guint i;

  if (!op_data || !op_ensure_buffer (filter, op_data))
    return FALSE;

  op_data->is_enabled = (method != GST_VAAPI_DEINTERLACE_METHOD_NONE);
  if (!op_data->is_enabled)
    return TRUE;

  algorithm = from_GstVaapiDeinterlaceMethod (method);
  for (i = 0, filter_caps = op_data->va_caps; i < op_data->va_num_caps; i++) {
    if (filter_caps[i].type == algorithm)
      break;
  }
  if (i == op_data->va_num_caps)
    return FALSE;

  buf = vaapi_map_buffer (filter->va_display, op_data->va_buffer);
  if (!buf)
    return FALSE;

  buf->type = op_data->va_type;
  buf->algorithm = algorithm;
  buf->flags = from_GstVaapiDeinterlaceFlags (flags);
  vaapi_unmap_buffer (filter->va_display, op_data->va_buffer, NULL);
  return TRUE;
}

static inline gboolean
op_set_deinterlace (GstVaapiFilter * filter, GstVaapiFilterOpData * op_data,
    GstVaapiDeinterlaceMethod method, guint flags)
{
  gboolean success = FALSE;

  GST_VAAPI_DISPLAY_LOCK (filter->display);
  success = op_set_deinterlace_unlocked (filter, op_data, method, flags);
  GST_VAAPI_DISPLAY_UNLOCK (filter->display);
  return success;
}

/* Update skin tone enhancement level */
static gboolean
op_set_skintone_level_unlocked (GstVaapiFilter * filter,
    GstVaapiFilterOpData * op_data, guint value)
{
  VAProcFilterParameterBuffer *buf;

  if (!op_data || !op_ensure_buffer (filter, op_data))
    return FALSE;

  op_data->is_enabled = 1;

  buf = vaapi_map_buffer (filter->va_display, op_data->va_buffer);
  if (!buf)
    return FALSE;
  buf->type = op_data->va_type;
  buf->value = value;
  vaapi_unmap_buffer (filter->va_display, op_data->va_buffer, NULL);
  return TRUE;
}

static inline gboolean
op_set_skintone_level (GstVaapiFilter * filter,
    GstVaapiFilterOpData * op_data, guint value)
{
  gboolean success = FALSE;

  GST_VAAPI_DISPLAY_LOCK (filter->display);
  success = op_set_skintone_level_unlocked (filter, op_data, value);
  GST_VAAPI_DISPLAY_UNLOCK (filter->display);
  return success;
}

#ifndef GST_REMOVE_DEPRECATED
/* Update skin tone enhancement */
static gboolean
op_set_skintone_unlocked (GstVaapiFilter * filter,
    GstVaapiFilterOpData * op_data, gboolean value)
{
  if (!op_data)
    return FALSE;

  if (!value) {
    op_data->is_enabled = 0;
    return TRUE;
  }

  return op_set_skintone_level_unlocked (filter, op_data, 3);
}

static inline gboolean
op_set_skintone (GstVaapiFilter * filter, GstVaapiFilterOpData * op_data,
    gboolean enhance)
{
  gboolean success = FALSE;

  GST_VAAPI_DISPLAY_LOCK (filter->display);
  success = op_set_skintone_unlocked (filter, op_data, enhance);
  GST_VAAPI_DISPLAY_UNLOCK (filter->display);
  return success;
}
#endif

static gboolean
op_set_hdr_tone_map_unlocked (GstVaapiFilter * filter,
    GstVaapiFilterOpData * op_data, gboolean value)
{
#if VA_CHECK_VERSION(1,4,0)
  const VAProcFilterCapHighDynamicRange *filter_caps;
  guint i;

  if (!op_data)
    return !value;

  if (!value) {
    op_data->is_enabled = 0;
    return TRUE;
  }

  if (!op_ensure_buffer (filter, op_data))
    return FALSE;

  for (i = 0, filter_caps = op_data->va_caps; i < op_data->va_num_caps; i++) {
    if (filter_caps[i].metadata_type == op_data->va_subtype &&
        (filter_caps[i].caps_flag & VA_TONE_MAPPING_HDR_TO_SDR))
      break;
  }
  if (i == op_data->va_num_caps)
    return FALSE;

  op_data->is_enabled = 1;

  return TRUE;
#else
  return !value;
#endif
}

static inline gboolean
op_set_hdr_tone_map (GstVaapiFilter * filter, GstVaapiFilterOpData * op_data,
    gboolean value)
{
  gboolean success = FALSE;
  GST_VAAPI_DISPLAY_LOCK (filter->display);
  success = op_set_hdr_tone_map_unlocked (filter, op_data, value);
  GST_VAAPI_DISPLAY_UNLOCK (filter->display);

  return success;
}

static gboolean
deint_refs_set (GArray * refs, GstVaapiSurface ** surfaces, guint num_surfaces)
{
  guint i;

  if (num_surfaces > 0 && !surfaces)
    return FALSE;

  for (i = 0; i < num_surfaces; i++)
    g_array_append_val (refs, GST_VAAPI_SURFACE_ID (surfaces[i]));
  return TRUE;
}

static void
deint_refs_clear (GArray * refs)
{
  if (refs->len > 0)
    g_array_remove_range (refs, 0, refs->len);
}

static inline void
deint_refs_clear_all (GstVaapiFilter * filter)
{
  deint_refs_clear (filter->forward_references);
  deint_refs_clear (filter->backward_references);
}

/* ------------------------------------------------------------------------- */
/* --- Surface Attribs                                                   --- */
/* ------------------------------------------------------------------------- */

static gboolean
ensure_attributes (GstVaapiFilter * filter)
{
  if (G_LIKELY (filter->attribs))
    return TRUE;

  filter->attribs = gst_vaapi_config_surface_attributes_get (filter->display,
      filter->va_config);
  return (filter->attribs != NULL);
}

static inline gboolean
is_special_format (GstVideoFormat format)
{
  return format == GST_VIDEO_FORMAT_UNKNOWN ||
      format == GST_VIDEO_FORMAT_ENCODED;
}

static gboolean
find_format (GstVaapiFilter * filter, GstVideoFormat format)
{
  guint i;
  GArray *formats;

  formats = filter->attribs->formats;
  if (is_special_format (format) || !formats)
    return FALSE;

  for (i = 0; i < formats->len; i++) {
    if (g_array_index (formats, GstVideoFormat, i) == format)
      return TRUE;
  }
  return FALSE;
}

/* ------------------------------------------------------------------------- */
/* --- Interface                                                         --- */
/* ------------------------------------------------------------------------- */

static void
gst_vaapi_filter_init (GstVaapiFilter * filter)
{
  filter->va_config = VA_INVALID_ID;
  filter->va_context = VA_INVALID_ID;
  filter->format = DEFAULT_FORMAT;

  filter->forward_references =
      g_array_sized_new (FALSE, FALSE, sizeof (VASurfaceID), 4);

  filter->backward_references =
      g_array_sized_new (FALSE, FALSE, sizeof (VASurfaceID), 4);
}

static gboolean
gst_vaapi_filter_initialize (GstVaapiFilter * filter)
{
  VAStatus va_status;

  if (!filter->display)
    return FALSE;

  va_status = vaCreateConfig (filter->va_display, VAProfileNone,
      VAEntrypointVideoProc, NULL, 0, &filter->va_config);
  if (!vaapi_check_status (va_status, "vaCreateConfig() [VPP]"))
    return FALSE;

  va_status = vaCreateContext (filter->va_display, filter->va_config, 0, 0, 0,
      NULL, 0, &filter->va_context);
  if (!vaapi_check_status (va_status, "vaCreateContext() [VPP]"))
    return FALSE;

  gst_video_colorimetry_from_string (&filter->input_colorimetry, NULL);
  gst_video_colorimetry_from_string (&filter->output_colorimetry, NULL);

  return TRUE;
}

static void
gst_vaapi_filter_finalize (GObject * object)
{
  GstVaapiFilter *const filter = GST_VAAPI_FILTER (object);
  guint i;

  if (!filter->display)
    goto bail;

  GST_VAAPI_DISPLAY_LOCK (filter->display);
  if (filter->operations) {
    for (i = 0; i < filter->operations->len; i++) {
      GstVaapiFilterOpData *const op_data =
          g_ptr_array_index (filter->operations, i);
      vaapi_destroy_buffer (filter->va_display, &op_data->va_buffer);
    }
    g_ptr_array_unref (filter->operations);
    filter->operations = NULL;
  }

  if (filter->va_context != VA_INVALID_ID) {
    vaDestroyContext (filter->va_display, filter->va_context);
    filter->va_context = VA_INVALID_ID;
  }

  if (filter->va_config != VA_INVALID_ID) {
    vaDestroyConfig (filter->va_display, filter->va_config);
    filter->va_config = VA_INVALID_ID;
  }
  GST_VAAPI_DISPLAY_UNLOCK (filter->display);
  gst_vaapi_display_replace (&filter->display, NULL);

bail:
  if (filter->forward_references) {
    g_array_unref (filter->forward_references);
    filter->forward_references = NULL;
  }

  if (filter->backward_references) {
    g_array_unref (filter->backward_references);
    filter->backward_references = NULL;
  }

  if (filter->attribs) {
    gst_vaapi_config_surface_attributes_free (filter->attribs);
    filter->attribs = NULL;
  }

  G_OBJECT_CLASS (gst_vaapi_filter_parent_class)->finalize (object);
}

static void
gst_vaapi_filter_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVaapiFilter *const filter = GST_VAAPI_FILTER (object);

  switch (property_id) {
    case PROP_DISPLAY:{
      GstVaapiDisplay *display = g_value_get_object (value);;

      if (display) {
        if (GST_VAAPI_DISPLAY_HAS_VPP (display)) {
          filter->display = gst_object_ref (display);
          filter->va_display = GST_VAAPI_DISPLAY_VADISPLAY (filter->display);
        } else {
          GST_WARNING_OBJECT (filter, "VA display doesn't support VPP");
        }
      }
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
gst_vaapi_filter_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstVaapiFilter *const filter = GST_VAAPI_FILTER (object);

  switch (property_id) {
    case PROP_DISPLAY:
      g_value_set_object (value, filter->display);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
gst_vaapi_filter_class_init (GstVaapiFilterClass * klass)
{
  GObjectClass *const object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = gst_vaapi_filter_set_property;
  object_class->get_property = gst_vaapi_filter_get_property;
  object_class->finalize = gst_vaapi_filter_finalize;

  /**
   * GstVaapiFilter:display:
   *
   * #GstVaapiDisplay to be used.
   */
  g_object_class_install_property (object_class, PROP_DISPLAY,
      g_param_spec_object ("display", "Gst VA-API Display",
          "The VA-API display object to use", GST_TYPE_VAAPI_DISPLAY,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_NAME));
}

/**
 * gst_vaapi_filter_new:
 * @display: a #GstVaapiDisplay
 *
 * Creates a new #GstVaapiFilter set up to operate in "identity"
 * mode. This means that no other operation than scaling is performed.
 *
 * Return value: the newly created #GstVaapiFilter object
 */
GstVaapiFilter *
gst_vaapi_filter_new (GstVaapiDisplay * display)
{
  GstVaapiFilter *filter;

  filter = g_object_new (GST_TYPE_VAAPI_FILTER, "display", display, NULL);
  if (!gst_vaapi_filter_initialize (filter))
    goto error;
  return filter;

  /* ERRORS */
error:
  {
    gst_object_unref (filter);
    return NULL;
  }
}

/**
 * gst_vaapi_filter_replace:
 * @old_filter_ptr: a pointer to a #GstVaapiFilter
 * @new_filter: a #GstVaapiFilter
 *
 * Atomically replaces the filter held in @old_filter_ptr with
 * @new_filter. This means that @old_filter_ptr shall reference a
 * valid filter. However, @new_filter can be NULL.
 */
void
gst_vaapi_filter_replace (GstVaapiFilter ** old_filter_ptr,
    GstVaapiFilter * new_filter)
{
  g_return_if_fail (old_filter_ptr != NULL);

  gst_object_replace ((GstObject **) old_filter_ptr, GST_OBJECT (new_filter));
}

/**
 * gst_vaapi_filter_get_operations:
 * @filter: a #GstVaapiFilter, or %NULL
 *
 * Determines the set of supported operations for video processing.
 * The caller owns an extra reference to the resulting array of
 * #GstVaapiFilterOpInfo elements, so it shall be released with
 * g_ptr_array_unref() after usage.
 *
 * If @filter is %NULL, then this function returns the video
 * processing operations supported by this library.
 *
 * Return value: the set of supported operations, or %NULL if an error
 *   occurred.
 */
GPtrArray *
gst_vaapi_filter_get_operations (GstVaapiFilter * filter)
{
  return get_operations (filter);
}

/**
 * gst_vaapi_filter_has_operation:
 * @filter: a #GstVaapiFilter
 * @op: a #GstVaapiFilterOp
 *
 * Determines whether the underlying VA driver advertises support for
 * the supplied operation @op.
 *
 * Return value: %TRUE if the specified operation may be supported by
 *   the underlying hardware, %FALSE otherwise
 */
gboolean
gst_vaapi_filter_has_operation (GstVaapiFilter * filter, GstVaapiFilterOp op)
{
  g_return_val_if_fail (filter != NULL, FALSE);

  return find_operation (filter, op) != NULL;
}

/**
 * gst_vaapi_filter_use_operation:
 * @filter: a #GstVaapiFilter
 * @op: a #GstVaapiFilterOp
 *
 * Determines whether the supplied operation @op was already enabled
 * through a prior call to gst_vaapi_filter_set_operation() or any
 * other operation-specific function.
 *
 * Note: should an operation be set to its default value, this means
 * that it is actually not enabled.
 *
 * Return value: %TRUE if the specified operation was already enabled,
 *   %FALSE otherwise
 */
gboolean
gst_vaapi_filter_use_operation (GstVaapiFilter * filter, GstVaapiFilterOp op)
{
  GstVaapiFilterOpData *op_data;

  g_return_val_if_fail (filter != NULL, FALSE);

  op_data = find_operation (filter, op);
  if (!op_data)
    return FALSE;
  return op_data->is_enabled;
}

/**
 * gst_vaapi_filter_set_operation:
 * @filter: a #GstVaapiFilter
 * @op: a #GstVaapiFilterOp
 * @value: the @op settings
 *
 * Enable the specified operation @op to be performed during video
 * processing, i.e. in gst_vaapi_filter_process(). The @value argument
 * specifies the operation settings. e.g. deinterlacing method for
 * deinterlacing, denoising level for noise reduction, etc.
 *
 * If @value is %NULL, then this function resets the operation
 * settings to their default values.
 *
 * Return value: %TRUE if the specified operation may be supported,
 *   %FALSE otherwise
 */
gboolean
gst_vaapi_filter_set_operation (GstVaapiFilter * filter, GstVaapiFilterOp op,
    const GValue * value)
{
  GstVaapiFilterOpData *op_data;

  g_return_val_if_fail (filter != NULL, FALSE);

  op_data = find_operation (filter, op);
  if (!op_data)
    return FALSE;

  if (value && !G_VALUE_HOLDS (value, G_PARAM_SPEC_VALUE_TYPE (op_data->pspec)))
    return FALSE;

  switch (op) {
    case GST_VAAPI_FILTER_OP_FORMAT:
      return gst_vaapi_filter_set_format (filter, value ?
          g_value_get_enum (value) : DEFAULT_FORMAT);
    case GST_VAAPI_FILTER_OP_CROP:
      return gst_vaapi_filter_set_cropping_rectangle (filter, value ?
          g_value_get_boxed (value) : NULL);
    case GST_VAAPI_FILTER_OP_DENOISE:
    case GST_VAAPI_FILTER_OP_SHARPEN:
      return op_set_generic (filter, op_data,
          (value ? g_value_get_float (value) :
              OP_DATA_DEFAULT_VALUE (float, op_data)));
    case GST_VAAPI_FILTER_OP_HUE:
    case GST_VAAPI_FILTER_OP_SATURATION:
    case GST_VAAPI_FILTER_OP_BRIGHTNESS:
    case GST_VAAPI_FILTER_OP_CONTRAST:
      return op_set_color_balance (filter, op_data,
          (value ? g_value_get_float (value) :
              OP_DATA_DEFAULT_VALUE (float, op_data)));
    case GST_VAAPI_FILTER_OP_DEINTERLACING:
      return op_set_deinterlace (filter, op_data,
          (value ? g_value_get_enum (value) :
              OP_DATA_DEFAULT_VALUE (enum, op_data)), 0);
      break;
    case GST_VAAPI_FILTER_OP_SCALING:
      return gst_vaapi_filter_set_scaling (filter,
          (value ? g_value_get_enum (value) :
              OP_DATA_DEFAULT_VALUE (enum, op_data)));
#ifndef GST_REMOVE_DEPRECATED
    case GST_VAAPI_FILTER_OP_SKINTONE:
      return op_set_skintone (filter, op_data,
          (value ? g_value_get_boolean (value) :
              OP_DATA_DEFAULT_VALUE (boolean, op_data)));
#endif
    case GST_VAAPI_FILTER_OP_SKINTONE_LEVEL:
      return op_set_skintone_level (filter, op_data,
          (value ? g_value_get_uint (value) :
              OP_DATA_DEFAULT_VALUE (uint, op_data)));
    case GST_VAAPI_FILTER_OP_VIDEO_DIRECTION:
      return gst_vaapi_filter_set_video_direction (filter,
          (value ? g_value_get_enum (value) :
              OP_DATA_DEFAULT_VALUE (enum, op_data)));
    case GST_VAAPI_FILTER_OP_HDR_TONE_MAP:
      return op_set_hdr_tone_map (filter, op_data,
          (value ? g_value_get_boolean (value) :
              OP_DATA_DEFAULT_VALUE (boolean, op_data)));
    default:
      break;
  }
  return FALSE;
}

#if VA_CHECK_VERSION(1,2,0)
static void
fill_color_standard (GstVideoColorimetry * colorimetry,
    VAProcColorStandardType * type, VAProcColorProperties * properties)
{
  *type = from_GstVideoColorimetry (colorimetry);

  properties->colour_primaries =
      gst_video_color_primaries_to_iso (colorimetry->primaries);
  properties->transfer_characteristics =
      gst_video_transfer_function_to_iso (colorimetry->transfer);
  properties->matrix_coefficients =
      gst_video_color_matrix_to_iso (colorimetry->matrix);

  properties->color_range = from_GstVideoColorRange (colorimetry->range);
}
#endif

static void
gst_vaapi_filter_fill_color_standards (GstVaapiFilter * filter,
    VAProcPipelineParameterBuffer * pipeline_param)
{
#if VA_CHECK_VERSION(1,2,0)
  fill_color_standard (&filter->input_colorimetry,
      &pipeline_param->surface_color_standard,
      &pipeline_param->input_color_properties);

  fill_color_standard (&filter->output_colorimetry,
      &pipeline_param->output_color_standard,
      &pipeline_param->output_color_properties);
#else
  pipeline_param->surface_color_standard = VAProcColorStandardNone;
  pipeline_param->output_color_standard = VAProcColorStandardNone;
#endif
}

/**
 * gst_vaapi_filter_process:
 * @filter: a #GstVaapiFilter
 * @src_surface: the source @GstVaapiSurface
 * @dst_surface: the destination @GstVaapiSurface
 * @flags: #GstVaapiSurfaceRenderFlags that apply to @src_surface
 *
 * Applies the operations currently defined in the @filter to
 * @src_surface and return the output in @dst_surface. The order of
 * operations is determined in a way that suits best the underlying
 * hardware. i.e. the only guarantee held is the generated outcome,
 * not any specific order of operations.
 *
 * Return value: a #GstVaapiFilterStatus
 */
static GstVaapiFilterStatus
gst_vaapi_filter_process_unlocked (GstVaapiFilter * filter,
    GstVaapiSurface * src_surface, GstVaapiSurface * dst_surface, guint flags)
{
  VAProcPipelineParameterBuffer *pipeline_param = NULL;
  VABufferID pipeline_param_buf_id = VA_INVALID_ID;
  VABufferID filters[N_PROPERTIES];
  VAProcPipelineCaps pipeline_caps;
  guint i, num_filters = 0;
  VAStatus va_status;
  VARectangle src_rect, dst_rect;
  guint va_mirror = 0, va_rotation = 0;

  if (!ensure_operations (filter))
    return GST_VAAPI_FILTER_STATUS_ERROR_ALLOCATION_FAILED;

  /* Build surface region (source) */
  if (filter->use_crop_rect) {
    const GstVaapiRectangle *const crop_rect = &filter->crop_rect;

    if ((crop_rect->x + crop_rect->width >
            GST_VAAPI_SURFACE_WIDTH (src_surface)) ||
        (crop_rect->y + crop_rect->height >
            GST_VAAPI_SURFACE_HEIGHT (src_surface)))
      goto error;

    src_rect.x = crop_rect->x;
    src_rect.y = crop_rect->y;
    src_rect.width = crop_rect->width;
    src_rect.height = crop_rect->height;
  } else {
    src_rect.x = 0;
    src_rect.y = 0;
    src_rect.width = GST_VAAPI_SURFACE_WIDTH (src_surface);
    src_rect.height = GST_VAAPI_SURFACE_HEIGHT (src_surface);
  }

  /* Build output region (target) */
  if (filter->use_target_rect) {
    const GstVaapiRectangle *const target_rect = &filter->target_rect;

    if ((target_rect->x + target_rect->width >
            GST_VAAPI_SURFACE_WIDTH (dst_surface)) ||
        (target_rect->y + target_rect->height >
            GST_VAAPI_SURFACE_HEIGHT (dst_surface)))
      goto error;

    dst_rect.x = target_rect->x;
    dst_rect.y = target_rect->y;
    dst_rect.width = target_rect->width;
    dst_rect.height = target_rect->height;
  } else {
    dst_rect.x = 0;
    dst_rect.y = 0;
    dst_rect.width = GST_VAAPI_SURFACE_WIDTH (dst_surface);
    dst_rect.height = GST_VAAPI_SURFACE_HEIGHT (dst_surface);
  }

  for (i = 0, num_filters = 0; i < filter->operations->len; i++) {
    GstVaapiFilterOpData *const op_data =
        g_ptr_array_index (filter->operations, i);
    if (!op_data->is_enabled)
      continue;
    if (op_data->va_buffer == VA_INVALID_ID) {
      GST_ERROR ("invalid VA buffer for operation %s",
          g_param_spec_get_name (op_data->pspec));
      goto error;
    }
    filters[num_filters++] = op_data->va_buffer;
  }

  /* Validate pipeline caps */
  va_status = vaQueryVideoProcPipelineCaps (filter->va_display,
      filter->va_context, filters, num_filters, &pipeline_caps);
  if (!vaapi_check_status (va_status, "vaQueryVideoProcPipelineCaps()"))
    goto error;

  if (!vaapi_create_buffer (filter->va_display, filter->va_context,
          VAProcPipelineParameterBufferType, sizeof (*pipeline_param),
          NULL, &pipeline_param_buf_id, (gpointer *) & pipeline_param))
    goto error;

  memset (pipeline_param, 0, sizeof (*pipeline_param));
  pipeline_param->surface = GST_VAAPI_SURFACE_ID (src_surface);
  pipeline_param->surface_region = &src_rect;

  gst_vaapi_filter_fill_color_standards (filter, pipeline_param);

  pipeline_param->output_region = &dst_rect;
  pipeline_param->output_background_color = 0xff000000;
  pipeline_param->filter_flags = from_GstVaapiSurfaceRenderFlags (flags) |
      from_GstVaapiScaleMethod (filter->scale_method);
  pipeline_param->filters = filters;
  pipeline_param->num_filters = num_filters;

  from_GstVideoOrientationMethod (filter->video_direction, &va_mirror,
      &va_rotation);

#if VA_CHECK_VERSION(1,1,0)
  pipeline_param->mirror_state = va_mirror;
  pipeline_param->rotation_state = va_rotation;
#endif

  // Reference frames for advanced deinterlacing
  if (filter->forward_references->len > 0) {
    pipeline_param->forward_references = (VASurfaceID *)
        filter->forward_references->data;
    pipeline_param->num_forward_references =
        MIN (filter->forward_references->len,
        pipeline_caps.num_forward_references);
  } else {
    pipeline_param->forward_references = NULL;
    pipeline_param->num_forward_references = 0;
  }

  if (filter->backward_references->len > 0) {
    pipeline_param->backward_references = (VASurfaceID *)
        filter->backward_references->data;
    pipeline_param->num_backward_references =
        MIN (filter->backward_references->len,
        pipeline_caps.num_backward_references);
  } else {
    pipeline_param->backward_references = NULL;
    pipeline_param->num_backward_references = 0;
  }

  vaapi_unmap_buffer (filter->va_display, pipeline_param_buf_id, NULL);

  va_status = vaBeginPicture (filter->va_display, filter->va_context,
      GST_VAAPI_SURFACE_ID (dst_surface));
  if (!vaapi_check_status (va_status, "vaBeginPicture()"))
    goto error;

  va_status = vaRenderPicture (filter->va_display, filter->va_context,
      &pipeline_param_buf_id, 1);
  if (!vaapi_check_status (va_status, "vaRenderPicture()"))
    goto error;

  va_status = vaEndPicture (filter->va_display, filter->va_context);
  if (!vaapi_check_status (va_status, "vaEndPicture()"))
    goto error;

  deint_refs_clear_all (filter);
  vaapi_destroy_buffer (filter->va_display, &pipeline_param_buf_id);
  return GST_VAAPI_FILTER_STATUS_SUCCESS;

  /* ERRORS */
error:
  {
    deint_refs_clear_all (filter);
    vaapi_destroy_buffer (filter->va_display, &pipeline_param_buf_id);
    return GST_VAAPI_FILTER_STATUS_ERROR_OPERATION_FAILED;
  }
}

GstVaapiFilterStatus
gst_vaapi_filter_process (GstVaapiFilter * filter,
    GstVaapiSurface * src_surface, GstVaapiSurface * dst_surface, guint flags)
{
  GstVaapiFilterStatus status;

  g_return_val_if_fail (filter != NULL,
      GST_VAAPI_FILTER_STATUS_ERROR_INVALID_PARAMETER);
  g_return_val_if_fail (src_surface != NULL,
      GST_VAAPI_FILTER_STATUS_ERROR_INVALID_PARAMETER);
  g_return_val_if_fail (dst_surface != NULL,
      GST_VAAPI_FILTER_STATUS_ERROR_INVALID_PARAMETER);

  GST_VAAPI_DISPLAY_LOCK (filter->display);
  status = gst_vaapi_filter_process_unlocked (filter,
      src_surface, dst_surface, flags);
  GST_VAAPI_DISPLAY_UNLOCK (filter->display);
  return status;
}

/**
 * gst_vaapi_filter_get_formats:
 * @filter: a #GstVaapiFilter
 * @min_width: the min width can be supported.
 * @min_height: the min height can be supported.
 * @max_width: the max width can be supported.
 * @max_height: the max height can be supported.
 *
 * Determines the set of supported source or target formats for video
 * processing.  The caller owns an extra reference to the resulting
 * array of #GstVideoFormat elements, so it shall be released with
 * g_array_unref() after usage.
 *
 * Return value: the set of supported target formats for video processing.
 */
GArray *
gst_vaapi_filter_get_formats (GstVaapiFilter * filter, gint * min_width,
    gint * min_height, gint * max_width, gint * max_height)
{
  GstVaapiConfigSurfaceAttributes *attribs;

  g_return_val_if_fail (filter != NULL, NULL);

  if (!ensure_attributes (filter))
    return NULL;

  attribs = filter->attribs;

  if (attribs->min_width >= attribs->max_width ||
      attribs->min_height >= attribs->max_height)
    return NULL;

  if (min_width)
    *min_width = attribs->min_width;
  if (min_height)
    *min_height = attribs->min_height;
  if (max_width)
    *max_width = attribs->max_width;
  if (max_height)
    *max_height = attribs->max_height;

  if (filter->attribs->formats)
    return g_array_ref (filter->attribs->formats);
  return NULL;
}

/**
 * gst_vaapi_filter_set_format:
 * @filter: a #GstVaapiFilter
 * @format: the target surface format
 *
 * Sets the desired pixel format of the resulting video processing
 * operations.
 *
 * If @format is #GST_VIDEO_FORMAT_UNKNOWN, the filter will assume iso
 * format conversion, i.e. no color conversion at all and the target
 * surface format shall match the source surface format.
 *
 * If @format is #GST_VIDEO_FORMAT_ENCODED, the filter will use the pixel
 * format of the target surface passed to gst_vaapi_filter_process().
 *
 * Return value: %TRUE if the color conversion to the specified @format
 *   may be supported, %FALSE otherwise.
 */
gboolean
gst_vaapi_filter_set_format (GstVaapiFilter * filter, GstVideoFormat format)
{
  g_return_val_if_fail (filter != NULL, FALSE);

  if (!ensure_attributes (filter))
    return FALSE;

  if (!is_special_format (format) && !find_format (filter, format))
    return FALSE;

  filter->format = format;
  return TRUE;
}

/**
 * gst_vaapi_filter_append_caps:
 * @filter: a #GstVaapiFilter
 * @structure: a #GstStructure from #GstCaps
 *
 * Extracts the config's surface attributes, from @filter's context,
 * and transforms it into a caps formats and appended them into
 * @structure.
 *
 * Returns: %TRUE if the capabilities could be extracted and appended
 * into @structure; otherwise %FALSE
 **/
gboolean
gst_vaapi_filter_append_caps (GstVaapiFilter * filter, GstStructure * structure)
{
  GstVaapiConfigSurfaceAttributes *attribs;

  g_return_val_if_fail (filter != NULL, FALSE);
  g_return_val_if_fail (structure != NULL, FALSE);

  if (!ensure_attributes (filter))
    return FALSE;

  attribs = filter->attribs;

  if (attribs->min_width >= attribs->max_width ||
      attribs->min_height >= attribs->max_height)
    return FALSE;

  gst_structure_set (structure, "width", GST_TYPE_INT_RANGE, attribs->min_width,
      attribs->max_width, "height", GST_TYPE_INT_RANGE, attribs->min_height,
      attribs->max_height, NULL);

  return TRUE;

}

/**
 * gst_vaapi_filter_get_memory_types:
 * @filter: a #GstVaapiFilter
 *
 * Gets the surface's memory types available in @filter's context.
 *
 * Returns: surface's memory types available in @filter context.
 **/
guint
gst_vaapi_filter_get_memory_types (GstVaapiFilter * filter)
{
  g_return_val_if_fail (filter != NULL, FALSE);

  if (!ensure_attributes (filter))
    return 0;
  return filter->attribs->mem_types;
}

/**
 * gst_vaapi_filter_set_cropping_rectangle:
 * @filter: a #GstVaapiFilter
 * @rect: the cropping region
 *
 * Sets the source surface cropping rectangle to use during the video
 * processing. If @rect is %NULL, the whole source surface will be used.
 *
 * Return value: %TRUE if the operation is supported, %FALSE otherwise.
 */
gboolean
gst_vaapi_filter_set_cropping_rectangle (GstVaapiFilter * filter,
    const GstVaapiRectangle * rect)
{
  g_return_val_if_fail (filter != NULL, FALSE);

  filter->use_crop_rect = rect != NULL;
  if (filter->use_crop_rect)
    filter->crop_rect = *rect;
  return TRUE;
}

/**
 * gst_vaapi_filter_set_target_rectangle:
 * @filter: a #GstVaapiFilter
 * @rect: the target render region
 *
 * Sets the region within the target surface where the source surface
 * would be rendered. i.e. where the hardware accelerator would emit
 * the outcome of video processing. If @rect is %NULL, the whole
 * source surface will be used.
 *
 * Return value: %TRUE if the operation is supported, %FALSE otherwise.
 */
gboolean
gst_vaapi_filter_set_target_rectangle (GstVaapiFilter * filter,
    const GstVaapiRectangle * rect)
{
  g_return_val_if_fail (filter != NULL, FALSE);

  filter->use_target_rect = rect != NULL;
  if (filter->use_target_rect)
    filter->target_rect = *rect;
  return TRUE;
}

/**
 * gst_vaapi_filter_set_denoising_level:
 * @filter: a #GstVaapiFilter
 * @level: the level of noise reduction to apply
 *
 * Sets the noise reduction level to apply. If @level is 0.0f, this
 * corresponds to disabling the noise reduction algorithm.
 *
 * Return value: %TRUE if the operation is supported, %FALSE otherwise.
 */
gboolean
gst_vaapi_filter_set_denoising_level (GstVaapiFilter * filter, gfloat level)
{
  g_return_val_if_fail (filter != NULL, FALSE);

  return op_set_generic (filter,
      find_operation (filter, GST_VAAPI_FILTER_OP_DENOISE), level);
}

/**
 * gst_vaapi_filter_set_sharpening_level:
 * @filter: a #GstVaapiFilter
 * @level: the sharpening factor
 *
 * Enables noise reduction with the specified factor.
 *
 * Return value: %TRUE if the operation is supported, %FALSE otherwise.
 */
gboolean
gst_vaapi_filter_set_sharpening_level (GstVaapiFilter * filter, gfloat level)
{
  g_return_val_if_fail (filter != NULL, FALSE);

  return op_set_generic (filter,
      find_operation (filter, GST_VAAPI_FILTER_OP_SHARPEN), level);
}

/**
 * gst_vaapi_filter_set_hue:
 * @filter: a #GstVaapiFilter
 * @value: the color hue value
 *
 * Enables color hue adjustment to the specified value.
 *
 * Return value: %TRUE if the operation is supported, %FALSE otherwise.
 */
gboolean
gst_vaapi_filter_set_hue (GstVaapiFilter * filter, gfloat value)
{
  g_return_val_if_fail (filter != NULL, FALSE);

  return op_set_color_balance (filter,
      find_operation (filter, GST_VAAPI_FILTER_OP_HUE), value);
}

/**
 * gst_vaapi_filter_set_saturation:
 * @filter: a #GstVaapiFilter
 * @value: the color saturation value
 *
 * Enables color saturation adjustment to the specified value.
 *
 * Return value: %TRUE if the operation is supported, %FALSE otherwise.
 */
gboolean
gst_vaapi_filter_set_saturation (GstVaapiFilter * filter, gfloat value)
{
  g_return_val_if_fail (filter != NULL, FALSE);

  return op_set_color_balance (filter,
      find_operation (filter, GST_VAAPI_FILTER_OP_SATURATION), value);
}

/**
 * gst_vaapi_filter_set_brightness:
 * @filter: a #GstVaapiFilter
 * @value: the color brightness value
 *
 * Enables color brightness adjustment to the specified value.
 *
 * Return value: %TRUE if the operation is supported, %FALSE otherwise.
 */
gboolean
gst_vaapi_filter_set_brightness (GstVaapiFilter * filter, gfloat value)
{
  g_return_val_if_fail (filter != NULL, FALSE);

  return op_set_color_balance (filter,
      find_operation (filter, GST_VAAPI_FILTER_OP_BRIGHTNESS), value);
}

/**
 * gst_vaapi_filter_set_contrast:
 * @filter: a #GstVaapiFilter
 * @value: the color contrast value
 *
 * Enables color contrast adjustment to the specified value.
 *
 * Return value: %TRUE if the operation is supported, %FALSE otherwise.
 */
gboolean
gst_vaapi_filter_set_contrast (GstVaapiFilter * filter, gfloat value)
{
  g_return_val_if_fail (filter != NULL, FALSE);

  return op_set_color_balance (filter,
      find_operation (filter, GST_VAAPI_FILTER_OP_CONTRAST), value);
}

/**
 * gst_vaapi_filter_set_deinterlacing:
 * @filter: a #GstVaapiFilter
 * @method: the deinterlacing algorithm (see #GstVaapiDeinterlaceMethod)
 * @flags: the additional flags
 *
 * Applies deinterlacing to the video processing pipeline. If @method
 * is not @GST_VAAPI_DEINTERLACE_METHOD_NONE, then @flags could
 * represent the initial picture structure of the source frame.
 *
 * Return value: %TRUE if the operation is supported, %FALSE otherwise.
 */
gboolean
gst_vaapi_filter_set_deinterlacing (GstVaapiFilter * filter,
    GstVaapiDeinterlaceMethod method, guint flags)
{
  g_return_val_if_fail (filter != NULL, FALSE);

  return op_set_deinterlace (filter,
      find_operation (filter, GST_VAAPI_FILTER_OP_DEINTERLACING), method,
      flags);
}

/**
 * gst_vaapi_filter_set_deinterlacing_references:
 * @filter: a #GstVaapiFilter
 * @forward_references: the set of #GstVaapiSurface objects used as
 *   forward references
 * @num_forward_references: the number of elements in the
 *   @forward_references array
 * @backward_references: the set of #GstVaapiSurface objects used as
 *   backward references
 * @num_backward_references: the number of elements in the
 *   @backward_references array
 *
 * Specifies the list of surfaces used for forward or backward reference in
 * advanced deinterlacing mode. The caller is responsible for maintaining
 * the associated surfaces live until gst_vaapi_filter_process() completes.
 * e.g. by holding an extra reference to the associated #GstVaapiSurfaceProxy.
 *
 * Temporal ordering is maintained as follows: the shorter index in
 * either array is, the closest the matching surface is relatively to
 * the current source surface to process. e.g. surface in
 * @forward_references array index 0 represents the immediately
 * preceding surface in display order, surface at index 1 is the one
 * preceding surface at index 0, etc.
 *
 * The video processing filter will only use the recommended number of
 * surfaces for backward and forward references.
 *
 * Note: the supplied lists of reference surfaces are not sticky. This
 * means that they are only valid for the next gst_vaapi_filter_process()
 * call, and thus needs to be submitted again for subsequent calls.
 *
 * Return value: %TRUE if the operation is supported, %FALSE otherwise.
 */
gboolean
gst_vaapi_filter_set_deinterlacing_references (GstVaapiFilter * filter,
    GstVaapiSurface ** forward_references, guint num_forward_references,
    GstVaapiSurface ** backward_references, guint num_backward_references)
{
  g_return_val_if_fail (filter != NULL, FALSE);

  deint_refs_clear_all (filter);

  if (!deint_refs_set (filter->forward_references, forward_references,
          num_forward_references))
    return FALSE;

  if (!deint_refs_set (filter->backward_references, backward_references,
          num_backward_references))
    return FALSE;
  return TRUE;
}

/**
 * gst_vaapi_filter_set_scaling:
 * @filter: a #GstVaapiFilter
 * @method: the scaling algorithm (see #GstVaapiScaleMethod)
 *
 * Applies scaling algorithm to the video processing pipeline.
 *
 * Return value: %TRUE if the operation is supported, %FALSE otherwise.
 */
gboolean
gst_vaapi_filter_set_scaling (GstVaapiFilter * filter,
    GstVaapiScaleMethod method)
{
  g_return_val_if_fail (filter != NULL, FALSE);

  filter->scale_method = method;
  return TRUE;
}

#ifndef GST_REMOVE_DEPRECATED
/**
 * gst_vaapi_filter_set_skintone:
 * @filter: a #GstVaapiFilter
 * @enhance: %TRUE if enable the skin tone enhancement algorithm
 *
 * Applies the skin tone enhancement algorithm.
 *
 * Return value: %TRUE if the operation is supported, %FALSE
 * otherwise.
  **/
gboolean
gst_vaapi_filter_set_skintone (GstVaapiFilter * filter, gboolean enhance)
{
  g_return_val_if_fail (filter != NULL, FALSE);

  return op_set_skintone (filter,
      find_operation (filter, GST_VAAPI_FILTER_OP_SKINTONE), enhance);
}
#endif

/**
 * gst_vaapi_filter_set_skintone_level:
 * @filter: a #GstVaapiFilter
 * @value: the value if enable the skin tone enhancement algorithm
 *
 * Applies the skin tone enhancement algorithm with specifled value.
 *
 * Return value: %TRUE if the operation is supported, %FALSE
 * otherwise.
  **/
gboolean
gst_vaapi_filter_set_skintone_level (GstVaapiFilter * filter, guint value)
{
  g_return_val_if_fail (filter != NULL, FALSE);

  return op_set_skintone_level (filter,
      find_operation (filter, GST_VAAPI_FILTER_OP_SKINTONE_LEVEL), value);
}

/**
 * gst_vaapi_filter_set_video_direction:
 * @filter: a #GstVaapiFilter
 * @method: the video direction (see #GstVideoOrientationMethod)
 *
 * Applies mirror/rotation to the video processing pipeline.
 *
 * Return value: %TRUE if the operation is supported, %FALSE otherwise.
 */
gboolean
gst_vaapi_filter_set_video_direction (GstVaapiFilter * filter,
    GstVideoOrientationMethod method)
{
  g_return_val_if_fail (filter != NULL, FALSE);

#if VA_CHECK_VERSION(1,1,0)
  {
    guint32 va_mirror = VA_MIRROR_NONE;
    guint32 va_rotation = VA_ROTATION_NONE;

    from_GstVideoOrientationMethod (method, &va_mirror, &va_rotation);

    if (va_mirror != VA_MIRROR_NONE && !(filter->mirror_flags & va_mirror))
      return FALSE;

    if (va_rotation != VA_ROTATION_NONE
        && !(filter->rotation_flags & (1 << va_rotation)))
      return FALSE;
  }
#else
  return FALSE;
#endif

  filter->video_direction = method;
  return TRUE;
}

/**
 * gst_vaapi_filter_get_video_direction:
 * @filter: a #GstVaapiFilter
 *
 * Return value: the currently applied video direction (see #GstVideoOrientationMethod)
 */
GstVideoOrientationMethod
gst_vaapi_filter_get_video_direction (GstVaapiFilter * filter)
{
  g_return_val_if_fail (filter != NULL, GST_VIDEO_ORIENTATION_IDENTITY);
  return filter->video_direction;
}

gfloat
gst_vaapi_filter_get_denoising_level_default (GstVaapiFilter * filter)
{
  OP_RET_DEFAULT_VALUE (float, filter, GST_VAAPI_FILTER_OP_DENOISE);
}

gfloat
gst_vaapi_filter_get_sharpening_level_default (GstVaapiFilter * filter)
{
  OP_RET_DEFAULT_VALUE (float, filter, GST_VAAPI_FILTER_OP_SHARPEN);
}

gfloat
gst_vaapi_filter_get_hue_default (GstVaapiFilter * filter)
{
  OP_RET_DEFAULT_VALUE (float, filter, GST_VAAPI_FILTER_OP_HUE);
}

gfloat
gst_vaapi_filter_get_saturation_default (GstVaapiFilter * filter)
{
  OP_RET_DEFAULT_VALUE (float, filter, GST_VAAPI_FILTER_OP_SATURATION);
}

gfloat
gst_vaapi_filter_get_brightness_default (GstVaapiFilter * filter)
{
  OP_RET_DEFAULT_VALUE (float, filter, GST_VAAPI_FILTER_OP_BRIGHTNESS);
}

gfloat
gst_vaapi_filter_get_contrast_default (GstVaapiFilter * filter)
{
  OP_RET_DEFAULT_VALUE (float, filter, GST_VAAPI_FILTER_OP_CONTRAST);
}

GstVaapiScaleMethod
gst_vaapi_filter_get_scaling_default (GstVaapiFilter * filter)
{
  OP_RET_DEFAULT_VALUE (enum, filter, GST_VAAPI_FILTER_OP_SCALING);
}

#ifndef GST_REMOVE_DEPRECATED
gboolean
gst_vaapi_filter_get_skintone_default (GstVaapiFilter * filter)
{
  OP_RET_DEFAULT_VALUE (boolean, filter, GST_VAAPI_FILTER_OP_SKINTONE);
}
#endif

guint
gst_vaapi_filter_get_skintone_level_default (GstVaapiFilter * filter)
{
  OP_RET_DEFAULT_VALUE (uint, filter, GST_VAAPI_FILTER_OP_SKINTONE_LEVEL);
}

GstVideoOrientationMethod
gst_vaapi_filter_get_video_direction_default (GstVaapiFilter * filter)
{
  OP_RET_DEFAULT_VALUE (enum, filter, GST_VAAPI_FILTER_OP_VIDEO_DIRECTION);
}

static gboolean
gst_vaapi_filter_set_colorimetry_unlocked (GstVaapiFilter * filter,
    GstVideoColorimetry * input, GstVideoColorimetry * output)
{
  gchar *in_color, *out_color;

  if (input)
    filter->input_colorimetry = *input;
  else
    gst_video_colorimetry_from_string (&filter->input_colorimetry, NULL);

  if (output)
    filter->output_colorimetry = *output;
  else
    gst_video_colorimetry_from_string (&filter->output_colorimetry, NULL);

  in_color = gst_video_colorimetry_to_string (&filter->input_colorimetry);
  GST_DEBUG_OBJECT (filter, " input colorimetry '%s'", in_color);

  out_color = gst_video_colorimetry_to_string (&filter->output_colorimetry);
  GST_DEBUG_OBJECT (filter, "output colorimetry '%s'", out_color);

  if (!gst_vaapi_display_has_driver_quirks (filter->display,
          GST_VAAPI_DRIVER_QUIRK_NO_CHECK_VPP_COLOR_STD)) {
    VAProcPipelineCaps pipeline_caps = { 0, };
    VAProcColorStandardType type;
    guint32 i;

    VAStatus va_status = vaQueryVideoProcPipelineCaps (filter->va_display,
        filter->va_context, NULL, 0, &pipeline_caps);

    if (!vaapi_check_status (va_status, "vaQueryVideoProcPipelineCaps()"))
      return FALSE;

    type = from_GstVideoColorimetry (&filter->input_colorimetry);
    for (i = 0; i < pipeline_caps.num_input_color_standards; i++)
      if (type == pipeline_caps.input_color_standards[i])
        break;
    if ((i == pipeline_caps.num_input_color_standards)
        && (type != VAProcColorStandardNone))
      GST_WARNING_OBJECT (filter,
          "driver does not support '%s' input colorimetry."
          " vpp may fail or produce unexpected results.", in_color);

    type = from_GstVideoColorimetry (&filter->output_colorimetry);
    for (i = 0; i < pipeline_caps.num_output_color_standards; i++)
      if (type == pipeline_caps.output_color_standards[i])
        break;
    if ((i == pipeline_caps.num_output_color_standards)
        && (type != VAProcColorStandardNone))
      GST_WARNING_OBJECT (filter,
          "driver does not support '%s' output colorimetry."
          " vpp may fail or produce unexpected results.", out_color);
  } else {
    GST_WARNING_OBJECT (filter,
        "driver does not report the supported input/output colorimetry."
        " vpp may fail or produce unexpected results.");
  }

  g_free (in_color);
  g_free (out_color);

  return TRUE;
}

gboolean
gst_vaapi_filter_set_colorimetry (GstVaapiFilter * filter,
    GstVideoColorimetry * input, GstVideoColorimetry * output)
{
  gboolean result;

  g_return_val_if_fail (filter != NULL, FALSE);

  GST_VAAPI_DISPLAY_LOCK (filter->display);
  result = gst_vaapi_filter_set_colorimetry_unlocked (filter, input, output);
  GST_VAAPI_DISPLAY_UNLOCK (filter->display);

  return result;
}

/**
 * gst_vaapi_filter_set_hdr_tone_map:
 * @filter: a #GstVaapiFilter
 * @value: %TRUE to enable hdr tone map algorithm
 *
 * Applies HDR tone mapping algorithm.
 *
 * Return value: %TRUE if the operation is supported, %FALSE otherwise.
 */
gboolean
gst_vaapi_filter_set_hdr_tone_map (GstVaapiFilter * filter, gboolean value)
{
  g_return_val_if_fail (filter != NULL, FALSE);

  return op_set_hdr_tone_map (filter,
      find_operation (filter, GST_VAAPI_FILTER_OP_HDR_TONE_MAP), value);
}

static gboolean
gst_vaapi_filter_set_hdr_tone_map_meta_unlocked (GstVaapiFilter * filter,
    GstVideoMasteringDisplayInfo * minfo, GstVideoContentLightLevel * linfo)
{
#if VA_CHECK_VERSION(1,4,0)
  GstVaapiFilterOpData *op_data;
  VAProcFilterParameterBufferHDRToneMapping *buf;
  VAHdrMetaDataHDR10 *meta = &filter->hdr_meta;

  op_data = find_operation (filter, GST_VAAPI_FILTER_OP_HDR_TONE_MAP);

  if (!op_data)
    return FALSE;

  meta->display_primaries_x[0] = minfo->display_primaries[1].x;
  meta->display_primaries_x[1] = minfo->display_primaries[2].x;
  meta->display_primaries_x[2] = minfo->display_primaries[0].x;

  meta->display_primaries_y[0] = minfo->display_primaries[1].y;
  meta->display_primaries_y[1] = minfo->display_primaries[2].y;
  meta->display_primaries_y[2] = minfo->display_primaries[0].y;

  meta->white_point_x = minfo->white_point.x;
  meta->white_point_y = minfo->white_point.y;

  meta->max_display_mastering_luminance =
      minfo->max_display_mastering_luminance;
  meta->min_display_mastering_luminance =
      minfo->min_display_mastering_luminance;

  meta->max_content_light_level = linfo->max_content_light_level;
  meta->max_pic_average_light_level = linfo->max_frame_average_light_level;

  buf = vaapi_map_buffer (filter->va_display, op_data->va_buffer);
  if (!buf)
    return FALSE;

  buf->type = op_data->va_type;
  buf->data.metadata_type = op_data->va_subtype;
  buf->data.metadata = meta;
  buf->data.metadata_size = sizeof (meta);

  vaapi_unmap_buffer (filter->va_display, op_data->va_buffer, NULL);

  return TRUE;
#else
  return FALSE;
#endif
}

/**
 * gst_vaapi_filter_set_hdr_tone_map_meta:
 * @filter: a #GstVaapiFilter
 * @minfo: a #GstVideoMasteringDisplayInfo
 * @linfo: a #GstVideoContentLightLevel
 *
 * Sets the input HDR meta data used for tone mapping.
 *
 * Return value: %TRUE if the operation is supported, %FALSE otherwise.
 */
gboolean
gst_vaapi_filter_set_hdr_tone_map_meta (GstVaapiFilter * filter,
    GstVideoMasteringDisplayInfo * minfo, GstVideoContentLightLevel * linfo)
{
  gboolean status = FALSE;

  g_return_val_if_fail (filter != NULL, FALSE);
  g_return_val_if_fail (minfo != NULL, FALSE);
  g_return_val_if_fail (linfo != NULL, FALSE);

  GST_VAAPI_DISPLAY_LOCK (filter->display);
  status =
      gst_vaapi_filter_set_hdr_tone_map_meta_unlocked (filter, minfo, linfo);
  GST_VAAPI_DISPLAY_UNLOCK (filter->display);

  return status;
}
