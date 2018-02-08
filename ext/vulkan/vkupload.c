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
 * @title: vulkanupload
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
    features = gst_caps_features_new (feature_name, NULL);

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

struct BufferUpload
{
  GstVulkanUpload *upload;
};

static gpointer
_buffer_new_impl (GstVulkanUpload * upload)
{
  struct BufferUpload *raw = g_new0 (struct BufferUpload, 1);

  raw->upload = upload;

  return raw;
}

static GstCaps *
_buffer_transform_caps (gpointer impl, GstPadDirection direction,
    GstCaps * caps)
{
  return gst_caps_ref (caps);
}

static gboolean
_buffer_set_caps (gpointer impl, GstCaps * in_caps, GstCaps * out_caps)
{
  return TRUE;
}

static void
_buffer_propose_allocation (gpointer impl, GstQuery * decide_query,
    GstQuery * query)
{
  struct BufferUpload *raw = impl;
  gboolean need_pool;
  GstCaps *caps;
  GstVideoInfo info;
  guint size;
  GstBufferPool *pool = NULL;

  gst_query_parse_allocation (query, &caps, &need_pool);

  if (caps == NULL)
    return;

  if (!gst_video_info_from_caps (&info, caps))
    return;

  /* the normal size of a frame */
  size = info.size;

  if (need_pool) {
    GstStructure *config;

    pool = gst_vulkan_buffer_pool_new (raw->upload->device);

    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_set_params (config, caps, size, 0, 0);

    if (!gst_buffer_pool_set_config (pool, config)) {
      g_object_unref (pool);
      return;
    }
  }

  gst_query_add_allocation_pool (query, pool, size, 1, 0);
  if (pool)
    g_object_unref (pool);

  return;
}

static GstFlowReturn
_buffer_perform (gpointer impl, GstBuffer * inbuf, GstBuffer ** outbuf)
{
  if (!gst_is_vulkan_buffer_memory (gst_buffer_peek_memory (inbuf, 0)))
    return GST_FLOW_ERROR;

  *outbuf = inbuf;

  return GST_FLOW_OK;
}

static void
_buffer_free (gpointer impl)
{
  g_free (impl);
}

static GstStaticCaps _buffer_in_templ =
    GST_STATIC_CAPS ("video/x-raw(" GST_CAPS_FEATURE_MEMORY_VULKAN_BUFFER ") ;"
    "video/x-raw");
static GstStaticCaps _buffer_out_templ =
GST_STATIC_CAPS ("video/x-raw(" GST_CAPS_FEATURE_MEMORY_VULKAN_BUFFER ")");

static const struct UploadMethod buffer_upload = {
  "VulkanBuffer",
  &_buffer_in_templ,
  &_buffer_out_templ,
  _buffer_new_impl,
  _buffer_transform_caps,
  _buffer_set_caps,
  _buffer_propose_allocation,
  _buffer_perform,
  _buffer_free,
};

struct RawToBufferUpload
{
  GstVulkanUpload *upload;

  GstVideoInfo in_info;
  GstVideoInfo out_info;

  GstBufferPool *pool;
  gboolean pool_active;

  gsize alloc_sizes[GST_VIDEO_MAX_PLANES];
};

static gpointer
_raw_to_buffer_new_impl (GstVulkanUpload * upload)
{
  struct RawToBufferUpload *raw = g_new0 (struct RawToBufferUpload, 1);

  raw->upload = upload;

  return raw;
}

static GstCaps *
_raw_to_buffer_transform_caps (gpointer impl, GstPadDirection direction,
    GstCaps * caps)
{
  GstCaps *ret;

  if (direction == GST_PAD_SINK) {
    ret =
        _set_caps_features_with_passthrough (caps,
        GST_CAPS_FEATURE_MEMORY_VULKAN_BUFFER, NULL);
  } else {
    ret =
        _set_caps_features_with_passthrough (caps,
        GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY, NULL);
  }

  return ret;
}

static gboolean
_raw_to_buffer_set_caps (gpointer impl, GstCaps * in_caps, GstCaps * out_caps)
{
  struct RawToBufferUpload *raw = impl;
  guint out_width, out_height;
  guint i;

  if (!gst_video_info_from_caps (&raw->in_info, in_caps))
    return FALSE;

  if (!gst_video_info_from_caps (&raw->out_info, out_caps))
    return FALSE;

  out_width = GST_VIDEO_INFO_WIDTH (&raw->out_info);
  out_height = GST_VIDEO_INFO_HEIGHT (&raw->out_info);

  for (i = 0; i < GST_VIDEO_INFO_N_PLANES (&raw->out_info); i++) {
    GstVideoFormat v_format = GST_VIDEO_INFO_FORMAT (&raw->out_info);
    GstVulkanImageMemory *img_mem;
    VkFormat vk_format;

    vk_format = gst_vulkan_format_from_video_format (v_format, i);

    img_mem = (GstVulkanImageMemory *)
        gst_vulkan_image_memory_alloc (raw->upload->device, vk_format,
        out_width, out_height, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    raw->alloc_sizes[i] = img_mem->requirements.size;

    gst_memory_unref (GST_MEMORY_CAST (img_mem));
  }

  return TRUE;
}

static void
_raw_to_buffer_propose_allocation (gpointer impl, GstQuery * decide_query,
    GstQuery * query)
{
  /* a little trickery with the impl pointer */
  _buffer_propose_allocation (impl, decide_query, query);
}

static GstFlowReturn
_raw_to_buffer_perform (gpointer impl, GstBuffer * inbuf, GstBuffer ** outbuf)
{
  struct RawToBufferUpload *raw = impl;
  GstVideoFrame v_frame;
  GstFlowReturn ret;
  guint i;

  if (!raw->pool) {
    GstStructure *config;
    guint min = 0, max = 0;
    gsize size = 1;

    raw->pool = gst_vulkan_buffer_pool_new (raw->upload->device);
    config = gst_buffer_pool_get_config (raw->pool);
    gst_buffer_pool_config_set_params (config, raw->upload->out_caps, size, min,
        max);
    gst_buffer_pool_set_config (raw->pool, config);
  }
  if (!raw->pool_active) {
    gst_buffer_pool_set_active (raw->pool, TRUE);
    raw->pool_active = TRUE;
  }

  if ((ret =
          gst_buffer_pool_acquire_buffer (raw->pool, outbuf,
              NULL)) != GST_FLOW_OK)
    goto out;

  if (!gst_video_frame_map (&v_frame, &raw->in_info, inbuf, GST_MAP_READ)) {
    GST_ELEMENT_ERROR (raw->upload, RESOURCE, NOT_FOUND,
        ("%s", "Failed to map input buffer"), NULL);
    return GST_FLOW_ERROR;
  }

  for (i = 0; i < GST_VIDEO_INFO_N_PLANES (&raw->out_info); i++) {
    GstMapInfo map_info;
    gsize plane_size;
    GstMemory *mem;

    mem = gst_buffer_peek_memory (*outbuf, i);
    if (!gst_memory_map (GST_MEMORY_CAST (mem), &map_info, GST_MAP_WRITE)) {
      GST_ELEMENT_ERROR (raw->upload, RESOURCE, NOT_FOUND,
          ("%s", "Failed to map output memory"), NULL);
      gst_buffer_unref (*outbuf);
      *outbuf = NULL;
      ret = GST_FLOW_ERROR;
      goto out;
    }

    plane_size =
        GST_VIDEO_INFO_PLANE_STRIDE (&raw->out_info,
        i) * GST_VIDEO_INFO_COMP_HEIGHT (&raw->out_info, i);
    g_assert (plane_size < map_info.size);
    memcpy (map_info.data, v_frame.data[i], plane_size);

    gst_memory_unmap (GST_MEMORY_CAST (mem), &map_info);
  }

  gst_video_frame_unmap (&v_frame);

  ret = GST_FLOW_OK;

out:
  return ret;
}

static void
_raw_to_buffer_free (gpointer impl)
{
  struct RawToBufferUpload *raw = impl;

  if (raw->pool) {
    if (raw->pool_active) {
      gst_buffer_pool_set_active (raw->pool, FALSE);
    }
    raw->pool_active = FALSE;
    gst_object_unref (raw->pool);
    raw->pool = NULL;
  }

  g_free (impl);
}

static GstStaticCaps _raw_to_buffer_in_templ = GST_STATIC_CAPS ("video/x-raw");
static GstStaticCaps _raw_to_buffer_out_templ =
GST_STATIC_CAPS ("video/x-raw(" GST_CAPS_FEATURE_MEMORY_VULKAN_BUFFER ")");

static const struct UploadMethod raw_to_buffer_upload = {
  "RawToVulkanBuffer",
  &_raw_to_buffer_in_templ,
  &_raw_to_buffer_out_templ,
  _raw_to_buffer_new_impl,
  _raw_to_buffer_transform_caps,
  _raw_to_buffer_set_caps,
  _raw_to_buffer_propose_allocation,
  _raw_to_buffer_perform,
  _raw_to_buffer_free,
};

static const struct UploadMethod *upload_methods[] = {
  &buffer_upload,
  &raw_to_buffer_upload,
};

static GstCaps *
_get_input_template_caps (void)
{
  GstCaps *ret = NULL;
  gint i;

  /* FIXME: cache this and invalidate on changes to upload_methods */
  for (i = 0; i < G_N_ELEMENTS (upload_methods); i++) {
    GstCaps *template = gst_static_caps_get (upload_methods[i]->in_template);
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

  /* FIXME: cache this and invalidate on changes to upload_methods */
  for (i = 0; i < G_N_ELEMENTS (upload_methods); i++) {
    GstCaps *template = gst_static_caps_get (upload_methods[i]->out_template);
    ret = ret == NULL ? template : gst_caps_merge (ret, template);
  }

  ret = gst_caps_simplify (ret);

  return ret;
}

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
static gboolean gst_vulkan_upload_propose_allocation (GstBaseTransform * bt,
    GstQuery * decide_query, GstQuery * query);
static gboolean gst_vulkan_upload_decide_allocation (GstBaseTransform * bt,
    GstQuery * query);
static GstFlowReturn gst_vulkan_upload_transform (GstBaseTransform * bt,
    GstBuffer * inbuf, GstBuffer * outbuf);
static GstFlowReturn gst_vulkan_upload_prepare_output_buffer (GstBaseTransform *
    bt, GstBuffer * inbuf, GstBuffer ** outbuf);

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

  gobject_class->finalize = gst_vulkan_upload_finalize;

  gstelement_class->change_state = gst_vulkan_upload_change_state;
  gstelement_class->set_context = gst_vulkan_upload_set_context;
  gstbasetransform_class->query = GST_DEBUG_FUNCPTR (gst_vulkan_upload_query);
  gstbasetransform_class->set_caps = gst_vulkan_upload_set_caps;
  gstbasetransform_class->transform_caps = gst_vulkan_upload_transform_caps;
  gstbasetransform_class->propose_allocation =
      gst_vulkan_upload_propose_allocation;
  gstbasetransform_class->decide_allocation =
      gst_vulkan_upload_decide_allocation;
  gstbasetransform_class->transform = gst_vulkan_upload_transform;
  gstbasetransform_class->prepare_output_buffer =
      gst_vulkan_upload_prepare_output_buffer;
}

static void
gst_vulkan_upload_init (GstVulkanUpload * vk_upload)
{
  guint i, n;

  n = G_N_ELEMENTS (upload_methods);
  vk_upload->upload_impls = g_malloc (sizeof (gpointer) * n);
  for (i = 0; i < n; i++) {
    vk_upload->upload_impls[i] = upload_methods[i]->new_impl (vk_upload);
  }
}

static void
gst_vulkan_upload_finalize (GObject * object)
{
  GstVulkanUpload *vk_upload = GST_VULKAN_UPLOAD (object);
  guint i;

  gst_caps_replace (&vk_upload->in_caps, NULL);
  gst_caps_replace (&vk_upload->out_caps, NULL);

  for (i = 0; i < G_N_ELEMENTS (upload_methods); i++) {
    upload_methods[i]->free (vk_upload->upload_impls[i]);
  }

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
      if (!gst_vulkan_device_run_context_query (GST_ELEMENT (vk_upload),
              &vk_upload->device)) {
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
  GstVulkanUpload *vk_upload = GST_VULKAN_UPLOAD (bt);
  GstCaps *result, *tmp;
  gint i;

  tmp = gst_caps_new_empty ();

  for (i = 0; i < G_N_ELEMENTS (upload_methods); i++) {
    GstCaps *tmp2;
    GstCaps *templ;

    if (direction == GST_PAD_SINK) {
      templ = gst_static_caps_get (upload_methods[i]->in_template);
    } else {
      templ = gst_static_caps_get (upload_methods[i]->out_template);
    }
    if (!gst_caps_can_intersect (caps, templ)) {
      gst_caps_unref (templ);
      continue;
    }
    gst_caps_unref (templ);

    tmp2 = upload_methods[i]->transform_caps (vk_upload->upload_impls[i],
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
gst_vulkan_upload_set_caps (GstBaseTransform * bt, GstCaps * in_caps,
    GstCaps * out_caps)
{
  GstVulkanUpload *vk_upload = GST_VULKAN_UPLOAD (bt);
  gboolean found_method = FALSE;
  guint i;

  gst_caps_replace (&vk_upload->in_caps, in_caps);
  gst_caps_replace (&vk_upload->out_caps, out_caps);

  for (i = 0; i < G_N_ELEMENTS (upload_methods); i++) {
    GstCaps *templ;

    templ = gst_static_caps_get (upload_methods[i]->in_template);
    if (!gst_caps_can_intersect (in_caps, templ)) {
      gst_caps_unref (templ);
      continue;
    }
    gst_caps_unref (templ);

    templ = gst_static_caps_get (upload_methods[i]->out_template);
    if (!gst_caps_can_intersect (out_caps, templ)) {
      gst_caps_unref (templ);
      continue;
    }
    gst_caps_unref (templ);

    if (!upload_methods[i]->set_caps (vk_upload->upload_impls[i], in_caps,
            out_caps))
      continue;

    GST_LOG_OBJECT (bt, "uploader %s accepted caps in: %" GST_PTR_FORMAT
        " out: %" GST_PTR_FORMAT, upload_methods[i]->name, in_caps, out_caps);

    vk_upload->current_impl = i;
    found_method = TRUE;
    break;
  }

  GST_DEBUG_OBJECT (bt,
      "set caps in: %" GST_PTR_FORMAT " out: %" GST_PTR_FORMAT, in_caps,
      out_caps);

  return found_method;
}

static gboolean
gst_vulkan_upload_propose_allocation (GstBaseTransform * bt,
    GstQuery * decide_query, GstQuery * query)
{
  GstVulkanUpload *vk_upload = GST_VULKAN_UPLOAD (bt);
  guint i;

  for (i = 0; i < G_N_ELEMENTS (upload_methods); i++) {
    GstCaps *templ;

    templ = gst_static_caps_get (upload_methods[i]->in_template);
    if (!gst_caps_can_intersect (vk_upload->in_caps, templ)) {
      gst_caps_unref (templ);
      continue;
    }
    gst_caps_unref (templ);

    templ = gst_static_caps_get (upload_methods[i]->out_template);
    if (!gst_caps_can_intersect (vk_upload->out_caps, templ)) {
      gst_caps_unref (templ);
      continue;
    }
    gst_caps_unref (templ);

    upload_methods[i]->propose_allocation (vk_upload->upload_impls[i],
        decide_query, query);
  }

  return TRUE;
}

static gboolean
gst_vulkan_upload_decide_allocation (GstBaseTransform * bt, GstQuery * query)
{
  return TRUE;
}

static gboolean
_upload_find_method (GstVulkanUpload * vk_upload)
{
  vk_upload->current_impl++;

  if (vk_upload->current_impl >= G_N_ELEMENTS (upload_methods))
    return FALSE;

  GST_DEBUG_OBJECT (vk_upload, "attempting upload with uploader %s",
      upload_methods[vk_upload->current_impl]->name);

  return TRUE;
}

static GstFlowReturn
gst_vulkan_upload_prepare_output_buffer (GstBaseTransform * bt,
    GstBuffer * inbuf, GstBuffer ** outbuf)
{
  GstBaseTransformClass *bclass = GST_BASE_TRANSFORM_GET_CLASS (bt);
  GstVulkanUpload *vk_upload = GST_VULKAN_UPLOAD (bt);
  GstFlowReturn ret;

restart:
  {
    gpointer method_impl;
    const struct UploadMethod *method;

    method = upload_methods[vk_upload->current_impl];
    method_impl = vk_upload->upload_impls[vk_upload->current_impl];

    ret = method->perform (method_impl, inbuf, outbuf);
    if (ret != GST_FLOW_OK) {
    next_method:
      if (!_upload_find_method (vk_upload)) {
        GST_ELEMENT_ERROR (bt, RESOURCE, NOT_FOUND,
            ("Could not find suitable uploader"), (NULL));
        return GST_FLOW_ERROR;
      }

      method = upload_methods[vk_upload->current_impl];
      method_impl = vk_upload->upload_impls[vk_upload->current_impl];
      if (!method->set_caps (method_impl, vk_upload->in_caps,
              vk_upload->out_caps))
        /* try the next method */
        goto next_method;

      /* try the uploading with the next method */
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
gst_vulkan_upload_transform (GstBaseTransform * bt, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  return GST_FLOW_OK;
}
