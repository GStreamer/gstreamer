/*
 * GStreamer
 * Copyright (C) 2015 Matthew Waters <matthew@centricular.com>
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
 * SECTION:element-vulkansink
 * @title: vulkansink
 *
 * vulkansink renders video frames to a drawable on a local or remote
 * display using Vulkan.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

//#include <gst/video/videooverlay.h>

#include "vksink.h"
#include "vkdevice.h"

GST_DEBUG_CATEGORY (gst_debug_vulkan_sink);
#define GST_CAT_DEFAULT gst_debug_vulkan_sink

#define DEFAULT_FORCE_ASPECT_RATIO TRUE
#define DEFAULT_PIXEL_ASPECT_RATIO_N 0
#define DEFAULT_PIXEL_ASPECT_RATIO_D 1

static void gst_vulkan_sink_finalize (GObject * object);
static void gst_vulkan_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * param_spec);
static void gst_vulkan_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * param_spec);

static gboolean gst_vulkan_sink_query (GstBaseSink * bsink, GstQuery * query);
static void gst_vulkan_sink_set_context (GstElement * element,
    GstContext * context);

static GstStateChangeReturn
gst_vulkan_sink_change_state (GstElement * element, GstStateChange transition);

static void gst_vulkan_sink_get_times (GstBaseSink * bsink, GstBuffer * buf,
    GstClockTime * start, GstClockTime * end);
static gboolean gst_vulkan_sink_set_caps (GstBaseSink * bsink, GstCaps * caps);
static GstCaps *gst_vulkan_sink_get_caps (GstBaseSink * bsink,
    GstCaps * filter);
static GstFlowReturn gst_vulkan_sink_prepare (GstBaseSink * bsink,
    GstBuffer * buf);
static GstFlowReturn gst_vulkan_sink_show_frame (GstVideoSink * bsink,
    GstBuffer * buf);

static GstStaticPadTemplate gst_vulkan_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_VULKAN_BUFFER,
            GST_VULKAN_SWAPPER_VIDEO_FORMATS)));

enum
{
  PROP_0,
  PROP_FORCE_ASPECT_RATIO,
  PROP_PIXEL_ASPECT_RATIO,
};

enum
{
  SIGNAL_0,
  LAST_SIGNAL
};

/* static guint gst_vulkan_sink_signals[LAST_SIGNAL] = { 0 }; */

#define gst_vulkan_sink_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstVulkanSink, gst_vulkan_sink,
    GST_TYPE_VIDEO_SINK, GST_DEBUG_CATEGORY_INIT (gst_debug_vulkan_sink,
        "vulkansink", 0, "Vulkan Video Sink"));

static void
gst_vulkan_sink_class_init (GstVulkanSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstbasesink_class;
  GstVideoSinkClass *gstvideosink_class;
  GstElementClass *element_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;
  gstvideosink_class = (GstVideoSinkClass *) klass;
  element_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_vulkan_sink_set_property;
  gobject_class->get_property = gst_vulkan_sink_get_property;

  g_object_class_install_property (gobject_class, PROP_FORCE_ASPECT_RATIO,
      g_param_spec_boolean ("force-aspect-ratio", "Force aspect ratio",
          "When enabled, scaling will respect original aspect ratio",
          DEFAULT_FORCE_ASPECT_RATIO,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_PIXEL_ASPECT_RATIO,
      gst_param_spec_fraction ("pixel-aspect-ratio", "Pixel Aspect Ratio",
          "The pixel aspect ratio of the device", 0, 1, G_MAXINT, 1, 1, 1,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_metadata (element_class, "Vulkan video sink",
      "Sink/Video", "A videosink based on OpenGL",
      "Matthew Waters <matthew@centricular.com>");

  gst_element_class_add_static_pad_template (element_class,
      &gst_vulkan_sink_template);

  gobject_class->finalize = gst_vulkan_sink_finalize;

  gstelement_class->change_state = gst_vulkan_sink_change_state;
  gstelement_class->set_context = gst_vulkan_sink_set_context;
  gstbasesink_class->query = GST_DEBUG_FUNCPTR (gst_vulkan_sink_query);
  gstbasesink_class->set_caps = gst_vulkan_sink_set_caps;
  gstbasesink_class->get_caps = gst_vulkan_sink_get_caps;
  gstbasesink_class->get_times = gst_vulkan_sink_get_times;
  gstbasesink_class->prepare = gst_vulkan_sink_prepare;

  gstvideosink_class->show_frame =
      GST_DEBUG_FUNCPTR (gst_vulkan_sink_show_frame);
}

static void
gst_vulkan_sink_init (GstVulkanSink * vk_sink)
{
  vk_sink->force_aspect_ratio = DEFAULT_FORCE_ASPECT_RATIO;
  vk_sink->par_n = DEFAULT_PIXEL_ASPECT_RATIO_N;
  vk_sink->par_d = DEFAULT_PIXEL_ASPECT_RATIO_D;

//  g_mutex_init (&vk_sink->drawing_lock);
}

static void
gst_vulkan_sink_finalize (GObject * object)
{
//  GstVulkanSink *vk_sink = GST_VULKAN_SINK (object);
//  g_mutex_clear (&vk_sink->drawing_lock);

//  GST_DEBUG ("finalized");
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_vulkan_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVulkanSink *vk_sink = GST_VULKAN_SINK (object);

  switch (prop_id) {
    case PROP_FORCE_ASPECT_RATIO:
      vk_sink->force_aspect_ratio = g_value_get_boolean (value);
      break;
    case PROP_PIXEL_ASPECT_RATIO:
      vk_sink->par_n = gst_value_get_fraction_numerator (value);
      vk_sink->par_d = gst_value_get_fraction_denominator (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vulkan_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVulkanSink *vk_sink = GST_VULKAN_SINK (object);

  switch (prop_id) {
    case PROP_FORCE_ASPECT_RATIO:
      g_value_set_boolean (value, vk_sink->force_aspect_ratio);
      break;
    case PROP_PIXEL_ASPECT_RATIO:
      gst_value_set_fraction (value, vk_sink->par_n, vk_sink->par_d);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_vulkan_sink_query (GstBaseSink * bsink, GstQuery * query)
{
  GstVulkanSink *vk_sink = GST_VULKAN_SINK (bsink);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:{
      if (gst_vulkan_handle_context_query (GST_ELEMENT (vk_sink), query,
              &vk_sink->display, &vk_sink->instance, &vk_sink->device))
        return TRUE;

      break;
    }
    default:
      break;
  }

  return GST_BASE_SINK_CLASS (parent_class)->query (bsink, query);
}

static void
gst_vulkan_sink_set_context (GstElement * element, GstContext * context)
{
  GstVulkanSink *vk_sink = GST_VULKAN_SINK (element);

  gst_vulkan_handle_set_context (element, context, &vk_sink->display,
      &vk_sink->instance);

  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);
}

static GstStateChangeReturn
gst_vulkan_sink_change_state (GstElement * element, GstStateChange transition)
{
  GstVulkanSink *vk_sink = GST_VULKAN_SINK (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GError *error = NULL;

  GST_DEBUG ("changing state: %s => %s",
      gst_element_state_get_name (GST_STATE_TRANSITION_CURRENT (transition)),
      gst_element_state_get_name (GST_STATE_TRANSITION_NEXT (transition)));

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_vulkan_ensure_element_data (element, &vk_sink->display,
              &vk_sink->instance)) {
        GST_ELEMENT_ERROR (vk_sink, RESOURCE, NOT_FOUND,
            ("Failed to retrieve vulkan instance/display"), (NULL));
        return GST_STATE_CHANGE_FAILURE;
      }

      if (!(vk_sink->device =
              gst_vulkan_instance_create_device (vk_sink->instance, &error))) {
        GST_ELEMENT_ERROR (vk_sink, RESOURCE, NOT_FOUND,
            ("Failed to create vulkan device"), ("%s", error->message));
        g_clear_error (&error);
        return GST_STATE_CHANGE_FAILURE;
      }

      if (!(vk_sink->window =
              gst_vulkan_display_create_window (vk_sink->display))) {
        GST_ELEMENT_ERROR (vk_sink, RESOURCE, NOT_FOUND,
            ("Failed to create a window"), (NULL));
        return GST_STATE_CHANGE_FAILURE;
      }

      if (!gst_vulkan_window_open (vk_sink->window, &error)) {
        GST_ELEMENT_ERROR (vk_sink, RESOURCE, NOT_FOUND,
            ("Failed to open window"), ("%s", error->message));
        g_clear_error (&error);
        return GST_STATE_CHANGE_FAILURE;
      }

      if (!(vk_sink->swapper =
              gst_vulkan_swapper_new (vk_sink->device, vk_sink->window))) {
        GST_ELEMENT_ERROR (vk_sink, RESOURCE, NOT_FOUND,
            ("Failed to create a swapper"), (NULL));
        return GST_STATE_CHANGE_FAILURE;
      }
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      if (vk_sink->swapper)
        gst_object_unref (vk_sink->swapper);
      vk_sink->swapper = NULL;
      if (vk_sink->display)
        gst_object_unref (vk_sink->display);
      vk_sink->display = NULL;
      if (vk_sink->window) {
        gst_vulkan_window_close (vk_sink->window);
        gst_object_unref (vk_sink->window);
      }
      vk_sink->window = NULL;
      if (vk_sink->device)
        gst_object_unref (vk_sink->device);
      vk_sink->device = NULL;
      if (vk_sink->instance)
        gst_object_unref (vk_sink->instance);
      vk_sink->instance = NULL;
      break;
    default:
      break;
  }

  return ret;
}

static void
gst_vulkan_sink_get_times (GstBaseSink * bsink, GstBuffer * buf,
    GstClockTime * start, GstClockTime * end)
{
  GstVulkanSink *vk_sink = GST_VULKAN_SINK (bsink);

  if (GST_BUFFER_TIMESTAMP_IS_VALID (buf)) {
    *start = GST_BUFFER_TIMESTAMP (buf);
    if (GST_BUFFER_DURATION_IS_VALID (buf))
      *end = *start + GST_BUFFER_DURATION (buf);
    else {
      if (GST_VIDEO_INFO_FPS_N (&vk_sink->v_info) > 0) {
        *end = *start +
            gst_util_uint64_scale_int (GST_SECOND,
            GST_VIDEO_INFO_FPS_D (&vk_sink->v_info),
            GST_VIDEO_INFO_FPS_N (&vk_sink->v_info));
      }
    }
  }
}

static GstCaps *
gst_vulkan_sink_get_caps (GstBaseSink * bsink, GstCaps * filter)
{
  GstVulkanSink *vk_sink = GST_VULKAN_SINK (bsink);
  GstCaps *tmp = NULL;
  GstCaps *result = NULL;
  GError *error = NULL;

  if (vk_sink->swapper) {
    if (!(result =
            gst_vulkan_swapper_get_supported_caps (vk_sink->swapper, &error))) {
      GST_ELEMENT_ERROR (bsink, RESOURCE, NOT_FOUND, ("%s", error->message),
          (NULL));
      g_clear_error (&error);
      return NULL;
    }
    return result;
  }

  tmp = gst_pad_get_pad_template_caps (GST_BASE_SINK_PAD (bsink));

  if (filter) {
    GST_DEBUG_OBJECT (bsink, "intersecting with filter caps %" GST_PTR_FORMAT,
        filter);

    result = gst_caps_intersect_full (filter, tmp, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (tmp);
  } else {
    result = tmp;
  }

  GST_DEBUG_OBJECT (bsink, "returning caps: %" GST_PTR_FORMAT, result);

  return result;
}

static gboolean
_configure_display_from_info (GstVulkanSink * vk_sink, GstVideoInfo * vinfo)
{
  guint display_ratio_num, display_ratio_den;
  gint display_par_n, display_par_d;
  gint par_n, par_d;
  gint width, height;
  gboolean ok;

  width = GST_VIDEO_INFO_WIDTH (vinfo);
  height = GST_VIDEO_INFO_HEIGHT (vinfo);

  par_n = GST_VIDEO_INFO_PAR_N (vinfo);
  par_d = GST_VIDEO_INFO_PAR_D (vinfo);

  if (!par_n)
    par_n = 1;

  /* get display's PAR */
  if (vk_sink->par_n != 0 && vk_sink->par_d != 0) {
    display_par_n = vk_sink->par_n;
    display_par_d = vk_sink->par_d;
  } else {
    display_par_n = 1;
    display_par_d = 1;
  }

  ok = gst_video_calculate_display_ratio (&display_ratio_num,
      &display_ratio_den, width, height, par_n, par_d, display_par_n,
      display_par_d);

  if (!ok)
    return FALSE;

  GST_TRACE ("PAR: %u/%u DAR:%u/%u", par_n, par_d, display_par_n,
      display_par_d);

  if (height % display_ratio_den == 0) {
    GST_DEBUG ("keeping video height");
    GST_VIDEO_SINK_WIDTH (vk_sink) = (guint)
        gst_util_uint64_scale_int (height, display_ratio_num,
        display_ratio_den);
    GST_VIDEO_SINK_HEIGHT (vk_sink) = height;
  } else if (width % display_ratio_num == 0) {
    GST_DEBUG ("keeping video width");
    GST_VIDEO_SINK_WIDTH (vk_sink) = width;
    GST_VIDEO_SINK_HEIGHT (vk_sink) = (guint)
        gst_util_uint64_scale_int (width, display_ratio_den, display_ratio_num);
  } else {
    GST_DEBUG ("approximating while keeping video height");
    GST_VIDEO_SINK_WIDTH (vk_sink) = (guint)
        gst_util_uint64_scale_int (height, display_ratio_num,
        display_ratio_den);
    GST_VIDEO_SINK_HEIGHT (vk_sink) = height;
  }
  GST_DEBUG ("scaling to %dx%d", GST_VIDEO_SINK_WIDTH (vk_sink),
      GST_VIDEO_SINK_HEIGHT (vk_sink));

  return TRUE;
}

static gboolean
gst_vulkan_sink_set_caps (GstBaseSink * bsink, GstCaps * caps)
{
  GstVulkanSink *vk_sink = GST_VULKAN_SINK (bsink);
  GError *error = NULL;
  GstVideoInfo v_info;

  GST_DEBUG_OBJECT (bsink, "set caps with %" GST_PTR_FORMAT, caps);

  if (!gst_video_info_from_caps (&v_info, caps))
    return FALSE;

  if (!_configure_display_from_info (vk_sink, &v_info))
    return FALSE;

  if (!gst_vulkan_swapper_set_caps (vk_sink->swapper, caps, &error)) {
    GST_ELEMENT_ERROR (vk_sink, RESOURCE, NOT_FOUND,
        ("Failed to configure caps"), ("%s", error->message));
    g_clear_error (&error);
    return FALSE;
  }

  vk_sink->v_info = v_info;

  return TRUE;
}

static GstFlowReturn
gst_vulkan_sink_prepare (GstBaseSink * bsink, GstBuffer * buf)
{
  GstVulkanSink *vk_sink = GST_VULKAN_SINK (bsink);

  GST_TRACE_OBJECT (vk_sink, "preparing buffer %" GST_PTR_FORMAT, buf);

  if (GST_VIDEO_SINK_WIDTH (vk_sink) < 1 || GST_VIDEO_SINK_HEIGHT (vk_sink) < 1) {
    return GST_FLOW_NOT_NEGOTIATED;
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_vulkan_sink_show_frame (GstVideoSink * vsink, GstBuffer * buf)
{
  GstVulkanSink *vk_sink = GST_VULKAN_SINK (vsink);
  GError *error = NULL;

  GST_TRACE_OBJECT (vk_sink, "rendering buffer %" GST_PTR_FORMAT, buf);

  if (!gst_vulkan_swapper_render_buffer (vk_sink->swapper, buf, &error)) {
    GST_ELEMENT_ERROR (vk_sink, RESOURCE, NOT_FOUND,
        ("Failed to render buffer"), ("%s", error->message));
    g_clear_error (&error);
    return GST_FLOW_ERROR;
  }

  return GST_FLOW_OK;
}
