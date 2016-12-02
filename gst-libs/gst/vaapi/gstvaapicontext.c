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
#include "gstvaapicontext_overlay.h"
#include "gstvaapidisplay_priv.h"
#include "gstvaapiobject_priv.h"
#include "gstvaapisurface.h"
#include "gstvaapisurface_priv.h"
#include "gstvaapisurfacepool.h"
#include "gstvaapisurfaceproxy.h"
#include "gstvaapivideopool_priv.h"
#include "gstvaapiutils.h"
#include "gstvaapiutils_core.h"

#define DEBUG 1
#include "gstvaapidebug.h"

/* Define default VA surface chroma format to YUV 4:2:0 */
#define DEFAULT_CHROMA_TYPE (GST_VAAPI_CHROMA_TYPE_YUV420)

/* Number of scratch surfaces beyond those used as reference */
#define SCRATCH_SURFACES_COUNT (4)

static gboolean
ensure_formats (GstVaapiContext * context)
{
  if (G_LIKELY (context->formats))
    return TRUE;

  context->formats =
      gst_vaapi_get_surface_formats (GST_VAAPI_OBJECT_DISPLAY (context),
      context->va_config);
  return (context->formats != NULL);
}

static void
unref_surface_cb (GstVaapiSurface * surface)
{
  gst_vaapi_surface_set_parent_context (surface, NULL);
  gst_vaapi_object_unref (surface);
}

static inline gboolean
context_get_attribute (GstVaapiContext * context, VAConfigAttribType type,
    guint * out_value_ptr)
{
  return gst_vaapi_get_config_attribute (GST_VAAPI_OBJECT_DISPLAY (context),
      context->va_profile, context->va_entrypoint, type, out_value_ptr);
}

static void
context_destroy_surfaces (GstVaapiContext * context)
{
  gst_vaapi_context_overlay_reset (context);

  if (context->surfaces) {
    g_ptr_array_unref (context->surfaces);
    context->surfaces = NULL;
  }
  gst_vaapi_video_pool_replace (&context->surfaces_pool, NULL);
}

static void
context_destroy (GstVaapiContext * context)
{
  GstVaapiDisplay *const display = GST_VAAPI_OBJECT_DISPLAY (context);
  VAContextID context_id;
  VAStatus status;

  context_id = GST_VAAPI_OBJECT_ID (context);
  GST_DEBUG ("context 0x%08x", context_id);

  if (context_id != VA_INVALID_ID) {
    GST_VAAPI_DISPLAY_LOCK (display);
    status = vaDestroyContext (GST_VAAPI_DISPLAY_VADISPLAY (display),
        context_id);
    GST_VAAPI_DISPLAY_UNLOCK (display);
    if (!vaapi_check_status (status, "vaDestroyContext()"))
      GST_WARNING ("failed to destroy context 0x%08x", context_id);
    GST_VAAPI_OBJECT_ID (context) = VA_INVALID_ID;
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

  if (context->formats) {
    g_array_unref (context->formats);
    context->formats = NULL;
  }
}

static gboolean
context_ensure_surfaces (GstVaapiContext * context)
{
  const GstVaapiContextInfo *const cip = &context->info;
  const guint num_surfaces = cip->ref_frames + SCRATCH_SURFACES_COUNT;
  GstVaapiSurface *surface;
  guint i;

  for (i = context->surfaces->len; i < num_surfaces; i++) {
    surface = gst_vaapi_surface_new (GST_VAAPI_OBJECT_DISPLAY (context),
        cip->chroma_type, cip->width, cip->height);
    if (!surface)
      return FALSE;
    gst_vaapi_surface_set_parent_context (surface, context);
    g_ptr_array_add (context->surfaces, surface);
    if (!gst_vaapi_video_pool_add_object (context->surfaces_pool, surface))
      return FALSE;
  }
  gst_vaapi_video_pool_set_capacity (context->surfaces_pool, num_surfaces);
  return TRUE;
}

static gboolean
context_create_surfaces (GstVaapiContext * context)
{
  const GstVaapiContextInfo *const cip = &context->info;
  GstVaapiDisplay *const display = GST_VAAPI_OBJECT_DISPLAY (context);
  guint num_surfaces;

  if (!gst_vaapi_context_overlay_reset (context))
    return FALSE;

  num_surfaces = cip->ref_frames + SCRATCH_SURFACES_COUNT;
  if (!context->surfaces) {
    context->surfaces = g_ptr_array_new_full (num_surfaces,
        (GDestroyNotify) unref_surface_cb);
    if (!context->surfaces)
      return FALSE;
  }

  if (!context->surfaces_pool) {
    context->surfaces_pool =
        gst_vaapi_surface_pool_new_with_chroma_type (display, cip->chroma_type,
        cip->width, cip->height);

    if (!context->surfaces_pool)
      return FALSE;
  }
  return context_ensure_surfaces (context);
}

static gboolean
context_create (GstVaapiContext * context)
{
  const GstVaapiContextInfo *const cip = &context->info;
  GstVaapiDisplay *const display = GST_VAAPI_OBJECT_DISPLAY (context);
  VAContextID context_id;
  VASurfaceID surface_id;
  VAStatus status;
  GArray *surfaces = NULL;
  gboolean success = FALSE;
  guint i;

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
    surface_id = GST_VAAPI_OBJECT_ID (surface);
    g_array_append_val (surfaces, surface_id);
  }
  g_assert (surfaces->len == context->surfaces->len);

  GST_VAAPI_DISPLAY_LOCK (display);
  status = vaCreateContext (GST_VAAPI_DISPLAY_VADISPLAY (display),
      context->va_config, cip->width, cip->height, VA_PROGRESSIVE,
      (VASurfaceID *) surfaces->data, surfaces->len, &context_id);
  GST_VAAPI_DISPLAY_UNLOCK (display);
  if (!vaapi_check_status (status, "vaCreateContext()"))
    goto cleanup;

  GST_DEBUG ("context 0x%08x", context_id);
  GST_VAAPI_OBJECT_ID (context) = context_id;
  success = TRUE;

cleanup:
  if (surfaces)
    g_array_free (surfaces, TRUE);
  return success;
}

static gboolean
config_create (GstVaapiContext * context)
{
  const GstVaapiContextInfo *const cip = &context->info;
  GstVaapiDisplay *const display = GST_VAAPI_OBJECT_DISPLAY (context);
  VAConfigAttrib attribs[3], *attrib = attribs;
  VAStatus status;
  guint value, va_chroma_format;

  /* Reset profile and entrypoint */
  if (!cip->profile || !cip->entrypoint)
    goto cleanup;
  context->va_profile = gst_vaapi_profile_get_va_profile (cip->profile);
  context->va_entrypoint =
      gst_vaapi_entrypoint_get_va_entrypoint (cip->entrypoint);

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
  attrib->value = va_chroma_format;
  attrib++;

  switch (cip->usage) {
#if USE_ENCODERS
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
        attrib++;
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
        attrib++;
      }
#if VA_CHECK_VERSION(0,37,0)
      if (cip->profile == GST_VAAPI_PROFILE_JPEG_BASELINE) {
        attrib->type = VAConfigAttribEncJPEG;
        if (!context_get_attribute (context, attrib->type, &value))
          goto cleanup;
        attrib->value = value;
        attrib++;
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
      context->va_profile, context->va_entrypoint, attribs, attrib - attribs,
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
  gst_vaapi_context_overlay_init (context);

  context->formats = NULL;
}

static void
gst_vaapi_context_finalize (GstVaapiContext * context)
{
  context_destroy (context);
  context_destroy_surfaces (context);
  gst_vaapi_context_overlay_finalize (context);
}

GST_VAAPI_OBJECT_DEFINE_CLASS (GstVaapiContext, gst_vaapi_context);

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

  g_return_val_if_fail (cip->profile, NULL);
  g_return_val_if_fail (cip->entrypoint, NULL);

  context = gst_vaapi_object_new (gst_vaapi_context_class (), display);
  if (!context)
    return NULL;

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
  return context;

  /* ERRORS */
error:
  {
    gst_vaapi_object_unref (context);
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

  if (reset_surfaces && !context_create_surfaces (context))
    return FALSE;
  else if (grow_surfaces && !context_ensure_surfaces (context))
    return FALSE;
  if (reset_config && !(config_create (context) && context_create (context)))
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

  return GST_VAAPI_OBJECT_ID (context);
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

  if (!ensure_formats (context))
    return NULL;
  return g_array_ref (context->formats);
}
