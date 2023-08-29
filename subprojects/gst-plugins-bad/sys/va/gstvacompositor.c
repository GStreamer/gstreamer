/* GStreamer
 * Copyright (C) 2022 Intel Corporation
 *     Author: U. Artie Eoff <ullysses.a.eoff@intel.com>
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
 * License along with this library; if not, write to the0
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:element-vacompositor
 * @title: vacompositor
 * @short_description: A VA-API based video compositing element
 *
 * A video compositing element that uses VA-API VPP to accelerate the compose,
 * blending, and scaling of multiple inputs into one output.
 *
 * ## Example launch line
 * ```
 *  gst-launch-1.0 videotestsrc                                 \
 *    ! "video/x-raw,format=(string)NV12,width=640,height=480"  \
 *    ! tee name=testsrc ! queue ! vacompositor name=comp       \
 *      sink_1::width=160 sink_1::height=120 sink_1::xpos=480   \
 *      sink_1::ypos=360 sink_1::alpha=0.75                     \
 *    ! autovideosink testsrc. ! queue ! comp.
 * ```
 *
 * Since: 1.22
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvacompositor.h"

#include <gst/va/gstva.h>
#include <gst/va/vasurfaceimage.h>
#include <gst/video/video.h>
#include <va/va_drmcommon.h>

#include "gstvabase.h"
#include "gstvacaps.h"
#include "gstvadisplay_priv.h"
#include "gstvafilter.h"
#include "gstvapluginutils.h"

GST_DEBUG_CATEGORY_STATIC (gst_va_compositor_debug);
#define GST_CAT_DEFAULT gst_va_compositor_debug

/**
 * GstVaCompositorPad:
 *
 * VA aggregator pad.
 *
 * Since: 1.22
 */
struct _GstVaCompositorPad
{
  GstVideoAggregatorPad parent;

  /*< private> */
  GstBufferPool *pool;

  gint xpos;
  gint ypos;
  gint width;
  gint height;
  gdouble alpha;
};

enum
{
  PROP_PAD_0,
  PROP_PAD_XPOS,
  PROP_PAD_YPOS,
  PROP_PAD_WIDTH,
  PROP_PAD_HEIGHT,
  PROP_PAD_ALPHA,
};

#define DEFAULT_PAD_XPOS    0
#define DEFAULT_PAD_YPOS    0
#define DEFAULT_PAD_WIDTH   0
#define DEFAULT_PAD_HEIGHT  0
#define DEFAULT_PAD_ALPHA   1.0

G_DEFINE_TYPE (GstVaCompositorPad, gst_va_compositor_pad,
    GST_TYPE_VIDEO_AGGREGATOR_PAD);

static void
gst_va_compositor_pad_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVaCompositorPad *self = GST_VA_COMPOSITOR_PAD (object);

  switch (prop_id) {
    case PROP_PAD_XPOS:
      g_value_set_int (value, self->xpos);
      break;
    case PROP_PAD_YPOS:
      g_value_set_int (value, self->ypos);
      break;
    case PROP_PAD_WIDTH:
      g_value_set_int (value, self->width);
      break;
    case PROP_PAD_HEIGHT:
      g_value_set_int (value, self->height);
      break;
    case PROP_PAD_ALPHA:
      g_value_set_double (value, self->alpha);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_va_compositor_pad_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVaCompositorPad *self = GST_VA_COMPOSITOR_PAD (object);

  GST_OBJECT_LOCK (object);
  switch (prop_id) {
    case PROP_PAD_XPOS:
      self->xpos = g_value_get_int (value);
      break;
    case PROP_PAD_YPOS:
      self->ypos = g_value_get_int (value);
      break;
    case PROP_PAD_WIDTH:
      self->width = g_value_get_int (value);
      break;
    case PROP_PAD_HEIGHT:
      self->height = g_value_get_int (value);
      break;
    case PROP_PAD_ALPHA:
      self->alpha = g_value_get_double (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (object);
}

static void
gst_va_compositor_pad_finalize (GObject * object)
{
  GstVaCompositorPad *self = GST_VA_COMPOSITOR_PAD (object);

  if (self->pool) {
    gst_buffer_pool_set_active (self->pool, FALSE);
    gst_clear_object (&self->pool);
  }

  G_OBJECT_CLASS (gst_va_compositor_pad_parent_class)->finalize (object);
}

static void
gst_va_compositor_pad_init (GstVaCompositorPad * self)
{
  self->pool = NULL;
  self->xpos = DEFAULT_PAD_XPOS;
  self->ypos = DEFAULT_PAD_YPOS;
  self->width = DEFAULT_PAD_WIDTH;
  self->height = DEFAULT_PAD_HEIGHT;
  self->alpha = DEFAULT_PAD_ALPHA;
}

static void
gst_va_compositor_pad_class_init (GstVaCompositorPadClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstVideoAggregatorPadClass *vaggpad_class =
      GST_VIDEO_AGGREGATOR_PAD_CLASS (klass);

  gobject_class->finalize = gst_va_compositor_pad_finalize;
  gobject_class->get_property = gst_va_compositor_pad_get_property;
  gobject_class->set_property = gst_va_compositor_pad_set_property;

  g_object_class_install_property (gobject_class, PROP_PAD_XPOS,
      g_param_spec_int ("xpos", "X Position", "X Position of the picture",
          G_MININT, G_MAXINT, DEFAULT_PAD_XPOS,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PAD_YPOS,
      g_param_spec_int ("ypos", "Y Position", "Y Position of the picture",
          G_MININT, G_MAXINT, DEFAULT_PAD_YPOS,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PAD_WIDTH,
      g_param_spec_int ("width", "Width",
          "Width of the picture (0, to use the width of the input frame)",
          0, G_MAXINT, DEFAULT_PAD_WIDTH,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PAD_HEIGHT,
      g_param_spec_int ("height", "Height",
          "Height of the picture (0, to use the height of the input frame)",
          0, G_MAXINT, DEFAULT_PAD_HEIGHT,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PAD_ALPHA,
      g_param_spec_double ("alpha", "Alpha", "Alpha of the picture", 0.0, 1.0,
          DEFAULT_PAD_ALPHA,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));

  /* Don't use mapped video frames.  Handle video buffers directly */
  vaggpad_class->prepare_frame = NULL;
  vaggpad_class->clean_frame = NULL;
}

#define GST_VA_COMPOSITOR(obj) ((GstVaCompositor *) obj)
#define GST_VA_COMPOSITOR_CLASS(klass) ((GstVaCompositorClass *) klass)
#define GST_VA_COMPOSITOR_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS ((obj), G_TYPE_FROM_INSTANCE (obj), GstVaCompositorClass))

typedef struct _GstVaCompositor GstVaCompositor;
typedef struct _GstVaCompositorClass GstVaCompositorClass;

struct _GstVaCompositorClass
{
  GstVideoAggregatorClass parent_class;

  /*< private > */
  gchar *render_device_path;
};

struct _GstVaCompositor
{
  GstVideoAggregator parent;

  GstVaDisplay *display;
  GstVaFilter *filter;

  GstVideoInfo other_info;      /* downstream info */
  GstBufferPool *other_pool;    /* downstream pool */

  guint32 scale_method;
};

struct CData
{
  gchar *render_device_path;
  gchar *description;
};

enum
{
  PROP_DEVICE_PATH = 1,
  PROP_SCALE_METHOD,
  N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES];
static GstElementClass *parent_class = NULL;

static void
gst_va_compositor_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVaCompositor *self = GST_VA_COMPOSITOR (object);

  switch (prop_id) {
    case PROP_SCALE_METHOD:
    {
      GST_OBJECT_LOCK (object);
      self->scale_method = g_value_get_enum (value);
      GST_OBJECT_UNLOCK (object);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
gst_va_compositor_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVaCompositor *self = GST_VA_COMPOSITOR (object);
  GstVaCompositorClass *klass = GST_VA_COMPOSITOR_GET_CLASS (self);

  switch (prop_id) {
    case PROP_DEVICE_PATH:
    {
      if (!self->display)
        g_value_set_string (value, klass->render_device_path);
      else if (GST_IS_VA_DISPLAY_PLATFORM (self->display))
        g_object_get_property (G_OBJECT (self->display), "path", value);
      else
        g_value_set_string (value, NULL);

      break;
    }
    case PROP_SCALE_METHOD:
    {
      GST_OBJECT_LOCK (object);
      g_value_set_enum (value, self->scale_method);
      GST_OBJECT_UNLOCK (object);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static gboolean
gst_va_compositor_start (GstAggregator * agg)
{
  GstElement *element = GST_ELEMENT (agg);
  GstVaCompositor *self = GST_VA_COMPOSITOR (agg);
  GstVaCompositorClass *klass = GST_VA_COMPOSITOR_GET_CLASS (agg);

  if (!gst_va_ensure_element_data (element, klass->render_device_path,
          &self->display))
    return FALSE;
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DEVICE_PATH]);

  self->filter = gst_va_filter_new (self->display);
  if (!gst_va_filter_open (self->filter))
    return FALSE;

  return GST_AGGREGATOR_CLASS (parent_class)->start (agg);
}

static gboolean
gst_va_compositor_stop (GstAggregator * agg)
{
  GstVaCompositor *self = GST_VA_COMPOSITOR (agg);

  gst_va_filter_close (self->filter);
  gst_clear_object (&self->filter);
  gst_clear_object (&self->display);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DEVICE_PATH]);

  return GST_AGGREGATOR_CLASS (parent_class)->stop (agg);
}

static void
gst_va_compositor_dispose (GObject * object)
{
  GstVaCompositor *self = GST_VA_COMPOSITOR (object);

  if (self->other_pool) {
    gst_buffer_pool_set_active (self->other_pool, FALSE);
    gst_clear_object (&self->other_pool);
  }

  gst_clear_object (&self->display);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static GstPad *
gst_va_compositor_request_new_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * req_name, const GstCaps * caps)
{
  GstPad *newpad = GST_PAD (GST_ELEMENT_CLASS
      (parent_class)->request_new_pad (element, templ, req_name, caps));

  if (!newpad)
    GST_DEBUG_OBJECT (element, "could not create/add pad");
  else
    gst_child_proxy_child_added (GST_CHILD_PROXY (element), G_OBJECT (newpad),
        GST_OBJECT_NAME (newpad));

  return newpad;
}

static void
gst_va_compositor_release_pad (GstElement * element, GstPad * pad)
{
  GstVaCompositor *self = GST_VA_COMPOSITOR (element);

  gst_child_proxy_child_removed (GST_CHILD_PROXY (self), G_OBJECT (pad),
      GST_OBJECT_NAME (pad));

  GST_ELEMENT_CLASS (parent_class)->release_pad (element, pad);
}

static void
gst_va_compositor_set_context (GstElement * element, GstContext * context)
{
  GstVaDisplay *old_display, *new_display;
  GstVaCompositor *self = GST_VA_COMPOSITOR (element);
  GstVaCompositorClass *klass = GST_VA_COMPOSITOR_GET_CLASS (self);
  gboolean ret;

  old_display = self->display ? gst_object_ref (self->display) : NULL;
  ret = gst_va_handle_set_context (element, context, klass->render_device_path,
      &self->display);
  new_display = self->display ? gst_object_ref (self->display) : NULL;

  if (!ret
      || (old_display && new_display && old_display != new_display
          && self->filter)) {
    GST_ELEMENT_WARNING (element, RESOURCE, BUSY,
        ("Can't replace VA display while operating"), (NULL));
  }

  gst_clear_object (&old_display);
  gst_clear_object (&new_display);

  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);
}

static gboolean
_handle_context_query (GstVaCompositor * self, GstQuery * query)
{
  GstVaDisplay *display = NULL;
  gboolean ret = FALSE;

  gst_object_replace ((GstObject **) & display, (GstObject *) self->display);
  ret = gst_va_handle_context_query (GST_ELEMENT_CAST (self), query, display);
  gst_clear_object (&display);

  return ret;
}

static GstCaps *
gst_va_compositor_sink_getcaps (GstPad * pad, GstCaps * filter)
{
  GstCaps *sinkcaps;
  GstCaps *template_caps;
  GstCaps *filtered_caps;
  GstCaps *returned_caps;

  template_caps = gst_pad_get_pad_template_caps (pad);

  sinkcaps = gst_pad_get_current_caps (pad);
  if (!sinkcaps) {
    sinkcaps = gst_caps_ref (template_caps);
  } else {
    sinkcaps = gst_caps_merge (sinkcaps, gst_caps_ref (template_caps));
  }

  if (filter) {
    filtered_caps = gst_caps_intersect (sinkcaps, filter);
    gst_caps_unref (sinkcaps);
  } else {
    filtered_caps = sinkcaps;
  }

  returned_caps = gst_caps_intersect (filtered_caps, template_caps);

  gst_caps_unref (template_caps);
  gst_caps_unref (filtered_caps);

  GST_DEBUG_OBJECT (pad, "returning %" GST_PTR_FORMAT, returned_caps);

  return returned_caps;
}

static gboolean
gst_va_compositor_sink_acceptcaps (GstPad * pad, GstCaps * caps)
{
  gboolean ret;
  GstCaps *template_caps;

  template_caps = gst_pad_get_pad_template_caps (pad);
  template_caps = gst_caps_make_writable (template_caps);

  ret = gst_caps_can_intersect (caps, template_caps);
  GST_DEBUG_OBJECT (pad, "%saccepted caps %" GST_PTR_FORMAT,
      (ret ? "" : "not "), caps);
  gst_caps_unref (template_caps);

  return ret;
}

static gboolean
gst_va_compositor_sink_query (GstAggregator * agg, GstAggregatorPad * pad,
    GstQuery * query)
{
  GstVaCompositor *self = GST_VA_COMPOSITOR (agg);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:
    {
      if (_handle_context_query (self, query))
        return TRUE;
      break;
    }
    case GST_QUERY_CAPS:
    {
      GstCaps *filter, *caps;

      gst_query_parse_caps (query, &filter);
      caps = gst_va_compositor_sink_getcaps (GST_PAD (pad), filter);
      gst_query_set_caps_result (query, caps);
      gst_caps_unref (caps);
      return TRUE;
    }
    case GST_QUERY_ACCEPT_CAPS:
    {
      GstCaps *caps;
      gboolean ret;

      gst_query_parse_accept_caps (query, &caps);
      ret = gst_va_compositor_sink_acceptcaps (GST_PAD (pad), caps);
      gst_query_set_accept_caps_result (query, ret);
      return TRUE;
    }
    default:
      break;
  }

  return GST_AGGREGATOR_CLASS (parent_class)->sink_query (agg, pad, query);
}

static gboolean
gst_va_compositor_src_query (GstAggregator * agg, GstQuery * query)
{
  GstVaCompositor *self = GST_VA_COMPOSITOR (agg);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:
      if (_handle_context_query (self, query))
        return TRUE;
      break;
    default:
      break;
  }

  return GST_AGGREGATOR_CLASS (parent_class)->src_query (agg, query);
}

static GstAllocator *
gst_va_compositor_allocator_from_caps (GstVaCompositor * self, GstCaps * caps)
{
  GstAllocator *allocator = NULL;

  if (gst_caps_is_dmabuf (caps)) {
    allocator = gst_va_dmabuf_allocator_new (self->display);
  } else {
    GArray *surface_formats = gst_va_filter_get_surface_formats (self->filter);
    allocator = gst_va_allocator_new (self->display, surface_formats);
  }

  return allocator;
}

/* Answer upstream allocation query. */
static gboolean
gst_va_compositor_propose_allocation (GstAggregator * agg,
    GstAggregatorPad * aggpad, GstQuery * decide_query, GstQuery * query)
{
  GstVaCompositor *self = GST_VA_COMPOSITOR (agg);
  GstAllocator *allocator = NULL;
  GstAllocationParams params = { 0, };
  GstBufferPool *pool;
  GstCaps *caps;
  GstVideoInfo info;
  gboolean update_allocator = FALSE;
  guint size, usage_hint;

  gst_query_parse_allocation (query, &caps, NULL);

  if (!caps)
    return FALSE;

  if (!gst_video_info_from_caps (&info, caps))
    return FALSE;

  if (gst_query_get_n_allocation_pools (query) > 0)
    return TRUE;

  usage_hint = va_get_surface_usage_hint (self->display,
      VAEntrypointVideoProc, GST_PAD_SINK, gst_video_is_dma_drm_caps (caps));

  size = GST_VIDEO_INFO_SIZE (&info);

  if (gst_query_get_n_allocation_params (query) > 0) {
    gst_query_parse_nth_allocation_param (query, 0, &allocator, &params);
    if (!GST_IS_VA_DMABUF_ALLOCATOR (allocator)
        && !GST_IS_VA_ALLOCATOR (allocator))
      gst_clear_object (&allocator);
    update_allocator = TRUE;
  } else {
    gst_allocation_params_init (&params);
  }

  if (!allocator) {
    if (!(allocator = gst_va_compositor_allocator_from_caps (self, caps)))
      return FALSE;
  }

  /* Now we have a VA-based allocator */

  pool = gst_va_pool_new_with_config (caps, size, 1, 0, usage_hint,
      GST_VA_FEATURE_AUTO, allocator, &params);
  if (!pool) {
    gst_object_unref (allocator);
    goto config_failed;
  }

  if (update_allocator)
    gst_query_set_nth_allocation_param (query, 0, allocator, &params);
  else
    gst_query_add_allocation_param (query, allocator, &params);

  gst_query_add_allocation_pool (query, pool, size, 1, 0);

  GST_DEBUG_OBJECT (self,
      "proposing %" GST_PTR_FORMAT " with allocator %" GST_PTR_FORMAT,
      pool, allocator);

  gst_object_unref (allocator);
  gst_object_unref (pool);

  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

  return TRUE;

config_failed:
  {
    GST_ERROR_OBJECT (self, "failed to set config");
    return FALSE;
  }
}

static GstBufferPool *
_create_other_pool (GstAllocator * allocator, GstAllocationParams * params,
    GstCaps * caps, guint size)
{
  GstBufferPool *pool = NULL;
  GstStructure *config;

  pool = gst_video_buffer_pool_new ();
  config = gst_buffer_pool_get_config (pool);

  gst_buffer_pool_config_set_params (config, caps, size, 0, 0);
  gst_buffer_pool_config_set_allocator (config, allocator, params);
  if (!gst_buffer_pool_set_config (pool, config)) {
    gst_clear_object (&pool);
  }

  return pool;
}

/* configure the allocation query that was answered downstream */
static gboolean
gst_va_compositor_decide_allocation (GstAggregator * agg, GstQuery * query)
{
  GstVaCompositor *self = GST_VA_COMPOSITOR (agg);
  GstVideoAggregator *vagg = GST_VIDEO_AGGREGATOR (agg);

  GstAllocator *allocator = NULL, *other_allocator = NULL;
  GstAllocationParams params, other_params;
  GstBufferPool *pool = NULL, *other_pool = NULL;
  GstCaps *caps = NULL;
  GstStructure *config;
  GstVideoInfo info;
  guint min, max, size = 0, usage_hint;
  gboolean update_pool, update_allocator, has_videometa, copy_frames;
  gboolean dont_use_other_pool = FALSE;

  gst_query_parse_allocation (query, &caps, NULL);

  gst_allocation_params_init (&other_params);
  gst_allocation_params_init (&params);

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (self, "Cannot parse caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }

  if (gst_query_get_n_allocation_params (query) > 0) {
    GstVaDisplay *display;

    gst_query_parse_nth_allocation_param (query, 0, &allocator, &other_params);
    display = gst_va_allocator_peek_display (allocator);
    if (!display) {
      /* save the allocator for the other pool */
      other_allocator = allocator;
      allocator = NULL;
    } else if (display != self->display) {
      /* The allocator and pool belong to other display, we should not use. */
      gst_clear_object (&allocator);
      dont_use_other_pool = TRUE;
    }

    update_allocator = TRUE;
  } else {
    update_allocator = FALSE;
  }

  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);

    if (pool) {
      if (!GST_IS_VA_POOL (pool)) {
        GST_DEBUG_OBJECT (self,
            "may need other pool for copy frames %" GST_PTR_FORMAT, pool);
        other_pool = pool;
        pool = NULL;
      } else if (dont_use_other_pool) {
        gst_clear_object (&pool);
      }
    }

    update_pool = TRUE;
  } else {
    size = GST_VIDEO_INFO_SIZE (&info);
    min = 1;
    max = 0;
    update_pool = FALSE;
  }

  if (!allocator) {
    if (gst_caps_is_dmabuf (caps) && GST_VIDEO_INFO_IS_RGB (&info))
      usage_hint = VA_SURFACE_ATTRIB_USAGE_HINT_GENERIC;
    if (!(allocator = gst_va_compositor_allocator_from_caps (self, caps)))
      return FALSE;
  }

  if (!pool)
    pool = gst_va_pool_new ();

  usage_hint = va_get_surface_usage_hint (self->display,
      VAEntrypointVideoProc, GST_PAD_SRC, gst_video_is_dma_drm_caps (caps));

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_allocator (config, allocator, &params);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  gst_buffer_pool_config_set_params (config, caps, size, min, max);
  gst_buffer_pool_config_set_va_allocation_params (config, usage_hint,
      GST_VA_FEATURE_AUTO);
  if (!gst_buffer_pool_set_config (pool, config)) {
    gst_object_unref (allocator);
    gst_object_unref (pool);
    return FALSE;
  }

  if (GST_IS_VA_DMABUF_ALLOCATOR (allocator)) {
    GstVideoInfoDmaDrm dma_info;

    gst_va_dmabuf_allocator_get_format (allocator, &dma_info, NULL);
    vagg->info = dma_info.vinfo;
  } else if (GST_IS_VA_ALLOCATOR (allocator)) {
    gst_va_allocator_get_format (allocator, &vagg->info, NULL, NULL);
  }

  if (update_allocator)
    gst_query_set_nth_allocation_param (query, 0, allocator, &params);
  else
    gst_query_add_allocation_param (query, allocator, &params);

  if (update_pool)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool (query, pool, size, min, max);

  has_videometa = gst_query_find_allocation_meta (query,
      GST_VIDEO_META_API_TYPE, NULL);

  copy_frames = (!has_videometa && gst_va_pool_requires_video_meta (pool)
      && gst_caps_is_raw (caps));
  if (copy_frames) {
    if (other_pool) {
      gst_object_replace ((GstObject **) & self->other_pool,
          (GstObject *) other_pool);
    } else {
      self->other_pool =
          _create_other_pool (other_allocator, &other_params, caps, size);
    }
    GST_DEBUG_OBJECT (self, "Use the other pool for copy %" GST_PTR_FORMAT,
        self->other_pool);
  } else {
    gst_clear_object (&self->other_pool);
  }

  GST_DEBUG_OBJECT (self,
      "decided pool %" GST_PTR_FORMAT " with allocator %" GST_PTR_FORMAT,
      pool, allocator);

  gst_object_unref (allocator);
  gst_object_unref (pool);
  gst_clear_object (&other_allocator);
  gst_clear_object (&other_pool);

  return TRUE;
}

static GstBufferPool *
_get_sinkpad_pool (GstElement * element, gpointer data)
{
  GstVaCompositor *self = GST_VA_COMPOSITOR (element);
  GstVaCompositorPad *pad = GST_VA_COMPOSITOR_PAD (data);
  GstAllocator *allocator;
  GstAllocationParams params = { 0, };
  GstCaps *caps;
  GstVideoInfo info;
  guint size, usage_hint;

  if (pad->pool)
    return pad->pool;

  gst_allocation_params_init (&params);

  caps = gst_pad_get_current_caps (GST_PAD (pad));
  if (!caps)
    return NULL;
  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (self, "Cannot parse caps %" GST_PTR_FORMAT, caps);
    gst_caps_unref (caps);
    return NULL;
  }

  usage_hint = va_get_surface_usage_hint (self->display,
      VAEntrypointVideoProc, GST_PAD_SINK, FALSE);

  size = GST_VIDEO_INFO_SIZE (&info);

  allocator = gst_va_compositor_allocator_from_caps (self, caps);
  pad->pool = gst_va_pool_new_with_config (caps, size, 1, 0, usage_hint,
      GST_VA_FEATURE_AUTO, allocator, &params);
  gst_caps_unref (caps);

  if (!pad->pool) {
    gst_object_unref (allocator);
    return NULL;
  }

  if (GST_IS_VA_DMABUF_ALLOCATOR (allocator)) {
    GstVideoInfoDmaDrm dma_info;

    gst_va_dmabuf_allocator_get_format (allocator, &dma_info, NULL);
    info = dma_info.vinfo;
  } else if (GST_IS_VA_ALLOCATOR (allocator)) {
    gst_va_allocator_get_format (allocator, &info, NULL, NULL);
  }

  gst_object_unref (allocator);

  if (!gst_buffer_pool_set_active (pad->pool, TRUE)) {
    GST_WARNING_OBJECT (self, "failed to active the sinkpad pool %"
        GST_PTR_FORMAT, pad->pool);
    return NULL;
  }

  return pad->pool;
}

static GstFlowReturn
gst_va_compositor_import_buffer (GstVaCompositor * self,
    GstVaCompositorPad * pad, GstBuffer * inbuf, GstBuffer ** buf)
{
  GstVaBufferImporter importer = {
    .element = GST_ELEMENT_CAST (self),
#ifndef GST_DISABLE_GST_DEBUG
    .debug_category = GST_CAT_DEFAULT,
#endif
    .display = self->display,
    .entrypoint = VAEntrypointVideoProc,
    .get_sinkpad_pool = _get_sinkpad_pool,
    .pool_data = pad,
  };
  GstCaps *caps;
  GstVideoInfo info;

  caps = gst_pad_get_current_caps (GST_PAD (pad));
  if (!caps)
    return GST_FLOW_ERROR;
  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (self, "Cannot parse caps %" GST_PTR_FORMAT, caps);
    gst_caps_unref (caps);
    return GST_FLOW_ERROR;
  }
  gst_caps_unref (caps);

  importer.in_info = importer.sinkpad_info = &info;

  return gst_va_buffer_importer_import (&importer, inbuf, buf);
}

typedef struct _GstVaCompositorSampleGenerator GstVaCompositorSampleGenerator;
struct _GstVaCompositorSampleGenerator
{
  GstVaCompositor *comp;
  GList *current;
  GstVaComposeSample sample;
};

static GstVaComposeSample *
gst_va_compositor_sample_next (gpointer data)
{
  GstVaCompositorSampleGenerator *generator;
  GstVideoAggregatorPad *vaggpad;
  GstVaCompositorPad *pad;
  GstBuffer *inbuf;
  GstBuffer *buf;
  GstFlowReturn res;
  GstVideoCropMeta *crop = NULL;

  generator = (GstVaCompositorSampleGenerator *) data;

  /* at the end of the generator? */
  while (generator->current) {
    /* get the current sinkpad for processing */
    vaggpad = GST_VIDEO_AGGREGATOR_PAD (generator->current->data);

    /* increment to next sinkpad */
    generator->current = generator->current->next;

    /* reset sample */
    /* *INDENT-OFF* */
    generator->sample = (GstVaComposeSample) { 0, };
    /* *INDENT-ON* */

    /* current sinkpad may not be queueing buffers yet (e.g. timestamp-offset)
     * or it may have reached EOS */
    if (!gst_video_aggregator_pad_has_current_buffer (vaggpad))
      continue;

    inbuf = gst_video_aggregator_pad_get_current_buffer (vaggpad);
    pad = GST_VA_COMPOSITOR_PAD (vaggpad);

    res = gst_va_compositor_import_buffer (generator->comp, pad, inbuf, &buf);
    if (res != GST_FLOW_OK)
      return &generator->sample;

    crop = gst_buffer_get_video_crop_meta (buf);

    GST_OBJECT_LOCK (vaggpad);
    /* *INDENT-OFF* */
    generator->sample = (GstVaComposeSample) {
      .buffer = buf,
      .input_region = (VARectangle) {
        .x = crop ? crop->x : 0,
        .y = crop ? crop->y : 0,
        .width = crop ? crop->width : GST_VIDEO_INFO_WIDTH (&vaggpad->info),
        .height = crop ? crop->height : GST_VIDEO_INFO_HEIGHT (&vaggpad->info),
      },
      .output_region = (VARectangle) {
        .x = pad->xpos,
        .y = pad->ypos,
        .width = (pad->width == DEFAULT_PAD_WIDTH)
            ? GST_VIDEO_INFO_WIDTH (&vaggpad->info) : pad->width,
        .height = (pad->height == DEFAULT_PAD_HEIGHT)
            ? GST_VIDEO_INFO_HEIGHT (&vaggpad->info) : pad->height,
      },
      .alpha = pad->alpha,
    };
    /* *INDENT-ON* */
    GST_OBJECT_UNLOCK (vaggpad);

    return &generator->sample;
  }

  return NULL;
}

static gboolean
gst_va_compositor_copy_output_buffer (GstVaCompositor * self,
    GstBuffer * src_buf, GstBuffer * dst_buf)
{
  GstVideoAggregator *vagg = GST_VIDEO_AGGREGATOR (self);
  GstVideoFrame src_frame, dst_frame;

  GST_LOG_OBJECT (self, "copying output buffer");

  if (!gst_video_frame_map (&src_frame, &vagg->info, src_buf, GST_MAP_READ)) {
    GST_ERROR_OBJECT (self, "couldn't map source buffer");
    return FALSE;
  }

  if (!gst_video_frame_map (&dst_frame, &self->other_info, dst_buf,
          GST_MAP_WRITE)) {
    GST_ERROR_OBJECT (self, "couldn't map output buffer");
    gst_video_frame_unmap (&src_frame);
    return FALSE;
  }

  if (!gst_video_frame_copy (&dst_frame, &src_frame)) {
    GST_ERROR_OBJECT (self, "couldn't copy output buffer");
    gst_video_frame_unmap (&src_frame);
    gst_video_frame_unmap (&dst_frame);
    return FALSE;
  }

  gst_video_frame_unmap (&src_frame);
  gst_video_frame_unmap (&dst_frame);

  return TRUE;
}

static GstFlowReturn
gst_va_compositor_aggregate_frames (GstVideoAggregator * vagg,
    GstBuffer * outbuf)
{
  GstVaCompositor *self = GST_VA_COMPOSITOR (vagg);
  GstVaCompositorSampleGenerator generator;
  GstVaComposeTransaction tx;
  GstBuffer *vabuffer;
  gboolean need_copy = FALSE;
  GstFlowReturn ret = GST_FLOW_OK;

  if (self->other_pool) {
    /* create a va buffer for filter */
    ret = GST_VIDEO_AGGREGATOR_CLASS (parent_class)->create_output_buffer
        (vagg, &vabuffer);
    if (ret != GST_FLOW_OK)
      return ret;

    need_copy = TRUE;
  } else {
    /* already a va buffer */
    vabuffer = gst_buffer_ref (outbuf);
  }

  /* *INDENT-OFF* */
  generator = (GstVaCompositorSampleGenerator) {
    .comp = self,
    .current = GST_ELEMENT (self)->sinkpads,
  };
  tx = (GstVaComposeTransaction) {
    .next = gst_va_compositor_sample_next,
    .output = vabuffer,
    .user_data = (gpointer) &generator,
  };
  /* *INDENT-ON* */

  GST_OBJECT_LOCK (self);

  if (!gst_va_filter_set_scale_method (self->filter, self->scale_method))
    GST_WARNING_OBJECT (self, "couldn't set filter scale method");

  if (!gst_va_filter_compose (self->filter, &tx)) {
    GST_ERROR_OBJECT (self, "couldn't apply filter");
    ret = GST_FLOW_ERROR;
  }

  GST_OBJECT_UNLOCK (self);

  if (ret != GST_FLOW_OK)
    goto done;

  if (need_copy && !gst_va_compositor_copy_output_buffer (self, vabuffer,
          outbuf)) {
    GST_ERROR_OBJECT (self, "couldn't copy va buffer to output buffer");
    ret = GST_FLOW_ERROR;
  }

done:
  gst_buffer_unref (vabuffer);
  return ret;
}

static GstFlowReturn
gst_va_compositor_create_output_buffer (GstVideoAggregator * vagg,
    GstBuffer ** outbuf)
{
  GstVaCompositor *self = GST_VA_COMPOSITOR (vagg);
  GstFlowReturn ret;

  *outbuf = NULL;

  if (!self->other_pool)
    /* no copy necessary, so use a va buffer directly */
    return GST_VIDEO_AGGREGATOR_CLASS (parent_class)->create_output_buffer
        (vagg, outbuf);

  /* use output buffers from downstream pool for copy */
  if (!gst_buffer_pool_is_active (self->other_pool) &&
      !gst_buffer_pool_set_active (self->other_pool, TRUE)) {
    GST_ERROR_OBJECT (self, "failed to activate other pool %"
        GST_PTR_FORMAT, self->other_pool);
    return GST_FLOW_ERROR;
  }

  /* acquire a buffer from downstream pool for copy */
  ret = gst_buffer_pool_acquire_buffer (self->other_pool, outbuf, NULL);
  if (ret != GST_FLOW_OK || !*outbuf) {
    GST_ERROR_OBJECT (self, "failed to acquire output buffer");
    return GST_FLOW_ERROR;
  }

  return GST_FLOW_OK;
}

static gboolean
gst_va_compositor_negotiated_src_caps (GstAggregator * agg, GstCaps * caps)
{
  GstVaCompositor *self = GST_VA_COMPOSITOR (agg);

  if (!gst_video_info_from_caps (&self->other_info, caps)) {
    GST_ERROR_OBJECT (self, "invalid caps");
    return FALSE;
  }

  if (self->other_pool) {
    gst_buffer_pool_set_active (self->other_pool, FALSE);
    gst_clear_object (&self->other_pool);
  }

  return GST_AGGREGATOR_CLASS (parent_class)->negotiated_src_caps (agg, caps);
}

static void
gst_va_compositor_pad_get_output_size (GstVaCompositorPad * pad, gint * width,
    gint * height)
{
  GstVideoAggregatorPad *vaggpad = GST_VIDEO_AGGREGATOR_PAD (pad);
  *width = (pad->width == DEFAULT_PAD_WIDTH)
      ? GST_VIDEO_INFO_WIDTH (&vaggpad->info) : pad->width;
  *height = (pad->height == DEFAULT_PAD_HEIGHT)
      ? GST_VIDEO_INFO_HEIGHT (&vaggpad->info) : pad->height;

  *width += MAX (pad->xpos, 0);
  *height += MAX (pad->ypos, 0);
}

static GstCaps *
gst_va_compositor_fixate_src_caps (GstAggregator * agg, GstCaps * caps)
{
  GstVideoAggregator *vagg = GST_VIDEO_AGGREGATOR (agg);
  GList *l;
  gint best_width = -1, best_height = -1;
  gint best_fps_n = -1, best_fps_d = -1;
  gdouble best_fps = 0.;
  GstCaps *ret = NULL;
  GstStructure *s;

  ret = gst_caps_make_writable (caps);

  GST_OBJECT_LOCK (vagg);
  for (l = GST_ELEMENT (vagg)->sinkpads; l; l = l->next) {
    GstVideoAggregatorPad *vaggpad = l->data;
    GstVaCompositorPad *pad = GST_VA_COMPOSITOR_PAD (vaggpad);
    gint this_width, this_height;
    gint fps_n, fps_d;
    gdouble cur_fps;

    fps_n = GST_VIDEO_INFO_FPS_N (&vaggpad->info);
    fps_d = GST_VIDEO_INFO_FPS_D (&vaggpad->info);

    gst_va_compositor_pad_get_output_size (pad, &this_width, &this_height);

    if (best_width < this_width)
      best_width = this_width;
    if (best_height < this_height)
      best_height = this_height;

    if (fps_d == 0)
      cur_fps = 0.0;
    else
      gst_util_fraction_to_double (fps_n, fps_d, &cur_fps);

    if (best_fps < cur_fps) {
      best_fps = cur_fps;
      best_fps_n = fps_n;
      best_fps_d = fps_d;
    }
  }
  GST_OBJECT_UNLOCK (vagg);

  if (best_fps_n <= 0 || best_fps_d <= 0 || best_fps == 0.0) {
    best_fps_n = 25;
    best_fps_d = 1;
    best_fps = 25.0;
  }

  s = gst_caps_get_structure (ret, 0);
  gst_structure_fixate_field_nearest_int (s, "width", best_width);
  gst_structure_fixate_field_nearest_int (s, "height", best_height);
  if (gst_structure_has_field (s, "framerate")) {
    gst_structure_fixate_field_nearest_fraction (s, "framerate", best_fps_n,
        best_fps_d);
  } else {
    gst_structure_set (s, "framerate", GST_TYPE_FRACTION, best_fps_n,
        best_fps_d, NULL);
  }

  return gst_caps_fixate (ret);
}

/* *INDENT-OFF* */
static const gchar *caps_str =
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_VA,
        "{ NV12, I420, YV12, YUY2, RGBA, BGRA, P010_10LE, ARGB, ABGR }") " ;"
    GST_VIDEO_CAPS_MAKE ("{ VUYA, GRAY8, NV12, NV21, YUY2, UYVY, YV12, "
        "I420, P010_10LE, RGBA, BGRA, ARGB, ABGR  }");
/* *INDENT-ON* */

static void
gst_va_compositor_class_init (gpointer g_class, gpointer class_data)
{
  GstCaps *doc_caps, *caps = NULL;
  GstPadTemplate *sink_pad_templ, *src_pad_templ;
  GObjectClass *object_class = G_OBJECT_CLASS (g_class);
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstAggregatorClass *agg_class = GST_AGGREGATOR_CLASS (g_class);
  GstVideoAggregatorClass *vagg_class = GST_VIDEO_AGGREGATOR_CLASS (g_class);
  GstVaCompositorClass *klass = GST_VA_COMPOSITOR_CLASS (g_class);
  GstVaDisplay *display;
  GstVaFilter *filter;
  struct CData *cdata = class_data;
  gchar *long_name;

  parent_class = g_type_class_peek_parent (g_class);

  klass->render_device_path = g_strdup (cdata->render_device_path);

  if (cdata->description) {
    long_name = g_strdup_printf ("VA-API Video Compositor in %s",
        cdata->description);
  } else {
    long_name = g_strdup ("VA-API Video Compositor");
  }

  display = gst_va_display_platform_new (klass->render_device_path);
  filter = gst_va_filter_new (display);

  if (gst_va_filter_open (filter)) {
    caps = gst_va_filter_get_caps (filter);
  } else {
    caps = gst_caps_from_string (caps_str);
  }

  object_class->dispose = GST_DEBUG_FUNCPTR (gst_va_compositor_dispose);
  object_class->get_property =
      GST_DEBUG_FUNCPTR (gst_va_compositor_get_property);
  object_class->set_property =
      GST_DEBUG_FUNCPTR (gst_va_compositor_set_property);

  gst_element_class_set_static_metadata (element_class, long_name,
      "Filter/Editor/Video/Compositor/Hardware",
      "VA-API based video compositor",
      "U. Artie Eoff <ullysses.a.eoff@intel.com>");

  element_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_va_compositor_request_new_pad);
  element_class->release_pad =
      GST_DEBUG_FUNCPTR (gst_va_compositor_release_pad);
  element_class->set_context =
      GST_DEBUG_FUNCPTR (gst_va_compositor_set_context);

  doc_caps = gst_caps_from_string (caps_str);

  sink_pad_templ = gst_pad_template_new_with_gtype ("sink_%u", GST_PAD_SINK,
      GST_PAD_REQUEST, caps, GST_TYPE_VA_COMPOSITOR_PAD);
  gst_element_class_add_pad_template (element_class, sink_pad_templ);
  gst_pad_template_set_documentation_caps (sink_pad_templ,
      gst_caps_ref (doc_caps));
  gst_type_mark_as_plugin_api (GST_TYPE_VA_COMPOSITOR_PAD, 0);

  src_pad_templ = gst_pad_template_new_with_gtype ("src", GST_PAD_SRC,
      GST_PAD_ALWAYS, caps, GST_TYPE_AGGREGATOR_PAD);
  gst_element_class_add_pad_template (element_class, src_pad_templ);
  gst_pad_template_set_documentation_caps (src_pad_templ,
      gst_caps_ref (doc_caps));

  gst_caps_unref (doc_caps);
  gst_caps_unref (caps);

  agg_class->sink_query = GST_DEBUG_FUNCPTR (gst_va_compositor_sink_query);
  agg_class->src_query = GST_DEBUG_FUNCPTR (gst_va_compositor_src_query);
  agg_class->start = GST_DEBUG_FUNCPTR (gst_va_compositor_start);
  agg_class->stop = GST_DEBUG_FUNCPTR (gst_va_compositor_stop);
  agg_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_va_compositor_propose_allocation);
  agg_class->fixate_src_caps =
      GST_DEBUG_FUNCPTR (gst_va_compositor_fixate_src_caps);
  agg_class->negotiated_src_caps =
      GST_DEBUG_FUNCPTR (gst_va_compositor_negotiated_src_caps);
  agg_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_va_compositor_decide_allocation);

  vagg_class->aggregate_frames =
      GST_DEBUG_FUNCPTR (gst_va_compositor_aggregate_frames);
  vagg_class->create_output_buffer =
      GST_DEBUG_FUNCPTR (gst_va_compositor_create_output_buffer);

  /**
   * GstVaCompositor:device-path:
   *
   * It shows the DRM device path used for the VA operation, if any.
   */
  properties[PROP_DEVICE_PATH] = g_param_spec_string ("device-path",
      "Device Path", GST_VA_DEVICE_PATH_PROP_DESC, NULL,
      GST_PARAM_DOC_SHOW_DEFAULT | G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  /**
   * GstVaCompositor:scale-method:
   *
   * Sets the scale method algorithm to use when resizing.
   */
  properties[PROP_SCALE_METHOD] = g_param_spec_enum ("scale-method",
      "Scale Method", "Scale method to use", GST_TYPE_VA_SCALE_METHOD,
      VA_FILTER_SCALING_DEFAULT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  gst_type_mark_as_plugin_api (GST_TYPE_VA_SCALE_METHOD, 0);

  g_object_class_install_properties (object_class, N_PROPERTIES, properties);

  g_free (long_name);
  g_free (cdata->description);
  g_free (cdata->render_device_path);
  g_free (cdata);
  gst_object_unref (filter);
  gst_object_unref (display);
}

static GObject *
gst_va_compositor_child_proxy_get_child_by_index (GstChildProxy * proxy,
    guint index)
{
  GstVaCompositor *self = GST_VA_COMPOSITOR (proxy);
  GObject *obj = NULL;

  GST_OBJECT_LOCK (self);
  obj = g_list_nth_data (GST_ELEMENT_CAST (self)->sinkpads, index);
  if (obj)
    gst_object_ref (obj);
  GST_OBJECT_UNLOCK (self);

  return obj;
}

static guint
gst_va_compositor_child_proxy_get_children_count (GstChildProxy * proxy)
{
  GstVaCompositor *self = GST_VA_COMPOSITOR (proxy);
  guint count = 0;

  GST_OBJECT_LOCK (self);
  count = GST_ELEMENT_CAST (self)->numsinkpads;
  GST_OBJECT_UNLOCK (self);
  GST_INFO_OBJECT (self, "Children Count: %d", count);

  return count;
}

static void
gst_va_compositor_child_proxy_init (gpointer g_iface, gpointer iface_data)
{
  GstChildProxyInterface *iface = (GstChildProxyInterface *) g_iface;

  iface->get_child_by_index = gst_va_compositor_child_proxy_get_child_by_index;
  iface->get_children_count = gst_va_compositor_child_proxy_get_children_count;
}

static void
gst_va_compositor_init (GTypeInstance * instance, gpointer g_class)
{
  GstVaCompositor *self = GST_VA_COMPOSITOR (instance);

  self->other_pool = NULL;
}

static gpointer
_register_debug_category (gpointer data)
{
  GST_DEBUG_CATEGORY_INIT (gst_va_compositor_debug, "vacompositor", 0,
      "VA Video Compositor");

  return NULL;
}

gboolean
gst_va_compositor_register (GstPlugin * plugin, GstVaDevice * device,
    guint rank)
{
  static GOnce debug_once = G_ONCE_INIT;
  GType type;
  GTypeInfo type_info = {
    .class_size = sizeof (GstVaCompositorClass),
    .class_init = gst_va_compositor_class_init,
    .instance_size = sizeof (GstVaCompositor),
    .instance_init = gst_va_compositor_init,
  };
  GInterfaceInfo interface_info = {
    (GInterfaceInitFunc) gst_va_compositor_child_proxy_init,
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

  gst_va_create_feature_name (device, "GstVaCompositor", "GstVa%sCompositor",
      &type_name, "vacompositor", "va%scompositor", &feature_name,
      &cdata->description, &rank);

  g_once (&debug_once, _register_debug_category, NULL);

  type = g_type_register_static (GST_TYPE_VIDEO_AGGREGATOR, type_name,
      &type_info, 0);
  g_type_add_interface_static (type, GST_TYPE_CHILD_PROXY, &interface_info);

  ret = gst_element_register (plugin, feature_name, rank, type);

  g_free (type_name);
  g_free (feature_name);

  return ret;
}
