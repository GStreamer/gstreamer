/* GStreamer
 * Copyright (C) 2025 Collabora Ltd.
 *   @author: Jakub Adam <jakub.adam@collabora.com>
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
 * SECTION:element-vaoverlaycompositor
 * @title: vaoverlaycompositor
 *
 * `vaoverlaycompositor` overlays upstream `GstVideoOverlayCompositionMeta`
 * onto the video stream.
 *
 * Since: 1.30
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvaoverlaycompositor.h"

#include <gst/va/vasurfaceimage.h>
#include <gst/va/gstvavideoformat.h>

#include "gstvabase.h"
#include "gstvabasetransform.h"
#include "gstvapluginutils.h"

GST_DEBUG_CATEGORY_STATIC (gst_va_overlay_compositor_debug);
#define GST_CAT_DEFAULT gst_va_overlay_compositor_debug

struct CData
{
  gchar *render_device_path;
  gchar *description;
};

typedef struct
{
  GstBufferPool *pool;
  GstVideoInfo info;
} OverlayPool;

static void
_overlay_pool_free (OverlayPool * overlay_pool)
{
  gst_buffer_pool_set_active (overlay_pool->pool, FALSE);
  gst_clear_object (&overlay_pool->pool);
  g_clear_pointer (&overlay_pool, g_free);
}

/* To import an overlay rectangle into VA, the element needs a buffer pool that
 * allocates memory of the corresponding size. Since overlay composition meta
 * can include rectangles of various dimensions, new pools are created as needed
 * and kept in a list for reuse. The size of the list is limited by this value.
 * (The least used pool is freed to make space for a new one.) */
static const guint MAX_OVERLAY_POOLS = 10;

#define GST_VA_OVERLAY_COMPOSITOR(obj) ((GstVaOverlayCompositor *) obj)
#define GST_VA_OVERLAY_COMPOSITOR_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS ((obj), G_TYPE_FROM_INSTANCE (obj), GstVaOverlayCompositorClass))

typedef struct
{
  GstVaBaseTransformClass parent_class;
} GstVaOverlayCompositorClass;

typedef struct
{
  GstVaBaseTransform parent;

  GSList *pools;
} GstVaOverlayCompositor;

static gpointer parent_class = NULL;

static gboolean
_drm_format_from_format (const GValue * val, GValue * dst)
{
  GstVideoFormat gst_format;

  gst_format = gst_video_format_from_string (g_value_get_string (val));
  if (gst_format != GST_VIDEO_FORMAT_UNKNOWN) {
    guint32 fourcc = gst_video_dma_drm_fourcc_from_format (gst_format);
    if (fourcc != DRM_FORMAT_INVALID) {
      g_value_init (dst, G_TYPE_STRING);
      g_value_take_string (dst, gst_video_dma_drm_fourcc_to_string (fourcc, 0));
      return TRUE;
    }
  }

  return FALSE;
}

static gboolean
_drm_format_to_format (const GValue * val, GValue * dst)
{
  guint32 fourcc;
  guint64 modifier;

  fourcc =
      gst_video_dma_drm_fourcc_from_string (g_value_get_string (val),
      &modifier);
  if (fourcc != DRM_FORMAT_INVALID) {
    GstVideoFormat format =
        gst_video_dma_drm_format_to_gst_format (fourcc, modifier);

    if (format != GST_VIDEO_FORMAT_UNKNOWN) {
      g_value_init (dst, G_TYPE_STRING);
      g_value_set_string (dst, gst_video_format_to_string (format));
      return TRUE;
    }
  }

  return FALSE;
}

static GstStructure *
_convert_to_dma_drm (const GstStructure * structure)
{
  GstStructure *s = NULL;
  const GValue *val = gst_structure_get_value (structure, "format");
  GValue drm_format_val = G_VALUE_INIT;

  if (G_VALUE_HOLDS_STRING (val)) {
    _drm_format_from_format (val, &drm_format_val);
  } else if (GST_VALUE_HOLDS_LIST (val)) {
    guint fmt_cnt = gst_value_list_get_size (val);

    gst_value_list_init (&drm_format_val, fmt_cnt);

    for (guint j = 0; j < fmt_cnt; j++) {
      GValue item = G_VALUE_INIT;
      if (_drm_format_from_format (gst_value_list_get_value (val, j), &item)) {
        gst_value_list_append_and_take_value (&drm_format_val, &item);
      }
    }
  }

  if (G_VALUE_TYPE (&drm_format_val) != G_TYPE_INVALID) {
    s = gst_structure_copy (structure);
    gst_structure_take_value (s, "drm-format", &drm_format_val);
    gst_structure_set (s, "format", G_TYPE_STRING,
        gst_video_format_to_string (GST_VIDEO_FORMAT_DMA_DRM), NULL);
  }

  return s;
}

static GstStructure *
_convert_from_dma_drm (const GstStructure * structure)
{
  GstStructure *s = NULL;
  const GValue *val = gst_structure_get_value (structure, "drm-format");
  GValue format_val = G_VALUE_INIT;

  if (G_VALUE_HOLDS_STRING (val)) {
    _drm_format_to_format (val, &format_val);
  } else if (GST_VALUE_HOLDS_LIST (val)) {
    guint fmt_cnt = gst_value_list_get_size (val);

    gst_value_list_init (&format_val, fmt_cnt);

    for (guint j = 0; j < fmt_cnt; j++) {
      GValue item = G_VALUE_INIT;
      if (_drm_format_to_format (gst_value_list_get_value (val, j), &item)) {
        gst_value_list_append_and_take_value (&format_val, &item);
      }
    }
  }

  if (G_VALUE_TYPE (&format_val) != G_TYPE_INVALID) {
    s = gst_structure_copy (structure);
    gst_structure_take_value (s, "format", &format_val);
    gst_structure_remove_field (s, "drm-format");
  }

  return s;
}

/* Returns all structures in @caps without @feature_name but now with
 * @feature_name */
static GstCaps *
_complete_caps_features (const GstCaps * caps, const gchar * feature_name)
{
  guint i, n;
  GstCaps *tmp;

  tmp = gst_caps_new_empty ();

  n = gst_caps_get_size (caps);
  for (i = 0; i < n; i++) {
    GstCapsFeatures *features, *orig_features;
    guint num_orig_features;
    GstStructure *s;
    guint j;

    s = gst_caps_get_structure (caps, i);
    orig_features = gst_caps_get_features (caps, i);

    if (gst_caps_features_contains (orig_features, feature_name)) {
      gst_caps_append_structure_full (tmp, gst_structure_copy (s),
          gst_caps_features_copy (orig_features));
      continue;
    }

    num_orig_features = gst_caps_features_get_size (orig_features);

    features = gst_caps_features_new_single_static_str (feature_name);
    for (j = 0; j != num_orig_features; ++j) {
      const GstIdStr *fstr =
          gst_caps_features_get_nth_id_str (orig_features, j);
      if (!g_str_has_prefix (gst_id_str_as_str (fstr), "memory:")) {
        gst_caps_features_add_id_str (features, fstr);
      }
    }

    if (!gst_caps_is_subset_structure_full (tmp, s, features)) {
      if (g_str_equal (feature_name, GST_CAPS_FEATURE_MEMORY_DMABUF)) {
        s = _convert_to_dma_drm (s);
      } else if (gst_caps_features_contains (orig_features,
              GST_CAPS_FEATURE_MEMORY_DMABUF)) {
        s = _convert_from_dma_drm (s);
      } else {
        s = gst_structure_copy (s);
      }
      if (s) {
        gst_caps_append_structure_full (tmp, s, g_steal_pointer (&features));
      }
    }
    g_clear_pointer (&features, gst_caps_features_free);
  }

  return tmp;
}

static GstCaps *
gst_va_overlay_compositor_transform_caps (GstBaseTransform * bt,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstCaps *ret = NULL;
  guint i;

  static const gchar *caps_features[] = { GST_CAPS_FEATURE_MEMORY_VA,
    GST_CAPS_FEATURE_MEMORY_DMABUF, GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY
  };

  if (direction == GST_PAD_SRC) {
    GstCaps *composition_caps;
    gint i;

    composition_caps = gst_caps_copy (caps);

    for (i = 0; i < gst_caps_get_size (composition_caps); i++) {
      GstCapsFeatures *f = gst_caps_get_features (composition_caps, i);
      if (!gst_caps_features_is_any (f)) {
        gst_caps_features_add (f,
            GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION);
      }
    }

    ret = gst_caps_merge (composition_caps, gst_caps_copy (caps));
  } else {
    GstCaps *removed;

    ret = gst_caps_copy (caps);
    removed = gst_caps_copy (caps);
    for (i = 0; i < gst_caps_get_size (removed); i++) {
      GstCapsFeatures *feat = gst_caps_get_features (removed, i);

      if (feat && gst_caps_features_contains (feat,
              GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION)) {
        feat = gst_caps_features_copy (feat);
        gst_caps_features_remove (feat,
            GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION);
        gst_caps_set_features (removed, i, feat);
      }
    }

    ret = gst_caps_merge (ret, removed);
  }

  for (i = 0; i < G_N_ELEMENTS (caps_features); i++) {
    GstCaps *tmp;

    tmp = _complete_caps_features (ret, caps_features[i]);
    if (!gst_caps_is_subset (tmp, ret)) {
      gst_caps_append (ret, tmp);
    } else {
      gst_caps_unref (tmp);
    }
  }

  if (filter) {
    GstCaps *intersection;

    intersection =
        gst_caps_intersect_full (filter, ret, GST_CAPS_INTERSECT_FIRST);
    gst_caps_replace (&ret, intersection);
    gst_clear_caps (&intersection);
  }

  return ret;
}

static gboolean
gst_va_overlay_compositor_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_query, GstQuery * query)
{
  gst_query_add_allocation_meta (query,
      GST_VIDEO_OVERLAY_COMPOSITION_META_API_TYPE, NULL);

  return GST_BASE_TRANSFORM_CLASS (parent_class)->propose_allocation (trans,
      decide_query, query);
}

static gboolean
gst_va_overlay_compositor_set_info (GstVaBaseTransform * bt, GstCaps * incaps,
    GstVideoInfo * in_info, GstCaps * outcaps, GstVideoInfo * out_info)
{
  GstVaOverlayCompositor *self = GST_VA_OVERLAY_COMPOSITOR (bt);
  GstCapsFeatures *in_features, *out_features;

  GST_DEBUG_OBJECT (bt, " incaps %" GST_PTR_FORMAT, incaps);
  GST_DEBUG_OBJECT (bt, "outcaps %" GST_PTR_FORMAT, outcaps);

  in_features = gst_caps_get_features (incaps, 0);
  out_features = gst_caps_get_features (outcaps, 0);

  if (gst_caps_features_contains (in_features,
          GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION) &&
      !gst_caps_features_contains (out_features,
          GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION)) {
    GST_INFO_OBJECT (bt, "caps say to render GstVideoOverlayCompositionMeta");
    gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (self), FALSE);
  } else {
    GST_INFO_OBJECT (bt,
        "caps say to not render GstVideoOverlayCompositionMeta");
    gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (self), TRUE);
  }

  return TRUE;
}

static OverlayPool *
gst_va_overlay_compositor_create_pool (GstVaOverlayCompositor * self,
    GstVideoInfo * info)
{
  GstVaBaseTransform *vabtrans = GST_VA_BASE_TRANSFORM (self);

  OverlayPool *result = NULL;
  GstCaps *caps = NULL;
  GstBufferPool *vapool;
  GstAllocator *allocator = NULL;
  GstAllocationParams params = { 0, };
  guint usage_hint;

  caps = gst_video_info_to_caps (info);

  if (!gst_va_base_convert_caps_to_va (caps)) {
    GST_ERROR_OBJECT (self, "Invalid caps %" GST_PTR_FORMAT, caps);
    goto out;
  }

  usage_hint = va_get_surface_usage_hint (vabtrans->display,
      VAEntrypointVideoProc, GST_PAD_SINK, FALSE);
  gst_allocation_params_init (&params);

  allocator = gst_va_base_transform_allocator_from_caps (vabtrans, caps);

  vapool = gst_va_pool_new_with_config (caps, 1, 0, usage_hint,
      GST_VA_FEATURE_AUTO, allocator, &params);
  if (!vapool) {
    goto out;
  }

  if (gst_buffer_pool_set_active (vapool, TRUE)) {
    result = g_new0 (OverlayPool, 1);
    result->pool = vapool;
    gst_va_allocator_get_format (allocator, &result->info, NULL, NULL);
  } else {
    GST_WARNING_OBJECT (self, "failed to activate pool %" GST_PTR_FORMAT,
        vapool);
    gst_clear_object (&vapool);
  }

out:
  gst_clear_caps (&caps);
  gst_clear_object (&allocator);

  return result;
}

static OverlayPool *
gst_va_overlay_compositor_get_pool_by_info (GstVaOverlayCompositor * self,
    GstVideoInfo * info)
{
  OverlayPool *result = NULL;
  GSList *it;

  for (it = self->pools; !result && it; it = g_slist_next (it)) {
    OverlayPool *pool = it->data;

    if (GST_VIDEO_INFO_WIDTH (info) == GST_VIDEO_INFO_WIDTH (&pool->info) &&
        GST_VIDEO_INFO_HEIGHT (info) == GST_VIDEO_INFO_HEIGHT (&pool->info)) {
      result = pool;

      /* Remove the pool from the list. It will later get added to the front as
       * the most recently used item. */
      self->pools = g_slist_delete_link (self->pools, it);
      break;
    }
  }

  if (!result) {
    result = gst_va_overlay_compositor_create_pool (self, info);
  }

  if (result) {
    self->pools = g_slist_prepend (self->pools, result);
  }

  return result;
}

typedef struct
{
  GstVaOverlayCompositor *compositor;
  GstBuffer *inbuf;
  gpointer state;
  GstVideoOverlayCompositionMeta *ometa;
  guint rect;
  GstVaComposeSample sample;
  gboolean inbuf_sent;
} GstVaOverlayCompositorSampleGenerator;

static GstBufferPool *
_get_pool (GstElement * element, gpointer data)
{
  GstVaOverlayCompositor *self = GST_VA_OVERLAY_COMPOSITOR (element);

  GstVaBufferImporter *importer = data;
  OverlayPool *pool = NULL;

  pool = gst_va_overlay_compositor_get_pool_by_info (self, importer->in_info);
  if (pool) {
    *importer->sinkpad_info = pool->info;
    return pool->pool;
  }

  return NULL;
}

static GstFlowReturn
gst_va_overlay_compositor_import_rectangle (GstVaOverlayCompositor * self,
    GstVideoOverlayRectangle * rect, GstBuffer ** outbuf,
    guint16 * width, guint16 * height)
{
  GstVaBaseTransform *vabtrans = GST_VA_BASE_TRANSFORM (self);

  GstBuffer *inbuf;
  GstVideoMeta *vmeta;
  GstVideoInfo in_info;
  GstVideoInfo out_info;

  /* Already hold GST_OBJECT_LOCK */
  GstVaBufferImporter importer = {
    .element = GST_ELEMENT_CAST (self),
#ifndef GST_DISABLE_GST_DEBUG
    .debug_category = GST_CAT_DEFAULT,
#endif
    .display = vabtrans->display,
    .entrypoint = VAEntrypointVideoProc,
    .get_sinkpad_pool = _get_pool,
    .in_info = &in_info,
    .sinkpad_info = &out_info,  /* Gets filled in _get_pool(). */
  };
  importer.pool_data = &importer;

  inbuf = gst_video_overlay_rectangle_get_pixels_unscaled_argb (rect,
      GST_VIDEO_OVERLAY_FORMAT_FLAG_NONE);
  vmeta = gst_buffer_get_video_meta (inbuf);
  gst_video_info_set_format (&in_info, vmeta->format, vmeta->width,
      vmeta->height);
  for (gint i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
    in_info.stride[i] = vmeta->stride[i];
  }

  *width = vmeta->width;
  *height = vmeta->height;

  return gst_va_buffer_importer_import (&importer, inbuf, outbuf);
}

/* When first called from gst_va_filter_compose(), _sample_next() generates the
 * sample for the whole input frame; subsequent calls will generate samples
 * for each overlay meta rectangle. */
static GstVaComposeSample *
_sample_next (gpointer data)
{
  GstVaOverlayCompositorSampleGenerator *gen = data;
  GstVaBaseTransform *vabasetrans = GST_VA_BASE_TRANSFORM (gen->compositor);
  GstVideoOverlayRectangle *rectangle = NULL;
  GstBuffer *buf = NULL;
  GstVideoRectangle render_rect;

  if (!gen->inbuf_sent) {
    /* First time the generator got called, return the input frame (background
     * for the composition). */
    /* *INDENT-OFF* */
    gen->sample = (GstVaComposeSample) {
      .buffer = gst_buffer_ref (gen->inbuf),
      .input_region = (VARectangle) {
        .x = 0,
        .y = 0,
        .width = GST_VIDEO_INFO_WIDTH (&vabasetrans->in_info),
        .height = GST_VIDEO_INFO_HEIGHT (&vabasetrans->in_info),
      },
      .output_region = (VARectangle) {
        .x = 0,
        .y = 0,
        .width = GST_VIDEO_INFO_WIDTH (&vabasetrans->in_info),
        .height = GST_VIDEO_INFO_HEIGHT (&vabasetrans->in_info),
      },
      .alpha = 1.0,
    };
    /* *INDENT-ON* */

    gen->inbuf_sent = TRUE;

    return &gen->sample;
  }

  /* Find the next rectangle to output. */
  while (!rectangle) {
    GstFlowReturn ret;

    if (!gen->ometa) {
      /* Retrieve the next composition meta attached to the buffer. */
      GstMeta *meta = gst_buffer_iterate_meta_filtered (gen->inbuf, &gen->state,
          GST_VIDEO_OVERLAY_COMPOSITION_META_API_TYPE);

      if (!meta) {
        /* No more metas, we're done. */
        return NULL;
      }

      gen->ometa = (GstVideoOverlayCompositionMeta *) meta;
      gen->rect = 0;
    }

    rectangle =
        gst_video_overlay_composition_get_rectangle (gen->ometa->overlay,
        gen->rect);
    if (!rectangle) {
      /* No more rectangles, move to the next composition meta. */
      gen->ometa = NULL;
      continue;
    }

    ret = gst_va_overlay_compositor_import_rectangle (gen->compositor,
        rectangle, &buf, &gen->sample.input_region.width,
        &gen->sample.input_region.height);
    if (ret != GST_FLOW_OK) {
      GST_WARNING_OBJECT (gen->compositor, "Failed to import composition "
          "rectangle %d from meta %" GST_PTR_FORMAT, gen->rect, gen->ometa);
      rectangle = NULL;
    }

    gen->rect++;
  }

  gst_video_overlay_rectangle_get_render_rectangle (rectangle,
      &render_rect.x, &render_rect.y,
      (guint *) & render_rect.w, (guint *) & render_rect.h);

  gen->sample.buffer = buf;
  gen->sample.input_region.x = 0;
  gen->sample.input_region.y = 0;
  /* *INDENT-OFF* */
  gen->sample.output_region = (VARectangle) {
    .x = render_rect.x,
    .y = render_rect.y,
    .width = render_rect.w,
    .height = render_rect.h,
  };
  /* *INDENT-ON* */
  gen->sample.alpha = gst_video_overlay_rectangle_get_global_alpha (rectangle);

  return &gen->sample;
}

static GstFlowReturn
gst_va_overlay_compositor_transform (GstBaseTransform * bt, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstVaOverlayCompositor *self = GST_VA_OVERLAY_COMPOSITOR (bt);
  GstVaBaseTransform *vabtrans = GST_VA_BASE_TRANSFORM (self);
  GstVaOverlayCompositorSampleGenerator generator;
  GstVaComposeTransaction tx;
  GstFlowReturn ret = GST_FLOW_OK;

  /* *INDENT-OFF* */
  generator = (GstVaOverlayCompositorSampleGenerator) {
    .compositor = self,
    .inbuf = inbuf,
    .state = NULL,
    .ometa = NULL,
  };
  tx = (GstVaComposeTransaction) {
    .next = _sample_next,
    .output = outbuf,
    .user_data = (gpointer) &generator,
  };
  /* *INDENT-ON* */

  if (!gst_va_filter_compose (vabtrans->filter, &tx)) {
    GST_ERROR_OBJECT (self, "couldn't apply filter");
    ret = GST_FLOW_ERROR;
  }

  /* TODO: Consider using a special surface allocator instead of a new pool per
   * rectangle. */
  /* Trim the overlay pool list by removing the least used items. */
  while (g_slist_length (self->pools) > MAX_OVERLAY_POOLS) {
    GSList *least_used = g_slist_last (self->pools);
    g_clear_pointer (&least_used->data, _overlay_pool_free);
    self->pools = g_slist_delete_link (self->pools, least_used);
  }

  return ret;
}

static gboolean
gst_va_overlay_compositor_stop (GstBaseTransform * bt)
{
  GstVaOverlayCompositor *self = GST_VA_OVERLAY_COMPOSITOR (bt);

  g_clear_slist (&self->pools, (GDestroyNotify) _overlay_pool_free);

  return TRUE;
}

/* *INDENT-OFF* */
static const gchar *caps_str =
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_VA,
        "{ NV12, I420, YV12, YUY2, RGBA, BGRA, P010_10LE, ARGB, ABGR }") " ;"
    GST_VIDEO_CAPS_MAKE ("{ VUYA, GRAY8, NV12, NV21, YUY2, UYVY, YV12, "
        "I420, P010_10LE, RGBA, BGRA, ARGB, ABGR  }");
/* *INDENT-ON* */

static gboolean
_add_overlay_meta (GstCapsFeatures * features, GstStructure * structure,
    gpointer user_data)
{
  gst_caps_features_add_static_str (features,
      GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION);
  return TRUE;
}

static GstCaps *
_add_overlay_meta_to_caps (GstCaps * caps)
{
  GstCaps *meta_caps = gst_caps_copy (caps);

  gst_caps_map_in_place (meta_caps, _add_overlay_meta, NULL);

  gst_caps_append (meta_caps, caps);

  return meta_caps;
}

static void
gst_va_overlay_compositor_class_init (GstVaOverlayCompositorClass * klass,
    struct CData *cdata)
{
  GstCaps *doc_caps, *caps = NULL;
  GstPadTemplate *sink_pad_templ, *src_pad_templ;
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *btrans_class = GST_BASE_TRANSFORM_CLASS (klass);
  GstVaBaseTransformClass *vabtrans_class = GST_VA_BASE_TRANSFORM_CLASS (klass);
  gchar *long_name;
  GstVaDisplay *display;
  GstVaFilter *filter;

  parent_class = g_type_class_peek_parent (klass);

  btrans_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_va_overlay_compositor_transform_caps);
  btrans_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_va_overlay_compositor_propose_allocation);
  btrans_class->transform =
      GST_DEBUG_FUNCPTR (gst_va_overlay_compositor_transform);
  btrans_class->stop = GST_DEBUG_FUNCPTR (gst_va_overlay_compositor_stop);

  vabtrans_class->render_device_path = g_strdup (cdata->render_device_path);
  vabtrans_class->set_info =
      GST_DEBUG_FUNCPTR (gst_va_overlay_compositor_set_info);

  if (cdata->description) {
    long_name = g_strdup_printf ("VA-API Video Overlay Compositor in %s",
        cdata->description);
  } else {
    long_name = g_strdup ("VA-API Video Overlay Compositor");
  }

  display = gst_va_display_platform_new (vabtrans_class->render_device_path);
  filter = gst_va_filter_new (display);

  if (gst_va_filter_open (filter)) {
    caps = gst_va_filter_get_caps (filter);
  } else {
    caps = gst_caps_from_string (caps_str);
  }

  caps = _add_overlay_meta_to_caps (caps);

  gst_element_class_set_static_metadata (element_class, long_name,
      "Filter/Video",
      "VA-API Overlay Composition element",
      "Jakub Adam <jakub.adam@collabora.com>");

  doc_caps = _add_overlay_meta_to_caps (gst_caps_from_string (caps_str));

  sink_pad_templ = gst_pad_template_new ("sink", GST_PAD_SINK,
      GST_PAD_ALWAYS, caps);
  gst_element_class_add_pad_template (element_class, sink_pad_templ);
  gst_pad_template_set_documentation_caps (sink_pad_templ,
      gst_caps_ref (doc_caps));

  src_pad_templ = gst_pad_template_new ("src", GST_PAD_SRC,
      GST_PAD_ALWAYS, caps);
  gst_element_class_add_pad_template (element_class, src_pad_templ);
  gst_pad_template_set_documentation_caps (src_pad_templ,
      gst_caps_ref (doc_caps));

  gst_caps_unref (doc_caps);
  gst_caps_unref (caps);

  g_clear_pointer (&long_name, g_free);
  g_clear_pointer (&cdata->description, g_free);
  g_clear_pointer (&cdata->render_device_path, g_free);
  g_clear_pointer (&cdata, g_free);
  gst_clear_object (&filter);
  gst_clear_object (&display);
}

static void
gst_va_overlay_compositor_init (GTypeInstance * instance, gpointer g_class)
{
}

static gpointer
_register_debug_category (gpointer data)
{
  GST_DEBUG_CATEGORY_INIT (gst_va_overlay_compositor_debug,
      "vaoverlaycompositor", 0, "VA Video Overlay Compositor");

  return NULL;
}

gboolean
gst_va_overlay_compositor_register (GstPlugin * plugin, GstVaDevice * device,
    guint rank)
{
  static GOnce debug_once = G_ONCE_INIT;
  GType type;
  GTypeInfo type_info = {
    .class_size = sizeof (GstVaOverlayCompositorClass),
    .class_init = (GClassInitFunc) gst_va_overlay_compositor_class_init,
    .instance_size = sizeof (GstVaOverlayCompositor),
    .instance_init = gst_va_overlay_compositor_init,
  };
  struct CData *cdata;
  gboolean ret;
  gchar *type_name, *feature_name;

  g_return_val_if_fail (GST_IS_PLUGIN (plugin), FALSE);
  g_return_val_if_fail (GST_IS_VA_DEVICE (device), FALSE);

  cdata = g_new (struct CData, 1);
  cdata->description = NULL;
  cdata->render_device_path = g_strdup (device->render_device_path);

  type_info.class_data = cdata;

  gst_va_create_feature_name (device, "GstVaOverlayCompositor",
      "GstVa%sOverlayCompositor", &type_name, "vaoverlaycompositor",
      "va%soverlaycompositor", &feature_name, &cdata->description, &rank);

  g_once (&debug_once, _register_debug_category, NULL);

  type =
      g_type_register_static (GST_TYPE_VA_BASE_TRANSFORM, type_name, &type_info,
      0);

  ret = gst_element_register (plugin, feature_name, rank, type);

  g_free (type_name);
  g_free (feature_name);

  return ret;
}
