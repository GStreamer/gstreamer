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

#include "sysdeps.h"
#include "gstvaapifilter.h"
#include "gstvaapiutils.h"
#include "gstvaapivalue.h"
#include "gstvaapiminiobject.h"
#include "gstvaapidisplay_priv.h"
#include "gstvaapisurface_priv.h"
#include "gstvaapiutils_core.h"

#if USE_VA_VPP
# include <va/va_vpp.h>
#endif

#define DEBUG 1
#include "gstvaapidebug.h"

#define GST_VAAPI_FILTER(obj) \
    ((GstVaapiFilter *)(obj))

typedef struct _GstVaapiFilterOpData GstVaapiFilterOpData;
struct _GstVaapiFilterOpData
{
  GstVaapiFilterOp op;
  GParamSpec *pspec;
  volatile gint ref_count;
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
  GstVaapiMiniObject parent_instance;

  GstVaapiDisplay *display;
  VADisplay va_display;
  VAConfigID va_config;
  VAContextID va_context;
  GPtrArray *operations;
  GstVideoFormat format;
  GstVaapiScaleMethod scale_method;
  GArray *formats;
  GArray *forward_references;
  GArray *backward_references;
  GstVaapiRectangle crop_rect;
  GstVaapiRectangle target_rect;
  guint use_crop_rect:1;
  guint use_target_rect:1;
};

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
#if USE_VA_VPP
    {GST_VAAPI_DEINTERLACE_METHOD_WEAVE,
        "Weave deinterlacing", "weave"},
    {GST_VAAPI_DEINTERLACE_METHOD_MOTION_ADAPTIVE,
        "Motion adaptive deinterlacing", "motion-adaptive"},
    {GST_VAAPI_DEINTERLACE_METHOD_MOTION_COMPENSATED,
        "Motion compensated deinterlacing", "motion-compensated"},
#endif
    {0, NULL, NULL},
  };

  if (g_once_init_enter (&g_type)) {
    const GType type =
        g_enum_register_static ("GstVaapiDeinterlaceMethod", enum_values);
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
    g_once_init_leave (&g_type, type);
  }
  return g_type;
}

/* ------------------------------------------------------------------------- */
/* --- VPP Helpers                                                       --- */
/* ------------------------------------------------------------------------- */

#if USE_VA_VPP
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
#endif

/* ------------------------------------------------------------------------- */
/* --- VPP Operations                                                   --- */
/* ------------------------------------------------------------------------- */

#if USE_VA_VPP
#define DEFAULT_FORMAT  GST_VIDEO_FORMAT_UNKNOWN
#define DEFAULT_SCALING GST_VAAPI_SCALE_METHOD_DEFAULT

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
  PROP_SKINTONE = GST_VAAPI_FILTER_OP_SKINTONE,

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
      DEFAULT_SCALING, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

#if VA_CHECK_VERSION(0,36,0)
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
  g_slice_free (GstVaapiFilterOpData, op_data);
}

static inline gpointer
op_data_new (GstVaapiFilterOp op, GParamSpec * pspec)
{
  GstVaapiFilterOpData *op_data;

  op_data = g_slice_new0 (GstVaapiFilterOpData);
  if (!op_data)
    return NULL;

  op_data->op = op;
  op_data->pspec = pspec;
  op_data->ref_count = 1;
  op_data->va_buffer = VA_INVALID_ID;

  switch (op) {
    case GST_VAAPI_FILTER_OP_FORMAT:
    case GST_VAAPI_FILTER_OP_CROP:
    case GST_VAAPI_FILTER_OP_SCALING:
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
#if VA_CHECK_VERSION(0,36,0)
    case GST_VAAPI_FILTER_OP_SKINTONE:
      op_data->va_type = VAProcFilterSkinToneEnhancement;
      op_data->va_buffer_size = sizeof (VAProcFilterParameterBuffer);
      break;
#endif
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

  op_data->va_caps = g_memdup (filter_cap, op_data->va_cap_size * va_num_caps);
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
          goto error;
      }
      if (!op_data_ensure_caps (op_data, filter_caps, num_filter_caps))
        goto error;
      g_ptr_array_add (ops, op_data_ref (op_data));
    }
    free (filter_caps);
    filter_caps = NULL;
  }

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
#endif

/* Determine the set of supported VPP operations by the specific
   filter, or known to this library if filter is NULL */
static GPtrArray *
get_operations (GstVaapiFilter * filter)
{
#if USE_VA_VPP
  GPtrArray *ops;

  if (filter && filter->operations)
    return g_ptr_array_ref (filter->operations);

  ops = get_operations_default ();
  if (!ops)
    return NULL;
  return filter ? get_operations_ordered (filter, ops) : ops;
#endif
  return NULL;
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
#if USE_VA_VPP
static inline gboolean
op_ensure_buffer (GstVaapiFilter * filter, GstVaapiFilterOpData * op_data)
{
  if (G_LIKELY (op_data->va_buffer != VA_INVALID_ID))
    return TRUE;
  return vaapi_create_buffer (filter->va_display, filter->va_context,
      VAProcFilterParameterBufferType, op_data->va_buffer_size, NULL,
      &op_data->va_buffer, NULL);
}
#endif

/* Update a generic filter (float value) */
#if USE_VA_VPP
static gboolean
op_set_generic_unlocked (GstVaapiFilter * filter,
    GstVaapiFilterOpData * op_data, gfloat value)
{
  VAProcFilterParameterBuffer *buf;
  VAProcFilterCap *filter_cap;
  gfloat va_value;

  if (!op_data || !op_ensure_buffer (filter, op_data))
    return FALSE;

  op_data->is_enabled =
      (value != G_PARAM_SPEC_FLOAT (op_data->pspec)->default_value);
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
#endif

static inline gboolean
op_set_generic (GstVaapiFilter * filter, GstVaapiFilterOpData * op_data,
    gfloat value)
{
  gboolean success = FALSE;

#if USE_VA_VPP
  GST_VAAPI_DISPLAY_LOCK (filter->display);
  success = op_set_generic_unlocked (filter, op_data, value);
  GST_VAAPI_DISPLAY_UNLOCK (filter->display);
#endif
  return success;
}

/* Update the color balance filter */
#if USE_VA_VPP
static gboolean
op_set_color_balance_unlocked (GstVaapiFilter * filter,
    GstVaapiFilterOpData * op_data, gfloat value)
{
  VAProcFilterParameterBufferColorBalance *buf;
  VAProcFilterCapColorBalance *filter_cap;
  gfloat va_value;

  if (!op_data || !op_ensure_buffer (filter, op_data))
    return FALSE;

  op_data->is_enabled =
      (value != G_PARAM_SPEC_FLOAT (op_data->pspec)->default_value);
  if (!op_data->is_enabled)
    return TRUE;

  filter_cap = op_data->va_caps;
  if (!op_data_get_value_float (op_data, &filter_cap->range, value, &va_value))
    return FALSE;

  buf = vaapi_map_buffer (filter->va_display, op_data->va_buffer);
  if (!buf)
    return FALSE;

  buf->type = op_data->va_type;
  buf->attrib = op_data->va_subtype;
  buf->value = va_value;
  vaapi_unmap_buffer (filter->va_display, op_data->va_buffer, NULL);
  return TRUE;
}
#endif

static inline gboolean
op_set_color_balance (GstVaapiFilter * filter, GstVaapiFilterOpData * op_data,
    gfloat value)
{
  gboolean success = FALSE;

#if USE_VA_VPP
  GST_VAAPI_DISPLAY_LOCK (filter->display);
  success = op_set_color_balance_unlocked (filter, op_data, value);
  GST_VAAPI_DISPLAY_UNLOCK (filter->display);
#endif
  return success;
}

/* Update deinterlace filter */
#if USE_VA_VPP
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
#endif

static inline gboolean
op_set_deinterlace (GstVaapiFilter * filter, GstVaapiFilterOpData * op_data,
    GstVaapiDeinterlaceMethod method, guint flags)
{
  gboolean success = FALSE;

#if USE_VA_VPP
  GST_VAAPI_DISPLAY_LOCK (filter->display);
  success = op_set_deinterlace_unlocked (filter, op_data, method, flags);
  GST_VAAPI_DISPLAY_UNLOCK (filter->display);
#endif
  return success;
}

/* Update skin tone enhancement */
#if USE_VA_VPP
static gboolean
op_set_skintone_unlocked (GstVaapiFilter * filter,
    GstVaapiFilterOpData * op_data, gboolean value)
{
  VAProcFilterParameterBuffer *buf;

  if (!op_data || !op_ensure_buffer (filter, op_data))
    return FALSE;

  op_data->is_enabled = value;
  if (!op_data->is_enabled)
    return TRUE;

  buf = vaapi_map_buffer (filter->va_display, op_data->va_buffer);
  if (!buf)
    return FALSE;
  buf->type = op_data->va_type;
  buf->value = 0;
  vaapi_unmap_buffer (filter->va_display, op_data->va_buffer, NULL);
  return TRUE;
}
#endif

static inline gboolean
op_set_skintone (GstVaapiFilter * filter, GstVaapiFilterOpData * op_data,
    gboolean enhance)
{
  gboolean success = FALSE;

#if USE_VA_VPP
  GST_VAAPI_DISPLAY_LOCK (filter->display);
  success = op_set_skintone_unlocked (filter, op_data, enhance);
  GST_VAAPI_DISPLAY_UNLOCK (filter->display);
#endif
  return success;
}


static gboolean
deint_refs_set (GArray * refs, GstVaapiSurface ** surfaces, guint num_surfaces)
{
  guint i;

  if (num_surfaces > 0 && !surfaces)
    return FALSE;

  for (i = 0; i < num_surfaces; i++)
    g_array_append_val (refs, GST_VAAPI_OBJECT_ID (surfaces[i]));
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
/* --- Surface Formats                                                   --- */
/* ------------------------------------------------------------------------- */

static gboolean
ensure_formats (GstVaapiFilter * filter)
{
  if (G_LIKELY (filter->formats))
    return TRUE;

  filter->formats = gst_vaapi_get_surface_formats (filter->display,
      filter->va_config);
  return (filter->formats != NULL);
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

  if (is_special_format (format) || !filter->formats)
    return FALSE;

  for (i = 0; i < filter->formats->len; i++) {
    if (g_array_index (filter->formats, GstVideoFormat, i) == format)
      return TRUE;
  }
  return FALSE;
}

/* ------------------------------------------------------------------------- */
/* --- Interface                                                         --- */
/* ------------------------------------------------------------------------- */

#if USE_VA_VPP
static gboolean
gst_vaapi_filter_init (GstVaapiFilter * filter, GstVaapiDisplay * display)
{
  VAStatus va_status;

  filter->display = gst_vaapi_display_ref (display);
  filter->va_display = GST_VAAPI_DISPLAY_VADISPLAY (display);
  filter->va_config = VA_INVALID_ID;
  filter->va_context = VA_INVALID_ID;
  filter->format = DEFAULT_FORMAT;

  filter->forward_references =
      g_array_sized_new (FALSE, FALSE, sizeof (VASurfaceID), 4);
  if (!filter->forward_references)
    return FALSE;

  filter->backward_references =
      g_array_sized_new (FALSE, FALSE, sizeof (VASurfaceID), 4);
  if (!filter->backward_references)
    return FALSE;

  if (!GST_VAAPI_DISPLAY_HAS_VPP (display))
    return FALSE;

  va_status = vaCreateConfig (filter->va_display, VAProfileNone,
      VAEntrypointVideoProc, NULL, 0, &filter->va_config);
  if (!vaapi_check_status (va_status, "vaCreateConfig() [VPP]"))
    return FALSE;

  va_status = vaCreateContext (filter->va_display, filter->va_config, 0, 0, 0,
      NULL, 0, &filter->va_context);
  if (!vaapi_check_status (va_status, "vaCreateContext() [VPP]"))
    return FALSE;
  return TRUE;
}

static void
gst_vaapi_filter_finalize (GstVaapiFilter * filter)
{
  guint i;

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

  if (filter->forward_references) {
    g_array_unref (filter->forward_references);
    filter->forward_references = NULL;
  }

  if (filter->backward_references) {
    g_array_unref (filter->backward_references);
    filter->backward_references = NULL;
  }

  if (filter->formats) {
    g_array_unref (filter->formats);
    filter->formats = NULL;
  }
}

static inline const GstVaapiMiniObjectClass *
gst_vaapi_filter_class (void)
{
  static const GstVaapiMiniObjectClass GstVaapiFilterClass = {
    sizeof (GstVaapiFilter),
    (GDestroyNotify) gst_vaapi_filter_finalize
  };
  return &GstVaapiFilterClass;
}
#endif

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
#if USE_VA_VPP
  GstVaapiFilter *filter;

  filter = (GstVaapiFilter *)
      gst_vaapi_mini_object_new0 (gst_vaapi_filter_class ());
  if (!filter)
    return NULL;

  if (!gst_vaapi_filter_init (filter, display))
    goto error;
  return filter;

  /* ERRORS */
error:
  {
    gst_vaapi_filter_unref (filter);
    return NULL;
  }
#else
  GST_WARNING ("video processing is not supported, "
      "please consider an upgrade to VA-API >= 0.34");
  return NULL;
#endif
}

/**
 * gst_vaapi_filter_ref:
 * @filter: a #GstVaapiFilter
 *
 * Atomically increases the reference count of the given @filter by one.
 *
 * Returns: The same @filter argument
 */
GstVaapiFilter *
gst_vaapi_filter_ref (GstVaapiFilter * filter)
{
  g_return_val_if_fail (filter != NULL, NULL);

  return
      GST_VAAPI_FILTER (gst_vaapi_mini_object_ref (GST_VAAPI_MINI_OBJECT
          (filter)));
}

/**
 * gst_vaapi_filter_unref:
 * @filter: a #GstVaapiFilter
 *
 * Atomically decreases the reference count of the @filter by one. If
 * the reference count reaches zero, the filter will be free'd.
 */
void
gst_vaapi_filter_unref (GstVaapiFilter * filter)
{
  g_return_if_fail (filter != NULL);

  gst_vaapi_mini_object_unref (GST_VAAPI_MINI_OBJECT (filter));
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

  gst_vaapi_mini_object_replace ((GstVaapiMiniObject **) old_filter_ptr,
      GST_VAAPI_MINI_OBJECT (new_filter));
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
#if USE_VA_VPP
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
              G_PARAM_SPEC_FLOAT (op_data->pspec)->default_value));
    case GST_VAAPI_FILTER_OP_HUE:
    case GST_VAAPI_FILTER_OP_SATURATION:
    case GST_VAAPI_FILTER_OP_BRIGHTNESS:
    case GST_VAAPI_FILTER_OP_CONTRAST:
      return op_set_color_balance (filter, op_data,
          (value ? g_value_get_float (value) :
              G_PARAM_SPEC_FLOAT (op_data->pspec)->default_value));
    case GST_VAAPI_FILTER_OP_DEINTERLACING:
      return op_set_deinterlace (filter, op_data,
          (value ? g_value_get_enum (value) :
              G_PARAM_SPEC_ENUM (op_data->pspec)->default_value), 0);
      break;
    case GST_VAAPI_FILTER_OP_SCALING:
      return gst_vaapi_filter_set_scaling (filter, value ?
          g_value_get_enum (value) : DEFAULT_SCALING);
    case GST_VAAPI_FILTER_OP_SKINTONE:
      return op_set_skintone (filter, op_data,
          (value ? g_value_get_boolean (value) :
              G_PARAM_SPEC_BOOLEAN (op_data->pspec)->default_value));
    default:
      break;
  }
#endif
  return FALSE;
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
#if USE_VA_VPP
  VAProcPipelineParameterBuffer *pipeline_param = NULL;
  VABufferID pipeline_param_buf_id = VA_INVALID_ID;
  VABufferID filters[N_PROPERTIES];
  VAProcPipelineCaps pipeline_caps;
  guint i, num_filters = 0;
  VAStatus va_status;
  VARectangle src_rect, dst_rect;

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
  pipeline_param->surface = GST_VAAPI_OBJECT_ID (src_surface);
  pipeline_param->surface_region = &src_rect;
  pipeline_param->surface_color_standard = VAProcColorStandardNone;
  pipeline_param->output_region = &dst_rect;
  pipeline_param->output_color_standard = VAProcColorStandardNone;
  pipeline_param->output_background_color = 0xff000000;
  pipeline_param->filter_flags = from_GstVaapiSurfaceRenderFlags (flags) |
      from_GstVaapiScaleMethod (filter->scale_method);
  pipeline_param->filters = filters;
  pipeline_param->num_filters = num_filters;

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
      GST_VAAPI_OBJECT_ID (dst_surface));
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
#endif
  return GST_VAAPI_FILTER_STATUS_ERROR_UNSUPPORTED_OPERATION;
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
 *
 * Determines the set of supported source or target formats for video
 * processing.  The caller owns an extra reference to the resulting
 * array of #GstVideoFormat elements, so it shall be released with
 * g_array_unref() after usage.
 *
 * Return value: the set of supported target formats for video processing.
 */
GArray *
gst_vaapi_filter_get_formats (GstVaapiFilter * filter)
{
  g_return_val_if_fail (filter != NULL, NULL);

  if (!ensure_formats (filter))
    return NULL;
  return g_array_ref (filter->formats);
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

  if (!ensure_formats (filter))
    return FALSE;

  if (!is_special_format (format) && !find_format (filter, format))
    return FALSE;

  filter->format = format;
  return TRUE;
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

static inline gfloat
op_get_float_default_value (GstVaapiFilter * filter,
    GstVaapiFilterOpData * op_data)
{
#if USE_VA_VPP
  GParamSpecFloat *const pspec = G_PARAM_SPEC_FLOAT (op_data->pspec);
  return pspec->default_value;
#endif
  return 0.0;
}

gfloat
gst_vaapi_filter_get_denoising_level_default (GstVaapiFilter * filter)
{
  g_return_val_if_fail (filter != NULL, FALSE);

  return op_get_float_default_value (filter,
      find_operation (filter, GST_VAAPI_FILTER_OP_DENOISE));
}

gfloat
gst_vaapi_filter_get_sharpening_level_default (GstVaapiFilter * filter)
{
  g_return_val_if_fail (filter != NULL, FALSE);

  return op_get_float_default_value (filter,
      find_operation (filter, GST_VAAPI_FILTER_OP_SHARPEN));
}

gfloat
gst_vaapi_filter_get_hue_default (GstVaapiFilter * filter)
{
  g_return_val_if_fail (filter != NULL, FALSE);

  return op_get_float_default_value (filter,
      find_operation (filter, GST_VAAPI_FILTER_OP_HUE));
}

gfloat
gst_vaapi_filter_get_saturation_default (GstVaapiFilter * filter)
{
  g_return_val_if_fail (filter != NULL, FALSE);

  return op_get_float_default_value (filter,
      find_operation (filter, GST_VAAPI_FILTER_OP_SATURATION));
}

gfloat
gst_vaapi_filter_get_brightness_default (GstVaapiFilter * filter)
{
  g_return_val_if_fail (filter != NULL, FALSE);

  return op_get_float_default_value (filter,
      find_operation (filter, GST_VAAPI_FILTER_OP_BRIGHTNESS));
}

gfloat
gst_vaapi_filter_get_contrast_default (GstVaapiFilter * filter)
{
  g_return_val_if_fail (filter != NULL, FALSE);

  return op_get_float_default_value (filter,
      find_operation (filter, GST_VAAPI_FILTER_OP_CONTRAST));
}

GstVaapiScaleMethod
gst_vaapi_filter_get_scaling_default (GstVaapiFilter * filter)
{
  g_return_val_if_fail (filter != NULL, FALSE);

  return DEFAULT_SCALING;
}

gboolean
gst_vaapi_filter_get_skintone_default (GstVaapiFilter * filter)
{
  g_return_val_if_fail (filter != NULL, FALSE);

  return FALSE;
}
