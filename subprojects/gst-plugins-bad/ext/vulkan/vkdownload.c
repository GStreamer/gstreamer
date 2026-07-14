/*
 * GStreamer
 * Copyright (C) 2019 Matthew Waters <matthew@centricular.com>
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
 * SECTION:element-vulkandownload
 * @title: vulkandownload
 *
 * vulkandownload downloads data into Vulkan memory objects.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "gstvulkanelements.h"
#include "gstvkutils.h"
#include "vkdownload.h"

GST_DEBUG_CATEGORY (gst_debug_vulkan_download);
#define GST_CAT_DEFAULT gst_debug_vulkan_download

static GstCaps *
_set_caps_features_with_passthrough (const GstCaps * caps,
    const gchar * feature_name, GstCapsFeatures * passthrough)
{
  guint i, j, m, n;
  GstCaps *tmp;

  tmp = gst_caps_copy (caps);

  n = gst_caps_get_size (caps);
  for (i = 0; i < n; i++) {
    GstCapsFeatures *features, *orig_features;

    orig_features = gst_caps_get_features (caps, i);
    features = gst_caps_features_new_static_str (feature_name, NULL);

    m = gst_caps_features_get_size (orig_features);
    for (j = 0; j < m; j++) {
      const gchar *feature = gst_caps_features_get_nth (orig_features, j);

      /* if we already have the features */
      if (gst_caps_features_contains (features, feature))
        continue;

      if (g_strcmp0 (feature, GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY) == 0)
        continue;

      if (passthrough && gst_caps_features_contains (passthrough, feature)) {
        gst_caps_features_add (features, feature);
      }
    }

    gst_caps_set_features (tmp, i, features);
  }

  return tmp;
}

struct ImageToRawDownload
{
  GstVulkanDownload *download;

  GstVideoInfo in_info;
  GstVideoInfo out_info;

  GstBufferPool *pool;

  GstVulkanOperation *exec;
};

static gpointer
_image_to_raw_new_impl (GstVulkanDownload * download)
{
  struct ImageToRawDownload *raw = g_new0 (struct ImageToRawDownload, 1);

  raw->download = download;

  return raw;
}

static GstCaps *
_image_to_raw_transform_caps (gpointer impl, GstPadDirection direction,
    GstCaps * caps)
{
  GstCaps *ret;

  if (direction == GST_PAD_SINK) {
    ret =
        _set_caps_features_with_passthrough (caps,
        GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY, NULL);
  } else {
    ret =
        _set_caps_features_with_passthrough (caps,
        GST_CAPS_FEATURE_MEMORY_VULKAN_IMAGE, NULL);
  }

  return ret;
}

static gboolean
_image_to_raw_set_caps (gpointer impl, GstCaps * in_caps, GstCaps * out_caps)
{
  struct ImageToRawDownload *raw = impl;

  if (!gst_video_info_from_caps (&raw->in_info, in_caps))
    return FALSE;

  if (!gst_video_info_from_caps (&raw->out_info, out_caps))
    return FALSE;

  return TRUE;
}

static void
_image_to_raw_propose_allocation (gpointer impl, GstQuery * decide_query,
    GstQuery * query)
{
  /* FIXME: implement */
}

static gboolean
_image_to_raw_decide_allocation (gpointer impl, GstQuery * query)
{
  struct ImageToRawDownload *raw = impl;
  GstStructure *config;
  guint min = 1, max = 0, size = 1;
  GstCaps *caps;
  gboolean update_pool = FALSE;
  GstBufferPool *pool = NULL;

  gst_query_parse_allocation (query, &caps, NULL);
  if (!caps)
    return FALSE;

  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
    if (GST_IS_VULKAN_BUFFER_POOL (pool)) {
      update_pool = TRUE;
    } else {
      gst_clear_object (&pool);
    }
  }

  /* let's null current pool */
  gst_clear_object (&raw->pool);

  if (!pool) {
    pool = gst_vulkan_buffer_pool_new (raw->download->device);
  }

  config = gst_buffer_pool_get_config (pool);

  gst_buffer_pool_config_set_params (config, raw->download->out_caps, size,
      min, max);
  if (gst_query_find_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL)) {
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);
  }

  if (!gst_buffer_pool_set_config (pool, config)) {
    gst_clear_object (&pool);
    GST_ERROR_OBJECT (raw->download, "Failed to set buffer pool config");
    return FALSE;
  }

  if (update_pool)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool (query, pool, size, min, max);

  raw->pool = pool;

  return TRUE;
}

static GstFlowReturn
_image_to_raw_perform (gpointer impl, GstBuffer * inbuf, GstBuffer ** outbuf)
{
  struct ImageToRawDownload *raw = impl;
  GstVulkanCommandBuffer *cmd_buf;
  GError *error = NULL;
  GstFlowReturn ret;
  GstVulkanBarrierState *barriers = NULL;
  int i, n_mems, n_planes;
  gboolean one_mem_per_plane, single_multiplanar;
  VkImageLayout dst_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

  if (!raw->exec) {
    GstVulkanCommandPool *cmd_pool;

    cmd_pool = gst_vulkan_queue_create_command_pool (raw->download->queue,
        &error);
    if (!cmd_pool)
      goto error;
    raw->exec = gst_vulkan_operation_new (cmd_pool);
    gst_object_unref (cmd_pool);
  }

  if (!raw->pool) {
    GST_ERROR_OBJECT (raw->download, "No pool found.");
    goto error;
  }

  if (!gst_buffer_pool_set_active (raw->pool, TRUE)) {
    GST_ERROR_OBJECT (raw->download, "Couldn't activate pool.");
    goto error;
  }

  if ((ret =
          gst_buffer_pool_acquire_buffer (raw->pool, outbuf,
              NULL)) != GST_FLOW_OK)
    goto out;

  if (!gst_vulkan_operation_begin (raw->exec, &error))
    goto error;

  n_mems = gst_buffer_n_memory (inbuf);
  if (n_mems > GST_VIDEO_MAX_PLANES) {
    GST_ERROR_OBJECT (raw->download,
        "Input buffer has %d memories, but at most %d are supported", n_mems,
        GST_VIDEO_MAX_PLANES);
    goto unlock_error;
  }

  if (!gst_vulkan_operation_add_dependency_frame (raw->exec, inbuf,
          VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT))
    goto unlock_error;

  cmd_buf = raw->exec->cmd_buf;

  if (!gst_vulkan_operation_add_frame_barrier (raw->exec, inbuf,
          VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
          VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
          NULL))
    goto unlock_error;

  barriers = gst_vulkan_operation_get_barriers (raw->exec);
  gst_vulkan_barrier_state_pipeline_barrier (barriers, cmd_buf, 0);
  dst_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

  n_planes = GST_VIDEO_INFO_N_PLANES (&raw->out_info);
  one_mem_per_plane = n_mems == n_planes;
  single_multiplanar = n_mems == 1;

  if (!one_mem_per_plane && !single_multiplanar) {
    GST_ERROR_OBJECT (raw->download,
        "Unsupported Vulkan image layout with %d planes mapped to %d memories",
        n_planes, n_mems);
    goto unlock_error;
  }

  if (single_multiplanar && n_planes > 3) {
    GST_ERROR_OBJECT (raw->download,
        "Unsupported multi-planar Vulkan image layout with %d planes",
        n_planes);
    goto unlock_error;
  }

  for (i = 0; i < n_planes; i++) {
    VkBufferImageCopy region;
    GstMemory *mem;
    GstVulkanBufferMemory *buf_mem;
    GstVulkanImageMemory *img_mem;
    gsize skip = 0;
    /* Two input layouts are supported here:
     *
     * - one memory per video plane: each plane is a separate single-plane
     *   image, so use VK_IMAGE_ASPECT_COLOR_BIT and require the plane to start
     *   at offset 0 within the resolved GstMemory
     * - one memory containing all planes: a native multi-planar image, so
     *   planes are addressed with VK_IMAGE_ASPECT_PLANE_n_BIT and later planes
     *   may legitimately resolve to a non-zero offset within that single
     *   GstMemory
     *
     * Other plane-to-memory layouts are rejected above.
     */
    const VkImageAspectFlags aspects[] = { VK_IMAGE_ASPECT_PLANE_0_BIT,
      VK_IMAGE_ASPECT_PLANE_1_BIT, VK_IMAGE_ASPECT_PLANE_2_BIT,
    };
    VkImageAspectFlags plane_aspect;
    guint32 width, height, row, img_h;

    mem = gst_vulkan_buffer_peek_plane_memory (inbuf, &raw->in_info, i, &skip);
    if (!mem)
      goto unlock_error;
    if (one_mem_per_plane && skip != 0) {
      GST_ERROR_OBJECT (raw->download,
          "Plane %d starts at offset %" G_GSIZE_FORMAT " in its backing "
          "memory, expected 0 for a one-memory-per-plane layout", i, skip);
      goto unlock_error;
    }
    if (!gst_is_vulkan_image_memory (mem)) {
      GST_WARNING_OBJECT (raw->download, "Input buffer is not a Vulkan image");
      goto unlock_error;
    }
    img_mem = (GstVulkanImageMemory *) mem;

    mem =
        gst_vulkan_buffer_peek_plane_memory (*outbuf, &raw->out_info, i, NULL);
    if (!mem)
      goto unlock_error;
    if (!gst_is_vulkan_buffer_memory (mem)) {
      GST_WARNING_OBJECT (raw->download,
          "Output buffer is not a Vulkan buffer");
      goto unlock_error;
    }
    buf_mem = (GstVulkanBufferMemory *) mem;

    if (one_mem_per_plane)
      plane_aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    else
      plane_aspect = aspects[i];

    gst_vulkan_buffer_get_plane_dimensions (*outbuf, &raw->out_info, i, &width,
        &height, &row, &img_h);

    /* *INDENT-OFF* */
    region = (VkBufferImageCopy) {
      .bufferOffset = 0,
      .bufferRowLength = row,
      .bufferImageHeight = img_h,
      .imageSubresource = {
        /* XXX: each plane is a buffer */
        .aspectMask = plane_aspect,
        .mipLevel = 0,
        .baseArrayLayer = 0,
        .layerCount = 1,
      },
      .imageOffset = { .x = 0, .y = 0, .z = 0, },
      .imageExtent = {
        .width = width,
        .height = height,
        .depth = 1,
      }
    };
    /* *INDENT-ON* */

    gst_vulkan_command_buffer_lock (cmd_buf);
    vkCmdCopyImageToBuffer (cmd_buf->cmd, img_mem->image, dst_layout,
        buf_mem->buffer, 1, &region);
    gst_vulkan_command_buffer_unlock (cmd_buf);
  }

  if (!gst_vulkan_operation_end (raw->exec, &error))
    goto error;

  /* XXX: STALL!
   * Need to have the buffer gst_memory_map() wait for this fence before
   * allowing access */
  gst_vulkan_operation_wait (raw->exec);

  ret = GST_FLOW_OK;

out:
  return ret;

unlock_error:
  gst_vulkan_operation_reset (raw->exec);

error:
  if (error) {
    GST_WARNING_OBJECT (raw->download, "Error: %s", error->message);
    g_clear_error (&error);
  }
  gst_clear_buffer (outbuf);
  ret = GST_FLOW_ERROR;
  goto out;
}

static void
_image_to_raw_free (gpointer impl)
{
  struct ImageToRawDownload *raw = impl;

  if (raw->pool) {
    gst_buffer_pool_set_active (raw->pool, FALSE);
    gst_object_unref (raw->pool);
    raw->pool = NULL;
  }

  gst_clear_object (&raw->exec);

  g_free (impl);
}

static GstStaticCaps _image_to_raw_in_templ =
GST_STATIC_CAPS ("video/x-raw(" GST_CAPS_FEATURE_MEMORY_VULKAN_IMAGE ")");
static GstStaticCaps _image_to_raw_out_templ = GST_STATIC_CAPS ("video/x-raw");

static const struct DownloadMethod image_to_raw_download = {
  "VulkanImageToRaw",
  &_image_to_raw_in_templ,
  &_image_to_raw_out_templ,
  _image_to_raw_new_impl,
  _image_to_raw_transform_caps,
  _image_to_raw_set_caps,
  _image_to_raw_propose_allocation,
  _image_to_raw_decide_allocation,
  _image_to_raw_perform,
  _image_to_raw_free,
};

static const struct DownloadMethod *download_methods[] = {
  &image_to_raw_download,
};

static GstCaps *
_get_input_template_caps (void)
{
  GstCaps *ret = NULL;
  gint i;

  /* FIXME: cache this and invalidate on changes to download_methods */
  for (i = 0; i < G_N_ELEMENTS (download_methods); i++) {
    GstCaps *template = gst_static_caps_get (download_methods[i]->in_template);
    ret = ret == NULL ? template : gst_caps_merge (ret, template);
  }

  ret = gst_caps_simplify (ret);

  return ret;
}

static GstCaps *
_get_output_template_caps (void)
{
  GstCaps *ret = NULL;
  gint i;

  /* FIXME: cache this and invalidate on changes to download_methods */
  for (i = 0; i < G_N_ELEMENTS (download_methods); i++) {
    GstCaps *template = gst_static_caps_get (download_methods[i]->out_template);
    ret = ret == NULL ? template : gst_caps_merge (ret, template);
  }

  ret = gst_caps_simplify (ret);

  return ret;
}

static void gst_vulkan_download_finalize (GObject * object);

static gboolean gst_vulkan_download_query (GstBaseTransform * bt,
    GstPadDirection direction, GstQuery * query);
static void gst_vulkan_download_set_context (GstElement * element,
    GstContext * context);
static GstStateChangeReturn gst_vulkan_download_change_state (GstElement *
    element, GstStateChange transition);

static gboolean gst_vulkan_download_set_caps (GstBaseTransform * bt,
    GstCaps * in_caps, GstCaps * out_caps);
static GstCaps *gst_vulkan_download_transform_caps (GstBaseTransform * bt,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static gboolean gst_vulkan_download_propose_allocation (GstBaseTransform * bt,
    GstQuery * decide_query, GstQuery * query);
static gboolean gst_vulkan_download_decide_allocation (GstBaseTransform * bt,
    GstQuery * query);
static GstFlowReturn gst_vulkan_download_transform (GstBaseTransform * bt,
    GstBuffer * inbuf, GstBuffer * outbuf);
static GstFlowReturn gst_vulkan_download_prepare_output_buffer (GstBaseTransform
    * bt, GstBuffer * inbuf, GstBuffer ** outbuf);

enum
{
  PROP_0,
};

enum
{
  SIGNAL_0,
  LAST_SIGNAL
};

/* static guint gst_vulkan_download_signals[LAST_SIGNAL] = { 0 }; */

#define gst_vulkan_download_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstVulkanDownload, gst_vulkan_download,
    GST_TYPE_BASE_TRANSFORM, GST_DEBUG_CATEGORY_INIT (gst_debug_vulkan_download,
        "vulkandownload", 0, "Vulkan Downloader"));
GST_ELEMENT_REGISTER_DEFINE_WITH_CODE (vulkandownload, "vulkandownload",
    GST_RANK_NONE, GST_TYPE_VULKAN_DOWNLOAD, vulkan_element_init (plugin));

static void
gst_vulkan_download_class_init (GstVulkanDownloadClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseTransformClass *gstbasetransform_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasetransform_class = (GstBaseTransformClass *) klass;

  gst_element_class_set_static_metadata (gstelement_class, "Vulkan Downloader",
      "Filter/Video/Downloader", "A Vulkan data downloader",
      "Matthew Waters <matthew@centricular.com>");

  {
    GstCaps *caps;

    caps = _get_input_template_caps ();
    gst_element_class_add_pad_template (gstelement_class,
        gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS, caps));
    gst_caps_unref (caps);

    caps = _get_output_template_caps ();
    gst_element_class_add_pad_template (gstelement_class,
        gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS, caps));
    gst_caps_unref (caps);
  }

  gobject_class->finalize = gst_vulkan_download_finalize;

  gstelement_class->change_state = gst_vulkan_download_change_state;
  gstelement_class->set_context = gst_vulkan_download_set_context;
  gstbasetransform_class->query = GST_DEBUG_FUNCPTR (gst_vulkan_download_query);
  gstbasetransform_class->set_caps = gst_vulkan_download_set_caps;
  gstbasetransform_class->transform_caps = gst_vulkan_download_transform_caps;
  gstbasetransform_class->propose_allocation =
      gst_vulkan_download_propose_allocation;
  gstbasetransform_class->decide_allocation =
      gst_vulkan_download_decide_allocation;
  gstbasetransform_class->transform = gst_vulkan_download_transform;
  gstbasetransform_class->prepare_output_buffer =
      gst_vulkan_download_prepare_output_buffer;
}

static void
gst_vulkan_download_init (GstVulkanDownload * vk_download)
{
  guint i, n;

  n = G_N_ELEMENTS (download_methods);
  vk_download->download_impls = g_malloc (sizeof (gpointer) * n);
  for (i = 0; i < n; i++) {
    vk_download->download_impls[i] =
        download_methods[i]->new_impl (vk_download);
  }
}

static void
gst_vulkan_download_finalize (GObject * object)
{
  GstVulkanDownload *vk_download = GST_VULKAN_DOWNLOAD (object);
  guint i;

  gst_caps_replace (&vk_download->in_caps, NULL);
  gst_caps_replace (&vk_download->out_caps, NULL);

  for (i = 0; i < G_N_ELEMENTS (download_methods); i++) {
    download_methods[i]->free (vk_download->download_impls[i]);
  }
  g_free (vk_download->download_impls);
  vk_download->download_impls = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_vulkan_download_query (GstBaseTransform * bt, GstPadDirection direction,
    GstQuery * query)
{
  GstVulkanDownload *vk_download = GST_VULKAN_DOWNLOAD (bt);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:{
      if (gst_vulkan_requested_extensions_handle_context_query (GST_ELEMENT
              (vk_download), query,
              direction == GST_PAD_SINK ? GST_PAD_SRC : GST_PAD_SINK,
              vk_download->instance))
        return TRUE;

      if (gst_vulkan_handle_context_query (GST_ELEMENT (vk_download), query,
              NULL, vk_download->instance, vk_download->device))
        return TRUE;

      if (gst_vulkan_queue_handle_context_query (GST_ELEMENT (vk_download),
              query, vk_download->queue))
        return TRUE;

      break;
    }
    default:
      break;
  }

  return GST_BASE_TRANSFORM_CLASS (parent_class)->query (bt, direction, query);
}

static void
gst_vulkan_download_set_context (GstElement * element, GstContext * context)
{
  GstVulkanDownload *vk_download = GST_VULKAN_DOWNLOAD (element);

  gst_vulkan_handle_set_context (element, context, NULL,
      &vk_download->instance);

  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);
}

struct choose_data
{
  GstVulkanDownload *download;
  GstVulkanQueue *queue;
};

static GstStateChangeReturn
gst_vulkan_download_change_state (GstElement * element,
    GstStateChange transition)
{
  GstVulkanDownload *vk_download = GST_VULKAN_DOWNLOAD (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  GST_DEBUG ("changing state: %s => %s",
      gst_state_get_name (GST_STATE_TRANSITION_CURRENT (transition)),
      gst_state_get_name (GST_STATE_TRANSITION_NEXT (transition)));

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (!gst_vulkan_ensure_element_data (element, NULL,
              &vk_download->instance)) {
        GST_ELEMENT_ERROR (vk_download, RESOURCE, NOT_FOUND,
            ("Failed to retrieve vulkan instance"), (NULL));
        return GST_STATE_CHANGE_FAILURE;
      }
      if (!gst_vulkan_ensure_element_device (element, vk_download->instance,
              &vk_download->device, 0)) {
        return GST_STATE_CHANGE_FAILURE;
      }

      if (gst_vulkan_queue_run_context_query (GST_ELEMENT (vk_download),
              &vk_download->queue)) {
        guint32 flags, idx;

        GST_DEBUG_OBJECT (vk_download, "Queue retrieved from peer elements");
        idx = vk_download->queue->family;
        flags = vk_download->device->physical_device->queue_family_props[idx]
            .queueFlags;
        if ((flags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_TRANSFER_BIT)) == 0) {
          GST_DEBUG_OBJECT (vk_download,
              "Queue does not support VK_QUEUE_GRAPHICS_BIT with VK_QUEUE_TRANSFER_BIT");
          gst_clear_object (&vk_download->queue);
        }
      }

      if (!vk_download->queue) {
        vk_download->queue =
            gst_vulkan_device_select_queue (vk_download->device,
            VK_QUEUE_GRAPHICS_BIT);
      }
      if (!vk_download->queue) {
        GST_ELEMENT_ERROR (vk_download, RESOURCE, NOT_FOUND,
            ("Failed to create/retrieve a valid vulkan queue"), (NULL));
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
      if (vk_download->queue)
        gst_object_unref (vk_download->queue);
      vk_download->queue = NULL;
      if (vk_download->device)
        gst_object_unref (vk_download->device);
      vk_download->device = NULL;
      if (vk_download->instance)
        gst_object_unref (vk_download->instance);
      vk_download->instance = NULL;
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}

static GstCaps *
gst_vulkan_download_transform_caps (GstBaseTransform * bt,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstVulkanDownload *vk_download = GST_VULKAN_DOWNLOAD (bt);
  GstCaps *result, *tmp;
  gint i;

  tmp = gst_caps_new_empty ();

  for (i = 0; i < G_N_ELEMENTS (download_methods); i++) {
    GstCaps *tmp2;
    GstCaps *templ;

    if (direction == GST_PAD_SINK) {
      templ = gst_static_caps_get (download_methods[i]->in_template);
    } else {
      templ = gst_static_caps_get (download_methods[i]->out_template);
    }
    if (!gst_caps_can_intersect (caps, templ)) {
      gst_caps_unref (templ);
      continue;
    }
    gst_caps_unref (templ);

    tmp2 = download_methods[i]->transform_caps (vk_download->download_impls[i],
        direction, caps);

    if (tmp2)
      tmp = gst_caps_merge (tmp, tmp2);
  }

  if (filter) {
    result = gst_caps_intersect_full (filter, tmp, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (tmp);
  } else {
    result = tmp;
  }

  return result;
}

static gboolean
gst_vulkan_download_set_caps (GstBaseTransform * bt, GstCaps * in_caps,
    GstCaps * out_caps)
{
  GstVulkanDownload *vk_download = GST_VULKAN_DOWNLOAD (bt);
  gboolean found_method = FALSE;
  guint i;

  gst_caps_replace (&vk_download->in_caps, in_caps);
  gst_caps_replace (&vk_download->out_caps, out_caps);

  for (i = 0; i < G_N_ELEMENTS (download_methods); i++) {
    GstCaps *templ;

    templ = gst_static_caps_get (download_methods[i]->in_template);
    if (!gst_caps_can_intersect (in_caps, templ)) {
      gst_caps_unref (templ);
      continue;
    }
    gst_caps_unref (templ);

    templ = gst_static_caps_get (download_methods[i]->out_template);
    if (!gst_caps_can_intersect (out_caps, templ)) {
      gst_caps_unref (templ);
      continue;
    }
    gst_caps_unref (templ);

    if (!download_methods[i]->set_caps (vk_download->download_impls[i], in_caps,
            out_caps))
      continue;

    GST_LOG_OBJECT (bt, "downloader %s accepted caps in: %" GST_PTR_FORMAT
        " out: %" GST_PTR_FORMAT, download_methods[i]->name, in_caps, out_caps);

    vk_download->current_impl = i;
    found_method = TRUE;
    break;
  }

  GST_DEBUG_OBJECT (bt,
      "set caps in: %" GST_PTR_FORMAT " out: %" GST_PTR_FORMAT, in_caps,
      out_caps);

  return found_method;
}

static gboolean
gst_vulkan_download_propose_allocation (GstBaseTransform * bt,
    GstQuery * decide_query, GstQuery * query)
{
  GstVulkanDownload *vk_download = GST_VULKAN_DOWNLOAD (bt);
  guint i;

  for (i = 0; i < G_N_ELEMENTS (download_methods); i++) {
    GstCaps *templ;

    templ = gst_static_caps_get (download_methods[i]->in_template);
    if (!gst_caps_can_intersect (vk_download->in_caps, templ)) {
      gst_caps_unref (templ);
      continue;
    }
    gst_caps_unref (templ);

    templ = gst_static_caps_get (download_methods[i]->out_template);
    if (!gst_caps_can_intersect (vk_download->out_caps, templ)) {
      gst_caps_unref (templ);
      continue;
    }
    gst_caps_unref (templ);

    download_methods[i]->propose_allocation (vk_download->download_impls[i],
        decide_query, query);
  }

  return TRUE;
}

static gboolean
gst_vulkan_download_decide_allocation (GstBaseTransform * bt, GstQuery * query)
{
  GstVulkanDownload *vk_download = GST_VULKAN_DOWNLOAD (bt);
  guint i;
  gboolean ret = TRUE;

  for (i = 0; i < G_N_ELEMENTS (download_methods); i++) {
    GstCaps *templ;
    gboolean res;

    templ = gst_static_caps_get (download_methods[i]->in_template);
    if (!gst_caps_can_intersect (vk_download->in_caps, templ)) {
      gst_caps_unref (templ);
      continue;
    }
    gst_caps_unref (templ);

    templ = gst_static_caps_get (download_methods[i]->out_template);
    if (!gst_caps_can_intersect (vk_download->out_caps, templ)) {
      gst_caps_unref (templ);
      continue;
    }
    gst_caps_unref (templ);

    res =
        download_methods[i]->decide_allocation (vk_download->download_impls[i],
        query);

    /* if all methods fail, function fails */
    if (i == 0)
      ret = res;
    else
      ret |= res;
  }

  return ret;
}

static gboolean
_download_find_method (GstVulkanDownload * vk_download)
{
  vk_download->current_impl++;

  if (vk_download->current_impl >= G_N_ELEMENTS (download_methods))
    return FALSE;

  GST_DEBUG_OBJECT (vk_download, "attempting download with downloader %s",
      download_methods[vk_download->current_impl]->name);

  return TRUE;
}

static GstFlowReturn
gst_vulkan_download_prepare_output_buffer (GstBaseTransform * bt,
    GstBuffer * inbuf, GstBuffer ** outbuf)
{
  GstBaseTransformClass *bclass = GST_BASE_TRANSFORM_GET_CLASS (bt);
  GstVulkanDownload *vk_download = GST_VULKAN_DOWNLOAD (bt);
  GstFlowReturn ret;

restart:
  {
    gpointer method_impl;
    const struct DownloadMethod *method;

    method = download_methods[vk_download->current_impl];
    method_impl = vk_download->download_impls[vk_download->current_impl];

    ret = method->perform (method_impl, inbuf, outbuf);
    if (ret != GST_FLOW_OK) {
    next_method:
      if (!_download_find_method (vk_download)) {
        GST_ELEMENT_ERROR (bt, RESOURCE, NOT_FOUND,
            ("Could not find suitable downloader"), (NULL));
        return GST_FLOW_ERROR;
      }

      method = download_methods[vk_download->current_impl];
      method_impl = vk_download->download_impls[vk_download->current_impl];
      if (!method->set_caps (method_impl, vk_download->in_caps,
              vk_download->out_caps))
        /* try the next method */
        goto next_method;

      /* try the downloading with the next method */
      goto restart;
    }
  }

  if (ret == GST_FLOW_OK) {
    /* basetransform doesn't unref if they're the same */
    if (inbuf != *outbuf)
      bclass->copy_metadata (bt, inbuf, *outbuf);
  }

  return ret;
}

static GstFlowReturn
gst_vulkan_download_transform (GstBaseTransform * bt, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  return GST_FLOW_OK;
}
