/*
 *  gstvaapiblend.c - Video processing blend
 *
 *  Copyright (C) 2019 Intel Corporation
 *    Author: U. Artie Eoff <ullysses.a.eoff@intel.com>
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
#include "gstvaapicompat.h"
#include "gstvaapiblend.h"
#include "gstvaapiutils.h"
#include "gstvaapivalue.h"
#include "gstvaapidisplay_priv.h"
#include "gstvaapisurface_priv.h"

struct _GstVaapiBlend
{
  GstObject parent_instance;

  GstVaapiDisplay *display;

  VAConfigID va_config;
  VAContextID va_context;

  guint32 flags;
};

typedef struct _GstVaapiBlendClass GstVaapiBlendClass;
struct _GstVaapiBlendClass
{
  GstObjectClass parent_class;
};

GST_DEBUG_CATEGORY_STATIC (gst_debug_vaapi_blend);
#ifndef GST_DISABLE_GST_DEBUG
#define GST_CAT_DEFAULT gst_debug_vaapi_blend
#else
#define GST_CAT_DEFAULT NULL
#endif

G_DEFINE_TYPE_WITH_CODE (GstVaapiBlend, gst_vaapi_blend, GST_TYPE_OBJECT,
    GST_DEBUG_CATEGORY_INIT (gst_debug_vaapi_blend, "vaapiblend", 0,
        "VA-API Blend"));

enum
{
  PROP_DISPLAY = 1,
};

static void
gst_vaapi_blend_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVaapiBlend *const blend = GST_VAAPI_BLEND (object);

  switch (property_id) {
    case PROP_DISPLAY:{
      GstVaapiDisplay *display = g_value_get_object (value);;
      if (display) {
        if (GST_VAAPI_DISPLAY_HAS_VPP (display)) {
          blend->display = gst_object_ref (display);
        } else {
          GST_WARNING_OBJECT (blend, "GstVaapiDisplay doesn't support VPP");
        }
      }
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
gst_vaapi_blend_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstVaapiBlend *const blend = GST_VAAPI_BLEND (object);

  switch (property_id) {
    case PROP_DISPLAY:
      g_value_set_object (value, blend->display);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
gst_vaapi_blend_finalize (GObject * object)
{
  GstVaapiBlend *const blend = GST_VAAPI_BLEND (object);

  GST_VAAPI_DISPLAY_LOCK (blend->display);

  if (blend->va_context != VA_INVALID_ID) {
    vaDestroyContext (GST_VAAPI_DISPLAY_VADISPLAY (blend->display),
        blend->va_context);
    blend->va_context = VA_INVALID_ID;
  }

  if (blend->va_config != VA_INVALID_ID) {
    vaDestroyConfig (GST_VAAPI_DISPLAY_VADISPLAY (blend->display),
        blend->va_config);
    blend->va_config = VA_INVALID_ID;
  }

  GST_VAAPI_DISPLAY_UNLOCK (blend->display);

  gst_vaapi_display_replace (&blend->display, NULL);

  G_OBJECT_CLASS (gst_vaapi_blend_parent_class)->finalize (object);
}

static void
gst_vaapi_blend_class_init (GstVaapiBlendClass * klass)
{
  GObjectClass *const object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = gst_vaapi_blend_set_property;
  object_class->get_property = gst_vaapi_blend_get_property;
  object_class->finalize = gst_vaapi_blend_finalize;

  g_object_class_install_property (object_class, PROP_DISPLAY,
      g_param_spec_object ("display", "Gst VA-API Display",
          "The VA-API display object to use", GST_TYPE_VAAPI_DISPLAY,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_NAME));
}

static void
gst_vaapi_blend_init (GstVaapiBlend * blend)
{
  blend->display = NULL;
  blend->va_config = VA_INVALID_ID;
  blend->va_context = VA_INVALID_ID;
  blend->flags = 0;
}

static gboolean
gst_vaapi_blend_initialize (GstVaapiBlend * blend)
{
  VAStatus status;
  VAProcPipelineCaps pipeline_caps = { 0, };

  if (!blend->display)
    return FALSE;

  status = vaCreateConfig (GST_VAAPI_DISPLAY_VADISPLAY (blend->display),
      VAProfileNone, VAEntrypointVideoProc, NULL, 0, &blend->va_config);
  if (!vaapi_check_status (status, "vaCreateConfig() [VPP]"))
    return FALSE;

  status = vaCreateContext (GST_VAAPI_DISPLAY_VADISPLAY (blend->display),
      blend->va_config, 0, 0, 0, NULL, 0, &blend->va_context);
  if (!vaapi_check_status (status, "vaCreateContext() [VPP]"))
    return FALSE;

#if VA_CHECK_VERSION(1,1,0)
  status =
      vaQueryVideoProcPipelineCaps (GST_VAAPI_DISPLAY_VADISPLAY
      (blend->display), blend->va_context, NULL, 0, &pipeline_caps);
  if (vaapi_check_status (status, "vaQueryVideoProcPipelineCaps()"))
    blend->flags = pipeline_caps.blend_flags;
#endif

  if (!(blend->flags & VA_BLEND_GLOBAL_ALPHA)) {
    GST_WARNING_OBJECT (blend, "VPP does not support global alpha blending");
    return FALSE;
  }

  return TRUE;
}

GstVaapiBlend *
gst_vaapi_blend_new (GstVaapiDisplay * display)
{
  GstVaapiBlend *blend = g_object_new (GST_TYPE_VAAPI_BLEND,
      "display", display, NULL);

  if (!gst_vaapi_blend_initialize (blend)) {
    gst_object_unref (blend);
    blend = NULL;
  }

  return blend;
}

void
gst_vaapi_blend_replace (GstVaapiBlend ** old_blend_ptr,
    GstVaapiBlend * new_blend)
{
  g_return_if_fail (old_blend_ptr != NULL);

  gst_object_replace ((GstObject **) old_blend_ptr, GST_OBJECT (new_blend));
}

/**
 * gst_vaapi_blend_process_begin:
 * @blend: a #GstVaapiBlend
 * @surface: the  #GstVaapiSurface output target
 *
 * Locks the VA display and prepares the VA processing pipeline for rendering.
 *
 * If this function fails, then the VA display is unlocked before returning.
 *
 * If this function succeeds, it must be paired with a call to
 * #gst_vaapi_blend_process_end to ensure the VA processing pipeline is
 * finalized and the VA display is unlocked.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 */
gboolean
gst_vaapi_blend_process_begin (GstVaapiBlend * blend, GstVaapiSurface * surface)
{
  VAStatus va_status;

  g_return_val_if_fail (blend != NULL, FALSE);
  g_return_val_if_fail (surface != NULL, FALSE);

  GST_VAAPI_DISPLAY_LOCK (blend->display);

  va_status = vaBeginPicture (GST_VAAPI_DISPLAY_VADISPLAY (blend->display),
      blend->va_context, GST_VAAPI_SURFACE_ID (surface));

  if (!vaapi_check_status (va_status, "vaBeginPicture()")) {
    GST_VAAPI_DISPLAY_UNLOCK (blend->display);
    return FALSE;
  }

  return TRUE;
}

/**
 * gst_vaapi_blend_process_render:
 * @blend: a #GstVaapiBlend
 * @surface: the source #GstVaapiSurface to send to the VA processing pipeline
 * for rendering.
 * @crop_rect: the @surface crop extents to process for rendering.
 * @target_rect: the extents for which the @surface is rendered within the
 * output surface.
 * @alpha: the alpha value in the range 0.0 to 1.0, inclusive, for blending
 * with the output surface background or with previously rendered surfaces.
 *
 * Renders the @surface in the currently active VA processing pipeline.
 *
 * This function must only be called after a successful call to
 * #gst_vaapi_blend_process_begin and before calling
 * #gst_vaapi_process_end.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 */
gboolean
gst_vaapi_blend_process_render (GstVaapiBlend * blend,
    const GstVaapiSurface * surface, const GstVaapiRectangle * crop_rect,
    const GstVaapiRectangle * target_rect, gdouble alpha)
{
  VADisplay va_display;
  VAStatus va_status;
  VAProcPipelineParameterBuffer *param = NULL;
  VABufferID id = VA_INVALID_ID;
  VARectangle src_rect = { 0, }, dst_rect = {
  0,};
#if VA_CHECK_VERSION(1,1,0)
  VABlendState blend_state;
#endif

  g_return_val_if_fail (blend != NULL, FALSE);
  g_return_val_if_fail (surface != NULL, FALSE);

  va_display = GST_VAAPI_DISPLAY_VADISPLAY (blend->display);

  /* Build surface region (source) */
  src_rect.width = GST_VAAPI_SURFACE_WIDTH (surface);
  src_rect.height = GST_VAAPI_SURFACE_HEIGHT (surface);
  if (crop_rect) {
    if ((crop_rect->x + crop_rect->width > src_rect.width) ||
        (crop_rect->y + crop_rect->height > src_rect.height))
      return FALSE;

    src_rect.x = crop_rect->x;
    src_rect.y = crop_rect->y;
    src_rect.width = crop_rect->width;
    src_rect.height = crop_rect->height;
  }

  /* Build output region (target) */
  dst_rect.width = src_rect.width;
  dst_rect.height = src_rect.height;
  if (target_rect) {
    dst_rect.x = target_rect->x;
    dst_rect.y = target_rect->y;
    dst_rect.width = target_rect->width;
    dst_rect.height = target_rect->height;
  }

  if (!vaapi_create_buffer (va_display, blend->va_context,
          VAProcPipelineParameterBufferType, sizeof (*param), NULL, &id,
          (gpointer *) & param))
    return FALSE;

  memset (param, 0, sizeof (*param));

  param->surface = GST_VAAPI_SURFACE_ID (surface);
  param->surface_region = &src_rect;
  param->output_region = &dst_rect;
  param->output_background_color = 0xff000000;

#if VA_CHECK_VERSION(1,1,0)
  blend_state.flags = VA_BLEND_GLOBAL_ALPHA;
  blend_state.global_alpha = alpha;
  param->blend_state = &blend_state;
#endif

  vaapi_unmap_buffer (va_display, id, NULL);

  va_status = vaRenderPicture (va_display, blend->va_context, &id, 1);
  if (!vaapi_check_status (va_status, "vaRenderPicture()")) {
    vaapi_destroy_buffer (va_display, &id);
    return FALSE;
  }

  vaapi_destroy_buffer (va_display, &id);

  return TRUE;
}

/**
 * gst_vaapi_blend_process_end:
 * @blend: a #GstVaapiBlend
 *
 * Finalizes all pending render operations in the active VA processing pipeline
 * and unlocks the VA display.
 *
 * This function must always be paired with a call to
 * #gst_vaapi_blend_process_begin to ensure the VA display gets unlocked.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 */
gboolean
gst_vaapi_blend_process_end (GstVaapiBlend * blend)
{
  VAStatus va_status;

  g_return_val_if_fail (blend != NULL, FALSE);

  va_status = vaEndPicture (GST_VAAPI_DISPLAY_VADISPLAY (blend->display),
      blend->va_context);

  GST_VAAPI_DISPLAY_UNLOCK (blend->display);

  if (!vaapi_check_status (va_status, "vaEndPicture()"))
    return FALSE;

  return TRUE;
}
