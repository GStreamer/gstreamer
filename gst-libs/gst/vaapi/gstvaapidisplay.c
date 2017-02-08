/*
 *  gstvaapidisplay.c - VA display abstraction
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@splitted-desktop.com>
 *  Copyright (C) 2011-2013 Intel Corporation
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

/**
 * SECTION:gstvaapidisplay
 * @short_description: VA display abstraction
 */

#include "sysdeps.h"
#include <string.h>
#include "gstvaapiutils.h"
#include "gstvaapivalue.h"
#include "gstvaapidisplay.h"
#include "gstvaapitexturemap.h"
#include "gstvaapidisplay_priv.h"
#include "gstvaapiworkarounds.h"

/* Debug category for all vaapi libs */
GST_DEBUG_CATEGORY (gst_debug_vaapi);

/* Debug category for VaapiDisplay */
GST_DEBUG_CATEGORY (gst_debug_vaapi_display);
#define GST_CAT_DEFAULT gst_debug_vaapi_display

#define _do_init \
    G_ADD_PRIVATE (GstVaapiDisplay); \
    GST_DEBUG_CATEGORY_INIT (gst_debug_vaapi_display, \
        "vaapidisplay", 0, "VA-API Display");

G_DEFINE_TYPE_WITH_CODE (GstVaapiDisplay, gst_vaapi_display, GST_TYPE_OBJECT,
    _do_init);

/* Ensure those symbols are actually defined in the resulting libraries */
#undef gst_vaapi_display_ref
#undef gst_vaapi_display_unref
#undef gst_vaapi_display_replace

typedef struct _GstVaapiConfig GstVaapiConfig;
struct _GstVaapiConfig
{
  GstVaapiProfile profile;
  GstVaapiEntrypoint entrypoint;
};

typedef struct _GstVaapiProperty GstVaapiProperty;
struct _GstVaapiProperty
{
  const gchar *name;
  VADisplayAttribute attribute;
  gint old_value;
};

typedef struct _GstVaapiFormatInfo GstVaapiFormatInfo;
struct _GstVaapiFormatInfo
{
  GstVideoFormat format;
  guint flags;
};

#define DEFAULT_RENDER_MODE     GST_VAAPI_RENDER_MODE_TEXTURE
#define DEFAULT_ROTATION        GST_VAAPI_ROTATION_0

enum
{
  PROP_0,

  PROP_RENDER_MODE,
  PROP_ROTATION,
  PROP_HUE,
  PROP_SATURATION,
  PROP_BRIGHTNESS,
  PROP_CONTRAST,

  N_PROPERTIES
};

static GstVaapiDisplayCache *g_display_cache;
static GMutex g_display_cache_lock;

static GParamSpec *g_properties[N_PROPERTIES] = { NULL, };

static void gst_vaapi_display_properties_init (void);

static gboolean
get_attribute (GstVaapiDisplay * display, VADisplayAttribType type,
    gint * value);

static gboolean
set_attribute (GstVaapiDisplay * display, VADisplayAttribType type, gint value);

static gboolean
get_color_balance (GstVaapiDisplay * display, guint prop_id, gfloat * v);

static gboolean
set_color_balance (GstVaapiDisplay * display, guint prop_id, gfloat v);

static void
libgstvaapi_init_once (void)
{
  static gsize g_once = FALSE;

  if (!g_once_init_enter (&g_once))
    return;

  GST_DEBUG_CATEGORY_INIT (gst_debug_vaapi, "vaapi", 0, "VA-API helper");

  gst_vaapi_display_properties_init ();

  g_once_init_leave (&g_once, TRUE);
}

static GstVaapiDisplayCache *
get_display_cache (void)
{
  GstVaapiDisplayCache *cache = NULL;

  g_mutex_lock (&g_display_cache_lock);
  if (!g_display_cache)
    g_display_cache = gst_vaapi_display_cache_new ();
  if (g_display_cache)
    cache = gst_vaapi_display_cache_ref (g_display_cache);
  g_mutex_unlock (&g_display_cache_lock);
  return cache;
}

static void
free_display_cache (void)
{
  g_mutex_lock (&g_display_cache_lock);
  if (g_display_cache && gst_vaapi_display_cache_is_empty (g_display_cache))
    gst_vaapi_display_cache_replace (&g_display_cache, NULL);
  g_mutex_unlock (&g_display_cache_lock);
}

/* GstVaapiDisplayType enumerations */
GType
gst_vaapi_display_type_get_type (void)
{
  static GType g_type = 0;

  static const GEnumValue display_types[] = {
    {GST_VAAPI_DISPLAY_TYPE_ANY,
        "Auto detection", "any"},
#if USE_X11
    {GST_VAAPI_DISPLAY_TYPE_X11,
        "VA/X11 display", "x11"},
#endif
#if USE_GLX
    {GST_VAAPI_DISPLAY_TYPE_GLX,
        "VA/GLX display", "glx"},
#endif
#if USE_EGL
    {GST_VAAPI_DISPLAY_TYPE_EGL,
        "VA/EGL display", "egl"},
#endif
#if USE_WAYLAND
    {GST_VAAPI_DISPLAY_TYPE_WAYLAND,
        "VA/Wayland display", "wayland"},
#endif
#if USE_DRM
    {GST_VAAPI_DISPLAY_TYPE_DRM,
        "VA/DRM display", "drm"},
#endif
    {0, NULL, NULL},
  };

  if (!g_type)
    g_type = g_enum_register_static ("GstVaapiDisplayType", display_types);
  return g_type;
}

/**
 * gst_vaapi_display_type_is_compatible:
 * @type1: the #GstVaapiDisplayType to test
 * @type2: the reference #GstVaapiDisplayType
 *
 * Compares whether #GstVaapiDisplay @type1 is compatible with @type2.
 * That is, if @type2 is in "any" category, or derived from @type1.
 *
 * Returns: %TRUE if @type1 is compatible with @type2, %FALSE otherwise.
 */
gboolean
gst_vaapi_display_type_is_compatible (GstVaapiDisplayType type1,
    GstVaapiDisplayType type2)
{
  if (type1 == type2)
    return TRUE;

  switch (type1) {
    case GST_VAAPI_DISPLAY_TYPE_GLX:
      if (type2 == GST_VAAPI_DISPLAY_TYPE_X11)
        return TRUE;
      break;
    default:
      break;
  }
  return type2 == GST_VAAPI_DISPLAY_TYPE_ANY;
}

/* Append GstVideoFormat to formats array */
static inline void
append_format (GArray * formats, GstVideoFormat format, guint flags)
{
  GstVaapiFormatInfo fi;

  fi.format = format;
  fi.flags = flags;
  g_array_append_val (formats, fi);
}

/* Append VAImageFormats to formats array */
static void
append_formats (GArray * formats, const VAImageFormat * va_formats,
    guint * flags, guint n)
{
  GstVideoFormat format;
  const GstVaapiFormatInfo *YV12_fip = NULL;
  const GstVaapiFormatInfo *I420_fip = NULL;
  guint i;

  for (i = 0; i < n; i++) {
    const VAImageFormat *const va_format = &va_formats[i];
    const GstVaapiFormatInfo **fipp;

    format = gst_vaapi_video_format_from_va_format (va_format);
    if (format == GST_VIDEO_FORMAT_UNKNOWN) {
      GST_DEBUG ("unsupported format %" GST_FOURCC_FORMAT,
          GST_FOURCC_ARGS (va_format->fourcc));
      continue;
    }
    append_format (formats, format, flags ? flags[i] : 0);

    switch (format) {
      case GST_VIDEO_FORMAT_YV12:
        fipp = &YV12_fip;
        break;
      case GST_VIDEO_FORMAT_I420:
        fipp = &I420_fip;
        break;
      default:
        fipp = NULL;
        break;
    }
    if (fipp)
      *fipp = &g_array_index (formats, GstVaapiFormatInfo, formats->len - 1);
  }

  /* Append I420 (resp. YV12) format if YV12 (resp. I420) is not
     supported by the underlying driver */
  if (YV12_fip && !I420_fip)
    append_format (formats, GST_VIDEO_FORMAT_I420, YV12_fip->flags);
  else if (I420_fip && !YV12_fip)
    append_format (formats, GST_VIDEO_FORMAT_YV12, I420_fip->flags);
}

/* Sort image formats. Prefer YUV formats first */
static gint
compare_yuv_formats (gconstpointer a, gconstpointer b)
{
  const GstVideoFormat fmt1 = ((GstVaapiFormatInfo *) a)->format;
  const GstVideoFormat fmt2 = ((GstVaapiFormatInfo *) b)->format;

  const gboolean is_fmt1_yuv = gst_vaapi_video_format_is_yuv (fmt1);
  const gboolean is_fmt2_yuv = gst_vaapi_video_format_is_yuv (fmt2);

  if (is_fmt1_yuv != is_fmt2_yuv)
    return is_fmt1_yuv ? -1 : 1;

  return ((gint) gst_vaapi_video_format_get_score (fmt1) -
      (gint) gst_vaapi_video_format_get_score (fmt2));
}

/* Sort subpicture formats. Prefer RGB formats first */
static gint
compare_rgb_formats (gconstpointer a, gconstpointer b)
{
  const GstVideoFormat fmt1 = ((GstVaapiFormatInfo *) a)->format;
  const GstVideoFormat fmt2 = ((GstVaapiFormatInfo *) b)->format;

  const gboolean is_fmt1_rgb = gst_vaapi_video_format_is_rgb (fmt1);
  const gboolean is_fmt2_rgb = gst_vaapi_video_format_is_rgb (fmt2);

  if (is_fmt1_rgb != is_fmt2_rgb)
    return is_fmt1_rgb ? -1 : 1;

  return ((gint) gst_vaapi_video_format_get_score (fmt1) -
      (gint) gst_vaapi_video_format_get_score (fmt2));
}

/* Check if configs array contains profile at entrypoint */
static inline gboolean
find_config (GArray * configs,
    GstVaapiProfile profile, GstVaapiEntrypoint entrypoint)
{
  GstVaapiConfig *config;
  guint i;

  if (!configs)
    return FALSE;

  for (i = 0; i < configs->len; i++) {
    config = &g_array_index (configs, GstVaapiConfig, i);
    if (config->profile == profile && config->entrypoint == entrypoint)
      return TRUE;
  }
  return FALSE;
}

/* HACK: append H.263 Baseline profile if MPEG-4:2 Simple profile is supported */
static void
append_h263_config (GArray * configs)
{
  GstVaapiConfig *config, tmp_config;
  GstVaapiConfig *mpeg4_simple_config = NULL;
  GstVaapiConfig *h263_baseline_config = NULL;
  guint i;

  if (!WORKAROUND_H263_BASELINE_DECODE_PROFILE)
    return;

  if (!configs)
    return;

  for (i = 0; i < configs->len; i++) {
    config = &g_array_index (configs, GstVaapiConfig, i);
    if (config->profile == GST_VAAPI_PROFILE_MPEG4_SIMPLE)
      mpeg4_simple_config = config;
    else if (config->profile == GST_VAAPI_PROFILE_H263_BASELINE)
      h263_baseline_config = config;
  }

  if (mpeg4_simple_config && !h263_baseline_config) {
    tmp_config = *mpeg4_simple_config;
    tmp_config.profile = GST_VAAPI_PROFILE_H263_BASELINE;
    g_array_append_val (configs, tmp_config);
  }
}

/* Sort profiles. Group per codec */
static gint
compare_profiles (gconstpointer a, gconstpointer b)
{
  const GstVaapiConfig *const config1 = (GstVaapiConfig *) a;
  const GstVaapiConfig *const config2 = (GstVaapiConfig *) b;

  if (config1->profile == config2->profile)
    return config1->entrypoint - config2->entrypoint;

  return config1->profile - config2->profile;
}

/* Convert configs array to profiles as GstCaps */
static GArray *
get_profiles (GArray * configs)
{
  GstVaapiConfig *config;
  GArray *out_profiles;
  guint i;

  if (!configs)
    return NULL;

  out_profiles = g_array_new (FALSE, FALSE, sizeof (GstVaapiProfile));
  if (!out_profiles)
    return NULL;

  for (i = 0; i < configs->len; i++) {
    config = &g_array_index (configs, GstVaapiConfig, i);
    g_array_append_val (out_profiles, config->profile);
  }
  return out_profiles;
}

/* Find format info */
static const GstVaapiFormatInfo *
find_format_info (GArray * formats, GstVideoFormat format)
{
  const GstVaapiFormatInfo *fip;
  guint i;

  for (i = 0; i < formats->len; i++) {
    fip = &g_array_index (formats, GstVaapiFormatInfo, i);
    if (fip->format == format)
      return fip;
  }
  return NULL;
}

/* Check if formats array contains format */
static inline gboolean
find_format (GArray * formats, GstVideoFormat format)
{
  return find_format_info (formats, format) != NULL;
}

/* Convert formats array to GstCaps */
static GArray *
get_formats (GArray * formats)
{
  const GstVaapiFormatInfo *fip;
  GArray *out_formats;
  guint i;

  out_formats = g_array_new (FALSE, FALSE, sizeof (GstVideoFormat));
  if (!out_formats)
    return NULL;

  for (i = 0; i < formats->len; i++) {
    fip = &g_array_index (formats, GstVaapiFormatInfo, i);
    g_array_append_val (out_formats, fip->format);
  }
  return out_formats;
}

/* Find display attribute */
static const GstVaapiProperty *
find_property (GArray * properties, const gchar * name)
{
  GstVaapiProperty *prop;
  guint i;

  if (!name)
    return NULL;

  for (i = 0; i < properties->len; i++) {
    prop = &g_array_index (properties, GstVaapiProperty, i);
    if (strcmp (prop->name, name) == 0)
      return prop;
  }
  return NULL;
}

#if 0
static const GstVaapiProperty *
find_property_by_type (GArray * properties, VADisplayAttribType type)
{
  GstVaapiProperty *prop;
  guint i;

  for (i = 0; i < properties->len; i++) {
    prop = &g_array_index (properties, GstVaapiProperty, i);
    if (prop->attribute.type == type)
      return prop;
  }
  return NULL;
}
#endif

static inline const GstVaapiProperty *
find_property_by_pspec (GstVaapiDisplay * display, GParamSpec * pspec)
{
  GstVaapiDisplayPrivate *const priv = GST_VAAPI_DISPLAY_GET_PRIVATE (display);

  return find_property (priv->properties, pspec->name);
}

static guint
find_property_id (const gchar * name)
{
  typedef struct
  {
    const gchar *name;
    guint id;
  } property_map;

  static const property_map g_property_map[] = {
    {GST_VAAPI_DISPLAY_PROP_RENDER_MODE, PROP_RENDER_MODE},
    {GST_VAAPI_DISPLAY_PROP_ROTATION, PROP_ROTATION},
    {GST_VAAPI_DISPLAY_PROP_HUE, PROP_HUE},
    {GST_VAAPI_DISPLAY_PROP_SATURATION, PROP_SATURATION},
    {GST_VAAPI_DISPLAY_PROP_BRIGHTNESS, PROP_BRIGHTNESS},
    {GST_VAAPI_DISPLAY_PROP_CONTRAST, PROP_CONTRAST},
    {NULL,}
  };

  const property_map *m;
  for (m = g_property_map; m->name != NULL; m++) {
    if (strcmp (m->name, name) == 0)
      return m->id;
  }
  return 0;
}

/* Initialize VA profiles (decoders, encoders) */
static gboolean
ensure_profiles (GstVaapiDisplay * display)
{
  GstVaapiDisplayPrivate *const priv = GST_VAAPI_DISPLAY_GET_PRIVATE (display);
  VAProfile *profiles = NULL;
  VAEntrypoint *entrypoints = NULL;
  gint i, j, n, num_entrypoints;
  VAStatus status;
  gboolean success = FALSE;

  if (priv->has_profiles)
    return TRUE;

  priv->decoders = g_array_new (FALSE, FALSE, sizeof (GstVaapiConfig));
  if (!priv->decoders)
    goto cleanup;
  priv->encoders = g_array_new (FALSE, FALSE, sizeof (GstVaapiConfig));
  if (!priv->encoders)
    goto cleanup;
  priv->has_profiles = TRUE;

  /* VA profiles */
  profiles = g_new (VAProfile, vaMaxNumProfiles (priv->display));
  if (!profiles)
    goto cleanup;
  entrypoints = g_new (VAEntrypoint, vaMaxNumEntrypoints (priv->display));
  if (!entrypoints)
    goto cleanup;

  n = 0;
  status = vaQueryConfigProfiles (priv->display, profiles, &n);
  if (!vaapi_check_status (status, "vaQueryConfigProfiles()"))
    goto cleanup;

  GST_DEBUG ("%d profiles", n);
  for (i = 0; i < n; i++) {
#if VA_CHECK_VERSION(0,34,0)
    /* Introduced in VA/VPP API */
    if (profiles[i] == VAProfileNone)
      continue;
#endif
    GST_DEBUG ("  %s", string_of_VAProfile (profiles[i]));
  }

  for (i = 0; i < n; i++) {
    GstVaapiConfig config;

    config.profile = gst_vaapi_profile (profiles[i]);
    if (!config.profile)
      continue;

    status = vaQueryConfigEntrypoints (priv->display,
        profiles[i], entrypoints, &num_entrypoints);
    if (!vaapi_check_status (status, "vaQueryConfigEntrypoints()"))
      continue;

    for (j = 0; j < num_entrypoints; j++) {
      config.entrypoint = gst_vaapi_entrypoint (entrypoints[j]);
      switch (config.entrypoint) {
        case GST_VAAPI_ENTRYPOINT_VLD:
        case GST_VAAPI_ENTRYPOINT_IDCT:
        case GST_VAAPI_ENTRYPOINT_MOCO:
          g_array_append_val (priv->decoders, config);
          break;
        case GST_VAAPI_ENTRYPOINT_SLICE_ENCODE:
        case GST_VAAPI_ENTRYPOINT_PICTURE_ENCODE:
        case GST_VAAPI_ENTRYPOINT_SLICE_ENCODE_LP:
          g_array_append_val (priv->encoders, config);
          break;
      }
    }
  }
  append_h263_config (priv->decoders);

  g_array_sort (priv->decoders, compare_profiles);
  g_array_sort (priv->encoders, compare_profiles);

  /* Video processing API */
#if USE_VA_VPP
  status = vaQueryConfigEntrypoints (priv->display, VAProfileNone,
      entrypoints, &num_entrypoints);
  if (vaapi_check_status (status, "vaQueryEntrypoints() [VAProfileNone]")) {
    for (j = 0; j < num_entrypoints; j++) {
      if (entrypoints[j] == VAEntrypointVideoProc)
        priv->has_vpp = TRUE;
    }
  }
#endif
  success = TRUE;

cleanup:
  g_free (profiles);
  g_free (entrypoints);
  return success;
}

/* Initialize VA display attributes */
static gboolean
ensure_properties (GstVaapiDisplay * display)
{
  GstVaapiDisplayPrivate *const priv = GST_VAAPI_DISPLAY_GET_PRIVATE (display);
  VADisplayAttribute *display_attrs = NULL;
  VAStatus status;
  gint i, n;
  gboolean success = FALSE;

  if (priv->properties)
    return TRUE;

  priv->properties = g_array_new (FALSE, FALSE, sizeof (GstVaapiProperty));
  if (!priv->properties)
    goto cleanup;

  /* VA display attributes */
  display_attrs =
      g_new (VADisplayAttribute, vaMaxNumDisplayAttributes (priv->display));
  if (!display_attrs)
    goto cleanup;

  n = 0;
  status = vaQueryDisplayAttributes (priv->display, display_attrs, &n);
  if (!vaapi_check_status (status, "vaQueryDisplayAttributes()"))
    goto cleanup;

  GST_DEBUG ("%d display attributes", n);
  for (i = 0; i < n; i++) {
    VADisplayAttribute *const attr = &display_attrs[i];
    GstVaapiProperty prop;
    gint value;

    GST_DEBUG ("  %s", string_of_VADisplayAttributeType (attr->type));

    switch (attr->type) {
#if !VA_CHECK_VERSION(0,34,0)
      case VADisplayAttribDirectSurface:
        prop.name = GST_VAAPI_DISPLAY_PROP_RENDER_MODE;
        break;
#endif
      case VADisplayAttribRenderMode:
        prop.name = GST_VAAPI_DISPLAY_PROP_RENDER_MODE;
        break;
      case VADisplayAttribRotation:
        prop.name = GST_VAAPI_DISPLAY_PROP_ROTATION;
        break;
      case VADisplayAttribHue:
        prop.name = GST_VAAPI_DISPLAY_PROP_HUE;
        break;
      case VADisplayAttribSaturation:
        prop.name = GST_VAAPI_DISPLAY_PROP_SATURATION;
        break;
      case VADisplayAttribBrightness:
        prop.name = GST_VAAPI_DISPLAY_PROP_BRIGHTNESS;
        break;
      case VADisplayAttribContrast:
        prop.name = GST_VAAPI_DISPLAY_PROP_CONTRAST;
        break;
      default:
        prop.name = NULL;
        break;
    }
    if (!prop.name)
      continue;

    /* Assume the attribute is really supported if we can get the
     * actual and current value */
    if (!get_attribute (display, attr->type, &value))
      continue;

    /* Some drivers (e.g. EMGD) have completely random initial
     * values */
    if (value < attr->min_value || value > attr->max_value)
      continue;

    prop.attribute = *attr;
    prop.old_value = value;
    g_array_append_val (priv->properties, prop);
  }
  success = TRUE;

cleanup:
  g_free (display_attrs);
  return success;
}

/* Initialize VA image formats */
static gboolean
ensure_image_formats (GstVaapiDisplay * display)
{
  GstVaapiDisplayPrivate *const priv = GST_VAAPI_DISPLAY_GET_PRIVATE (display);
  VAImageFormat *formats = NULL;
  VAStatus status;
  gint i, n;
  gboolean success = FALSE;

  if (priv->image_formats)
    return TRUE;

  priv->image_formats = g_array_new (FALSE, FALSE, sizeof (GstVaapiFormatInfo));
  if (!priv->image_formats)
    goto cleanup;

  /* VA image formats */
  formats = g_new (VAImageFormat, vaMaxNumImageFormats (priv->display));
  if (!formats)
    goto cleanup;

  n = 0;
  status = vaQueryImageFormats (priv->display, formats, &n);
  if (!vaapi_check_status (status, "vaQueryImageFormats()"))
    goto cleanup;

  GST_DEBUG ("%d image formats", n);
  for (i = 0; i < n; i++)
    GST_DEBUG ("  %" GST_FOURCC_FORMAT, GST_FOURCC_ARGS (formats[i].fourcc));

  append_formats (priv->image_formats, formats, NULL, n);
  g_array_sort (priv->image_formats, compare_yuv_formats);
  success = TRUE;

cleanup:
  g_free (formats);
  return success;
}

/* Initialize VA subpicture formats */
static gboolean
ensure_subpicture_formats (GstVaapiDisplay * display)
{
  GstVaapiDisplayPrivate *const priv = GST_VAAPI_DISPLAY_GET_PRIVATE (display);
  VAImageFormat *formats = NULL;
  unsigned int *flags = NULL;
  VAStatus status;
  guint i, n;
  gboolean success = FALSE;

  if (priv->subpicture_formats)
    return TRUE;

  priv->subpicture_formats =
      g_array_new (FALSE, FALSE, sizeof (GstVaapiFormatInfo));
  if (!priv->subpicture_formats)
    goto cleanup;

  /* VA subpicture formats */
  n = vaMaxNumSubpictureFormats (priv->display);
  formats = g_new (VAImageFormat, n);
  if (!formats)
    goto cleanup;
  flags = g_new (guint, n);
  if (!flags)
    goto cleanup;

  n = 0;
  status = vaQuerySubpictureFormats (priv->display, formats, flags, &n);
  if (!vaapi_check_status (status, "vaQuerySubpictureFormats()"))
    goto cleanup;

  GST_DEBUG ("%d subpicture formats", n);
  for (i = 0; i < n; i++) {
    GST_DEBUG ("  %" GST_FOURCC_FORMAT, GST_FOURCC_ARGS (formats[i].fourcc));
    flags[i] = to_GstVaapiSubpictureFlags (flags[i]);
  }

  append_formats (priv->subpicture_formats, formats, flags, n);
  g_array_sort (priv->subpicture_formats, compare_rgb_formats);
  success = TRUE;

cleanup:
  g_free (formats);
  g_free (flags);
  return success;
}

static void
gst_vaapi_display_calculate_pixel_aspect_ratio (GstVaapiDisplay * display)
{
  GstVaapiDisplayPrivate *const priv = GST_VAAPI_DISPLAY_GET_PRIVATE (display);
  gdouble ratio, delta;
  gint i, j, index, windex;

  static const gint par[][2] = {
    {1, 1},                     /* regular screen            */
    {16, 15},                   /* PAL TV                    */
    {11, 10},                   /* 525 line Rec.601 video    */
    {54, 59},                   /* 625 line Rec.601 video    */
    {64, 45},                   /* 1280x1024 on 16:9 display */
    {5, 3},                     /* 1280x1024 on  4:3 display */
    {4, 3}                      /*  800x600  on 16:9 display */
  };

  /* First, calculate the "real" ratio based on the X values;
   * which is the "physical" w/h divided by the w/h in pixels of the
   * display */
  if (!priv->width || !priv->height || !priv->width_mm || !priv->height_mm)
    ratio = 1.0;
  else
    ratio = (gdouble) (priv->width_mm * priv->height) /
        (priv->height_mm * priv->width);
  GST_DEBUG ("calculated pixel aspect ratio: %f", ratio);

  /* Now, find the one from par[][2] with the lowest delta to the real one */
#define DELTA(idx, w) (ABS(ratio - ((gdouble)par[idx][w] / par[idx][!(w)])))
  delta = DELTA (0, 0);
  index = 0;
  windex = 0;

  for (i = 1; i < G_N_ELEMENTS (par); i++) {
    for (j = 0; j < 2; j++) {
      const gdouble this_delta = DELTA (i, j);
      if (this_delta < delta) {
        index = i;
        windex = j;
        delta = this_delta;
      }
    }
  }
#undef DELTA

  priv->par_n = par[index][windex];
  priv->par_d = par[index][windex ^ 1];
}

static void
gst_vaapi_display_destroy (GstVaapiDisplay * display)
{
  GstVaapiDisplayPrivate *const priv = GST_VAAPI_DISPLAY_GET_PRIVATE (display);

  if (priv->decoders) {
    g_array_free (priv->decoders, TRUE);
    priv->decoders = NULL;
  }

  if (priv->encoders) {
    g_array_free (priv->encoders, TRUE);
    priv->encoders = NULL;
  }

  if (priv->image_formats) {
    g_array_free (priv->image_formats, TRUE);
    priv->image_formats = NULL;
  }

  if (priv->subpicture_formats) {
    g_array_free (priv->subpicture_formats, TRUE);
    priv->subpicture_formats = NULL;
  }

  if (priv->properties) {
    g_array_free (priv->properties, TRUE);
    priv->properties = NULL;
  }

  if (priv->display) {
    if (!priv->parent)
      vaTerminate (priv->display);
    priv->display = NULL;
  }

  if (!priv->use_foreign_display) {
    GstVaapiDisplayClass *klass = GST_VAAPI_DISPLAY_GET_CLASS (display);
    if (klass->close_display)
      klass->close_display (display);
  }

  g_free (priv->display_name);
  priv->display_name = NULL;

  g_free (priv->vendor_string);
  priv->vendor_string = NULL;

  gst_vaapi_display_replace_internal (&priv->parent, NULL);

  if (priv->cache) {
    gst_vaapi_display_cache_lock (priv->cache);
    gst_vaapi_display_cache_remove (priv->cache, display);
    gst_vaapi_display_cache_unlock (priv->cache);
  }
  gst_vaapi_display_cache_replace (&priv->cache, NULL);
  free_display_cache ();
}

static gboolean
gst_vaapi_display_create_unlocked (GstVaapiDisplay * display,
    GstVaapiDisplayInitType init_type, gpointer init_value)
{
  GstVaapiDisplayPrivate *const priv = GST_VAAPI_DISPLAY_GET_PRIVATE (display);
  const GstVaapiDisplayClass *const klass =
      GST_VAAPI_DISPLAY_GET_CLASS (display);
  GstVaapiDisplayInfo info;
  const GstVaapiDisplayInfo *cached_info = NULL;

  memset (&info, 0, sizeof (info));
  info.display = display;
  info.display_type = priv->display_type;

  switch (init_type) {
    case GST_VAAPI_DISPLAY_INIT_FROM_VA_DISPLAY:
      info.va_display = init_value;
      priv->display = init_value;
      priv->use_foreign_display = TRUE;
      break;
    case GST_VAAPI_DISPLAY_INIT_FROM_DISPLAY_NAME:
      if (klass->open_display && !klass->open_display (display, init_value))
        return FALSE;
      goto create_display;
    case GST_VAAPI_DISPLAY_INIT_FROM_NATIVE_DISPLAY:
      if (klass->bind_display && !klass->bind_display (display, init_value))
        return FALSE;
      // fall-through
    create_display:
      if (!klass->get_display || !klass->get_display (display, &info))
        return FALSE;
      priv->display = info.va_display;
      priv->display_type = info.display_type;
      priv->native_display = info.native_display;
      if (klass->get_size)
        klass->get_size (display, &priv->width, &priv->height);
      if (klass->get_size_mm)
        klass->get_size_mm (display, &priv->width_mm, &priv->height_mm);
      gst_vaapi_display_calculate_pixel_aspect_ratio (display);
      break;
  }
  if (!priv->display)
    return FALSE;

  cached_info = gst_vaapi_display_cache_lookup_by_va_display (priv->cache,
      info.va_display);
  if (cached_info) {
    gst_vaapi_display_replace_internal (&priv->parent, cached_info->display);
    priv->display_type = cached_info->display_type;
  }

  if (!priv->parent) {
    if (!vaapi_initialize (priv->display))
      return FALSE;
  }

  if (!cached_info) {
    if (!gst_vaapi_display_cache_add (priv->cache, &info))
      return FALSE;
  }

  GST_INFO_OBJECT (display, "new display addr=%p", display);
  g_free (priv->display_name);
  priv->display_name = g_strdup (info.display_name);
  return TRUE;
}

static gboolean
gst_vaapi_display_create (GstVaapiDisplay * display,
    GstVaapiDisplayInitType init_type, gpointer init_value)
{
  GstVaapiDisplayPrivate *const priv = GST_VAAPI_DISPLAY_GET_PRIVATE (display);
  GstVaapiDisplayCache *cache;
  gboolean success;

  cache = get_display_cache ();
  if (!cache)
    return FALSE;
  gst_vaapi_display_cache_replace (&priv->cache, cache);
  gst_vaapi_display_cache_unref (cache);

  gst_vaapi_display_cache_lock (cache);
  success = gst_vaapi_display_create_unlocked (display, init_type, init_value);
  gst_vaapi_display_cache_unlock (cache);
  return success;
}

static void
gst_vaapi_display_lock_default (GstVaapiDisplay * display)
{
  GstVaapiDisplayPrivate *priv = GST_VAAPI_DISPLAY_GET_PRIVATE (display);

  if (priv->parent)
    priv = GST_VAAPI_DISPLAY_GET_PRIVATE (priv->parent);
  g_rec_mutex_lock (&priv->mutex);
}

static void
gst_vaapi_display_unlock_default (GstVaapiDisplay * display)
{
  GstVaapiDisplayPrivate *priv = GST_VAAPI_DISPLAY_GET_PRIVATE (display);

  if (priv->parent)
    priv = GST_VAAPI_DISPLAY_GET_PRIVATE (priv->parent);
  g_rec_mutex_unlock (&priv->mutex);
}

static void
gst_vaapi_display_init (GstVaapiDisplay * display)
{
  GstVaapiDisplayPrivate *const priv =
      gst_vaapi_display_get_instance_private (display);

  display->priv = priv;
  priv->display_type = GST_VAAPI_DISPLAY_TYPE_ANY;
  priv->par_n = 1;
  priv->par_d = 1;

  g_rec_mutex_init (&priv->mutex);
}

static void
gst_vaapi_display_finalize (GObject * object)
{
  GstVaapiDisplay *const display = GST_VAAPI_DISPLAY (object);
  GstVaapiDisplayPrivate *const priv = GST_VAAPI_DISPLAY_GET_PRIVATE (display);

  gst_vaapi_display_destroy (display);
  g_rec_mutex_clear (&priv->mutex);

  G_OBJECT_CLASS (gst_vaapi_display_parent_class)->finalize (object);
}

void
gst_vaapi_display_class_init (GstVaapiDisplayClass * klass)
{
  GObjectClass *const object_class = G_OBJECT_CLASS (klass);

  libgstvaapi_init_once ();

  object_class->finalize = gst_vaapi_display_finalize;
  klass->lock = gst_vaapi_display_lock_default;
  klass->unlock = gst_vaapi_display_unlock_default;
}

static void
gst_vaapi_display_properties_init (void)
{
  /**
   * GstVaapiDisplay:render-mode:
   *
   * The VA display rendering mode, expressed as a #GstVaapiRenderMode.
   */
  g_properties[PROP_RENDER_MODE] =
      g_param_spec_enum (GST_VAAPI_DISPLAY_PROP_RENDER_MODE,
      "render mode",
      "The display rendering mode",
      GST_VAAPI_TYPE_RENDER_MODE, DEFAULT_RENDER_MODE, G_PARAM_READWRITE);

  /**
   * GstVaapiDisplay:rotation:
   *
   * The VA display rotation mode, expressed as a #GstVaapiRotation.
   */
  g_properties[PROP_ROTATION] =
      g_param_spec_enum (GST_VAAPI_DISPLAY_PROP_ROTATION,
      "rotation",
      "The display rotation mode",
      GST_VAAPI_TYPE_ROTATION, DEFAULT_ROTATION, G_PARAM_READWRITE);

  /**
   * GstVaapiDisplay:hue:
   *
   * The VA display hue, expressed as a float value. Range is -180.0
   * to 180.0. Default value is 0.0 and represents no modification.
   */
  g_properties[PROP_HUE] =
      g_param_spec_float (GST_VAAPI_DISPLAY_PROP_HUE,
      "hue", "The display hue value", -180.0, 180.0, 0.0, G_PARAM_READWRITE);

  /**
   * GstVaapiDisplay:saturation:
   *
   * The VA display saturation, expressed as a float value. Range is
   * 0.0 to 2.0. Default value is 1.0 and represents no modification.
   */
  g_properties[PROP_SATURATION] =
      g_param_spec_float (GST_VAAPI_DISPLAY_PROP_SATURATION,
      "saturation",
      "The display saturation value", 0.0, 2.0, 1.0, G_PARAM_READWRITE);

  /**
   * GstVaapiDisplay:brightness:
   *
   * The VA display brightness, expressed as a float value. Range is
   * -1.0 to 1.0. Default value is 0.0 and represents no modification.
   */
  g_properties[PROP_BRIGHTNESS] =
      g_param_spec_float (GST_VAAPI_DISPLAY_PROP_BRIGHTNESS,
      "brightness",
      "The display brightness value", -1.0, 1.0, 0.0, G_PARAM_READWRITE);

  /**
   * GstVaapiDisplay:contrast:
   *
   * The VA display contrast, expressed as a float value. Range is 0.0
   * to 2.0. Default value is 1.0 and represents no modification.
   */
  g_properties[PROP_CONTRAST] =
      g_param_spec_float (GST_VAAPI_DISPLAY_PROP_CONTRAST,
      "contrast",
      "The display contrast value", 0.0, 2.0, 1.0, G_PARAM_READWRITE);
}

GstVaapiDisplay *
gst_vaapi_display_new (GstVaapiDisplay * display,
    GstVaapiDisplayInitType init_type, gpointer init_value)
{
  g_return_val_if_fail (display != NULL, NULL);

  if (!gst_vaapi_display_create (display, init_type, init_value))
    goto error;
  return display;

  /* ERRORS */
error:
  {
    gst_vaapi_display_unref_internal (display);
    return NULL;
  }
}

/**
 * gst_vaapi_display_new_with_display:
 * @va_display: a #VADisplay
 *
 * Creates a new #GstVaapiDisplay, using @va_display as the VA
 * display.
 *
 * Return value: the newly created #GstVaapiDisplay object
 */
GstVaapiDisplay *
gst_vaapi_display_new_with_display (VADisplay va_display)
{
  GstVaapiDisplayCache *const cache = get_display_cache ();
  const GstVaapiDisplayInfo *info;

  g_return_val_if_fail (va_display != NULL, NULL);
  g_return_val_if_fail (cache != NULL, NULL);

  info = gst_vaapi_display_cache_lookup_by_va_display (cache, va_display);
  if (info)
    return gst_vaapi_display_ref_internal (info->display);

  return gst_vaapi_display_new (g_object_new (GST_TYPE_VAAPI_DISPLAY, NULL),
      GST_VAAPI_DISPLAY_INIT_FROM_VA_DISPLAY, va_display);
}

/**
 * gst_vaapi_display_ref:
 * @display: a #GstVaapiDisplay
 *
 * Atomically increases the reference count of the given @display by one.
 *
 * Returns: The same @display argument
 */
GstVaapiDisplay *
gst_vaapi_display_ref (GstVaapiDisplay * display)
{
  return gst_vaapi_display_ref_internal (display);
}

/**
 * gst_vaapi_display_unref:
 * @display: a #GstVaapiDisplay
 *
 * Atomically decreases the reference count of the @display by one. If
 * the reference count reaches zero, the display will be free'd.
 */
void
gst_vaapi_display_unref (GstVaapiDisplay * display)
{
  gst_vaapi_display_unref_internal (display);
}

/**
 * gst_vaapi_display_replace:
 * @old_display_ptr: a pointer to a #GstVaapiDisplay
 * @new_display: a #GstVaapiDisplay
 *
 * Atomically replaces the display display held in @old_display_ptr
 * with @new_display. This means that @old_display_ptr shall reference
 * a valid display. However, @new_display can be NULL.
 */
void
gst_vaapi_display_replace (GstVaapiDisplay ** old_display_ptr,
    GstVaapiDisplay * new_display)
{
  gst_vaapi_display_replace_internal (old_display_ptr, new_display);
}

/**
 * gst_vaapi_display_lock:
 * @display: a #GstVaapiDisplay
 *
 * Locks @display. If @display is already locked by another thread,
 * the current thread will block until @display is unlocked by the
 * other thread.
 */
void
gst_vaapi_display_lock (GstVaapiDisplay * display)
{
  GstVaapiDisplayClass *klass;

  g_return_if_fail (display != NULL);

  klass = GST_VAAPI_DISPLAY_GET_CLASS (display);
  if (klass->lock)
    klass->lock (display);
}

/**
 * gst_vaapi_display_unlock:
 * @display: a #GstVaapiDisplay
 *
 * Unlocks @display. If another thread is blocked in a
 * gst_vaapi_display_lock() call for @display, it will be woken and
 * can lock @display itself.
 */
void
gst_vaapi_display_unlock (GstVaapiDisplay * display)
{
  GstVaapiDisplayClass *klass;

  g_return_if_fail (display != NULL);

  klass = GST_VAAPI_DISPLAY_GET_CLASS (display);
  if (klass->unlock)
    klass->unlock (display);
}

/**
 * gst_vaapi_display_sync:
 * @display: a #GstVaapiDisplay
 *
 * Flushes any requests queued for the windowing system and waits until
 * all requests have been handled. This is often used for making sure
 * that the display is synchronized with the current state of the program.
 *
 * This is most useful for X11. On windowing systems where requests are
 * handled synchronously, this function will do nothing.
 */
void
gst_vaapi_display_sync (GstVaapiDisplay * display)
{
  GstVaapiDisplayClass *klass;

  g_return_if_fail (display != NULL);

  klass = GST_VAAPI_DISPLAY_GET_CLASS (display);
  if (klass->sync)
    klass->sync (display);
  else if (klass->flush)
    klass->flush (display);
}

/**
 * gst_vaapi_display_flush:
 * @display: a #GstVaapiDisplay
 *
 * Flushes any requests queued for the windowing system.
 *
 * This is most useful for X11. On windowing systems where requests
 * are handled synchronously, this function will do nothing.
 */
void
gst_vaapi_display_flush (GstVaapiDisplay * display)
{
  GstVaapiDisplayClass *klass;

  g_return_if_fail (display != NULL);

  klass = GST_VAAPI_DISPLAY_GET_CLASS (display);
  if (klass->flush)
    klass->flush (display);
}

/**
 * gst_vaapi_display_get_class_type:
 * @display: a #GstVaapiDisplay
 *
 * Returns the #GstVaapiDisplayType of @display. This is the type of
 * the object, thus the associated class, not the type of the VA
 * display.
 *
 * Return value: the #GstVaapiDisplayType
 */
GstVaapiDisplayType
gst_vaapi_display_get_class_type (GstVaapiDisplay * display)
{
  g_return_val_if_fail (display != NULL, GST_VAAPI_DISPLAY_TYPE_ANY);

  return GST_VAAPI_DISPLAY_GET_CLASS_TYPE (display);
}

/**
 * gst_vaapi_display_get_display_type:
 * @display: a #GstVaapiDisplay
 *
 * Returns the #GstVaapiDisplayType of the VA display bound to
 * @display. This is not the type of the @display object.
 *
 * Return value: the #GstVaapiDisplayType
 */
GstVaapiDisplayType
gst_vaapi_display_get_display_type (GstVaapiDisplay * display)
{
  g_return_val_if_fail (display != NULL, GST_VAAPI_DISPLAY_TYPE_ANY);

  return GST_VAAPI_DISPLAY_VADISPLAY_TYPE (display);
}

/**
 * gst_vaapi_display_get_display_type:
 * @display: a #GstVaapiDisplay
 *
 * Returns the @display name.
 *
 * Return value: the display name
 */
const gchar *
gst_vaapi_display_get_display_name (GstVaapiDisplay * display)
{
  g_return_val_if_fail (display != NULL, NULL);

  return GST_VAAPI_DISPLAY_GET_PRIVATE (display)->display_name;
}

/**
 * gst_vaapi_display_get_display:
 * @display: a #GstVaapiDisplay
 *
 * Returns the #VADisplay bound to @display.
 *
 * Return value: the #VADisplay
 */
VADisplay
gst_vaapi_display_get_display (GstVaapiDisplay * display)
{
  g_return_val_if_fail (display != NULL, NULL);

  return GST_VAAPI_DISPLAY_GET_PRIVATE (display)->display;
}

/**
 * gst_vaapi_display_get_width:
 * @display: a #GstVaapiDisplay
 *
 * Retrieves the width of a #GstVaapiDisplay.
 *
 * Return value: the width of the @display, in pixels
 */
guint
gst_vaapi_display_get_width (GstVaapiDisplay * display)
{
  g_return_val_if_fail (display != NULL, 0);

  return GST_VAAPI_DISPLAY_GET_PRIVATE (display)->width;
}

/**
 * gst_vaapi_display_get_height:
 * @display: a #GstVaapiDisplay
 *
 * Retrieves the height of a #GstVaapiDisplay
 *
 * Return value: the height of the @display, in pixels
 */
guint
gst_vaapi_display_get_height (GstVaapiDisplay * display)
{
  g_return_val_if_fail (display != NULL, 0);

  return GST_VAAPI_DISPLAY_GET_PRIVATE (display)->height;
}

/**
 * gst_vaapi_display_get_size:
 * @display: a #GstVaapiDisplay
 * @pwidth: return location for the width, or %NULL
 * @pheight: return location for the height, or %NULL
 *
 * Retrieves the dimensions of a #GstVaapiDisplay.
 */
void
gst_vaapi_display_get_size (GstVaapiDisplay * display, guint * pwidth,
    guint * pheight)
{
  g_return_if_fail (GST_VAAPI_DISPLAY (display));

  if (pwidth)
    *pwidth = GST_VAAPI_DISPLAY_GET_PRIVATE (display)->width;

  if (pheight)
    *pheight = GST_VAAPI_DISPLAY_GET_PRIVATE (display)->height;
}

/**
 * gst_vaapi_display_get_pixel_aspect_ratio:
 * @display: a #GstVaapiDisplay
 * @par_n: return location for the numerator of pixel aspect ratio, or %NULL
 * @par_d: return location for the denominator of pixel aspect ratio, or %NULL
 *
 * Retrieves the pixel aspect ratio of a #GstVaapiDisplay.
 */
void
gst_vaapi_display_get_pixel_aspect_ratio (GstVaapiDisplay * display,
    guint * par_n, guint * par_d)
{
  g_return_if_fail (display != NULL);

  if (par_n)
    *par_n = GST_VAAPI_DISPLAY_GET_PRIVATE (display)->par_n;

  if (par_d)
    *par_d = GST_VAAPI_DISPLAY_GET_PRIVATE (display)->par_d;
}

/**
 * gst_vaapi_display_has_video_processing:
 * @display: a #GstVaapiDisplay
 *
 * Checks whether the underlying VA driver implementation supports
 * video processing (VPP) acceleration.
 *
 * Returns: %TRUE if some VPP features are available
 */
gboolean
gst_vaapi_display_has_video_processing (GstVaapiDisplay * display)
{
  g_return_val_if_fail (display != NULL, FALSE);

  if (!ensure_profiles (display))
    return FALSE;
  return GST_VAAPI_DISPLAY_GET_PRIVATE (display)->has_vpp;
}

/**
 * gst_vaapi_display_get_decode_profiles:
 * @display: a #GstVaapiDisplay
 *
 * Gets the supported profiles for decoding. The caller owns an extra
 * reference to the resulting array of #GstVaapiProfile elements, so
 * it shall be released with g_array_unref() after usage.
 *
 * Return value: a newly allocated #GArray, or %NULL or error or if
 *   decoding is not supported at all
 */
GArray *
gst_vaapi_display_get_decode_profiles (GstVaapiDisplay * display)
{
  g_return_val_if_fail (display != NULL, NULL);

  if (!ensure_profiles (display))
    return NULL;
  return get_profiles (GST_VAAPI_DISPLAY_GET_PRIVATE (display)->decoders);
}

/**
 * gst_vaapi_display_has_decoder:
 * @display: a #GstVaapiDisplay
 * @profile: a #VAProfile
 * @entrypoint: a #GstVaaiEntrypoint
 *
 * Returns whether VA @display supports @profile for decoding at the
 * specified @entrypoint.
 *
 * Return value: %TRUE if VA @display supports @profile for decoding.
 */
gboolean
gst_vaapi_display_has_decoder (GstVaapiDisplay * display,
    GstVaapiProfile profile, GstVaapiEntrypoint entrypoint)
{
  g_return_val_if_fail (display != NULL, FALSE);

  if (!ensure_profiles (display))
    return FALSE;
  return find_config (GST_VAAPI_DISPLAY_GET_PRIVATE (display)->decoders,
      profile, entrypoint);
}

/**
 * gst_vaapi_display_get_encode_profiles:
 * @display: a #GstVaapiDisplay
 *
 * Gets the supported profiles for encoding. The caller owns an extra
 * reference to the resulting array of #GstVaapiProfile elements, so
 * it shall be released with g_array_unref() after usage.
 *
 * Return value: a newly allocated #GArray, or %NULL or error or if
 *   encoding is not supported at all
 */
GArray *
gst_vaapi_display_get_encode_profiles (GstVaapiDisplay * display)
{
  g_return_val_if_fail (display != NULL, NULL);

  if (!ensure_profiles (display))
    return NULL;
  return get_profiles (GST_VAAPI_DISPLAY_GET_PRIVATE (display)->encoders);
}

/**
 * gst_vaapi_display_has_encoder:
 * @display: a #GstVaapiDisplay
 * @profile: a #VAProfile
 * @entrypoint: a #GstVaapiEntrypoint
 *
 * Returns whether VA @display supports @profile for encoding at the
 * specified @entrypoint.
 *
 * Return value: %TRUE if VA @display supports @profile for encoding.
 */
gboolean
gst_vaapi_display_has_encoder (GstVaapiDisplay * display,
    GstVaapiProfile profile, GstVaapiEntrypoint entrypoint)
{
  g_return_val_if_fail (display != NULL, FALSE);

  if (!ensure_profiles (display))
    return FALSE;
  return find_config (GST_VAAPI_DISPLAY_GET_PRIVATE (display)->encoders,
      profile, entrypoint);
}

/**
 * gst_vaapi_display_get_image_formats:
 * @display: a #GstVaapiDisplay
 *
 * Gets the supported image formats for gst_vaapi_surface_get_image()
 * or gst_vaapi_surface_put_image().
 *
 * Note that this method does not necessarily map image formats
 * returned by vaQueryImageFormats(). The set of capabilities can be
 * stripped down, if gstreamer-vaapi does not support the format, or
 * expanded to cover compatible formats not exposed by the underlying
 * driver. e.g. I420 can be supported even if the driver only exposes
 * YV12.
 *
 * Note: the caller owns an extra reference to the resulting array of
 * #GstVideoFormat elements, so it shall be released with
 * g_array_unref() after usage.
 *
 * Return value: a newly allocated #GArray, or %NULL on error or if
 *   the set is empty
 */
GArray *
gst_vaapi_display_get_image_formats (GstVaapiDisplay * display)
{
  g_return_val_if_fail (display != NULL, NULL);

  if (!ensure_image_formats (display))
    return NULL;
  return get_formats (GST_VAAPI_DISPLAY_GET_PRIVATE (display)->image_formats);
}

/**
 * gst_vaapi_display_has_image_format:
 * @display: a #GstVaapiDisplay
 * @format: a #GstVideoFormat
 *
 * Returns whether VA @display supports @format image format.
 *
 * Return value: %TRUE if VA @display supports @format image format
 */
gboolean
gst_vaapi_display_has_image_format (GstVaapiDisplay * display,
    GstVideoFormat format)
{
  GstVaapiDisplayPrivate *priv;

  g_return_val_if_fail (display != NULL, FALSE);
  g_return_val_if_fail (format, FALSE);

  priv = GST_VAAPI_DISPLAY_GET_PRIVATE (display);

  if (!ensure_image_formats (display))
    return FALSE;
  if (find_format (priv->image_formats, format))
    return TRUE;

  /* XXX: try subpicture formats since some drivers could report a
   * set of VA image formats that is not a superset of the set of VA
   * subpicture formats
   */
  if (!ensure_subpicture_formats (display))
    return FALSE;
  return find_format (priv->subpicture_formats, format);
}

/**
 * gst_vaapi_display_get_subpicture_formats:
 * @display: a #GstVaapiDisplay
 *
 * Gets the supported subpicture formats.
 *
 * Note that this method does not necessarily map subpicture formats
 * returned by vaQuerySubpictureFormats(). The set of capabilities can
 * be stripped down if gstreamer-vaapi does not support the
 * format. e.g. this is the case for paletted formats like IA44.
 *
 * Note: the caller owns an extra reference to the resulting array of
 * #GstVideoFormat elements, so it shall be released with
 * g_array_unref() after usage.
 *
 * Return value: a newly allocated #GArray, or %NULL on error of if
 *   the set is empty
 */
GArray *
gst_vaapi_display_get_subpicture_formats (GstVaapiDisplay * display)
{
  g_return_val_if_fail (display != NULL, NULL);

  if (!ensure_subpicture_formats (display))
    return NULL;
  return
      get_formats (GST_VAAPI_DISPLAY_GET_PRIVATE (display)->subpicture_formats);
}

/**
 * gst_vaapi_display_has_subpicture_format:
 * @display: a #GstVaapiDisplay
 * @format: a #GstVideoFormat
 * @flags_ptr: pointer to #GstVaapiSubpictureFlags, or zero
 *
 * Returns whether VA @display supports @format subpicture format with
 * the supplied @flags.
 *
 * Return value: %TRUE if VA @display supports @format subpicture format
 */
gboolean
gst_vaapi_display_has_subpicture_format (GstVaapiDisplay * display,
    GstVideoFormat format, guint * flags_ptr)
{
  GstVaapiDisplayPrivate *priv;
  const GstVaapiFormatInfo *fip;

  g_return_val_if_fail (display != NULL, FALSE);
  g_return_val_if_fail (format, FALSE);

  priv = GST_VAAPI_DISPLAY_GET_PRIVATE (display);

  if (!ensure_subpicture_formats (display))
    return FALSE;

  fip = find_format_info (priv->subpicture_formats, format);
  if (!fip)
    return FALSE;

  if (flags_ptr)
    *flags_ptr = fip->flags;
  return TRUE;
}

/**
 * gst_vaapi_display_has_property:
 * @display: a #GstVaapiDisplay
 * @name: the property name to check
 *
 * Returns whether VA @display supports the requested property. The
 * check is performed against the property @name. So, the client
 * application may perform this check only once and cache this
 * information.
 *
 * Return value: %TRUE if VA @display supports property @name
 */
gboolean
gst_vaapi_display_has_property (GstVaapiDisplay * display, const gchar * name)
{
  g_return_val_if_fail (display != NULL, FALSE);
  g_return_val_if_fail (name, FALSE);

  if (!ensure_properties (display))
    return FALSE;
  return find_property (GST_VAAPI_DISPLAY_GET_PRIVATE (display)->properties,
      name) != NULL;
}

gboolean
gst_vaapi_display_get_property (GstVaapiDisplay * display, const gchar * name,
    GValue * out_value)
{
  const GstVaapiProperty *prop;

  g_return_val_if_fail (display != NULL, FALSE);
  g_return_val_if_fail (name != NULL, FALSE);
  g_return_val_if_fail (out_value != NULL, FALSE);

  if (!ensure_properties (display))
    return FALSE;

  prop =
      find_property (GST_VAAPI_DISPLAY_GET_PRIVATE (display)->properties, name);
  if (!prop)
    return FALSE;

  switch (prop->attribute.type) {
    case VADisplayAttribRenderMode:{
      GstVaapiRenderMode mode;
      if (!gst_vaapi_display_get_render_mode (display, &mode))
        return FALSE;
      g_value_init (out_value, GST_VAAPI_TYPE_RENDER_MODE);
      g_value_set_enum (out_value, mode);
      break;
    }
    case VADisplayAttribRotation:{
      GstVaapiRotation rotation;
      rotation = gst_vaapi_display_get_rotation (display);
      g_value_init (out_value, GST_VAAPI_TYPE_ROTATION);
      g_value_set_enum (out_value, rotation);
      break;
    }
    case VADisplayAttribHue:
    case VADisplayAttribSaturation:
    case VADisplayAttribBrightness:
    case VADisplayAttribContrast:{
      gfloat value;
      if (!get_color_balance (display, find_property_id (name), &value))
        return FALSE;
      g_value_init (out_value, G_TYPE_FLOAT);
      g_value_set_float (out_value, value);
      break;
    }
    default:
      GST_WARNING ("unsupported property '%s'", name);
      return FALSE;
  }
  return TRUE;
}

gboolean
gst_vaapi_display_set_property (GstVaapiDisplay * display, const gchar * name,
    const GValue * value)
{
  const GstVaapiProperty *prop;

  g_return_val_if_fail (display != NULL, FALSE);
  g_return_val_if_fail (name != NULL, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);

  if (!ensure_properties (display))
    return FALSE;

  prop =
      find_property (GST_VAAPI_DISPLAY_GET_PRIVATE (display)->properties, name);
  if (!prop)
    return FALSE;

  switch (prop->attribute.type) {
    case VADisplayAttribRenderMode:{
      GstVaapiRenderMode mode;
      if (!G_VALUE_HOLDS (value, GST_VAAPI_TYPE_RENDER_MODE))
        return FALSE;
      mode = g_value_get_enum (value);
      return gst_vaapi_display_set_render_mode (display, mode);
    }
    case VADisplayAttribRotation:{
      GstVaapiRotation rotation;
      if (!G_VALUE_HOLDS (value, GST_VAAPI_TYPE_ROTATION))
        return FALSE;
      rotation = g_value_get_enum (value);
      return gst_vaapi_display_set_rotation (display, rotation);
    }
    case VADisplayAttribHue:
    case VADisplayAttribSaturation:
    case VADisplayAttribBrightness:
    case VADisplayAttribContrast:{
      gfloat v;
      if (!G_VALUE_HOLDS (value, G_TYPE_FLOAT))
        return FALSE;
      v = g_value_get_float (value);
      return set_color_balance (display, find_property_id (name), v);
    }
    default:
      break;
  }

  GST_WARNING ("unsupported property '%s'", name);
  return FALSE;
}

static gboolean
get_attribute (GstVaapiDisplay * display, VADisplayAttribType type,
    gint * value)
{
  GstVaapiDisplayPrivate *const priv = GST_VAAPI_DISPLAY_GET_PRIVATE (display);
  VADisplayAttribute attr = { 0, };
  VAStatus status;

  attr.type = type;
  attr.flags = VA_DISPLAY_ATTRIB_GETTABLE;
  status = vaGetDisplayAttributes (priv->display, &attr, 1);
  if (!vaapi_check_status (status, "vaGetDisplayAttributes()"))
    return FALSE;
  *value = attr.value;
  return TRUE;
}

static gboolean
set_attribute (GstVaapiDisplay * display, VADisplayAttribType type, gint value)
{
  GstVaapiDisplayPrivate *const priv = GST_VAAPI_DISPLAY_GET_PRIVATE (display);
  VADisplayAttribute attr = { 0, };
  VAStatus status;

  attr.type = type;
  attr.value = value;
  attr.flags = VA_DISPLAY_ATTRIB_SETTABLE;
  status = vaSetDisplayAttributes (priv->display, &attr, 1);
  if (!vaapi_check_status (status, "vaSetDisplayAttributes()"))
    return FALSE;
  return TRUE;
}

static gboolean
get_render_mode_VADisplayAttribRenderMode (GstVaapiDisplay * display,
    GstVaapiRenderMode * pmode)
{
  gint modes, devices;

  if (!get_attribute (display, VADisplayAttribRenderDevice, &devices))
    return FALSE;
  if (!devices)
    return FALSE;
  if (!get_attribute (display, VADisplayAttribRenderMode, &modes))
    return FALSE;

  /* Favor "overlay" mode since it is the most restrictive one */
  if (modes & (VA_RENDER_MODE_LOCAL_OVERLAY | VA_RENDER_MODE_EXTERNAL_OVERLAY))
    *pmode = GST_VAAPI_RENDER_MODE_OVERLAY;
  else
    *pmode = GST_VAAPI_RENDER_MODE_TEXTURE;
  return TRUE;
}

static gboolean
get_render_mode_VADisplayAttribDirectSurface (GstVaapiDisplay * display,
    GstVaapiRenderMode * pmode)
{
#if VA_CHECK_VERSION(0,34,0)
  /* VADisplayAttribDirectsurface was removed in VA-API >= 0.34.0 */
  return FALSE;
#else
  gint direct_surface;

  if (!get_attribute (display, VADisplayAttribDirectSurface, &direct_surface))
    return FALSE;
  if (direct_surface)
    *pmode = GST_VAAPI_RENDER_MODE_OVERLAY;
  else
    *pmode = GST_VAAPI_RENDER_MODE_TEXTURE;
  return TRUE;
#endif
}

static gboolean
get_render_mode_default (GstVaapiDisplay * display, GstVaapiRenderMode * pmode)
{
  GstVaapiDisplayPrivate *const priv = GST_VAAPI_DISPLAY_GET_PRIVATE (display);

  switch (priv->display_type) {
#if USE_WAYLAND
    case GST_VAAPI_DISPLAY_TYPE_WAYLAND:
      /* wl_buffer mapped from VA surface through vaGetSurfaceBufferWl() */
      *pmode = GST_VAAPI_RENDER_MODE_OVERLAY;
      break;
#endif
#if USE_DRM
    case GST_VAAPI_DISPLAY_TYPE_DRM:
      /* vaGetSurfaceBufferDRM() returns the underlying DRM buffer handle */
      *pmode = GST_VAAPI_RENDER_MODE_OVERLAY;
      break;
#endif
    default:
      /* This includes VA/X11 and VA/GLX modes */
      *pmode = DEFAULT_RENDER_MODE;
      break;
  }
  return TRUE;
}

/**
 * gst_vaapi_display_get_render_mode:
 * @display: a #GstVaapiDisplay
 * @pmode: return location for the VA @display rendering mode
 *
 * Returns the current VA @display rendering mode.
 *
 * Return value: %TRUE if VA @display rendering mode could be determined
 */
gboolean
gst_vaapi_display_get_render_mode (GstVaapiDisplay * display,
    GstVaapiRenderMode * pmode)
{
  g_return_val_if_fail (display != NULL, FALSE);

  /* Try with render-mode attribute */
  if (get_render_mode_VADisplayAttribRenderMode (display, pmode))
    return TRUE;

  /* Try with direct-surface attribute */
  if (get_render_mode_VADisplayAttribDirectSurface (display, pmode))
    return TRUE;

  /* Default: determine from the display type */
  return get_render_mode_default (display, pmode);
}

/**
 * gst_vaapi_display_set_render_mode:
 * @display: a #GstVaapiDisplay
 * @mode: the #GstVaapiRenderMode to set
 *
 * Sets the VA @display rendering mode to the supplied @mode. This
 * function returns %FALSE if the rendering mode could not be set,
 * e.g. run-time switching rendering mode is not supported.
 *
 * Return value: %TRUE if VA @display rendering @mode could be changed
 *   to the requested value
 */
gboolean
gst_vaapi_display_set_render_mode (GstVaapiDisplay * display,
    GstVaapiRenderMode mode)
{
  gint modes, devices;

  g_return_val_if_fail (display != NULL, FALSE);

  if (!get_attribute (display, VADisplayAttribRenderDevice, &devices))
    return FALSE;

  modes = 0;
  switch (mode) {
    case GST_VAAPI_RENDER_MODE_OVERLAY:
      if (devices & VA_RENDER_DEVICE_LOCAL)
        modes |= VA_RENDER_MODE_LOCAL_OVERLAY;
      if (devices & VA_RENDER_DEVICE_EXTERNAL)
        modes |= VA_RENDER_MODE_EXTERNAL_OVERLAY;
      break;
    case GST_VAAPI_RENDER_MODE_TEXTURE:
      if (devices & VA_RENDER_DEVICE_LOCAL)
        modes |= VA_RENDER_MODE_LOCAL_GPU;
      if (devices & VA_RENDER_DEVICE_EXTERNAL)
        modes |= VA_RENDER_MODE_EXTERNAL_GPU;
      break;
  }
  if (!modes)
    return FALSE;
  if (!set_attribute (display, VADisplayAttribRenderMode, modes))
    return FALSE;
  return TRUE;
}

/**
 * gst_vaapi_display_get_rotation:
 * @display: a #GstVaapiDisplay
 *
 * Returns the current VA @display rotation angle. If the VA driver
 * does not support "rotation" display attribute, then the display is
 * assumed to be un-rotated.
 *
 * Return value: the current #GstVaapiRotation value
 */
GstVaapiRotation
gst_vaapi_display_get_rotation (GstVaapiDisplay * display)
{
  gint value;

  g_return_val_if_fail (display != NULL, DEFAULT_ROTATION);

  if (!get_attribute (display, VADisplayAttribRotation, &value))
    value = VA_ROTATION_NONE;
  return to_GstVaapiRotation (value);
}

/**
 * gst_vaapi_display_set_rotation:
 * @display: a #GstVaapiDisplay
 * @rotation: the #GstVaapiRotation value to set
 *
 * Sets the VA @display rotation angle to the supplied @rotation
 * value. This function returns %FALSE if the rotation angle could not
 * be set, e.g. the VA driver does not allow to change the display
 * rotation angle.
 *
 * Return value: %TRUE if VA @display rotation angle could be changed
 *   to the requested value
 */
gboolean
gst_vaapi_display_set_rotation (GstVaapiDisplay * display,
    GstVaapiRotation rotation)
{
  guint value;

  g_return_val_if_fail (display != NULL, FALSE);

  value = from_GstVaapiRotation (rotation);
  if (!set_attribute (display, VADisplayAttribRotation, value))
    return FALSE;
  return TRUE;
}

/* Get color balance attributes */
static gboolean
get_color_balance (GstVaapiDisplay * display, guint prop_id, gfloat * v)
{
  GParamSpecFloat *const pspec = G_PARAM_SPEC_FLOAT (g_properties[prop_id]);
  const GstVaapiProperty *prop;
  const VADisplayAttribute *attr;
  gfloat out_value;
  gint value;

  if (!ensure_properties (display))
    return FALSE;

  if (!pspec)
    return FALSE;

  prop = find_property_by_pspec (display, &pspec->parent_instance);
  if (!prop)
    return FALSE;
  attr = &prop->attribute;

  if (!get_attribute (display, attr->type, &value))
    return FALSE;

  /* Scale wrt. the medium ("default") value */
  out_value = pspec->default_value;
  if (value > attr->value)
    out_value += ((gfloat) (value - attr->value) /
        (attr->max_value - attr->value) *
        (pspec->maximum - pspec->default_value));
  else if (value < attr->value)
    out_value -= ((gfloat) (attr->value - value) /
        (attr->value - attr->min_value) *
        (pspec->default_value - pspec->minimum));
  *v = out_value;
  return TRUE;
}

/* Set color balance attribute */
static gboolean
set_color_balance (GstVaapiDisplay * display, guint prop_id, gfloat v)
{
  GParamSpecFloat *const pspec = G_PARAM_SPEC_FLOAT (g_properties[prop_id]);
  const GstVaapiProperty *prop;
  const VADisplayAttribute *attr;
  gint value;

  if (!ensure_properties (display))
    return FALSE;

  if (!pspec)
    return FALSE;

  prop = find_property_by_pspec (display, &pspec->parent_instance);
  if (!prop)
    return FALSE;
  attr = &prop->attribute;

  /* Scale wrt. the medium ("default") value */
  value = attr->value;
  if (v > pspec->default_value)
    value += ((v - pspec->default_value) /
        (pspec->maximum - pspec->default_value) *
        (attr->max_value - attr->value));
  else if (v < pspec->default_value)
    value -= ((pspec->default_value - v) /
        (pspec->default_value - pspec->minimum) *
        (attr->value - attr->min_value));
  if (!set_attribute (display, attr->type, value))
    return FALSE;
  return TRUE;
}

/* Ensures the VA driver vendor string was copied */
static gboolean
ensure_vendor_string (GstVaapiDisplay * display)
{
  GstVaapiDisplayPrivate *const priv = GST_VAAPI_DISPLAY_GET_PRIVATE (display);
  const gchar *vendor_string;

  GST_VAAPI_DISPLAY_LOCK (display);
  if (!priv->vendor_string) {
    vendor_string = vaQueryVendorString (priv->display);
    if (vendor_string)
      priv->vendor_string = g_strdup (vendor_string);
  }
  GST_VAAPI_DISPLAY_UNLOCK (display);
  return priv->vendor_string != NULL;
}

/**
 * gst_vaapi_display_get_vendor_string:
 * @display: a #GstVaapiDisplay
 *
 * Returns the VA driver vendor string attached to the supplied VA @display.
 * The @display owns the vendor string, do *not* de-allocate it.
 *
 * This function is thread safe.
 *
 * Return value: the current #GstVaapiRotation value
 */
const gchar *
gst_vaapi_display_get_vendor_string (GstVaapiDisplay * display)
{
  g_return_val_if_fail (display != NULL, NULL);

  if (!ensure_vendor_string (display))
    return NULL;
  return GST_VAAPI_DISPLAY_GET_PRIVATE (display)->vendor_string;
}

/**
 * gst_vaapi_display_has_opengl:
 * @display: a #GstVaapiDisplay
 *
 * Returns wether the @display that was created does support OpenGL
 * context to be attached.
 *
 * This function is thread safe.
 *
 * Return value: %TRUE if the @display supports OpenGL context, %FALSE
 *   otherwise
 */
gboolean
gst_vaapi_display_has_opengl (GstVaapiDisplay * display)
{
  GstVaapiDisplayClass *klass;

  g_return_val_if_fail (display != NULL, FALSE);

  klass = GST_VAAPI_DISPLAY_GET_CLASS (display);
  return (klass->display_type == GST_VAAPI_DISPLAY_TYPE_GLX ||
      klass->display_type == GST_VAAPI_DISPLAY_TYPE_EGL);
}

/**
 * gst_vaapi_display_reset_texture_map:
 * @display: a #GstVaapiDisplay
 *
 * Reset the internal #GstVaapiTextureMap if available.
 *
 * This function is thread safe.
 */
void
gst_vaapi_display_reset_texture_map (GstVaapiDisplay * display)
{
  GstVaapiDisplayClass *klass;
  GstVaapiTextureMap *map;

  g_return_if_fail (display != NULL);

  if (!gst_vaapi_display_has_opengl (display))
    return;
  klass = GST_VAAPI_DISPLAY_GET_CLASS (display);
  if (!klass->get_texture_map)
    return;
  if ((map = klass->get_texture_map (display)))
    gst_vaapi_texture_map_reset (map);
}
