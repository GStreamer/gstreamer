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
#include <gst/vaapi/gstvaapisurface_drm.h>
#include <gst/base/gstpushsrc.h>
#include "gstvaapipluginbase.h"
#include "gstvaapipluginutil.h"
#include "gstvaapivideocontext.h"
#include "gstvaapivideometa.h"
#if GST_CHECK_VERSION(1,0,0)
#include "gstvaapivideobufferpool.h"
#endif
#if GST_CHECK_VERSION(1,1,0)
#include <gst/allocators/allocators.h>
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

static gboolean
plugin_update_sinkpad_info_from_buffer (GstVaapiPluginBase * plugin,
    GstBuffer * buf)
{
  GstVideoInfo *const vip = &plugin->sinkpad_info;
  GstVideoMeta *vmeta;
  guint i;

  vmeta = gst_buffer_get_video_meta (buf);
  if (!vmeta)
    return TRUE;

  if (GST_VIDEO_INFO_FORMAT (vip) != vmeta->format ||
      GST_VIDEO_INFO_WIDTH (vip) != vmeta->width ||
      GST_VIDEO_INFO_HEIGHT (vip) != vmeta->height ||
      GST_VIDEO_INFO_N_PLANES (vip) != vmeta->n_planes)
    return FALSE;

  for (i = 0; i < GST_VIDEO_INFO_N_PLANES (vip); ++i) {
    GST_VIDEO_INFO_PLANE_OFFSET (vip, i) = vmeta->offset[i];
    GST_VIDEO_INFO_PLANE_STRIDE (vip, i) = vmeta->stride[i];
  }
  GST_VIDEO_INFO_SIZE (vip) = gst_buffer_get_size (buf);
  return TRUE;
}

#if GST_CHECK_VERSION(1,1,0)
static gboolean
is_dma_buffer (GstBuffer * buf)
{
  GstMemory *mem;

  if (gst_buffer_n_memory (buf) < 1)
    return FALSE;

  mem = gst_buffer_peek_memory (buf, 0);
  if (!mem || !gst_is_dmabuf_memory (mem))
    return FALSE;
  return TRUE;
}

static gboolean
plugin_bind_dma_to_vaapi_buffer (GstVaapiPluginBase * plugin,
    GstBuffer * inbuf, GstBuffer * outbuf)
{
  GstVideoInfo *const vip = &plugin->sinkpad_info;
  GstVaapiVideoMeta *meta;
  GstVaapiSurface *surface;
  GstVaapiSurfaceProxy *proxy;
  gint fd;

  fd = gst_dmabuf_memory_get_fd (gst_buffer_peek_memory (inbuf, 0));
  if (fd < 0)
    return FALSE;

  if (!plugin_update_sinkpad_info_from_buffer (plugin, inbuf))
    goto error_update_sinkpad_info;

  meta = gst_buffer_get_vaapi_video_meta (outbuf);
  g_return_val_if_fail (meta != NULL, FALSE);

  surface = gst_vaapi_surface_new_with_dma_buf_handle (plugin->display, fd,
      GST_VIDEO_INFO_SIZE (vip), GST_VIDEO_INFO_FORMAT (vip),
      GST_VIDEO_INFO_WIDTH (vip), GST_VIDEO_INFO_HEIGHT (vip),
      vip->offset, vip->stride);
  if (!surface)
    goto error_create_surface;

  proxy = gst_vaapi_surface_proxy_new (surface);
  gst_vaapi_object_unref (surface);
  if (!proxy)
    goto error_create_proxy;

  gst_vaapi_surface_proxy_set_destroy_notify (proxy,
      (GDestroyNotify) gst_buffer_unref, (gpointer) gst_buffer_ref (inbuf));
  gst_vaapi_video_meta_set_surface_proxy (meta, proxy);
  gst_vaapi_surface_proxy_unref (proxy);
  return TRUE;

  /* ERRORS */
error_update_sinkpad_info:
  GST_ERROR ("failed to update sink pad video info from video meta");
  return FALSE;
error_create_surface:
  GST_ERROR ("failed to create VA surface from dma_buf handle");
  return FALSE;
error_create_proxy:
  GST_ERROR ("failed to create VA surface proxy from wrapped VA surface");
  return FALSE;
}
#endif

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
  gst_object_replace (&plugin->gl_context, NULL);

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
 * gst_vaapi_plugin_base_has_display_type:
 * @plugin: a #GstVaapiPluginBase
 * @display_type_req: the desired #GstVaapiDisplayType
 *
 * Checks whether the @plugin elements already has a #GstVaapiDisplay
 * instance compatible with type @display_type_req.
 *
 * Return value: %TRUE if @plugin has a compatible display, %FALSE otherwise
 */
gboolean
gst_vaapi_plugin_base_has_display_type (GstVaapiPluginBase * plugin,
    GstVaapiDisplayType display_type_req)
{
  GstVaapiDisplayType display_type;

  if (!plugin->display)
    return FALSE;

  display_type = plugin->display_type;
  if (gst_vaapi_display_type_is_compatible (display_type, display_type_req))
    return TRUE;

  display_type = gst_vaapi_display_get_class_type (plugin->display);
  if (gst_vaapi_display_type_is_compatible (display_type, display_type_req))
    return TRUE;
  return FALSE;
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
  if (gst_vaapi_plugin_base_has_display_type (plugin, plugin->display_type_req))
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

/* Checks whether the supplied pad peer element supports DMABUF sharing */
/* XXX: this is a workaround to the absence of any proposer way to
   specify DMABUF memory capsfeatures or bufferpool option to downstream */
static gboolean
has_dmabuf_capable_peer (GstVaapiPluginBase * plugin, GstPad * pad)
{
  GstPad *other_pad = NULL;
  GstElement *element = NULL;
  gchar *element_name = NULL;
  gboolean is_dmabuf_capable = FALSE;
  gint v;

  do {
    other_pad = gst_pad_get_peer (pad);
    if (!other_pad)
      break;

    element = gst_pad_get_parent_element (other_pad);
    if (!element || !GST_IS_PUSH_SRC (element))
      break;

    element_name = gst_element_get_name (element);
    if (!element_name || sscanf (element_name, "v4l2src%d", &v) != 1)
      break;

    v = 0;
    g_object_get (element, "io-mode", &v, NULL);
    is_dmabuf_capable = v == 5; /* "dmabuf-import" enum value */
  } while (0);

  g_free (element_name);
  g_clear_object (&element);
  g_clear_object (&other_pad);
  return is_dmabuf_capable;
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

    if (has_dmabuf_capable_peer (plugin, plugin->sinkpad)) {
      GstStructure *const config =
          gst_buffer_pool_get_config (plugin->sinkpad_buffer_pool);

      gst_buffer_pool_config_add_option (config,
          GST_BUFFER_POOL_OPTION_DMABUF_MEMORY);
      if (!gst_buffer_pool_set_config (plugin->sinkpad_buffer_pool, config))
        goto error_pool_config;
    }
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
error_pool_config:
  {
    GST_ERROR ("failed to reset buffer pool config");
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
  guint idx;
#endif

  g_return_val_if_fail (plugin->display != NULL, FALSE);

  gst_query_parse_allocation (query, &caps, &need_pool);

  /* We don't need any GL context beyond this point if not requested
     so explicitly through GstVideoGLTextureUploadMeta */
  gst_object_replace (&plugin->gl_context, NULL);

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
      GST_VIDEO_GL_TEXTURE_UPLOAD_META_API_TYPE, &idx);

#if USE_GST_GL_HELPERS
  if (has_texture_upload_meta) {
    const GstStructure *params;
    GstObject *gl_context;

    gst_query_parse_nth_allocation_meta (query, idx, &params);
    if (params) {
      if (gst_structure_get (params, "gst.gl.GstGLContext", GST_GL_TYPE_CONTEXT,
              &gl_context, NULL) && gl_context) {
        gst_vaapi_plugin_base_set_gl_context (plugin, gl_context);
        gst_object_unref (gl_context);
      }
    }
  }
#endif
#endif

  /* Make sure the display we pass down to the buffer pool is actually
     the expected one, especially when the downstream element requires
     a GLX or EGL display */
  if (!gst_vaapi_plugin_base_ensure_display (plugin))
    goto error_ensure_display;

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
error_ensure_display:
  {
    GST_ERROR_OBJECT (plugin, "failed to ensure display of type %d",
        plugin->display_type_req);
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

#if GST_CHECK_VERSION(1,1,0)
  if (is_dma_buffer (inbuf)) {
    if (!plugin_bind_dma_to_vaapi_buffer (plugin, inbuf, outbuf))
      goto error_bind_dma_buffer;
    goto done;
  }
#endif

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

done:
  gst_buffer_copy_into (outbuf, inbuf, GST_BUFFER_COPY_FLAGS |
      GST_BUFFER_COPY_TIMESTAMPS, 0, -1);
  *outbuf_ptr = outbuf;
  return GST_FLOW_OK;

  /* ERRORS */
error_no_pool:
  {
    GST_ELEMENT_ERROR (plugin, STREAM, FAILED,
        ("no buffer pool was negotiated"),
        ("no buffer pool was negotiated"));
    return GST_FLOW_ERROR;
  }
error_active_pool:
  {
    GST_ELEMENT_ERROR (plugin, STREAM, FAILED,
        ("failed to activate buffer pool"),
        ("failed to activate buffer pool"));
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
    GST_ELEMENT_ERROR (plugin, STREAM, FAILED,
        ("failed to validate source buffer"),
        ("failed to validate source buffer"));
    return GST_FLOW_ERROR;
  }
error_create_buffer:
  {
    GST_ELEMENT_ERROR (plugin, STREAM, FAILED, ("Allocation failed"),
        ("failed to create buffer"));
    return GST_FLOW_ERROR;
  }
error_bind_dma_buffer:
  {
    GST_ELEMENT_ERROR (plugin, STREAM, FAILED, ("Allocation failed"),
        ("failed to bind dma_buf to VA surface buffer"));
    gst_buffer_unref (outbuf);
    return GST_FLOW_ERROR;
  }
error_copy_buffer:
  {
    GST_WARNING ("failed to upload buffer to VA surface");
    gst_buffer_unref (outbuf);
    return GST_FLOW_NOT_SUPPORTED;
  }
}

/**
 * gst_vaapi_plugin_base_set_gl_context:
 * @plugin: a #GstVaapiPluginBase
 * @object: the new GL context from downstream
 *
 * Registers the new GL context. The change is effective at the next
 * call to gst_vaapi_plugin_base_ensure_display(), where the
 * underlying display object could be re-allocated to fit the GL
 * context needs
 */
void
gst_vaapi_plugin_base_set_gl_context (GstVaapiPluginBase * plugin,
    GstObject * object)
{
#if USE_GST_GL_HELPERS
  GstGLContext *const gl_context = GST_GL_CONTEXT (object);
  GstVaapiDisplayType display_type;

  gst_object_replace (&plugin->gl_context, object);

  switch (gst_gl_context_get_gl_platform (gl_context)) {
#if USE_GLX
    case GST_GL_PLATFORM_GLX:
      display_type = GST_VAAPI_DISPLAY_TYPE_GLX;
      break;
#endif
    default:
      display_type = plugin->display_type;
      break;
  }
  gst_vaapi_plugin_base_set_display_type (plugin, display_type);
#endif
}
