/*
 *  gstvaapicontext_overlay.c - VA context abstraction (overlay composition)
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

#include "sysdeps.h"
#include "gstvaapicontext_overlay.h"
#include "gstvaapiutils.h"
#include "gstvaapiimage.h"
#include "gstvaapisubpicture.h"

#define DEBUG 1
#include "gstvaapidebug.h"

typedef struct _GstVaapiOverlayRectangle GstVaapiOverlayRectangle;
struct _GstVaapiOverlayRectangle
{
  GstVaapiContext *context;
  GstVaapiSubpicture *subpicture;
  GstVaapiRectangle render_rect;
  guint seq_num;
  guint layer_id;
  GstBuffer *rect_buffer;
  GstVideoOverlayRectangle *rect;
  guint is_associated:1;
};

static inline void
gst_video_overlay_rectangle_replace (GstVideoOverlayRectangle ** old_rect_ptr,
    GstVideoOverlayRectangle * new_rect)
{
  gst_mini_object_replace ((GstMiniObject **) old_rect_ptr,
      GST_MINI_OBJECT_CAST (new_rect));
}

#define overlay_rectangle_ref(overlay) \
    gst_vaapi_mini_object_ref(GST_VAAPI_MINI_OBJECT(overlay))

#define overlay_rectangle_unref(overlay) \
    gst_vaapi_mini_object_unref(GST_VAAPI_MINI_OBJECT(overlay))

#define overlay_rectangle_replace(old_overlay_ptr, new_overlay) \
    gst_vaapi_mini_object_replace((GstVaapiMiniObject **)(old_overlay_ptr), \
        (GstVaapiMiniObject *)(new_overlay))

static void overlay_rectangle_finalize (GstVaapiOverlayRectangle * overlay);

static gboolean
overlay_rectangle_associate (GstVaapiOverlayRectangle * overlay);

static gboolean
overlay_rectangle_deassociate (GstVaapiOverlayRectangle * overlay);

static inline const GstVaapiMiniObjectClass *
overlay_rectangle_class (void)
{
  static const GstVaapiMiniObjectClass GstVaapiOverlayRectangleClass = {
    sizeof (GstVaapiOverlayRectangle),
    (GDestroyNotify) overlay_rectangle_finalize
  };
  return &GstVaapiOverlayRectangleClass;
}

static GstVaapiOverlayRectangle *
overlay_rectangle_new (GstVideoOverlayRectangle * rect,
    GstVaapiContext * context, guint layer_id)
{
  GstVaapiOverlayRectangle *overlay;
  GstVaapiRectangle *render_rect;
  guint width, height, flags;
  gint x, y;

  overlay = (GstVaapiOverlayRectangle *)
      gst_vaapi_mini_object_new0 (overlay_rectangle_class ());
  if (!overlay)
    return NULL;

  overlay->context = context;
  overlay->seq_num = gst_video_overlay_rectangle_get_seqnum (rect);
  overlay->layer_id = layer_id;
  overlay->rect = gst_video_overlay_rectangle_ref (rect);

  flags = gst_video_overlay_rectangle_get_flags (rect);
  gst_buffer_replace (&overlay->rect_buffer,
      gst_video_overlay_rectangle_get_pixels_unscaled_raw (rect, flags));
  if (!overlay->rect_buffer)
    goto error;

  overlay->subpicture =
      gst_vaapi_subpicture_new_from_overlay_rectangle (GST_VAAPI_OBJECT_DISPLAY
      (context), rect);
  if (!overlay->subpicture)
    goto error;

  gst_video_overlay_rectangle_get_render_rectangle (rect,
      &x, &y, &width, &height);
  render_rect = &overlay->render_rect;
  render_rect->x = x;
  render_rect->y = y;
  render_rect->width = width;
  render_rect->height = height;
  return overlay;

  /* ERRORS */
error:
  {
    overlay_rectangle_unref (overlay);
    return NULL;
  }
}

static void
overlay_rectangle_finalize (GstVaapiOverlayRectangle * overlay)
{
  gst_buffer_replace (&overlay->rect_buffer, NULL);
  gst_video_overlay_rectangle_unref (overlay->rect);

  if (overlay->subpicture) {
    overlay_rectangle_deassociate (overlay);
    gst_vaapi_object_unref (overlay->subpicture);
    overlay->subpicture = NULL;
  }
}

static gboolean
overlay_rectangle_associate (GstVaapiOverlayRectangle * overlay)
{
  GstVaapiSubpicture *const subpicture = overlay->subpicture;
  GPtrArray *const surfaces = overlay->context->surfaces;
  guint i, n_associated;

  if (overlay->is_associated)
    return TRUE;

  n_associated = 0;
  for (i = 0; i < surfaces->len; i++) {
    GstVaapiSurface *const surface = g_ptr_array_index (surfaces, i);
    if (gst_vaapi_surface_associate_subpicture (surface, subpicture,
            NULL, &overlay->render_rect))
      n_associated++;
  }

  overlay->is_associated = TRUE;
  return n_associated == surfaces->len;
}

static gboolean
overlay_rectangle_deassociate (GstVaapiOverlayRectangle * overlay)
{
  GstVaapiSubpicture *const subpicture = overlay->subpicture;
  GPtrArray *const surfaces = overlay->context->surfaces;
  guint i, n_associated;

  if (!overlay->is_associated)
    return TRUE;

  n_associated = surfaces->len;
  for (i = 0; i < surfaces->len; i++) {
    GstVaapiSurface *const surface = g_ptr_array_index (surfaces, i);
    if (gst_vaapi_surface_deassociate_subpicture (surface, subpicture))
      n_associated--;
  }

  overlay->is_associated = FALSE;
  return n_associated == 0;
}

static gboolean
overlay_rectangle_changed_pixels (GstVaapiOverlayRectangle * overlay,
    GstVideoOverlayRectangle * rect)
{
  guint flags;
  GstBuffer *buffer;

  if (overlay->seq_num == gst_video_overlay_rectangle_get_seqnum (rect))
    return FALSE;

  flags =
      to_GstVideoOverlayFormatFlags (gst_vaapi_subpicture_get_flags
      (overlay->subpicture));

  buffer = gst_video_overlay_rectangle_get_pixels_unscaled_raw (rect, flags);
  if (!buffer)
    return FALSE;
  {
    const guint n_blocks = gst_buffer_n_memory (buffer);
    gsize ofs;
    guint i;

    if (buffer == overlay->rect_buffer)
      return TRUE;

    if (n_blocks != gst_buffer_n_memory (overlay->rect_buffer))
      return FALSE;

    for (i = 0; i < n_blocks; i++) {
      GstMemory *const mem1 = gst_buffer_peek_memory (buffer, i);
      GstMemory *const mem2 = gst_buffer_peek_memory (overlay->rect_buffer, i);
      if (!gst_memory_is_span (mem1, mem2, &ofs))
        return FALSE;
    }
  }
  return TRUE;
}

static gboolean
overlay_rectangle_changed_render_rect (GstVaapiOverlayRectangle * overlay,
    GstVideoOverlayRectangle * rect)
{
  GstVaapiRectangle *const render_rect = &overlay->render_rect;
  guint width, height;
  gint x, y;

  gst_video_overlay_rectangle_get_render_rectangle (rect,
      &x, &y, &width, &height);

  if (x == render_rect->x &&
      y == render_rect->y &&
      width == render_rect->width && height == render_rect->height)
    return FALSE;

  render_rect->x = x;
  render_rect->y = y;
  render_rect->width = width;
  render_rect->height = height;
  return TRUE;
}

static inline gboolean
overlay_rectangle_update_global_alpha (GstVaapiOverlayRectangle * overlay,
    GstVideoOverlayRectangle * rect)
{
  const guint flags = gst_video_overlay_rectangle_get_flags (rect);
  if (!(flags & GST_VIDEO_OVERLAY_FORMAT_FLAG_GLOBAL_ALPHA))
    return TRUE;
  return gst_vaapi_subpicture_set_global_alpha (overlay->subpicture,
      gst_video_overlay_rectangle_get_global_alpha (rect));
}

static gboolean
overlay_rectangle_update (GstVaapiOverlayRectangle * overlay,
    GstVideoOverlayRectangle * rect, gboolean * reassociate_ptr)
{
  if (overlay_rectangle_changed_pixels (overlay, rect))
    return FALSE;
  if (overlay_rectangle_changed_render_rect (overlay, rect))
    *reassociate_ptr = TRUE;
  if (!overlay_rectangle_update_global_alpha (overlay, rect))
    return FALSE;
  gst_video_overlay_rectangle_replace (&overlay->rect, rect);
  return TRUE;
}

static inline GPtrArray *
overlay_new (void)
{
  return g_ptr_array_new_with_free_func (
      (GDestroyNotify) gst_vaapi_mini_object_unref);
}

static void
overlay_destroy (GPtrArray ** overlay_ptr)
{
  GPtrArray *const overlay = *overlay_ptr;

  if (!overlay)
    return;
  g_ptr_array_unref (overlay);
  *overlay_ptr = NULL;
}

static void
overlay_clear (GPtrArray * overlay)
{
  if (overlay && overlay->len > 0)
    g_ptr_array_remove_range (overlay, 0, overlay->len);
}

static GstVaapiOverlayRectangle *
overlay_lookup (GPtrArray * overlays, GstVideoOverlayRectangle * rect)
{
  guint i;

  for (i = 0; i < overlays->len; i++) {
    GstVaapiOverlayRectangle *const overlay = g_ptr_array_index (overlays, i);

    if (overlay->rect == rect)
      return overlay;
  }
  return NULL;
}

static gboolean
overlay_reassociate (GPtrArray * overlays)
{
  guint i;

  for (i = 0; i < overlays->len; i++)
    overlay_rectangle_deassociate (g_ptr_array_index (overlays, i));

  for (i = 0; i < overlays->len; i++) {
    if (!overlay_rectangle_associate (g_ptr_array_index (overlays, i)))
      return FALSE;
  }
  return TRUE;
}

static gboolean
overlay_ensure (GPtrArray ** overlay_ptr)
{
  GPtrArray *overlay = *overlay_ptr;

  if (!overlay) {
    overlay = overlay_new ();
    if (!overlay)
      return FALSE;
    *overlay_ptr = overlay;
  }
  return TRUE;
}

/** Initializes overlay resources */
gboolean
gst_vaapi_context_overlay_init (GstVaapiContext * context)
{
  if (!overlay_ensure (&context->overlays[0]))
    return FALSE;
  if (!overlay_ensure (&context->overlays[1]))
    return FALSE;
  return TRUE;
}

/** Destroys overlay resources */
void
gst_vaapi_context_overlay_finalize (GstVaapiContext * context)
{
  overlay_destroy (&context->overlays[0]);
  overlay_destroy (&context->overlays[1]);
}

/** Resets overlay resources to a clean state */
gboolean
gst_vaapi_context_overlay_reset (GstVaapiContext * context)
{
  guint num_errors = 0;

  if (overlay_ensure (&context->overlays[0]))
    overlay_clear (context->overlays[0]);
  else
    num_errors++;

  if (overlay_ensure (&context->overlays[1]))
    overlay_clear (context->overlays[1]);
  else
    num_errors++;

  context->overlay_id = 0;
  return num_errors == 0;
}

/**
 * gst_vaapi_context_apply_composition:
 * @context: a #GstVaapiContext
 * @composition: a #GstVideoOverlayComposition
 *
 * Applies video composition planes to all surfaces bound to @context.
 * This helper function resets any additional subpictures the user may
 * have associated himself. A %NULL @composition will also clear all
 * the existing subpictures.
 *
 * Return value: %TRUE if all composition planes could be applied,
 *   %FALSE otherwise
 */
gboolean
gst_vaapi_context_apply_composition (GstVaapiContext * context,
    GstVideoOverlayComposition * composition)
{
  GPtrArray *curr_overlay, *next_overlay;
  guint i, n_rectangles;
  gboolean reassociate = FALSE;

  g_return_val_if_fail (context != NULL, FALSE);

  if (!context->surfaces)
    return FALSE;

  if (!composition) {
    gst_vaapi_context_overlay_reset (context);
    return TRUE;
  }

  curr_overlay = context->overlays[context->overlay_id];
  next_overlay = context->overlays[context->overlay_id ^ 1];
  overlay_clear (next_overlay);

  n_rectangles = gst_video_overlay_composition_n_rectangles (composition);
  for (i = 0; i < n_rectangles; i++) {
    GstVideoOverlayRectangle *const rect =
        gst_video_overlay_composition_get_rectangle (composition, i);
    GstVaapiOverlayRectangle *overlay;

    overlay = overlay_lookup (curr_overlay, rect);
    if (overlay && overlay_rectangle_update (overlay, rect, &reassociate)) {
      overlay_rectangle_ref (overlay);
      if (overlay->layer_id != i)
        reassociate = TRUE;
    } else {
      overlay = overlay_rectangle_new (rect, context, i);
      if (!overlay) {
        GST_WARNING ("could not create VA overlay rectangle");
        goto error;
      }
      reassociate = TRUE;
    }
    g_ptr_array_add (next_overlay, overlay);
  }

  overlay_clear (curr_overlay);
  context->overlay_id ^= 1;

  if (reassociate && !overlay_reassociate (next_overlay))
    return FALSE;
  return TRUE;

  /* ERRORS */
error:
  {
    gst_vaapi_context_overlay_reset (context);
    return FALSE;
  }
}
