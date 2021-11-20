/*
 *  gstvaapioverlay.c - VA-API vpp overlay
 *
 *  Copyright (C) 2019 Intel Corporation
 *    Author: U. Artie Eoff <ullysses.a.eoff@intel.com>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this program; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
*/

/**
 * SECTION:element-vaapioverlay
 * @title: vaapioverlay
 * @short_description: a VA-API base video compositor
 *
 * The vaapioverlay element is similar to the base compositor element
 * but uses VA-API VPP blend functions to accelerate the
 * overlay/compositing.
 *
 * Currently this element only works with iHD driver.
 *
 * ## Example launch line
 *
 * |[
 *   gst-launch-1.0 -vf videotestsrc ! vaapipostproc      \
 *     ! tee name=testsrc ! queue                         \
 *     ! vaapioverlay sink_1::xpos=300 sink_1::alpha=0.75 \
 *     name=overlay ! vaapisink testsrc. ! queue ! overlay.
 * ]|
 */

#include "gstvaapioverlay.h"
#include "gstvaapipluginutil.h"
#include "gstvaapivideobufferpool.h"

#define GST_PLUGIN_NAME "vaapioverlay"
#define GST_PLUGIN_DESC "A VA-API overlay filter"

GST_DEBUG_CATEGORY_STATIC (gst_debug_vaapi_overlay);
#ifndef GST_DISABLE_GST_DEBUG
#define GST_CAT_DEFAULT gst_debug_vaapi_overlay
#else
#define GST_CAT_DEFAULT NULL
#endif

/* Default templates */
/* *INDENT-OFF* */
static const char gst_vaapi_overlay_sink_caps_str[] =
  GST_VAAPI_MAKE_SURFACE_CAPS ";"
  GST_VIDEO_CAPS_MAKE (GST_VAAPI_FORMATS_ALL);
/* *INDENT-ON* */

/* *INDENT-OFF* */
static const char gst_vaapi_overlay_src_caps_str[] =
  GST_VAAPI_MAKE_SURFACE_CAPS ";"
  GST_VIDEO_CAPS_MAKE (GST_VAAPI_FORMATS_ALL);
/* *INDENT-ON* */

/* *INDENT-OFF* */
static GstStaticPadTemplate gst_vaapi_overlay_sink_factory =
  GST_STATIC_PAD_TEMPLATE ("sink_%u",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS (gst_vaapi_overlay_sink_caps_str));
/* *INDENT-ON* */

/* *INDENT-OFF* */
static GstStaticPadTemplate gst_vaapi_overlay_src_factory =
  GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (gst_vaapi_overlay_src_caps_str));
/* *INDENT-ON* */

G_DEFINE_TYPE (GstVaapiOverlaySinkPad, gst_vaapi_overlay_sink_pad,
    GST_TYPE_VIDEO_AGGREGATOR_PAD);

typedef struct _GstVaapiOverlaySurfaceGenerator GstVaapiOverlaySurfaceGenerator;
struct _GstVaapiOverlaySurfaceGenerator
{
  GstVaapiOverlay *overlay;
  GList *current;
  GstVaapiBlendSurface blend_surface;
};

#define DEFAULT_PAD_XPOS   0
#define DEFAULT_PAD_YPOS   0
#define DEFAULT_PAD_ALPHA  1.0
#define DEFAULT_PAD_WIDTH  0
#define DEFAULT_PAD_HEIGHT 0

enum
{
  PROP_PAD_0,
  PROP_PAD_XPOS,
  PROP_PAD_YPOS,
  PROP_PAD_ALPHA,
  PROP_PAD_WIDTH,
  PROP_PAD_HEIGHT,
};

static void
gst_vaapi_overlay_sink_pad_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVaapiOverlaySinkPad *pad = GST_VAAPI_OVERLAY_SINK_PAD (object);

  switch (prop_id) {
    case PROP_PAD_XPOS:
      g_value_set_int (value, pad->xpos);
      break;
    case PROP_PAD_YPOS:
      g_value_set_int (value, pad->ypos);
      break;
    case PROP_PAD_ALPHA:
      g_value_set_double (value, pad->alpha);
      break;
    case PROP_PAD_WIDTH:
      g_value_set_int (value, pad->width);
      break;
    case PROP_PAD_HEIGHT:
      g_value_set_int (value, pad->height);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vaapi_overlay_sink_pad_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVaapiOverlaySinkPad *pad = GST_VAAPI_OVERLAY_SINK_PAD (object);

  switch (prop_id) {
    case PROP_PAD_XPOS:
      pad->xpos = g_value_get_int (value);
      break;
    case PROP_PAD_YPOS:
      pad->ypos = g_value_get_int (value);
      break;
    case PROP_PAD_ALPHA:
      pad->alpha = g_value_get_double (value);
      break;
    case PROP_PAD_WIDTH:
      pad->width = g_value_get_int (value);
      break;
    case PROP_PAD_HEIGHT:
      pad->height = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vaapi_overlay_sink_pad_finalize (GObject * object)
{
  gst_vaapi_pad_private_finalize (GST_VAAPI_OVERLAY_SINK_PAD (object)->priv);

  G_OBJECT_CLASS (gst_vaapi_overlay_sink_pad_parent_class)->finalize (object);
}

static void
gst_vaapi_overlay_sink_pad_class_init (GstVaapiOverlaySinkPadClass * klass)
{
  GObjectClass *const gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = gst_vaapi_overlay_sink_pad_finalize;
  gobject_class->set_property = gst_vaapi_overlay_sink_pad_set_property;
  gobject_class->get_property = gst_vaapi_overlay_sink_pad_get_property;

  g_object_class_install_property (gobject_class, PROP_PAD_XPOS,
      g_param_spec_int ("xpos", "X Position", "X Position of the picture",
          G_MININT, G_MAXINT, DEFAULT_PAD_XPOS,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PAD_YPOS,
      g_param_spec_int ("ypos", "Y Position", "Y Position of the picture",
          G_MININT, G_MAXINT, DEFAULT_PAD_YPOS,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PAD_ALPHA,
      g_param_spec_double ("alpha", "Alpha", "Alpha of the picture", 0.0, 1.0,
          DEFAULT_PAD_ALPHA,
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
}

static void
gst_vaapi_overlay_sink_pad_init (GstVaapiOverlaySinkPad * pad)
{
  pad->xpos = DEFAULT_PAD_XPOS;
  pad->ypos = DEFAULT_PAD_YPOS;
  pad->alpha = DEFAULT_PAD_ALPHA;
  pad->width = DEFAULT_PAD_WIDTH;
  pad->height = DEFAULT_PAD_HEIGHT;
  pad->priv = gst_vaapi_pad_private_new ();
}

static void
gst_vaapi_overlay_child_proxy_init (gpointer g_iface, gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (GstVaapiOverlay, gst_vaapi_overlay,
    GST_TYPE_VIDEO_AGGREGATOR, GST_VAAPI_PLUGIN_BASE_INIT_INTERFACES
    G_IMPLEMENT_INTERFACE (GST_TYPE_CHILD_PROXY,
        gst_vaapi_overlay_child_proxy_init));

GST_VAAPI_PLUGIN_BASE_DEFINE_SET_CONTEXT (gst_vaapi_overlay_parent_class);

static GstPad *
gst_vaapi_overlay_request_new_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * req_name, const GstCaps * caps)
{
  GstPad *newpad = GST_PAD (GST_ELEMENT_CLASS
      (gst_vaapi_overlay_parent_class)->request_new_pad (element, templ,
          req_name, caps));

  if (!newpad)
    GST_DEBUG_OBJECT (element, "could not create/add pad");
  else
    gst_child_proxy_child_added (GST_CHILD_PROXY (element), G_OBJECT (newpad),
        GST_OBJECT_NAME (newpad));

  return newpad;
}

static void
gst_vaapi_overlay_release_pad (GstElement * element, GstPad * pad)
{
  GstVaapiOverlay *const overlay = GST_VAAPI_OVERLAY (element);

  gst_child_proxy_child_removed (GST_CHILD_PROXY (overlay), G_OBJECT (pad),
      GST_OBJECT_NAME (pad));

  GST_ELEMENT_CLASS (gst_vaapi_overlay_parent_class)->release_pad (element,
      pad);
}

static inline gboolean
gst_vaapi_overlay_ensure_display (GstVaapiOverlay * overlay)
{
  return gst_vaapi_plugin_base_ensure_display (GST_VAAPI_PLUGIN_BASE (overlay));
}

static gboolean
gst_vaapi_overlay_sink_query (GstAggregator * agg, GstAggregatorPad * bpad,
    GstQuery * query)
{
  GstVaapiOverlay *const overlay = GST_VAAPI_OVERLAY (agg);

  if (GST_QUERY_TYPE (query) == GST_QUERY_CONTEXT) {
    if (gst_vaapi_handle_context_query (GST_ELEMENT (overlay), query)) {
      GST_DEBUG_OBJECT (overlay, "sharing display %" GST_PTR_FORMAT,
          GST_VAAPI_PLUGIN_BASE_DISPLAY (overlay));
      return TRUE;
    }
  } else if (GST_QUERY_TYPE (query) == GST_QUERY_ALLOCATION) {
    GstCaps *caps;

    gst_query_parse_allocation (query, &caps, NULL);

    if (caps == NULL)
      return FALSE;

    if (!gst_vaapi_plugin_base_pad_set_caps
        (GST_VAAPI_PLUGIN_BASE (overlay), GST_PAD (bpad), caps, NULL, NULL))
      return FALSE;
  }

  return GST_AGGREGATOR_CLASS (gst_vaapi_overlay_parent_class)->sink_query
      (agg, bpad, query);
}

static gboolean
gst_vaapi_overlay_src_query (GstAggregator * agg, GstQuery * query)
{
  GstVaapiOverlay *const overlay = GST_VAAPI_OVERLAY (agg);

  if (GST_QUERY_TYPE (query) == GST_QUERY_CONTEXT) {
    if (gst_vaapi_handle_context_query (GST_ELEMENT (overlay), query)) {
      GST_DEBUG_OBJECT (overlay, "sharing display %" GST_PTR_FORMAT,
          GST_VAAPI_PLUGIN_BASE_DISPLAY (overlay));
      return TRUE;
    }
  }

  return GST_AGGREGATOR_CLASS (gst_vaapi_overlay_parent_class)->src_query
      (agg, query);
}

static gboolean
gst_vaapi_overlay_start (GstAggregator * agg)
{
  GstVaapiOverlay *const overlay = GST_VAAPI_OVERLAY (agg);

  if (!gst_vaapi_plugin_base_open (GST_VAAPI_PLUGIN_BASE (overlay)))
    return FALSE;

  if (!gst_vaapi_overlay_ensure_display (overlay))
    return FALSE;

  overlay->blend =
      gst_vaapi_blend_new (GST_VAAPI_PLUGIN_BASE_DISPLAY (overlay));
  if (!overlay->blend)
    return FALSE;

  return TRUE;
}

static gboolean
_reset_sinkpad_private (GstElement * element, GstPad * pad, gpointer user_data)
{
  gst_vaapi_pad_private_reset (GST_VAAPI_OVERLAY_SINK_PAD (pad)->priv);

  return TRUE;
}

static gboolean
gst_vaapi_overlay_stop (GstAggregator * agg)
{
  GstVaapiOverlay *const overlay = GST_VAAPI_OVERLAY (agg);

  gst_vaapi_video_pool_replace (&overlay->blend_pool, NULL);
  gst_vaapi_blend_replace (&overlay->blend, NULL);

  gst_vaapi_plugin_base_close (GST_VAAPI_PLUGIN_BASE (overlay));

  gst_element_foreach_sink_pad (GST_ELEMENT (overlay), _reset_sinkpad_private,
      NULL);

  return TRUE;
}

static void
gst_vaapi_overlay_destroy (GstVaapiOverlay * const overlay)
{
  gst_vaapi_plugin_base_close (GST_VAAPI_PLUGIN_BASE (overlay));
  gst_element_foreach_sink_pad (GST_ELEMENT (overlay), _reset_sinkpad_private,
      NULL);
}

static void
gst_vaapi_overlay_finalize (GObject * object)
{
  GstVaapiOverlay *const overlay = GST_VAAPI_OVERLAY (object);

  gst_vaapi_overlay_destroy (overlay);
  gst_vaapi_plugin_base_finalize (GST_VAAPI_PLUGIN_BASE (overlay));

  G_OBJECT_CLASS (gst_vaapi_overlay_parent_class)->finalize (object);
}

static gboolean
gst_vaapi_overlay_propose_allocation (GstAggregator * agg,
    GstAggregatorPad * pad, GstQuery * decide_query, GstQuery * query)
{
  return gst_vaapi_plugin_base_pad_propose_allocation
      (GST_VAAPI_PLUGIN_BASE (agg), GST_PAD (pad), query);
}

static gboolean
gst_vaapi_overlay_decide_allocation (GstAggregator * agg, GstQuery * query)
{
  return gst_vaapi_plugin_base_decide_allocation
      (GST_VAAPI_PLUGIN_BASE (agg), query);
}

static GstVaapiBlendSurface *
gst_vaapi_overlay_surface_next (gpointer data)
{
  GstVaapiOverlaySurfaceGenerator *generator;
  GstVideoAggregatorPad *vagg_pad;
  GstVaapiOverlaySinkPad *pad;
  GstVideoFrame *inframe;
  GstBuffer *inbuf;
  GstBuffer *buf;
  GstVaapiVideoMeta *inbuf_meta;
  GstVaapiBlendSurface *blend_surface;

  generator = (GstVaapiOverlaySurfaceGenerator *) data;

  /* at the end of the generator? */
  while (generator->current) {
    /* get the current video aggregator sinkpad */
    vagg_pad = GST_VIDEO_AGGREGATOR_PAD (generator->current->data);

    /* increment list pointer */
    generator->current = generator->current->next;

    /* recycle the blend surface from the overlay surface generator */
    blend_surface = &generator->blend_surface;
    blend_surface->surface = NULL;

    /* Current sinkpad may not be queueing buffers yet (e.g. timestamp-offset)
     * or it may have reached EOS */
    if (!gst_video_aggregator_pad_has_current_buffer (vagg_pad))
      continue;

    inframe = gst_video_aggregator_pad_get_prepared_frame (vagg_pad);
    buf = gst_video_aggregator_pad_get_current_buffer (vagg_pad);
    pad = GST_VAAPI_OVERLAY_SINK_PAD (vagg_pad);

    if (gst_vaapi_plugin_base_pad_get_input_buffer (GST_VAAPI_PLUGIN_BASE
            (generator->overlay), GST_PAD (pad), buf, &inbuf) != GST_FLOW_OK)
      return blend_surface;

    inbuf_meta = gst_buffer_get_vaapi_video_meta (inbuf);
    if (inbuf_meta) {
      blend_surface->surface = gst_vaapi_video_meta_get_surface (inbuf_meta);
      blend_surface->crop = gst_vaapi_video_meta_get_render_rect (inbuf_meta);
      blend_surface->target.x = pad->xpos;
      blend_surface->target.y = pad->ypos;
      blend_surface->target.width = (pad->width == DEFAULT_PAD_WIDTH)
          ? GST_VIDEO_FRAME_WIDTH (inframe) : pad->width;
      blend_surface->target.height = (pad->height == DEFAULT_PAD_HEIGHT)
          ? GST_VIDEO_FRAME_HEIGHT (inframe) : pad->height;
      blend_surface->alpha = pad->alpha;
    }

    gst_buffer_unref (inbuf);
    return blend_surface;
  }

  return NULL;
}

static GstFlowReturn
gst_vaapi_overlay_aggregate_frames (GstVideoAggregator * vagg,
    GstBuffer * outbuf)
{
  GstVaapiOverlay *const overlay = GST_VAAPI_OVERLAY (vagg);
  GstVaapiVideoMeta *outbuf_meta;
  GstVaapiSurface *outbuf_surface;
  GstVaapiSurfaceProxy *proxy;
  GstVaapiOverlaySurfaceGenerator generator;

  if (!overlay->blend_pool) {
    GstVaapiVideoPool *pool =
        gst_vaapi_surface_pool_new_full (GST_VAAPI_PLUGIN_BASE_DISPLAY
        (overlay),
        GST_VAAPI_PLUGIN_BASE_SRC_PAD_INFO (overlay), 0);
    if (!pool)
      return GST_FLOW_ERROR;
    gst_vaapi_video_pool_replace (&overlay->blend_pool, pool);
    gst_vaapi_video_pool_unref (pool);
  }

  outbuf_meta = gst_buffer_get_vaapi_video_meta (outbuf);
  if (!outbuf_meta)
    return GST_FLOW_ERROR;

  if (!gst_vaapi_video_meta_get_surface_proxy (outbuf_meta)) {
    proxy = gst_vaapi_surface_proxy_new_from_pool
        (GST_VAAPI_SURFACE_POOL (overlay->blend_pool));
    if (!proxy)
      return GST_FLOW_ERROR;
    gst_vaapi_video_meta_set_surface_proxy (outbuf_meta, proxy);
    gst_vaapi_surface_proxy_unref (proxy);
  }

  outbuf_surface = gst_vaapi_video_meta_get_surface (outbuf_meta);

  /* initialize the surface generator */
  generator.overlay = overlay;
  generator.current = GST_ELEMENT (overlay)->sinkpads;

  if (!gst_vaapi_blend_process (overlay->blend, outbuf_surface,
          gst_vaapi_overlay_surface_next, &generator))
    return GST_FLOW_ERROR;

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_vaapi_overlay_create_output_buffer (GstVideoAggregator * vagg,
    GstBuffer ** outbuf)
{
  GstVaapiOverlay *const overlay = GST_VAAPI_OVERLAY (vagg);
  GstBufferPool *const pool =
      GST_VAAPI_PLUGIN_BASE_SRC_PAD_BUFFER_POOL (overlay);

  g_return_val_if_fail (pool != NULL, GST_FLOW_ERROR);

  if (!gst_buffer_pool_is_active (pool) &&
      !gst_buffer_pool_set_active (pool, TRUE)) {
    GST_ERROR_OBJECT (overlay, "failed to activate output video buffer pool");
    return GST_FLOW_ERROR;
  }

  *outbuf = NULL;
  if ((gst_buffer_pool_acquire_buffer (pool, outbuf, NULL) != GST_FLOW_OK)
      || !*outbuf) {
    GST_ERROR_OBJECT (overlay, "failed to create output video buffer");
    return GST_FLOW_ERROR;
  }

  return GST_FLOW_OK;
}

static gboolean
gst_vaapi_overlay_negotiated_src_caps (GstAggregator * agg, GstCaps * caps)
{
  if (!gst_vaapi_plugin_base_set_caps (GST_VAAPI_PLUGIN_BASE (agg), NULL, caps))
    return FALSE;

  return
      GST_AGGREGATOR_CLASS (gst_vaapi_overlay_parent_class)->negotiated_src_caps
      (agg, caps);
}

static GstCaps *
gst_vaapi_overlay_fixate_src_caps (GstAggregator * agg, GstCaps * caps)
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
    GstVaapiOverlaySinkPad *pad = GST_VAAPI_OVERLAY_SINK_PAD (vaggpad);
    gint this_width, this_height;
    gint fps_n, fps_d;
    gdouble cur_fps;

    fps_n = GST_VIDEO_INFO_FPS_N (&vaggpad->info);
    fps_d = GST_VIDEO_INFO_FPS_D (&vaggpad->info);

    this_width = (pad->width == DEFAULT_PAD_WIDTH)
        ? GST_VIDEO_INFO_WIDTH (&vaggpad->info) : pad->width;
    this_height = (pad->height == DEFAULT_PAD_HEIGHT)
        ? GST_VIDEO_INFO_HEIGHT (&vaggpad->info) : pad->height;

    this_width += MAX (pad->xpos, 0);
    this_height += MAX (pad->ypos, 0);

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
  gst_structure_fixate_field_nearest_fraction (s, "framerate", best_fps_n,
      best_fps_d);

  return gst_caps_fixate (ret);
}

static GstVaapiPadPrivate *
gst_vaapi_overlay_get_vaapi_pad_private (GstVaapiPluginBase * plugin,
    GstPad * pad)
{
  if (GST_IS_VAAPI_OVERLAY_SINK_PAD (pad))
    return GST_VAAPI_OVERLAY_SINK_PAD (pad)->priv;

  g_assert (GST_VAAPI_PLUGIN_BASE_SRC_PAD (plugin) == pad);
  return GST_VAAPI_PLUGIN_BASE_SRC_PAD_PRIVATE (plugin);
}

static void
gst_vaapi_overlay_class_init (GstVaapiOverlayClass * klass)
{
  GObjectClass *const object_class = G_OBJECT_CLASS (klass);
  GstElementClass *const element_class = GST_ELEMENT_CLASS (klass);
  GstAggregatorClass *const agg_class = GST_AGGREGATOR_CLASS (klass);
  GstVideoAggregatorClass *const vagg_class =
      GST_VIDEO_AGGREGATOR_CLASS (klass);
  GstVaapiPluginBaseClass *plugin_class = GST_VAAPI_PLUGIN_BASE_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (gst_debug_vaapi_overlay,
      GST_PLUGIN_NAME, 0, GST_PLUGIN_DESC);

  gst_vaapi_plugin_base_class_init (plugin_class);
  plugin_class->get_vaapi_pad_private =
      GST_DEBUG_FUNCPTR (gst_vaapi_overlay_get_vaapi_pad_private);

  object_class->finalize = GST_DEBUG_FUNCPTR (gst_vaapi_overlay_finalize);

  agg_class->sink_query = GST_DEBUG_FUNCPTR (gst_vaapi_overlay_sink_query);
  agg_class->src_query = GST_DEBUG_FUNCPTR (gst_vaapi_overlay_src_query);
  agg_class->start = GST_DEBUG_FUNCPTR (gst_vaapi_overlay_start);
  agg_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_vaapi_overlay_propose_allocation);
  agg_class->fixate_src_caps =
      GST_DEBUG_FUNCPTR (gst_vaapi_overlay_fixate_src_caps);
  agg_class->negotiated_src_caps =
      GST_DEBUG_FUNCPTR (gst_vaapi_overlay_negotiated_src_caps);
  agg_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_vaapi_overlay_decide_allocation);
  agg_class->stop = GST_DEBUG_FUNCPTR (gst_vaapi_overlay_stop);

  vagg_class->aggregate_frames =
      GST_DEBUG_FUNCPTR (gst_vaapi_overlay_aggregate_frames);
  vagg_class->create_output_buffer =
      GST_DEBUG_FUNCPTR (gst_vaapi_overlay_create_output_buffer);

  element_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_vaapi_overlay_request_new_pad);
  element_class->release_pad =
      GST_DEBUG_FUNCPTR (gst_vaapi_overlay_release_pad);
  element_class->set_context = GST_DEBUG_FUNCPTR (gst_vaapi_base_set_context);

  gst_element_class_add_static_pad_template_with_gtype (element_class,
      &gst_vaapi_overlay_sink_factory, GST_TYPE_VAAPI_OVERLAY_SINK_PAD);

  gst_element_class_add_static_pad_template_with_gtype (element_class,
      &gst_vaapi_overlay_src_factory, GST_TYPE_AGGREGATOR_PAD);

  gst_element_class_set_static_metadata (element_class,
      "VA-API overlay",
      "Filter/Editor/Video/Compositor/Hardware",
      GST_PLUGIN_DESC, "U. Artie Eoff <ullysses.a.eoff@intel.com>");
}

static void
gst_vaapi_overlay_init (GstVaapiOverlay * overlay)
{
  gst_vaapi_plugin_base_init (GST_VAAPI_PLUGIN_BASE (overlay), GST_CAT_DEFAULT);
}

/* GstChildProxy implementation */
static GObject *
gst_vaapi_overlay_child_proxy_get_child_by_index (GstChildProxy * child_proxy,
    guint index)
{
  GstVaapiOverlay *overlay = GST_VAAPI_OVERLAY (child_proxy);
  GObject *obj = NULL;

  GST_OBJECT_LOCK (overlay);
  obj = g_list_nth_data (GST_ELEMENT_CAST (overlay)->sinkpads, index);
  if (obj)
    gst_object_ref (obj);
  GST_OBJECT_UNLOCK (overlay);

  return obj;
}

static guint
gst_vaapi_overlay_child_proxy_get_children_count (GstChildProxy * child_proxy)
{
  guint count = 0;
  GstVaapiOverlay *overlay = GST_VAAPI_OVERLAY (child_proxy);

  GST_OBJECT_LOCK (overlay);
  count = GST_ELEMENT_CAST (overlay)->numsinkpads;
  GST_OBJECT_UNLOCK (overlay);

  return count;
}

static void
gst_vaapi_overlay_child_proxy_init (gpointer g_iface, gpointer iface_data)
{
  GstChildProxyInterface *iface = g_iface;

  iface->get_child_by_index = gst_vaapi_overlay_child_proxy_get_child_by_index;
  iface->get_children_count = gst_vaapi_overlay_child_proxy_get_children_count;
}

gboolean
gst_vaapioverlay_register (GstPlugin * plugin, GstVaapiDisplay * display)
{
  GstVaapiBlend *blend = NULL;

  blend = gst_vaapi_blend_new (display);
  if (!blend)
    return FALSE;
  gst_vaapi_blend_replace (&blend, NULL);

  return gst_element_register (plugin, "vaapioverlay", GST_RANK_NONE,
      GST_TYPE_VAAPI_OVERLAY);
}
