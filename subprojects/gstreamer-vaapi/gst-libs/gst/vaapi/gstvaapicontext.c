/*
 *  gstvaapicontext.c - VA context abstraction
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@splitted-desktop.com>
 *  Copyright (C) 2011-2014 Intel Corporation
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
 * SECTION:gstvaapicontext
 * @short_description: VA context abstraction
 */

#include "sysdeps.h"
#include "gstvaapicompat.h"
#include "gstvaapicontext.h"
#include "gstvaapidisplay_priv.h"
#include "gstvaapisurface_priv.h"
#include "gstvaapisurfacepool.h"
#include "gstvaapisurfaceproxy.h"
#include "gstvaapivideopool_priv.h"
#include "gstvaapiutils.h"

/* Define default VA surface chroma format to YUV 4:2:0 */
#define DEFAULT_CHROMA_TYPE (GST_VAAPI_CHROMA_TYPE_YUV420)

/* Number of scratch surfaces beyond those used as reference */
#define SCRATCH_SURFACES_COUNT (4)

/* Debug category for GstVaapiContext */
GST_DEBUG_CATEGORY (gst_debug_vaapi_context);
#define GST_CAT_DEFAULT gst_debug_vaapi_context

static void
_init_vaapi_context_debug (void)
{
#ifndef GST_DISABLE_GST_DEBUG
  static gsize _init = 0;

  if (g_once_init_enter (&_init)) {
    GST_DEBUG_CATEGORY_INIT (gst_debug_vaapi_context, "vaapicontext", 0,
        "VA-API context");
    g_once_init_leave (&_init, 1);
  }
#endif
}

static inline gboolean
_context_is_broken_jpeg_decoder (GstVaapiContext * context)
{
  GstVaapiDisplay *const display = GST_VAAPI_CONTEXT_DISPLAY (context);

  return (context->info.profile == GST_VAAPI_PROFILE_JPEG_BASELINE
      && context->info.entrypoint == GST_VAAPI_ENTRYPOINT_VLD
      && gst_vaapi_display_has_driver_quirks (display,
          GST_VAAPI_DRIVER_QUIRK_JPEG_DEC_BROKEN_FORMATS));
}

static gboolean
ensure_attributes (GstVaapiContext * context)
{
  if (G_LIKELY (context->attribs))
    return TRUE;

  context->attribs =
      gst_vaapi_config_surface_attributes_get (GST_VAAPI_CONTEXT_DISPLAY
      (context), context->va_config);

  if (!context->attribs)
    return FALSE;

  if (_context_is_broken_jpeg_decoder (context)) {
    GstVideoFormat fmt = GST_VIDEO_FORMAT_NV12;
    g_array_prepend_val (context->attribs->formats, fmt);

    context->attribs->mem_types &= ~VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME;
  }
  return TRUE;
}

/* XXX(victor): verify the preferred video format concords with the
 * chroma type; otherwise it is changed for the (very arbritrary)
 * preferred format from the requested context chroma type, in the
 * context attributes */
static void
ensure_preferred_format (GstVaapiContext * context)
{
  const GstVaapiContextInfo *const cip = &context->info;
  GArray *formats;
  guint i;

  if (context->preferred_format != GST_VIDEO_FORMAT_UNKNOWN)
    return;

  if (_context_is_broken_jpeg_decoder (context))
    return;

  if (!ensure_attributes (context) || !context->attribs->formats)
    return;

  formats = context->attribs->formats;
  for (i = 0; i < formats->len; i++) {
    GstVideoFormat format = g_array_index (formats, GstVideoFormat, i);
    if (format == gst_vaapi_video_format_from_chroma (cip->chroma_type)) {
      context->preferred_format = format;
      break;
    }
  }

  return;
}

static inline gboolean
context_get_attribute (GstVaapiContext * context, VAConfigAttribType type,
    guint * out_value_ptr)
{
  return gst_vaapi_get_config_attribute (GST_VAAPI_CONTEXT_DISPLAY (context),
      context->va_profile, context->va_entrypoint, type, out_value_ptr);
}

static void
context_destroy_surfaces (GstVaapiContext * context)
{
  if (context->surfaces) {
    g_ptr_array_unref (context->surfaces);
    context->surfaces = NULL;
  }

  context->preferred_format = GST_VIDEO_FORMAT_UNKNOWN;

  gst_vaapi_video_pool_replace (&context->surfaces_pool, NULL);
}

static void
context_destroy (GstVaapiContext * context)
{
  GstVaapiDisplay *const display = GST_VAAPI_CONTEXT_DISPLAY (context);
  VAContextID context_id;
  VAStatus status;

  context_id = GST_VAAPI_CONTEXT_ID (context);
  GST_DEBUG ("context 0x%08x / config 0x%08x", context_id, context->va_config);

  if (context_id != VA_INVALID_ID) {
    GST_VAAPI_DISPLAY_LOCK (display);
    status = vaDestroyContext (GST_VAAPI_DISPLAY_VADISPLAY (display),
        context_id);
    GST_VAAPI_DISPLAY_UNLOCK (display);
    if (!vaapi_check_status (status, "vaDestroyContext()"))
      GST_WARNING ("failed to destroy context 0x%08x", context_id);
    GST_VAAPI_CONTEXT_ID (context) = VA_INVALID_ID;
  }

  if (context->va_config != VA_INVALID_ID) {
    GST_VAAPI_DISPLAY_LOCK (display);
    status = vaDestroyConfig (GST_VAAPI_DISPLAY_VADISPLAY (display),
        context->va_config);
    GST_VAAPI_DISPLAY_UNLOCK (display);
    if (!vaapi_check_status (status, "vaDestroyConfig()"))
      GST_WARNING ("failed to destroy config 0x%08x", context->va_config);
    context->va_config = VA_INVALID_ID;
  }

  if (context->attribs) {
    gst_vaapi_config_surface_attributes_free (context->attribs);
    context->attribs = NULL;
  }
}

static gboolean
context_ensure_surfaces (GstVaapiContext * context)
{
  GstVaapiDisplay *display = GST_VAAPI_CONTEXT_DISPLAY (context);
  const GstVaapiContextInfo *const cip = &context->info;
  const guint num_surfaces = cip->ref_frames + SCRATCH_SURFACES_COUNT;
  GstVaapiSurface *surface;
  GstVideoFormat format;
  guint i, capacity;

  ensure_preferred_format (context);
  format = context->preferred_format;
  for (i = context->surfaces->len; i < num_surfaces; i++) {
    if (format != GST_VIDEO_FORMAT_UNKNOWN) {
      surface = gst_vaapi_surface_new_with_format (display, format, cip->width,
          cip->height, 0);
    } else {
      surface = gst_vaapi_surface_new (display, cip->chroma_type, cip->width,
          cip->height);
    }
    if (!surface)
      return FALSE;
    g_ptr_array_add (context->surfaces, surface);
    if (!gst_vaapi_video_pool_add_object (context->surfaces_pool, surface))
      return FALSE;
  }

  capacity = cip->usage == GST_VAAPI_CONTEXT_USAGE_DECODE ? 0 : num_surfaces;
  gst_vaapi_video_pool_set_capacity (context->surfaces_pool, capacity);
  return TRUE;
}

static gboolean
context_create_surfaces (GstVaapiContext * context)
{
  const GstVaapiContextInfo *const cip = &context->info;
  GstVaapiDisplay *const display = GST_VAAPI_CONTEXT_DISPLAY (context);
  guint num_surfaces;

  num_surfaces = cip->ref_frames + SCRATCH_SURFACES_COUNT;
  if (!context->surfaces) {
    context->surfaces = g_ptr_array_new_full (num_surfaces,
        (GDestroyNotify) gst_mini_object_unref);
    if (!context->surfaces)
      return FALSE;
  }

  if (!context->surfaces_pool) {
    context->surfaces_pool =
        gst_vaapi_surface_pool_new_with_chroma_type (display, cip->chroma_type,
        cip->width, cip->height, 0);

    if (!context->surfaces_pool)
      return FALSE;
  }
  return context_ensure_surfaces (context);
}

static gboolean
context_create (GstVaapiContext * context)
{
  const GstVaapiContextInfo *const cip = &context->info;
  GstVaapiDisplay *const display = GST_VAAPI_CONTEXT_DISPLAY (context);
  VAContextID context_id;
  VASurfaceID surface_id;
  VASurfaceID *surfaces_data = NULL;
  VAStatus status;
  GArray *surfaces = NULL;
  gboolean success = FALSE;
  guint i;
  gint num_surfaces = 0;

  if (!context->surfaces && !context_create_surfaces (context))
    goto cleanup;

  /* Create VA surfaces list for vaCreateContext() */
  surfaces = g_array_sized_new (FALSE,
      FALSE, sizeof (VASurfaceID), context->surfaces->len);
  if (!surfaces)
    goto cleanup;

  for (i = 0; i < context->surfaces->len; i++) {
    GstVaapiSurface *const surface = g_ptr_array_index (context->surfaces, i);
    if (!surface)
      goto cleanup;
    surface_id = GST_VAAPI_SURFACE_ID (surface);
    g_array_append_val (surfaces, surface_id);
  }

  g_assert (surfaces->len == context->surfaces->len);

  /* vaCreateContext() doesn't really need an array of VASurfaceIDs (see
   * https://lists.01.org/pipermail/intel-vaapi-media/2017-July/000052.html and
   * https://github.com/intel/libva/issues/251); pass a dummy list of valid
   * (non-null) IDs until the signature gets updated. */
  if (cip->usage != GST_VAAPI_CONTEXT_USAGE_DECODE) {
    surfaces_data = (VASurfaceID *) surfaces->data;
    num_surfaces = surfaces->len;
  }

  GST_VAAPI_DISPLAY_LOCK (display);
  status = vaCreateContext (GST_VAAPI_DISPLAY_VADISPLAY (display),
      context->va_config, cip->width, cip->height, VA_PROGRESSIVE,
      surfaces_data, num_surfaces, &context_id);
  GST_VAAPI_DISPLAY_UNLOCK (display);
  if (!vaapi_check_status (status, "vaCreateContext()"))
    goto cleanup;

  GST_VAAPI_CONTEXT_ID (context) = context_id;
  success = TRUE;

cleanup:
  if (surfaces)
    g_array_unref (surfaces);
  return success;
}

static gboolean
config_create (GstVaapiContext * context)
{
  const GstVaapiContextInfo *const cip = &context->info;
  GstVaapiDisplay *const display = GST_VAAPI_CONTEXT_DISPLAY (context);
  VAConfigAttrib attribs[7], *attrib;
  VAStatus status;
  guint value, va_chroma_format, attrib_index;

  /* Reset profile and entrypoint */
  if (cip->profile == GST_VAAPI_PROFILE_UNKNOWN
      || cip->entrypoint == GST_VAAPI_ENTRYPOINT_INVALID)
    goto cleanup;
  context->va_profile = gst_vaapi_profile_get_va_profile (cip->profile);
  context->va_entrypoint =
      gst_vaapi_entrypoint_get_va_entrypoint (cip->entrypoint);

  attrib_index = 0;
  attrib = &attribs[attrib_index];
  g_assert (attrib_index < G_N_ELEMENTS (attribs));

  /* Validate VA surface format */
  va_chroma_format = from_GstVaapiChromaType (cip->chroma_type);
  if (!va_chroma_format)
    goto cleanup;
  attrib->type = VAConfigAttribRTFormat;
  if (!context_get_attribute (context, attrib->type, &value))
    goto cleanup;
  if (!(value & va_chroma_format)) {
    GST_ERROR ("unsupported chroma format (%s)",
        string_of_va_chroma_format (va_chroma_format));
    goto cleanup;
  }
  attrib->value = value;
  attrib = &attribs[++attrib_index];
  g_assert (attrib_index < G_N_ELEMENTS (attribs));

  switch (cip->usage) {
#if GST_VAAPI_USE_ENCODERS
    case GST_VAAPI_CONTEXT_USAGE_ENCODE:
    {
      const GstVaapiConfigInfoEncoder *const config = &cip->config.encoder;
      guint va_rate_control;

      /* Rate control */
      va_rate_control = from_GstVaapiRateControl (config->rc_mode);
      if (va_rate_control != VA_RC_NONE) {
        attrib->type = VAConfigAttribRateControl;
        if (!context_get_attribute (context, attrib->type, &value))
          goto cleanup;

        if ((value & va_rate_control) != va_rate_control) {
          GST_ERROR ("unsupported %s rate control",
              string_of_VARateControl (va_rate_control));
          goto cleanup;
        }
        attrib->value = va_rate_control;
        attrib = &attribs[++attrib_index];
        g_assert (attrib_index < G_N_ELEMENTS (attribs));
      }
      /* Packed headers */
      if (config->packed_headers) {
        attrib->type = VAConfigAttribEncPackedHeaders;
        if (!context_get_attribute (context, attrib->type, &value))
          goto cleanup;

        if ((value & config->packed_headers) != config->packed_headers) {
          GST_ERROR ("unsupported packed headers 0x%08x",
              config->packed_headers & ~(value & config->packed_headers));
          goto cleanup;
        }
        attrib->value = config->packed_headers;
        attrib = &attribs[++attrib_index];
        g_assert (attrib_index < G_N_ELEMENTS (attribs));
      }
      if (cip->profile == GST_VAAPI_PROFILE_JPEG_BASELINE) {
        attrib->type = VAConfigAttribEncJPEG;
        if (!context_get_attribute (context, attrib->type, &value))
          goto cleanup;
        attrib->value = value;
        attrib = &attribs[++attrib_index];
        g_assert (attrib_index < G_N_ELEMENTS (attribs));
      }
#if VA_CHECK_VERSION(0,39,1)
      if (config->roi_capability) {
        VAConfigAttribValEncROI *roi_config;

        attrib->type = VAConfigAttribEncROI;
        if (!context_get_attribute (context, attrib->type, &value))
          goto cleanup;
        roi_config = (VAConfigAttribValEncROI *) & value;
        if (roi_config->bits.num_roi_regions != config->roi_num_supported) {
          GST_ERROR ("Mismatched ROI support: number of regions supported: %d",
              roi_config->bits.num_roi_regions);
          goto cleanup;
        }
        if (config->rc_mode != GST_VAAPI_RATECONTROL_CQP
            && VA_ROI_RC_QP_DELTA_SUPPORT (roi_config) == 0) {
          GST_ERROR ("Mismatched ROI support:  ROI delta QP: %d",
              VA_ROI_RC_QP_DELTA_SUPPORT (roi_config));
          goto cleanup;
        }
        attrib->value = value;
        attrib = &attribs[++attrib_index];
        g_assert (attrib_index < G_N_ELEMENTS (attribs));
      }
#endif
      break;
    }
#endif
    default:
      break;
  }

  GST_VAAPI_DISPLAY_LOCK (display);
  status = vaCreateConfig (GST_VAAPI_DISPLAY_VADISPLAY (display),
      context->va_profile, context->va_entrypoint, attribs, attrib_index,
      &context->va_config);
  GST_VAAPI_DISPLAY_UNLOCK (display);
  if (!vaapi_check_status (status, "vaCreateConfig()"))
    goto cleanup;

  return TRUE;
cleanup:
  GST_WARNING ("Failed to create vaConfig");
  return FALSE;
}

/** Updates config for encoding. Returns %TRUE if config changed */
static gboolean
context_update_config_encoder (GstVaapiContext * context,
    const GstVaapiConfigInfoEncoder * new_config)
{
  GstVaapiConfigInfoEncoder *const config = &context->info.config.encoder;
  gboolean config_changed = FALSE;

  g_assert (context->info.usage == GST_VAAPI_CONTEXT_USAGE_ENCODE);

  if (config->rc_mode != new_config->rc_mode) {
    config->rc_mode = new_config->rc_mode;
    config_changed = TRUE;
  }

  if (config->packed_headers != new_config->packed_headers) {
    config->packed_headers = new_config->packed_headers;
    config_changed = TRUE;
  }

  if (config->roi_capability != new_config->roi_capability ||
      config->roi_num_supported != new_config->roi_num_supported) {
    config->roi_capability = new_config->roi_capability;
    config->roi_num_supported = new_config->roi_num_supported;
    config_changed = TRUE;
  }

  return config_changed;
}

static inline void
gst_vaapi_context_init (GstVaapiContext * context,
    const GstVaapiContextInfo * new_cip)
{
  GstVaapiContextInfo *const cip = &context->info;

  *cip = *new_cip;
  if (!cip->chroma_type)
    cip->chroma_type = DEFAULT_CHROMA_TYPE;

  context->va_config = VA_INVALID_ID;
  context->reset_on_resize = TRUE;

  context->attribs = NULL;
  context->preferred_format = GST_VIDEO_FORMAT_UNKNOWN;
}

/**
 * gst_vaapi_context_new:
 * @display: a #GstVaapiDisplay
 * @cip: a pointer to the #GstVaapiContextInfo
 *
 * Creates a new #GstVaapiContext with the configuration specified by
 * @cip, thus including profile, entry-point, encoded size and maximum
 * number of reference frames reported by the bitstream.
 *
 * Return value: the newly allocated #GstVaapiContext object
 */
GstVaapiContext *
gst_vaapi_context_new (GstVaapiDisplay * display,
    const GstVaapiContextInfo * cip)
{
  GstVaapiContext *context;

  g_return_val_if_fail (display, NULL);

  _init_vaapi_context_debug ();

  if (cip->profile == GST_VAAPI_PROFILE_UNKNOWN
      || cip->entrypoint == GST_VAAPI_ENTRYPOINT_INVALID)
    return NULL;

  context = g_new (GstVaapiContext, 1);
  if (!context)
    return NULL;

  GST_VAAPI_CONTEXT_DISPLAY (context) = gst_object_ref (display);
  GST_VAAPI_CONTEXT_ID (context) = VA_INVALID_ID;
  g_atomic_int_set (&context->ref_count, 1);
  context->surfaces = NULL;
  context->surfaces_pool = NULL;

  gst_vaapi_context_init (context, cip);

  if (!config_create (context))
    goto error;

  /* this means we don't want to create a VAcontext */
  if (cip->width == 0 && cip->height == 0)
    goto done;

  /* this is not valid */
  if (cip->width == 0 || cip->height == 0)
    goto error;

  if (!context_create (context))
    goto error;

done:
  GST_DEBUG ("context 0x%08" G_GSIZE_MODIFIER "x / config 0x%08x",
      GST_VAAPI_CONTEXT_ID (context), context->va_config);
  return context;

  /* ERRORS */
error:
  {
    gst_vaapi_context_unref (context);
    return NULL;
  }
}

/**
 * gst_vaapi_context_reset:
 * @context: a #GstVaapiContext
 * @new_cip: a pointer to the new #GstVaapiContextInfo details
 *
 * Resets @context to the configuration specified by @new_cip, thus
 * including profile, entry-point, encoded size and maximum number of
 * reference frames reported by the bitstream.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_context_reset (GstVaapiContext * context,
    const GstVaapiContextInfo * new_cip)
{
  GstVaapiContextInfo *const cip = &context->info;
  gboolean reset_surfaces = FALSE, reset_config = FALSE;
  gboolean grow_surfaces = FALSE;
  GstVaapiChromaType chroma_type;

  if (new_cip->profile == GST_VAAPI_PROFILE_UNKNOWN
      || new_cip->entrypoint == GST_VAAPI_ENTRYPOINT_INVALID)
    return FALSE;

  chroma_type = new_cip->chroma_type ? new_cip->chroma_type :
      DEFAULT_CHROMA_TYPE;
  if (cip->chroma_type != chroma_type) {
    cip->chroma_type = chroma_type;
    reset_surfaces = TRUE;
  }

  if (cip->width != new_cip->width || cip->height != new_cip->height) {
    cip->width = new_cip->width;
    cip->height = new_cip->height;
    reset_surfaces = TRUE;
  }

  if (cip->profile != new_cip->profile ||
      cip->entrypoint != new_cip->entrypoint) {
    cip->profile = new_cip->profile;
    cip->entrypoint = new_cip->entrypoint;
    reset_config = TRUE;
  }

  if (cip->ref_frames < new_cip->ref_frames) {
    cip->ref_frames = new_cip->ref_frames;
    grow_surfaces = TRUE;
  }

  if (cip->usage != new_cip->usage) {
    cip->usage = new_cip->usage;
    reset_config = TRUE;
    memcpy (&cip->config, &new_cip->config, sizeof (cip->config));
  } else if (new_cip->usage == GST_VAAPI_CONTEXT_USAGE_ENCODE) {
    if (context_update_config_encoder (context, &new_cip->config.encoder))
      reset_config = TRUE;
  } else if (new_cip->usage == GST_VAAPI_CONTEXT_USAGE_DECODE) {
    if ((reset_surfaces && context->reset_on_resize) || grow_surfaces)
      reset_config = TRUE;
  }

  if (reset_surfaces)
    context_destroy_surfaces (context);
  if (reset_config)
    context_destroy (context);

  if (reset_config && !(config_create (context) && context_create (context)))
    return FALSE;
  if (reset_surfaces && !context_create_surfaces (context))
    return FALSE;
  else if (grow_surfaces && !context_ensure_surfaces (context))
    return FALSE;
  return TRUE;
}

/**
 * gst_vaapi_context_get_id:
 * @context: a #GstVaapiContext
 *
 * Returns the underlying VAContextID of the @context.
 *
 * Return value: the underlying VA context id
 */
GstVaapiID
gst_vaapi_context_get_id (GstVaapiContext * context)
{
  g_return_val_if_fail (context != NULL, VA_INVALID_ID);

  return GST_VAAPI_CONTEXT_ID (context);
}

/**
 * gst_vaapi_context_get_surface_proxy:
 * @context: a #GstVaapiContext
 *
 * Acquires a free surface, wrapped into a #GstVaapiSurfaceProxy. The
 * returned surface will be automatically released when the proxy is
 * destroyed. So, it is enough to call gst_vaapi_surface_proxy_unref()
 * after usage.
 *
 * This function returns %NULL if there is no free surface available
 * in the pool. The surfaces are pre-allocated during context creation
 * though.
 *
 * Return value: a free surface, or %NULL if none is available
 */
GstVaapiSurfaceProxy *
gst_vaapi_context_get_surface_proxy (GstVaapiContext * context)
{
  g_return_val_if_fail (context != NULL, NULL);

  return
      gst_vaapi_surface_proxy_new_from_pool (GST_VAAPI_SURFACE_POOL
      (context->surfaces_pool));
}

/**
 * gst_vaapi_context_get_surface_count:
 * @context: a #GstVaapiContext
 *
 * Retrieves the number of free surfaces left in the pool.
 *
 * Return value: the number of free surfaces available in the pool
 */
guint
gst_vaapi_context_get_surface_count (GstVaapiContext * context)
{
  g_return_val_if_fail (context != NULL, 0);

  if (gst_vaapi_video_pool_get_capacity (context->surfaces_pool) == 0)
    return G_MAXUINT;
  return gst_vaapi_video_pool_get_size (context->surfaces_pool);
}

/**
 * gst_vaapi_context_reset_on_resize:
 * @context: a #GstVaapiContext
 * @reset_on_resize: Should the context be reset on size change
 *
 * Sets whether the underlying context should be reset when a size change
 * happens. The proper setting for this is codec dependent.
 */
void
gst_vaapi_context_reset_on_resize (GstVaapiContext * context,
    gboolean reset_on_resize)
{
  g_return_if_fail (context != NULL);

  context->reset_on_resize = reset_on_resize;
}

/**
 * gst_vaapi_context_get_surface_formats:
 * @context: a #GstVaapiContext
 *
 * Determines the set of supported formats by the surfaces associated
 * to @context. The caller owns an extra reference of the resulting
 * array of #GstVideoFormat elements, so it shall be released with
 * g_array_unref after usage.
 *
 * Return value: (transfer full): the set of target formats supported
 * by the surfaces in @context.
 */
GArray *
gst_vaapi_context_get_surface_formats (GstVaapiContext * context)
{
  g_return_val_if_fail (context, NULL);

  if (!ensure_attributes (context))
    return NULL;

  if (context->attribs->formats)
    return g_array_ref (context->attribs->formats);
  return NULL;
}

/**
 * gst_vaapi_context_get_surface_attributes:
 * @context: a #GstVaapiContext
 * @out_attribs: an allocated #GstVaapiConfigSurfaceAttributes
 *
 * Copy context's surface restrictions to @out_attribs, EXCEPT the
 * color formats. Use gst_vaapi_context_get_surface_formats() to get
 * them.
 *
 * Returns: %TRUE if the attributes were extracted and copied; %FALSE,
 * otherwise
 **/
gboolean
gst_vaapi_context_get_surface_attributes (GstVaapiContext * context,
    GstVaapiConfigSurfaceAttributes * out_attribs)
{
  g_return_val_if_fail (context, FALSE);

  if (!ensure_attributes (context))
    return FALSE;

  if (out_attribs) {
    out_attribs->min_width = context->attribs->min_width;
    out_attribs->min_height = context->attribs->min_height;
    out_attribs->max_width = context->attribs->max_width;
    out_attribs->max_height = context->attribs->max_height;
    out_attribs->mem_types = context->attribs->mem_types;
    out_attribs->formats = NULL;
  }

  return TRUE;
}

/**
 * gst_vaapi_context_ref:
 * @context: a #GstVaapiContext
 *
 * Atomically increases the reference count of the given @context by one.
 *
 * Returns: The same @context argument
 */
GstVaapiContext *
gst_vaapi_context_ref (GstVaapiContext * context)
{
  g_return_val_if_fail (context != NULL, NULL);

  g_atomic_int_inc (&context->ref_count);

  return context;
}

/**
 * gst_vaapi_context_unref:
 * @context: a #GstVaapiContext
 *
 * Atomically decreases the reference count of the @context by one. If
 * the reference count reaches zero, the object will be free'd.
 */
void
gst_vaapi_context_unref (GstVaapiContext * context)
{
  g_return_if_fail (context != NULL);
  g_return_if_fail (context->ref_count > 0);

  if (g_atomic_int_dec_and_test (&context->ref_count)) {
    context_destroy (context);
    context_destroy_surfaces (context);
    gst_vaapi_display_replace (&context->display, NULL);
    g_free (context);
  }
}
