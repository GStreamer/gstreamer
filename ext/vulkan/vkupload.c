/*
 * GStreamer
 * Copyright (C) 2016 Matthew Waters <matthew@centricular.com>
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
 * SECTION:element-vulkanupload
 *
 * vulkanupload uploads data into Vulkan memory objects.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "vkupload.h"

GST_DEBUG_CATEGORY (gst_debug_vulkan_upload);
#define GST_CAT_DEFAULT gst_debug_vulkan_upload

static void gst_vulkan_upload_finalize (GObject * object);
static void gst_vulkan_upload_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * param_spec);
static void gst_vulkan_upload_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * param_spec);

static gboolean gst_vulkan_upload_query (GstBaseTransform * bt,
    GstPadDirection direction, GstQuery * query);
static void gst_vulkan_upload_set_context (GstElement * element,
    GstContext * context);
static GstStateChangeReturn gst_vulkan_upload_change_state (GstElement *
    element, GstStateChange transition);

static gboolean gst_vulkan_upload_set_caps (GstBaseTransform * bt,
    GstCaps * in_caps, GstCaps * out_caps);
static GstCaps *gst_vulkan_upload_transform_caps (GstBaseTransform * bt,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static GstFlowReturn gst_vulkan_upload_transform (GstBaseTransform * bt,
    GstBuffer * inbuf, GstBuffer * outbuf);
static GstFlowReturn gst_vulkan_upload_prepare_output_buffer (GstBaseTransform *
    bt, GstBuffer * inbuf, GstBuffer ** outbuf);

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw"));

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw(memory:"
        GST_VULKAN_BUFFER_MEMORY_ALLOCATOR_NAME ") ; " "video/x-raw"));

enum
{
  PROP_0,
};

enum
{
  SIGNAL_0,
  LAST_SIGNAL
};

/* static guint gst_vulkan_upload_signals[LAST_SIGNAL] = { 0 }; */

#define gst_vulkan_upload_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstVulkanUpload, gst_vulkan_upload,
    GST_TYPE_BASE_TRANSFORM, GST_DEBUG_CATEGORY_INIT (gst_debug_vulkan_upload,
        "vulkanupload", 0, "Vulkan Uploader"));

static void
gst_vulkan_upload_class_init (GstVulkanUploadClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseTransformClass *gstbasetransform_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasetransform_class = (GstBaseTransformClass *) klass;

  gobject_class->set_property = gst_vulkan_upload_set_property;
  gobject_class->get_property = gst_vulkan_upload_get_property;

  gst_element_class_set_metadata (gstelement_class, "Vulkan Uploader",
      "Filter/Video", "A Vulkan data uploader",
      "Matthew Waters <matthew@centricular.com>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_template));

  gobject_class->finalize = gst_vulkan_upload_finalize;

  gstelement_class->change_state = gst_vulkan_upload_change_state;
  gstelement_class->set_context = gst_vulkan_upload_set_context;
  gstbasetransform_class->query = GST_DEBUG_FUNCPTR (gst_vulkan_upload_query);
  gstbasetransform_class->set_caps = gst_vulkan_upload_set_caps;
  gstbasetransform_class->transform_caps = gst_vulkan_upload_transform_caps;
  gstbasetransform_class->transform = gst_vulkan_upload_transform;
  gstbasetransform_class->prepare_output_buffer =
      gst_vulkan_upload_prepare_output_buffer;
}

static void
gst_vulkan_upload_init (GstVulkanUpload * vk_upload)
{
}

static void
gst_vulkan_upload_finalize (GObject * object)
{
//  GstVulkanUpload *vk_upload = GST_VULKAN_UPLOAD (object);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_vulkan_upload_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
//  GstVulkanUpload *vk_upload = GST_VULKAN_UPLOAD (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vulkan_upload_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
//  GstVulkanUpload *vk_upload = GST_VULKAN_UPLOAD (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_vulkan_upload_query (GstBaseTransform * bt, GstPadDirection direction,
    GstQuery * query)
{
  GstVulkanUpload *vk_upload = GST_VULKAN_UPLOAD (bt);
  gboolean res = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:{
      res = gst_vulkan_handle_context_query (GST_ELEMENT (vk_upload), query,
          &vk_upload->display, &vk_upload->instance, &vk_upload->device);

      if (res)
        return res;
      break;
    }
    default:
      break;
  }

  return GST_BASE_TRANSFORM_CLASS (parent_class)->query (bt, direction, query);
}

static void
gst_vulkan_upload_set_context (GstElement * element, GstContext * context)
{
  GstVulkanUpload *vk_upload = GST_VULKAN_UPLOAD (element);

  gst_vulkan_handle_set_context (element, context, &vk_upload->display,
      &vk_upload->instance);

  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);
}

static gboolean
_find_vulkan_device (GstVulkanUpload * upload)
{
  /* Requires the instance to exist */
  GstQuery *query;
  GstContext *context;
  const GstStructure *s;

  if (upload->device)
    return TRUE;

  query = gst_query_new_context ("gst.vulkan.device");
  if (!upload->device
      && gst_vulkan_run_query (GST_ELEMENT (upload), query, GST_PAD_SRC)) {
    gst_query_parse_context (query, &context);
    if (context) {
      s = gst_context_get_structure (context);
      gst_structure_get (s, "device", GST_TYPE_VULKAN_DEVICE, &upload->device,
          NULL);
    }
  }
  if (!upload->device
      && gst_vulkan_run_query (GST_ELEMENT (upload), query, GST_PAD_SINK)) {
    gst_query_parse_context (query, &context);
    if (context) {
      s = gst_context_get_structure (context);
      gst_structure_get (s, "device", GST_TYPE_VULKAN_DEVICE, &upload->device,
          NULL);
    }
  }

  GST_DEBUG_OBJECT (upload, "found device %p", upload->device);

  gst_query_unref (query);

  if (upload->device)
    return TRUE;

  return FALSE;
}

static GstStateChangeReturn
gst_vulkan_upload_change_state (GstElement * element, GstStateChange transition)
{
  GstVulkanUpload *vk_upload = GST_VULKAN_UPLOAD (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  GST_DEBUG ("changing state: %s => %s",
      gst_element_state_get_name (GST_STATE_TRANSITION_CURRENT (transition)),
      gst_element_state_get_name (GST_STATE_TRANSITION_NEXT (transition)));

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (!gst_vulkan_ensure_element_data (element, &vk_upload->display,
              &vk_upload->instance)) {
        GST_ELEMENT_ERROR (vk_upload, RESOURCE, NOT_FOUND,
            ("Failed to retreive vulkan instance/display"), (NULL));
        return GST_STATE_CHANGE_FAILURE;
      }
      if (!_find_vulkan_device (vk_upload)) {
        GST_ELEMENT_ERROR (vk_upload, RESOURCE, NOT_FOUND,
            ("Failed to retreive vulkan device"), (NULL));
        return GST_STATE_CHANGE_FAILURE;
      }
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
      if (vk_upload->display)
        gst_object_unref (vk_upload->display);
      vk_upload->display = NULL;
      if (vk_upload->device)
        gst_object_unref (vk_upload->device);
      vk_upload->device = NULL;
      if (vk_upload->instance)
        gst_object_unref (vk_upload->instance);
      vk_upload->instance = NULL;
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}

static GstCaps *
gst_vulkan_upload_transform_caps (GstBaseTransform * bt,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
//  GstVulkanUpload *vk_upload = GST_VULKAN_UPLOAD (bt);
  GstCaps *tmp = NULL;
  GstCaps *result = NULL;

  tmp = gst_caps_copy (caps);

  if (direction == GST_PAD_SINK) {
    gst_caps_set_features (tmp, 0,
        gst_caps_features_from_string (GST_CAPS_FEATURE_MEMORY_VULKAN_BUFFER));
  } else {
    gst_caps_set_features (tmp, 0,
        gst_caps_features_from_string (GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY));
  }

  if (filter) {
    GST_DEBUG_OBJECT (bt, "intersecting with filter caps %" GST_PTR_FORMAT,
        filter);

    result = gst_caps_intersect_full (filter, tmp, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (tmp);
  } else {
    result = tmp;
  }

  GST_DEBUG_OBJECT (bt, "returning caps: %" GST_PTR_FORMAT, result);

  return result;
}

static gboolean
gst_vulkan_upload_set_caps (GstBaseTransform * bt, GstCaps * in_caps,
    GstCaps * out_caps)
{
  GstVulkanUpload *vk_upload = GST_VULKAN_UPLOAD (bt);
  guint out_width, out_height;
  guint i;

  if (!gst_video_info_from_caps (&vk_upload->in_info, in_caps))
    return FALSE;

  if (!gst_video_info_from_caps (&vk_upload->out_info, out_caps))
    return FALSE;

  out_width = GST_VIDEO_INFO_WIDTH (&vk_upload->out_info);
  out_height = GST_VIDEO_INFO_HEIGHT (&vk_upload->out_info);

  for (i = 0; i < GST_VIDEO_INFO_N_PLANES (&vk_upload->out_info); i++) {
    GstVideoFormat v_format = GST_VIDEO_INFO_FORMAT (&vk_upload->out_info);
    GstVulkanImageMemory *img_mem;
    VkFormat vk_format;

    vk_format = gst_vulkan_format_from_video_format (v_format, i);

    img_mem = (GstVulkanImageMemory *)
        gst_vulkan_image_memory_alloc (vk_upload->device, vk_format, out_width,
        out_height, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    vk_upload->alloc_sizes[i] = img_mem->requirements.size;

    gst_memory_unref (GST_MEMORY_CAST (img_mem));
  }

  GST_DEBUG_OBJECT (bt,
      "set caps in: %" GST_PTR_FORMAT " out: %" GST_PTR_FORMAT, in_caps,
      out_caps);

  return TRUE;
}

static GstFlowReturn
gst_vulkan_upload_prepare_output_buffer (GstBaseTransform * bt,
    GstBuffer * inbuf, GstBuffer ** outbuf)
{
  GstVulkanUpload *vk_upload = GST_VULKAN_UPLOAD (bt);
  GstBaseTransformClass *bclass;
  GstVideoFrame v_frame;
  guint i;

  bclass = GST_BASE_TRANSFORM_GET_CLASS (bt);

  if (!gst_video_frame_map (&v_frame, &vk_upload->in_info, inbuf, GST_MAP_READ)) {
    GST_ELEMENT_ERROR (bt, RESOURCE, NOT_FOUND,
        ("%s", "Failed to map input buffer"), NULL);
    return GST_FLOW_ERROR;
  }

  *outbuf = gst_buffer_new ();

  for (i = 0; i < GST_VIDEO_INFO_N_PLANES (&vk_upload->out_info); i++) {
    GstVideoFormat v_format = GST_VIDEO_INFO_FORMAT (&vk_upload->out_info);
    GstMapInfo map_info;
    VkFormat vk_format;
    gsize plane_size;
    GstMemory *mem;

    vk_format = gst_vulkan_format_from_video_format (v_format, i);

    mem = gst_vulkan_buffer_memory_alloc_bind (vk_upload->device,
        vk_format, vk_upload->alloc_sizes[i],
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

    if (!gst_memory_map (GST_MEMORY_CAST (mem), &map_info, GST_MAP_WRITE)) {
      GST_ELEMENT_ERROR (bt, RESOURCE, NOT_FOUND,
          ("%s", "Failed to map output memory"), NULL);
      gst_buffer_unref (*outbuf);
      *outbuf = NULL;
      return GST_FLOW_ERROR;
    }

    plane_size =
        GST_VIDEO_INFO_PLANE_STRIDE (&vk_upload->out_info,
        i) * GST_VIDEO_INFO_COMP_HEIGHT (&vk_upload->out_info, i);
    g_assert (plane_size < map_info.size);
    memcpy (map_info.data, v_frame.data[i], plane_size);

    gst_memory_unmap (GST_MEMORY_CAST (mem), &map_info);

    gst_buffer_append_memory (*outbuf, mem);
  }

  gst_video_frame_unmap (&v_frame);

  bclass->copy_metadata (bt, inbuf, *outbuf);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_vulkan_upload_transform (GstBaseTransform * bt, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  return GST_FLOW_OK;
}
