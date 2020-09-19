/* GStreamer
 * Copyright (C) 2020 Igalia, S.L.
 *     Author: Víctor Jáquez <vjaquez@igalia.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvafilter.h"

#include <gst/video/video.h>

#include <va/va_vpp.h>
#include <va/va_drmcommon.h>

#include "gstvacaps.h"
#include "gstvavideoformat.h"

struct _GstVaFilter
{
  GstObject parent;

  GstVaDisplay *display;
  VAConfigID config;
  VAContextID context;

  VAProcPipelineCaps pipeline_caps;

  guint32 mem_types;
  gint min_width;
  gint max_width;
  gint min_height;
  gint max_height;

  guint mirror;
  guint rotation;
  GstVideoOrientationMethod orientation;

  GArray *surface_formats;
  GArray *image_formats;

  GArray *available_filters;

  GArray *filters;
};

GST_DEBUG_CATEGORY_STATIC (gst_va_filter_debug);
#define GST_CAT_DEFAULT gst_va_filter_debug

#define gst_va_filter_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstVaFilter, gst_va_filter, GST_TYPE_OBJECT,
    GST_DEBUG_CATEGORY_INIT (gst_va_filter_debug, "vafilter", 0, "VA Filter"));

enum
{
  PROP_DISPLAY = 1,
  N_PROPERTIES
};

static GParamSpec *g_properties[N_PROPERTIES];

static void
gst_va_filter_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVaFilter *self = GST_VA_FILTER (object);

  switch (prop_id) {
    case PROP_DISPLAY:{
      g_assert (!self->display);
      self->display = g_value_dup_object (value);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_va_filter_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstVaFilter *self = GST_VA_FILTER (object);

  switch (prop_id) {
    case PROP_DISPLAY:
      g_value_set_object (value, self->display);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_va_filter_dispose (GObject * object)
{
  GstVaFilter *self = GST_VA_FILTER (object);

  gst_va_filter_close (self);

  g_clear_pointer (&self->available_filters, g_array_unref);
  g_clear_pointer (&self->image_formats, g_array_unref);
  g_clear_pointer (&self->surface_formats, g_array_unref);
  gst_clear_object (&self->display);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_va_filter_class_init (GstVaFilterClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->set_property = gst_va_filter_set_property;
  gobject_class->get_property = gst_va_filter_get_property;
  gobject_class->dispose = gst_va_filter_dispose;

  g_properties[PROP_DISPLAY] =
      g_param_spec_object ("display", "GstVaDisplay", "GstVADisplay object",
      GST_TYPE_VA_DISPLAY,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class, N_PROPERTIES, g_properties);
}

static void
gst_va_filter_init (GstVaFilter * self)
{
  self->config = VA_INVALID_ID;
  self->context = VA_INVALID_ID;

  self->min_height = 1;
  self->max_height = G_MAXINT;
  self->min_width = 1;
  self->max_height = G_MAXINT;
}

GstVaFilter *
gst_va_filter_new (GstVaDisplay * display)
{
  g_return_val_if_fail (GST_IS_VA_DISPLAY (display), NULL);

  return g_object_new (GST_TYPE_VA_FILTER, "display", display, NULL);
}

gboolean
gst_va_filter_is_open (GstVaFilter * self)
{
  gboolean ret;

  g_return_val_if_fail (GST_IS_VA_FILTER (self), FALSE);

  GST_OBJECT_LOCK (self);
  ret = (self->config != VA_INVALID_ID && self->context != VA_INVALID_ID);
  GST_OBJECT_UNLOCK (self);
  return ret;
}

static gboolean
gst_va_filter_ensure_config_attributes (GstVaFilter * self,
    guint32 * rt_formats_ptr)
{
  VAConfigAttrib attribs[] = {
    {.type = VAConfigAttribMaxPictureWidth,},
    {.type = VAConfigAttribMaxPictureHeight,},
    {.type = VAConfigAttribRTFormat,},
  };
  VADisplay dpy;
  VAStatus status;
  guint i, value, rt_formats = 0, max_width = 0, max_height = 0;

  dpy = gst_va_display_get_va_dpy (self->display);

  gst_va_display_lock (self->display);
  status = vaGetConfigAttributes (dpy, VAProfileNone, VAEntrypointVideoProc,
      attribs, G_N_ELEMENTS (attribs));
  gst_va_display_unlock (self->display);
  if (status != VA_STATUS_SUCCESS) {
    GST_ERROR_OBJECT (self, "vaGetConfigAttributes: %s", vaErrorStr (status));
    return FALSE;
  }

  for (i = 0; i < G_N_ELEMENTS (attribs); i++) {
    value = attribs[i].value;
    if (value == VA_ATTRIB_NOT_SUPPORTED)
      continue;
    switch (attribs[i].type) {
      case VAConfigAttribMaxPictureHeight:
        max_height = value;
        break;
      case VAConfigAttribMaxPictureWidth:
        max_width = value;
        break;
      case VAConfigAttribRTFormat:
        rt_formats = value;
        break;
      default:
        break;
    }
  }

  if (rt_formats_ptr && rt_formats != 0)
    *rt_formats_ptr = rt_formats;
  if (max_width > 0 && max_width < G_MAXINT)
    self->max_width = max_width;
  if (max_height > 0 && max_height < G_MAXINT)
    self->max_height = max_height;

  return TRUE;
}

static gboolean
gst_va_filter_ensure_surface_attributes (GstVaFilter * self)
{
  GArray *surface_formats;
  GstVideoFormat format;
  VASurfaceAttrib *attribs;
  guint i, attrib_count;

  attribs =
      gst_va_get_surface_attribs (self->display, self->config, &attrib_count);
  if (!attribs)
    return FALSE;
  surface_formats = g_array_new (FALSE, FALSE, sizeof (GstVideoFormat));

  for (i = 0; i < attrib_count; i++) {
    if (attribs[i].value.type != VAGenericValueTypeInteger)
      continue;
    switch (attribs[i].type) {
      case VASurfaceAttribPixelFormat:
        format = gst_va_video_format_from_va_fourcc (attribs[i].value.value.i);
        if (format != GST_VIDEO_FORMAT_UNKNOWN)
          g_array_append_val (surface_formats, format);
        break;
      case VASurfaceAttribMinWidth:
        self->min_width = MAX (self->min_width, attribs[i].value.value.i);
        break;
      case VASurfaceAttribMaxWidth:
        if (self->max_width > 0)
          self->max_width = MIN (self->max_width, attribs[i].value.value.i);
        else
          self->max_width = attribs[i].value.value.i;
        break;
      case VASurfaceAttribMinHeight:
        self->min_height = MAX (self->min_height, attribs[i].value.value.i);
        break;
      case VASurfaceAttribMaxHeight:
        if (self->max_height > 0)
          self->max_height = MIN (self->max_height, attribs[i].value.value.i);
        else
          self->max_height = attribs[i].value.value.i;
        break;
      case VASurfaceAttribMemoryType:
        self->mem_types = attribs[i].value.value.i;
        break;
      default:
        break;
    }
  }

  if (surface_formats->len == 0)
    g_clear_pointer (&surface_formats, g_array_unref);

  self->surface_formats = surface_formats;

  g_free (attribs);

  return TRUE;
}

static gboolean
gst_va_filter_ensure_pipeline_caps (GstVaFilter * self)
{
  VADisplay dpy;
  VAStatus status;

  dpy = gst_va_display_get_va_dpy (self->display);

  gst_va_display_lock (self->display);
  status = vaQueryVideoProcPipelineCaps (dpy, self->context, NULL, 0,
      &self->pipeline_caps);
  gst_va_display_unlock (self->display);
  if (status != VA_STATUS_SUCCESS) {
    GST_ERROR_OBJECT (self, "vaQueryVideoProcPipelineCaps: %s",
        vaErrorStr (status));
    return FALSE;
  }

  return TRUE;
}

gboolean
gst_va_filter_open (GstVaFilter * self)
{
  VAConfigAttrib attrib = {
    .type = VAConfigAttribRTFormat,
  };
  VADisplay dpy;
  VAStatus status;

  g_return_val_if_fail (GST_IS_VA_FILTER (self), FALSE);

  if (gst_va_filter_is_open (self))
    return TRUE;

  if (!gst_va_filter_ensure_config_attributes (self, &attrib.value))
    return FALSE;

  if (!gst_va_filter_ensure_pipeline_caps (self))
    return FALSE;

  self->image_formats = gst_va_display_get_image_formats (self->display);
  if (!self->image_formats)
    return FALSE;

  dpy = gst_va_display_get_va_dpy (self->display);

  gst_va_display_lock (self->display);
  status = vaCreateConfig (dpy, VAProfileNone, VAEntrypointVideoProc, &attrib,
      1, &self->config);
  gst_va_display_unlock (self->display);
  if (status != VA_STATUS_SUCCESS) {
    GST_ERROR_OBJECT (self, "vaCreateConfig: %s", vaErrorStr (status));
    return FALSE;
  }

  if (!gst_va_filter_ensure_surface_attributes (self))
    goto bail;

  gst_va_display_lock (self->display);
  status = vaCreateContext (dpy, self->config, 0, 0, 0, NULL, 0,
      &self->context);
  gst_va_display_unlock (self->display);
  if (status != VA_STATUS_SUCCESS) {
    GST_ERROR_OBJECT (self, "vaCreateContext: %s", vaErrorStr (status));
    goto bail;
  }

  return TRUE;

bail:
  {
    gst_va_display_lock (self->display);
    status = vaDestroyConfig (dpy, self->config);
    gst_va_display_unlock (self->display);

    return FALSE;
  }
}

gboolean
gst_va_filter_close (GstVaFilter * self)
{
  VADisplay dpy;
  VAStatus status;

  g_return_val_if_fail (GST_IS_VA_FILTER (self), FALSE);

  if (!gst_va_filter_is_open (self))
    return TRUE;

  dpy = gst_va_display_get_va_dpy (self->display);

  if (self->context != VA_INVALID_ID) {
    gst_va_display_lock (self->display);
    status = vaDestroyContext (dpy, self->context);
    gst_va_display_unlock (self->display);
    if (status != VA_STATUS_SUCCESS)
      GST_ERROR_OBJECT (self, "vaDestroyContext: %s", vaErrorStr (status));
  }

  gst_va_display_lock (self->display);
  status = vaDestroyConfig (dpy, self->config);
  gst_va_display_unlock (self->display);
  if (status != VA_STATUS_SUCCESS) {
    GST_ERROR_OBJECT (self, "vaDestroyConfig: %s", vaErrorStr (status));
    return FALSE;
  }

  GST_OBJECT_LOCK (self);
  g_clear_pointer (&self->available_filters, g_array_unref);
  g_clear_pointer (&self->filters, g_array_unref);
  gst_va_filter_init (self);
  GST_OBJECT_UNLOCK (self);

  return TRUE;
}

/* *INDENT-OFF* */
static const struct VaFilterCapMap {
  VAProcFilterType type;
  guint count;
} filter_cap_map[] = {
  { VAProcFilterNoiseReduction, 1 },
  { VAProcFilterDeinterlacing, VAProcDeinterlacingCount },
  { VAProcFilterSharpening, 1 },
  { VAProcFilterColorBalance, VAProcColorBalanceCount },
  { VAProcFilterSkinToneEnhancement, 1 },
  { VAProcFilterTotalColorCorrection, VAProcTotalColorCorrectionCount },
  { VAProcFilterHVSNoiseReduction, 0 },
  { VAProcFilterHighDynamicRangeToneMapping, 1 },
};
/* *INDENT-ON* */

static const struct VaFilterCapMap *
gst_va_filter_get_filter_cap (VAProcFilterType type)
{
  guint i;

  for (i = 0; i < G_N_ELEMENTS (filter_cap_map); i++) {
    if (filter_cap_map[i].type == type)
      return &filter_cap_map[i];
  }

  return NULL;
}

static guint
gst_va_filter_get_filter_cap_count (VAProcFilterType type)
{
  const struct VaFilterCapMap *map = gst_va_filter_get_filter_cap (type);
  return map ? map->count : 0;
}

struct VaFilter
{
  VAProcFilterType type;
  guint num_caps;
  union
  {
    VAProcFilterCap simple;
    VAProcFilterCapDeinterlacing deint[VAProcDeinterlacingCount];
    VAProcFilterCapColorBalance cb[VAProcColorBalanceCount];
    VAProcFilterCapTotalColorCorrection cc[VAProcTotalColorCorrectionCount];
    VAProcFilterCapHighDynamicRange hdr;
  } caps;
};

static gboolean
gst_va_filter_ensure_filters (GstVaFilter * self)
{
  GArray *filters;
  VADisplay dpy;
  VAProcFilterType *filter_types;
  VAStatus status;
  guint i, num = VAProcFilterCount;
  gboolean ret = FALSE;

  GST_OBJECT_LOCK (self);
  if (self->available_filters) {
    GST_OBJECT_UNLOCK (self);
    return TRUE;
  }
  GST_OBJECT_UNLOCK (self);

  filter_types = g_malloc_n (num, sizeof (*filter_types));

  dpy = gst_va_display_get_va_dpy (self->display);

  gst_va_display_lock (self->display);
  status = vaQueryVideoProcFilters (dpy, self->context, filter_types, &num);
  gst_va_display_unlock (self->display);
  if (status == VA_STATUS_ERROR_MAX_NUM_EXCEEDED) {
    filter_types = g_try_realloc_n (filter_types, num, sizeof (*filter_types));
    gst_va_display_lock (self->display);
    status = vaQueryVideoProcFilters (dpy, self->context, filter_types, &num);
    gst_va_display_unlock (self->display);
  }
  if (status != VA_STATUS_SUCCESS) {
    GST_ERROR_OBJECT (self, "vaQueryVideoProcFilters: %s", vaErrorStr (status));
    goto bail;
  }

  if (num == 0)
    goto bail;

  filters = g_array_sized_new (FALSE, FALSE, sizeof (struct VaFilter), num);

  for (i = 0; i < num; i++) {
    guint num_caps = gst_va_filter_get_filter_cap_count (filter_types[i]);
    struct VaFilter filter = { filter_types[i], num_caps, {{{0,}}} };

    if (num_caps > 0) {
      gst_va_display_lock (self->display);
      status = vaQueryVideoProcFilterCaps (dpy, self->context, filter.type,
          &filter.caps, &filter.num_caps);
      gst_va_display_unlock (self->display);
      if (status != VA_STATUS_SUCCESS) {
        GST_WARNING_OBJECT (self, "vaQueryVideoProcFiltersCaps: %s",
            vaErrorStr (status));
        continue;
      }
    }

    g_array_append_val (filters, filter);
  }

  GST_OBJECT_LOCK (self);
  g_clear_pointer (&self->available_filters, g_array_unref);
  self->available_filters = filters;
  GST_OBJECT_UNLOCK (self);

  ret = TRUE;

bail:
  g_free (filter_types);

  return ret;
}

gboolean
gst_va_filter_install_properties (GstVaFilter * self, GObjectClass * klass)
{
  guint i;

  g_return_val_if_fail (GST_IS_VA_FILTER (self), FALSE);

  if (!gst_va_filter_is_open (self))
    return FALSE;

  if (!gst_va_filter_ensure_filters (self))
    return FALSE;

  for (i = 0; i < self->available_filters->len; i++) {
    const struct VaFilter *filter =
        &g_array_index (self->available_filters, struct VaFilter, i);

    switch (filter->type) {
      case VAProcFilterNoiseReduction:{
        const VAProcFilterCap *caps = &filter->caps.simple;

        g_object_class_install_property (klass, GST_VA_FILTER_PROP_DENOISE,
            g_param_spec_float ("denoise", "Noise reduction",
                "Noise reduction factor", caps->range.min_value,
                caps->range.max_value, caps->range.default_value,
                G_PARAM_READWRITE | GST_PARAM_CONDITIONALLY_AVAILABLE
                | G_PARAM_STATIC_STRINGS | GST_PARAM_DOC_SHOW_DEFAULT));
        break;
      }
      case VAProcFilterSharpening:{
        const VAProcFilterCap *caps = &filter->caps.simple;

        g_object_class_install_property (klass, GST_VA_FILTER_PROP_SHARPEN,
            g_param_spec_float ("sharpen", "Sharpening Level",
                "Sharpening/blurring filter", caps->range.min_value,
                caps->range.max_value, caps->range.default_value,
                G_PARAM_READWRITE | GST_PARAM_CONDITIONALLY_AVAILABLE
                | G_PARAM_STATIC_STRINGS | GST_PARAM_DOC_SHOW_DEFAULT));
        break;
      }
      case VAProcFilterSkinToneEnhancement:{
        const VAProcFilterCap *caps = &filter->caps.simple;
        const GParamFlags flags = G_PARAM_READWRITE
            | GST_PARAM_CONDITIONALLY_AVAILABLE | G_PARAM_STATIC_STRINGS
            | GST_PARAM_DOC_SHOW_DEFAULT;
        GParamSpec *pspec;

        /* i965 filter */
        if (filter->num_caps == 0) {
          pspec = g_param_spec_boolean ("skin-tone", "Skin Tone Enhancenment",
              "Skin Tone Enhancenment filter", FALSE, flags);
        } else {
          pspec = g_param_spec_float ("skin-tone", "Skin Tone Enhancenment",
              "Skin Tone Enhancenment filter", caps->range.min_value,
              caps->range.max_value, caps->range.default_value, flags);
        }

        g_object_class_install_property (klass, GST_VA_FILTER_PROP_SKINTONE,
            pspec);
        break;
      }
      case VAProcFilterColorBalance:{
        const VAProcFilterCapColorBalance *caps = filter->caps.cb;
        const GParamFlags flags = G_PARAM_READWRITE
            | GST_PARAM_CONDITIONALLY_AVAILABLE | G_PARAM_STATIC_STRINGS
            | GST_PARAM_DOC_SHOW_DEFAULT;
        GParamSpec *pspec;
        const char *name, *nick, *blurb;
        guint prop_id;

        for (i = 0; i < filter->num_caps; i++) {
          switch (caps[i].type) {
            case VAProcColorBalanceHue:
              name = "hue";
              nick = "Hue";
              blurb = "Color hue value";
              prop_id = GST_VA_FILTER_PROP_HUE;
              break;
            case VAProcColorBalanceSaturation:
              name = "saturation";
              nick = "Saturation";
              blurb = "Color saturation value";
              prop_id = GST_VA_FILTER_PROP_SATURATION;
              break;
            case VAProcColorBalanceBrightness:
              name = "brightness";
              nick = "Brightness";
              blurb = "Color brightness value";
              prop_id = GST_VA_FILTER_PROP_BRIGHTNESS;
              break;
            case VAProcColorBalanceContrast:
              name = "contrast";
              nick = "Contrast";
              blurb = "Color contrast value";
              prop_id = GST_VA_FILTER_PROP_CONTRAST;
              break;
            case VAProcColorBalanceAutoSaturation:
              name = "auto-saturation";
              nick = "Auto-Saturation";
              blurb = "Enable auto saturation";
              prop_id = GST_VA_FILTER_PROP_AUTO_SATURATION;
              break;
            case VAProcColorBalanceAutoBrightness:
              name = "auto-brightness";
              nick = "Auto-Brightness";
              blurb = "Enable auto brightness";
              prop_id = GST_VA_FILTER_PROP_AUTO_BRIGHTNESS;
              break;
            case VAProcColorBalanceAutoContrast:
              name = "auto-contrast";
              nick = "Auto-Contrast";
              blurb = "Enable auto contrast";
              prop_id = GST_VA_FILTER_PROP_AUTO_CONTRAST;
              break;
            default:
              continue;
          }

          if (caps[i].range.min_value < caps[i].range.max_value) {
            pspec = g_param_spec_float (name, nick, blurb,
                caps[i].range.min_value, caps[i].range.max_value,
                caps[i].range.default_value, flags);
          } else {
            pspec = g_param_spec_boolean (name, nick, blurb, FALSE, flags);
          }

          g_object_class_install_property (klass, prop_id, pspec);
        }
      }
      default:
        break;
    }
  }

  if (self->pipeline_caps.mirror_flags != VA_MIRROR_NONE
      || self->pipeline_caps.rotation_flags != VA_ROTATION_NONE) {
    g_object_class_install_property (klass, GST_VA_FILTER_PROP_VIDEO_DIR,
        g_param_spec_enum ("video-direction", "Video Direction",
            "Video direction: rotation and flipping",
            GST_TYPE_VIDEO_ORIENTATION_METHOD, GST_VIDEO_ORIENTATION_IDENTITY,
            G_PARAM_READWRITE | GST_PARAM_CONDITIONALLY_AVAILABLE
            | G_PARAM_STATIC_STRINGS | GST_PARAM_DOC_SHOW_DEFAULT));
  }

  return TRUE;
}

const gpointer
gst_va_filter_get_filter_caps (GstVaFilter * self, VAProcFilterType type,
    guint * num_caps)
{
  struct VaFilter *filter = NULL;
  /* *INDENT-OFF* */
  static const VAProcFilterCap i965_ste_caps = {
    .range = {
      .min_value = 0.0,
      .max_value = 1.0,
      .default_value = 0.0,
      .step = 1.0,
    },
  };
  /* *INDENT-ON* */
  gpointer ret = NULL;
  guint i;

  if (!gst_va_filter_is_open (self))
    return FALSE;

  if (!gst_va_filter_ensure_filters (self))
    return FALSE;

  for (i = 0; i < self->available_filters->len; i++) {
    filter = &g_array_index (self->available_filters, struct VaFilter, i);
    if (filter->type == type) {
      if (filter->num_caps > 0)
        ret = &filter->caps;
      else if (type == VAProcFilterSkinToneEnhancement && filter->num_caps == 0)
        ret = (gpointer) & i965_ste_caps;
      break;
    }
  }

  if (ret && filter && num_caps)
    *num_caps = filter->num_caps;

  return ret;
}

guint32
gst_va_filter_get_mem_types (GstVaFilter * self)
{
  g_return_val_if_fail (GST_IS_VA_FILTER (self), 0);

  return self->mem_types;
}

GArray *
gst_va_filter_get_surface_formats (GstVaFilter * self)
{
  g_return_val_if_fail (GST_IS_VA_FILTER (self), NULL);

  return self->surface_formats ? g_array_ref (self->surface_formats) : NULL;
}

static gboolean
_from_video_orientation_method (GstVideoOrientationMethod orientation,
    guint * mirror, guint * rotation)
{
  switch (orientation) {
    case GST_VIDEO_ORIENTATION_IDENTITY:
      *mirror = VA_MIRROR_NONE;
      *rotation = VA_ROTATION_NONE;
      break;
    case GST_VIDEO_ORIENTATION_HORIZ:
      *mirror = VA_MIRROR_HORIZONTAL;
      *rotation = VA_ROTATION_NONE;
      break;
    case GST_VIDEO_ORIENTATION_VERT:
      *mirror = VA_MIRROR_VERTICAL;
      *rotation = VA_ROTATION_NONE;
      break;
    case GST_VIDEO_ORIENTATION_90R:
      *mirror = VA_MIRROR_NONE;
      *rotation = VA_ROTATION_90;
      break;
    case GST_VIDEO_ORIENTATION_180:
      *mirror = VA_MIRROR_NONE;
      *rotation = VA_ROTATION_180;
      break;
    case GST_VIDEO_ORIENTATION_90L:
      *mirror = VA_MIRROR_NONE;
      *rotation = VA_ROTATION_270;
      break;
    case GST_VIDEO_ORIENTATION_UL_LR:
      *mirror = VA_MIRROR_HORIZONTAL;
      *rotation = VA_ROTATION_90;
      break;
    case GST_VIDEO_ORIENTATION_UR_LL:
      *mirror = VA_MIRROR_VERTICAL;
      *rotation = VA_ROTATION_90;
      break;
    default:
      return FALSE;
      break;
  }

  return TRUE;
}

gboolean
gst_va_filter_set_orientation (GstVaFilter * self,
    GstVideoOrientationMethod orientation)
{
  guint32 mirror = VA_MIRROR_NONE, rotation = VA_ROTATION_NONE;

  if (!_from_video_orientation_method (orientation, &mirror, &rotation))
    return FALSE;

  if (mirror != VA_MIRROR_NONE && !(self->pipeline_caps.mirror_flags & mirror))
    return FALSE;

  if (rotation != VA_ROTATION_NONE
      && !(self->pipeline_caps.rotation_flags & (1 << rotation)))
    return FALSE;

  GST_OBJECT_LOCK (self);
  self->orientation = orientation;
  self->mirror = mirror;
  self->rotation = rotation;
  GST_OBJECT_UNLOCK (self);

  return TRUE;
}

GstVideoOrientationMethod
gst_va_filter_get_orientation (GstVaFilter * self)
{
  return self->orientation;
}

static inline GstCaps *
_create_base_caps (GstVaFilter * self)
{
  /* XXX(victor): remove interlace-mode when deinterlacing is
   * supported */
  return gst_caps_new_simple ("video/x-raw", "width", GST_TYPE_INT_RANGE,
      self->min_width, self->max_width, "height", GST_TYPE_INT_RANGE,
      self->min_height, self->max_height, "interlace-mode", G_TYPE_STRING,
      "progressive", NULL);
}

GstCaps *
gst_va_filter_get_caps (GstVaFilter * self)
{
  GstCaps *caps, *base_caps, *feature_caps;
  GstCapsFeatures *features;

  g_return_val_if_fail (GST_IS_VA_FILTER (self), NULL);

  if (!gst_va_filter_is_open (self))
    return NULL;

  base_caps = _create_base_caps (self);

  if (!gst_caps_set_format_array (base_caps, self->surface_formats)) {
    gst_caps_unref (base_caps);
    return NULL;
  }

  caps = gst_caps_new_empty ();

  if (self->mem_types & VA_SURFACE_ATTRIB_MEM_TYPE_VA) {
    feature_caps = gst_caps_copy (base_caps);
    features = gst_caps_features_from_string ("memory:VAMemory");
    gst_caps_set_features_simple (feature_caps, features);
    caps = gst_caps_merge (caps, feature_caps);
  }
  if (self->mem_types & VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME
      || self->mem_types & VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2) {
    feature_caps = gst_caps_copy (base_caps);
    features = gst_caps_features_from_string ("memory:DMABuf");
    gst_caps_set_features_simple (feature_caps, features);
    caps = gst_caps_merge (caps, feature_caps);
  }

  gst_caps_unref (base_caps);

  base_caps = _create_base_caps (self);

  if (!gst_caps_set_format_array (base_caps, self->image_formats)) {
    gst_caps_unref (base_caps);
    return NULL;
  }

  return gst_caps_merge (caps, base_caps);
}

static gboolean
_destroy_filters (GstVaFilter * self)
{
  VABufferID buffer;
  VADisplay dpy;
  VAStatus status;
  gboolean ret = TRUE;
  guint i;

  if (!self->filters)
    return TRUE;

  GST_TRACE_OBJECT (self, "Destroy filter buffers");

  dpy = gst_va_display_get_va_dpy (self->display);

  for (i = 0; i < self->filters->len; i++) {
    buffer = g_array_index (self->filters, VABufferID, i);
    gst_va_display_lock (self->display);
    status = vaDestroyBuffer (dpy, buffer);
    gst_va_display_unlock (self->display);
    if (status != VA_STATUS_SUCCESS) {
      ret = FALSE;
      GST_WARNING ("Failed to destroy filter buffer: %s", vaErrorStr (status));
    }
  }

  self->filters = g_array_set_size (self->filters, 0);

  return ret;
}

gboolean
gst_va_filter_add_filter_buffer (GstVaFilter * self, gpointer data, gsize size,
    guint num)
{
  VABufferID buffer;
  VADisplay dpy;
  VAStatus status;

  g_return_val_if_fail (GST_IS_VA_FILTER (self), FALSE);
  g_return_val_if_fail (data && size > 0, FALSE);

  if (!gst_va_filter_is_open (self))
    return FALSE;

  dpy = gst_va_display_get_va_dpy (self->display);
  gst_va_display_lock (self->display);
  status = vaCreateBuffer (dpy, self->context, VAProcFilterParameterBufferType,
      size, num, data, &buffer);
  gst_va_display_unlock (self->display);
  if (status != VA_STATUS_SUCCESS) {
    GST_ERROR_OBJECT (self, "vaCreateBuffer: %s", vaErrorStr (status));
    return FALSE;
  }

  /* leazy creation */
  if (!self->filters)
    self->filters = g_array_sized_new (FALSE, FALSE, sizeof (VABufferID), 16);

  g_array_append_val (self->filters, buffer);

  return TRUE;
}

static gboolean
_create_pipeline_buffer (GstVaFilter * self, VASurfaceID surface,
    VARectangle * src_rect, VARectangle * dst_rect, VABufferID * buffer)
{
  VADisplay dpy;
  VAStatus status;
  VABufferID *filters = (self->filters && self->filters->len > 0) ?
      (VABufferID *) self->filters->data : NULL;
  guint32 num_filters = self->filters ? self->filters->len : 0;

  /* *INDENT-OFF* */
  VAProcPipelineParameterBuffer params = {
    .surface = surface,
    .surface_region = src_rect,
    .surface_color_standard = VAProcColorStandardNone,
    .output_region = dst_rect,
    .output_background_color = 0xff000000,
    .output_color_standard = VAProcColorStandardNone,
    .filters = filters,
    .num_filters = num_filters,
    .rotation_state = self->rotation,
    .mirror_state = self->mirror,
  };
  /* *INDENT-ON* */

  dpy = gst_va_display_get_va_dpy (self->display);
  gst_va_display_lock (self->display);
  status = vaCreateBuffer (dpy, self->context,
      VAProcPipelineParameterBufferType, sizeof (params), 1, &params, buffer);
  gst_va_display_unlock (self->display);
  if (status != VA_STATUS_SUCCESS) {
    GST_ERROR_OBJECT (self, "vaCreateBuffer: %s", vaErrorStr (status));
    return FALSE;
  }

  return TRUE;
}

gboolean
gst_va_filter_convert_surface (GstVaFilter * self, VASurfaceID in_surface,
    GstVideoInfo * in_info, VASurfaceID out_surface, GstVideoInfo * out_info)
{
  VABufferID buffer;
  VADisplay dpy;
  VAProcPipelineCaps pipeline_caps = { 0, };
  VARectangle src_rect;
  VARectangle dst_rect;
  VAStatus status;
  VABufferID *filters;
  guint32 num_filters;
  gboolean ret = FALSE;

  g_return_val_if_fail (GST_IS_VA_FILTER (self), FALSE);
  g_return_val_if_fail (in_surface != VA_INVALID_ID
      && out_surface != VA_INVALID_ID, FALSE);
  g_return_val_if_fail (out_info && in_info, FALSE);

  if (!gst_va_filter_is_open (self))
    return FALSE;

  GST_TRACE_OBJECT (self, "Processing %#x", in_surface);

  /* *INDENT-OFF* */
  src_rect = (VARectangle) {
    .width = GST_VIDEO_INFO_WIDTH (in_info),
    .height = GST_VIDEO_INFO_HEIGHT (in_info),
  };
  dst_rect = (VARectangle) {
    .width = GST_VIDEO_INFO_WIDTH (out_info),
    .height = GST_VIDEO_INFO_HEIGHT (out_info),
  };
  /* *INDENT-ON* */

  num_filters = (self->filters) ? self->filters->len : 0;
  filters = (self->filters && num_filters > 0) ?
      (VABufferID *) self->filters->data : NULL;

  dpy = gst_va_display_get_va_dpy (self->display);

  gst_va_display_lock (self->display);
  status = vaQueryVideoProcPipelineCaps (dpy, self->context, filters,
      num_filters, &pipeline_caps);
  gst_va_display_unlock (self->display);
  if (status != VA_STATUS_SUCCESS) {
    GST_ERROR_OBJECT (self, "vaQueryVideoProcPipelineCaps: %s",
        vaErrorStr (status));
    return FALSE;
  }

  if (!_create_pipeline_buffer (self, in_surface, &src_rect, &dst_rect,
          &buffer))
    return FALSE;

  gst_va_display_lock (self->display);
  status = vaBeginPicture (dpy, self->context, out_surface);
  gst_va_display_unlock (self->display);
  if (status != VA_STATUS_SUCCESS) {
    GST_ERROR_OBJECT (self, "vaBeginPicture: %s", vaErrorStr (status));
    goto fail_end_pic;
  }

  gst_va_display_lock (self->display);
  status = vaRenderPicture (dpy, self->context, &buffer, 1);
  gst_va_display_unlock (self->display);
  if (status != VA_STATUS_SUCCESS) {
    GST_ERROR_OBJECT (self, "vaRenderPicture: %s", vaErrorStr (status));
    goto fail_end_pic;
  }

  gst_va_display_lock (self->display);
  status = vaEndPicture (dpy, self->context);
  gst_va_display_unlock (self->display);
  if (status != VA_STATUS_SUCCESS) {
    GST_ERROR_OBJECT (self, "vaEndPicture: %s", vaErrorStr (status));
    goto bail;
  }

  ret = TRUE;

bail:
  gst_va_display_lock (self->display);
  status = vaDestroyBuffer (dpy, buffer);
  gst_va_display_unlock (self->display);
  if (status != VA_STATUS_SUCCESS) {
    ret = FALSE;
    GST_WARNING ("Failed to destroy filter buffer: %s", vaErrorStr (status));
  }

  if (!_destroy_filters (self))
    return FALSE;

  return ret;

fail_end_pic:
  {
    gst_va_display_lock (self->display);
    status = vaEndPicture (dpy, self->context);
    gst_va_display_unlock (self->display);
    if (status != VA_STATUS_SUCCESS)
      GST_ERROR_OBJECT (self, "vaEndPicture: %s", vaErrorStr (status));
    goto bail;
  }
}
