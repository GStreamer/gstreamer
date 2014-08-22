/*
 *  gstvaapipluginbase.c - Base GStreamer VA-API Plugin element
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

#include "gst/vaapi/sysdeps.h"
#include "gstvaapipluginbase.h"
#include "gstvaapipluginutil.h"
#include "gstvaapivideocontext.h"
#include "gstvaapivideometa.h"
#if GST_CHECK_VERSION(1,0,0)
#include "gstvaapivideobufferpool.h"
#endif

/* Default debug category is from the subclass */
#define GST_CAT_DEFAULT (plugin->debug_category)

/* GstImplementsInterface interface */
#if !GST_CHECK_VERSION(1,0,0)
static gboolean
implements_interface_supported (GstImplementsInterface * iface, GType type)
{
  GstVaapiPluginBase *const plugin = GST_VAAPI_PLUGIN_BASE (iface);

  if (type == GST_TYPE_VIDEO_CONTEXT)
    return TRUE;
  return GST_VAAPI_PLUGIN_BASE_GET_CLASS (plugin)->has_interface (plugin, type);
}

static void
implements_interface_init (GstImplementsInterfaceClass * iface)
{
  iface->supported = implements_interface_supported;
}
#endif

/* GstVideoContext interface */
static void
plugin_set_display (GstVaapiPluginBase * plugin, GstVaapiDisplay * display)
{
  const gchar *const display_name =
      gst_vaapi_display_get_display_name (display);

  if (plugin->display_name && g_strcmp0 (plugin->display_name, display_name)) {
    GST_DEBUG_OBJECT (plugin, "incompatible display name '%s', requested '%s'",
        display_name, plugin->display_name);
    gst_vaapi_display_replace (&plugin->display, NULL);
  } else {
    GST_INFO_OBJECT (plugin, "set display %p", display);
    gst_vaapi_display_replace (&plugin->display, display);
    plugin->display_type = gst_vaapi_display_get_display_type (display);
    gst_vaapi_plugin_base_set_display_name (plugin, display_name);
  }
  gst_vaapi_display_unref (display);
}

#if GST_CHECK_VERSION(1,1,0)
static void
plugin_set_context (GstElement * element, GstContext * context)
{
  GstVaapiPluginBase *const plugin = GST_VAAPI_PLUGIN_BASE (element);
  GstVaapiDisplay *display = NULL;

  if (gst_vaapi_video_context_get_display (context, &display))
    plugin_set_display (plugin, display);
}
#else
static void
plugin_set_context (GstVideoContext * context, const gchar * type,
    const GValue * value)
{
  GstVaapiPluginBase *const plugin = GST_VAAPI_PLUGIN_BASE (context);
  GstVaapiDisplay *display = NULL;

  gst_vaapi_set_display (type, value, &display);
  plugin_set_display (plugin, display);
}

static void
video_context_interface_init (GstVideoContextInterface * iface)
{
  iface->set_context = plugin_set_context;
}

#define GstVideoContextClass GstVideoContextInterface
#endif

void
gst_vaapi_plugin_base_init_interfaces (GType g_define_type_id)
{
#if !GST_CHECK_VERSION(1,0,0)
  G_IMPLEMENT_INTERFACE (GST_TYPE_IMPLEMENTS_INTERFACE,
      implements_interface_init);
#endif
#if !GST_CHECK_VERSION(1,1,0)
  G_IMPLEMENT_INTERFACE (GST_TYPE_VIDEO_CONTEXT, video_context_interface_init);
#endif
}

static gboolean
default_has_interface (GstVaapiPluginBase * plugin, GType type)
{
  return FALSE;
}

static void
default_display_changed (GstVaapiPluginBase * plugin)
{
}

void
gst_vaapi_plugin_base_class_init (GstVaapiPluginBaseClass * klass)
{
  klass->has_interface = default_has_interface;
  klass->display_changed = default_display_changed;

#if GST_CHECK_VERSION(1,1,0)
  GstElementClass *const element_class = GST_ELEMENT_CLASS (klass);
  element_class->set_context = GST_DEBUG_FUNCPTR (plugin_set_context);
#endif
}

void
gst_vaapi_plugin_base_init (GstVaapiPluginBase * plugin,
    GstDebugCategory * debug_category)
{
  plugin->debug_category = debug_category;
  plugin->display_type = GST_VAAPI_DISPLAY_TYPE_ANY;
  plugin->display_type_req = GST_VAAPI_DISPLAY_TYPE_ANY;

  /* sink pad */
  plugin->sinkpad = gst_element_get_static_pad (GST_ELEMENT (plugin), "sink");
  plugin->sinkpad_query = GST_PAD_QUERYFUNC (plugin->sinkpad);
  gst_video_info_init (&plugin->sinkpad_info);

  /* src pad */
  if (!(GST_OBJECT_FLAGS (plugin) & GST_ELEMENT_FLAG_SINK)) {
    plugin->srcpad = gst_element_get_static_pad (GST_ELEMENT (plugin), "src");
    plugin->srcpad_query = GST_PAD_QUERYFUNC (plugin->srcpad);
  }
  gst_video_info_init (&plugin->srcpad_info);
}

void
gst_vaapi_plugin_base_finalize (GstVaapiPluginBase * plugin)
{
  gst_vaapi_plugin_base_close (plugin);
  g_free (plugin->display_name);
  if (plugin->sinkpad)
    gst_object_unref (plugin->sinkpad);
  if (plugin->srcpad)
    gst_object_unref (plugin->srcpad);
}

/**
 * gst_vaapi_plugin_base_open:
 * @plugin: a #GstVaapiPluginBase
 *
 * Allocates any internal resources needed for correct operation from
 * the subclass.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 */
gboolean
gst_vaapi_plugin_base_open (GstVaapiPluginBase * plugin)
{
  return TRUE;
}

/**
 * gst_vaapi_plugin_base_close:
 * @plugin: a #GstVaapiPluginBase
 *
 * Deallocates all internal resources that were allocated so
 * far. i.e. put the base plugin object into a clean state.
 */
void
gst_vaapi_plugin_base_close (GstVaapiPluginBase * plugin)
{
  g_clear_object (&plugin->uploader);
  gst_vaapi_display_replace (&plugin->display, NULL);

  gst_caps_replace (&plugin->sinkpad_caps, NULL);
  plugin->sinkpad_caps_changed = FALSE;
  gst_video_info_init (&plugin->sinkpad_info);
#if GST_CHECK_VERSION(1,0,0)
  if (plugin->sinkpad_buffer_pool) {
    gst_object_unref (plugin->sinkpad_buffer_pool);
    plugin->sinkpad_buffer_pool = NULL;
  }
  g_clear_object (&plugin->srcpad_buffer_pool);
#endif

  gst_caps_replace (&plugin->srcpad_caps, NULL);
  plugin->srcpad_caps_changed = FALSE;
  gst_video_info_init (&plugin->srcpad_info);
}

/**
 * gst_vaapi_plugin_base_set_display_type:
 * @plugin: a #GstVaapiPluginBase
 * @display_type: the new request #GstVaapiDisplayType
 *
 * Requests a new display type. The change is effective at the next
 * call to gst_vaapi_plugin_base_ensure_display().
 */
void
gst_vaapi_plugin_base_set_display_type (GstVaapiPluginBase * plugin,
    GstVaapiDisplayType display_type)
{
  plugin->display_type_req = display_type;
}

/**
 * gst_vaapi_plugin_base_set_display_name:
 * @plugin: a #GstVaapiPluginBase
 * @display_name: the new display name to match
 *
 * Sets the name of the display to look for. The change is effective
 * at the next call to gst_vaapi_plugin_base_ensure_display().
 */
void
gst_vaapi_plugin_base_set_display_name (GstVaapiPluginBase * plugin,
    const gchar * display_name)
{
  g_free (plugin->display_name);
  plugin->display_name = g_strdup (display_name);
}

/**
 * gst_vaapi_plugin_base_ensure_display:
 * @plugin: a #GstVaapiPluginBase
 *
 * Ensures the display stored in @plugin complies with the requested
 * display type constraints.
 *
 * Returns: %TRUE if the display was created to match the requested
 *   type, %FALSE otherwise.
 */
gboolean
gst_vaapi_plugin_base_ensure_display (GstVaapiPluginBase * plugin)
{
  if (plugin->display
      && gst_vaapi_display_type_is_compatible (plugin->display_type,
          plugin->display_type_req))
    return TRUE;
  gst_vaapi_display_replace (&plugin->display, NULL);

  if (!gst_vaapi_ensure_display (plugin, plugin->display_type_req))
    return FALSE;
  plugin->display_type = gst_vaapi_display_get_display_type (plugin->display);

  GST_VAAPI_PLUGIN_BASE_GET_CLASS (plugin)->display_changed (plugin);
  return TRUE;
}

/**
 * gst_vaapi_plugin_base_ensure_uploader:
 * @plugin: a #GstVaapiPluginBase
 *
 * Makes sure the built-in #GstVaapiUploader object is created, or
 * that it was successfully notified of any VA display change.
 *
 * Returns: %TRUE if the uploader was successfully created, %FALSE otherwise.
 */
gboolean
gst_vaapi_plugin_base_ensure_uploader (GstVaapiPluginBase * plugin)
{
  if (plugin->uploader) {
    if (!gst_vaapi_uploader_ensure_display (plugin->uploader, plugin->display))
      return FALSE;
  } else {
    plugin->uploader = gst_vaapi_uploader_new (plugin->display);
    if (!plugin->uploader)
      return FALSE;
  }
  return TRUE;
}

/**
 * ensure_sinkpad_buffer_pool:
 * @plugin: a #GstVaapiPluginBase
 * @caps: the initial #GstCaps for the resulting buffer pool
 *
 * Makes sure the sink pad video buffer pool is created with the
 * appropriate @caps.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 */
static gboolean
ensure_sinkpad_buffer_pool (GstVaapiPluginBase * plugin, GstCaps * caps)
{
#if GST_CHECK_VERSION(1,0,0)
  GstBufferPool *pool;
  GstCaps *pool_caps;
  GstStructure *config;
  GstVideoInfo vi;
  gboolean need_pool;

  if (!gst_vaapi_plugin_base_ensure_display (plugin))
    return FALSE;

  if (plugin->sinkpad_buffer_pool) {
    config = gst_buffer_pool_get_config (plugin->sinkpad_buffer_pool);
    gst_buffer_pool_config_get_params (config, &pool_caps, NULL, NULL, NULL);
    need_pool = !gst_caps_is_equal (caps, pool_caps);
    gst_structure_free (config);
    if (!need_pool)
      return TRUE;
    g_clear_object (&plugin->sinkpad_buffer_pool);
    plugin->sinkpad_buffer_size = 0;
  }

  pool = gst_vaapi_video_buffer_pool_new (plugin->display);
  if (!pool)
    goto error_create_pool;

  gst_video_info_init (&vi);
  gst_video_info_from_caps (&vi, caps);
  if (GST_VIDEO_INFO_FORMAT (&vi) == GST_VIDEO_FORMAT_ENCODED) {
    GST_DEBUG ("assume video buffer pool format is NV12");
    gst_video_info_set_format (&vi, GST_VIDEO_FORMAT_NV12,
        GST_VIDEO_INFO_WIDTH (&vi), GST_VIDEO_INFO_HEIGHT (&vi));
  }
  plugin->sinkpad_buffer_size = vi.size;

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, caps,
      plugin->sinkpad_buffer_size, 0, 0);
  gst_buffer_pool_config_add_option (config,
      GST_BUFFER_POOL_OPTION_VAAPI_VIDEO_META);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  if (!gst_buffer_pool_set_config (pool, config))
    goto error_pool_config;
  plugin->sinkpad_buffer_pool = pool;
  return TRUE;

  /* ERRORS */
error_create_pool:
  {
    GST_ERROR ("failed to create buffer pool");
    return FALSE;
  }
error_pool_config:
  {
    GST_ERROR ("failed to reset buffer pool config");
    gst_object_unref (pool);
    return FALSE;
  }
#else
  return TRUE;
#endif
}

/**
 * gst_vaapi_plugin_base_set_caps:
 * @plugin: a #GstVaapiPluginBase
 * @incaps: the sink pad (input) caps
 * @outcaps: the src pad (output) caps
 *
 * Notifies the base plugin object of the new input and output caps,
 * obtained from the subclass.
 *
 * Returns: %TRUE if the update of caps was successful, %FALSE otherwise.
 */
gboolean
gst_vaapi_plugin_base_set_caps (GstVaapiPluginBase * plugin, GstCaps * incaps,
    GstCaps * outcaps)
{
  if (incaps && incaps != plugin->sinkpad_caps) {
    gst_caps_replace (&plugin->sinkpad_caps, incaps);
    if (!gst_video_info_from_caps (&plugin->sinkpad_info, incaps))
      return FALSE;
    plugin->sinkpad_caps_changed = TRUE;
    plugin->sinkpad_caps_is_raw = !gst_caps_has_vaapi_surface (incaps);
  }

  if (outcaps && outcaps != plugin->srcpad_caps) {
    gst_caps_replace (&plugin->srcpad_caps, outcaps);
    if (!gst_video_info_from_caps (&plugin->srcpad_info, outcaps))
      return FALSE;
    plugin->srcpad_caps_changed = TRUE;
  }

  if (plugin->uploader && plugin->sinkpad_caps_is_raw) {
    if (!gst_vaapi_uploader_ensure_display (plugin->uploader, plugin->display))
      return FALSE;
    if (!gst_vaapi_uploader_ensure_caps (plugin->uploader,
            plugin->sinkpad_caps, plugin->srcpad_caps))
      return FALSE;
  }

  if (!ensure_sinkpad_buffer_pool (plugin, plugin->sinkpad_caps))
    return FALSE;
  return TRUE;
}

/**
 * gst_vaapi_plugin_base_propose_allocation:
 * @plugin: a #GstVaapiPluginBase
 * @query: the allocation query to configure
 *
 * Proposes allocation parameters to the upstream elements.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 */
#if GST_CHECK_VERSION(1,0,0)
gboolean
gst_vaapi_plugin_base_propose_allocation (GstVaapiPluginBase * plugin,
    GstQuery * query)
{
  GstCaps *caps = NULL;
  gboolean need_pool;

  gst_query_parse_allocation (query, &caps, &need_pool);

  if (need_pool) {
    if (!caps)
      goto error_no_caps;
    if (!ensure_sinkpad_buffer_pool (plugin, caps))
      return FALSE;
    gst_query_add_allocation_pool (query, plugin->sinkpad_buffer_pool,
        plugin->sinkpad_buffer_size, 0, 0);
  }

  gst_query_add_allocation_meta (query, GST_VAAPI_VIDEO_META_API_TYPE, NULL);
  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);
  return TRUE;

  /* ERRORS */
error_no_caps:
  {
    GST_ERROR ("no caps specified");
    return FALSE;
  }
}
#endif

/**
 * gst_vaapi_plugin_base_decide_allocation:
 * @plugin: a #GstVaapiPluginBase
 * @query: the allocation query to parse
 * @feature: the desired #GstVaapiCapsFeature, or zero to find the
 *   preferred one
 *
 * Decides allocation parameters for the downstream elements.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 */
#if GST_CHECK_VERSION(1,0,0)
gboolean
gst_vaapi_plugin_base_decide_allocation (GstVaapiPluginBase * plugin,
    GstQuery * query, guint feature)
{
  GstCaps *caps = NULL;
  GstBufferPool *pool;
  GstStructure *config;
  GstVideoInfo vi;
  guint size, min, max;
  gboolean need_pool, update_pool;
  gboolean has_video_meta = FALSE;
  gboolean has_video_alignment = FALSE;
#if GST_CHECK_VERSION(1,1,0) && USE_GLX
  gboolean has_texture_upload_meta = FALSE;
#endif

  g_return_val_if_fail (plugin->display != NULL, FALSE);

  gst_query_parse_allocation (query, &caps, &need_pool);

  if (!caps)
    goto error_no_caps;

  if (!feature)
    feature =
        gst_vaapi_find_preferred_caps_feature (plugin->srcpad,
        GST_VIDEO_FORMAT_ENCODED);

  has_video_meta = gst_query_find_allocation_meta (query,
      GST_VIDEO_META_API_TYPE, NULL);

#if GST_CHECK_VERSION(1,1,0) && USE_GLX
  has_texture_upload_meta = gst_query_find_allocation_meta (query,
      GST_VIDEO_GL_TEXTURE_UPLOAD_META_API_TYPE, NULL);
#endif

  gst_video_info_init (&vi);
  gst_video_info_from_caps (&vi, caps);
  if (GST_VIDEO_INFO_FORMAT (&vi) == GST_VIDEO_FORMAT_ENCODED)
    gst_video_info_set_format (&vi, GST_VIDEO_FORMAT_I420,
        GST_VIDEO_INFO_WIDTH (&vi), GST_VIDEO_INFO_HEIGHT (&vi));

  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
    size = MAX (size, vi.size);
    update_pool = TRUE;

    /* Check whether downstream element proposed a bufferpool but did
       not provide a correct propose_allocation() implementation */
    has_video_alignment = gst_buffer_pool_has_option (pool,
        GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);
  } else {
    pool = NULL;
    size = vi.size;
    min = max = 0;
    update_pool = FALSE;
  }

  /* GstVaapiVideoMeta is mandatory, and this implies VA surface memory */
  if (!pool || !gst_buffer_pool_has_option (pool,
          GST_BUFFER_POOL_OPTION_VAAPI_VIDEO_META)) {
    GST_INFO_OBJECT (plugin, "no pool or doesn't support GstVaapiVideoMeta, "
        "making new pool");
    if (pool)
      gst_object_unref (pool);
    pool = gst_vaapi_video_buffer_pool_new (plugin->display);
    if (!pool)
      goto error_create_pool;

    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_set_params (config, caps, size, min, max);
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VAAPI_VIDEO_META);
    gst_buffer_pool_set_config (pool, config);
  }

  /* Check whether GstVideoMeta, or GstVideoAlignment, is needed (raw video) */
  if (has_video_meta) {
    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);
#if GST_CHECK_VERSION(1,1,0) && USE_GLX
    if (has_texture_upload_meta)
      gst_buffer_pool_config_add_option (config,
          GST_BUFFER_POOL_OPTION_VIDEO_GL_TEXTURE_UPLOAD_META);
#endif
    gst_buffer_pool_set_config (pool, config);
  } else if (has_video_alignment) {
    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);
    gst_buffer_pool_set_config (pool, config);
  }

  /* GstVideoGLTextureUploadMeta (OpenGL) */
#if GST_CHECK_VERSION(1,1,0) && USE_GLX
  if (feature == GST_VAAPI_CAPS_FEATURE_GL_TEXTURE_UPLOAD_META
      && !has_texture_upload_meta) {
    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_GL_TEXTURE_UPLOAD_META);
    gst_buffer_pool_set_config (pool, config);
  }
#endif

  if (update_pool)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool (query, pool, size, min, max);

  g_clear_object (&plugin->srcpad_buffer_pool);
  plugin->srcpad_buffer_pool = pool;
  return TRUE;

  /* ERRORS */
error_no_caps:
  {
    GST_ERROR_OBJECT (plugin, "no caps specified");
    return FALSE;
  }
error_create_pool:
  {
    GST_ERROR_OBJECT (plugin, "failed to create buffer pool");
    return FALSE;
  }
}
#endif

/**
 * gst_vaapi_plugin_base_allocate_input_buffer:
 * @plugin: a #GstVaapiPluginBase
 * @caps: the buffer caps constraints to honour
 * @outbuf_ptr: the pointer location to the newly allocated buffer
 *
 * Creates a buffer that holds a VA surface memory for the sink pad to
 * use it as the result for buffer_alloc() impementations.
 *
 * Return: #GST_FLOW_OK if the buffer could be created.
 */
GstFlowReturn
gst_vaapi_plugin_base_allocate_input_buffer (GstVaapiPluginBase * plugin,
    GstCaps * caps, GstBuffer ** outbuf_ptr)
{
  GstBuffer *outbuf;

  *outbuf_ptr = NULL;

  if (!plugin->sinkpad_caps_changed) {
    if (!gst_video_info_from_caps (&plugin->sinkpad_info, caps))
      return GST_FLOW_NOT_SUPPORTED;
    plugin->sinkpad_caps_changed = TRUE;
  }

  if (!plugin->sinkpad_caps_is_raw)
    return GST_FLOW_OK;

  if (!gst_vaapi_uploader_ensure_display (plugin->uploader, plugin->display))
    return GST_FLOW_NOT_SUPPORTED;
  if (!gst_vaapi_uploader_ensure_caps (plugin->uploader, caps, NULL))
    return GST_FLOW_NOT_SUPPORTED;

  outbuf = gst_vaapi_uploader_get_buffer (plugin->uploader);
  if (!outbuf) {
    GST_WARNING ("failed to allocate resources for raw YUV buffer");
    return GST_FLOW_NOT_SUPPORTED;
  }

  *outbuf_ptr = outbuf;
  return GST_FLOW_OK;
}

/**
 * gst_vaapi_plugin_base_get_input_buffer:
 * @plugin: a #GstVaapiPluginBase
 * @incaps: the sink pad (input) buffer
 * @outbuf_ptr: the pointer to location to the VA surface backed buffer
 *
 * Acquires the sink pad (input) buffer as a VA surface backed
 * buffer. This is mostly useful for raw YUV buffers, as source
 * buffers that are already backed as a VA surface are passed
 * verbatim.
 *
 * Returns: #GST_FLOW_OK if the buffer could be acquired
 */
GstFlowReturn
gst_vaapi_plugin_base_get_input_buffer (GstVaapiPluginBase * plugin,
    GstBuffer * inbuf, GstBuffer ** outbuf_ptr)
{
  GstVaapiVideoMeta *meta;
  GstBuffer *outbuf;
#if GST_CHECK_VERSION(1,0,0)
  GstVideoFrame src_frame, out_frame;
  gboolean success;
#endif

  g_return_val_if_fail (inbuf != NULL, GST_FLOW_ERROR);
  g_return_val_if_fail (outbuf_ptr != NULL, GST_FLOW_ERROR);

  meta = gst_buffer_get_vaapi_video_meta (inbuf);
#if GST_CHECK_VERSION(1,0,0)
  if (meta) {
    *outbuf_ptr = gst_buffer_ref (inbuf);
    return GST_FLOW_OK;
  }

  if (!plugin->sinkpad_caps_is_raw)
    goto error_invalid_buffer;

  if (!plugin->sinkpad_buffer_pool)
    goto error_no_pool;

  if (!gst_buffer_pool_set_active (plugin->sinkpad_buffer_pool, TRUE))
    goto error_active_pool;

  outbuf = NULL;
  if (gst_buffer_pool_acquire_buffer (plugin->sinkpad_buffer_pool,
          &outbuf, NULL) != GST_FLOW_OK)
    goto error_create_buffer;

  if (!gst_video_frame_map (&src_frame, &plugin->sinkpad_info, inbuf,
          GST_MAP_READ))
    goto error_map_src_buffer;

  if (!gst_video_frame_map (&out_frame, &plugin->sinkpad_info, outbuf,
          GST_MAP_WRITE))
    goto error_map_dst_buffer;

  success = gst_video_frame_copy (&out_frame, &src_frame);
  gst_video_frame_unmap (&out_frame);
  gst_video_frame_unmap (&src_frame);
  if (!success)
    goto error_copy_buffer;

  gst_buffer_copy_into (outbuf, inbuf, GST_BUFFER_COPY_TIMESTAMPS, 0, -1);
  *outbuf_ptr = outbuf;
  return GST_FLOW_OK;

  /* ERRORS */
error_no_pool:
  {
    GST_ERROR ("no buffer pool was negotiated");
    return GST_FLOW_ERROR;
  }
error_active_pool:
  {
    GST_ERROR ("failed to activate buffer pool");
    return GST_FLOW_ERROR;
  }
error_map_dst_buffer:
  {
    gst_video_frame_unmap (&src_frame);
    // fall-through
  }
error_map_src_buffer:
  {
    GST_WARNING ("failed to map buffer");
    gst_buffer_unref (outbuf);
    return GST_FLOW_NOT_SUPPORTED;
  }
#else
  if (meta)
    outbuf = gst_buffer_ref (inbuf);
  else if (plugin->sinkpad_caps_is_raw) {
    outbuf = gst_vaapi_uploader_get_buffer (plugin->uploader);
    if (!outbuf)
      goto error_create_buffer;
    gst_buffer_copy_metadata (outbuf, inbuf,
        GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS);
  } else
    goto error_invalid_buffer;

  if (plugin->sinkpad_caps_is_raw &&
      !gst_vaapi_uploader_process (plugin->uploader, inbuf, outbuf))
    goto error_copy_buffer;

  *outbuf_ptr = outbuf;
  return GST_FLOW_OK;
#endif

  /* ERRORS */
error_invalid_buffer:
  {
    GST_ERROR ("failed to validate source buffer");
    return GST_FLOW_ERROR;
  }
error_create_buffer:
  {
    GST_ERROR ("failed to create buffer");
    return GST_FLOW_ERROR;
  }
error_copy_buffer:
  {
    GST_WARNING ("failed to upload buffer to VA surface");
    gst_buffer_unref (outbuf);
    return GST_FLOW_NOT_SUPPORTED;
  }
}
