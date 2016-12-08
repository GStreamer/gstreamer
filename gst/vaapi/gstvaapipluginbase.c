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

#include "gstcompat.h"
#include <gst/vaapi/gstvaapisurface_drm.h>
#include <gst/base/gstpushsrc.h>
#include "gstvaapipluginbase.h"
#include "gstvaapipluginutil.h"
#include "gstvaapivideocontext.h"
#include "gstvaapivideometa.h"
#include "gstvaapivideobufferpool.h"
#if USE_GST_GL_HELPERS
# include <gst/gl/gl.h>
#endif

/* Default debug category is from the subclass */
#define GST_CAT_DEFAULT (plugin->debug_category)

#define BUFFER_POOL_SINK_MIN_BUFFERS 2

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
    GST_INFO_OBJECT (plugin, "set display %" GST_PTR_FORMAT, display);
    gst_vaapi_display_replace (&plugin->display, display);
    plugin->display_type = gst_vaapi_display_get_display_type (display);
    gst_vaapi_plugin_base_set_display_name (plugin, display_name);
  }
  gst_vaapi_display_unref (display);
}

/**
 * gst_vaapi_plugin_base_set_context:
 * @plugin: a #GstVaapiPluginBase instance
 * @context: a #GstContext to set
 *
 * This is a common set_context() element's vmethod for all the
 * GStreamer VA-API elements.
 *
 * It normally should be used through the macro
 * #GST_VAAPI_PLUGIN_BASE_DEFINE_SET_CONTEXT()
 **/
void
gst_vaapi_plugin_base_set_context (GstVaapiPluginBase * plugin,
    GstContext * context)
{
  GstVaapiDisplay *display = NULL;

  if (gst_vaapi_video_context_get_display (context, &display))
    plugin_set_display (plugin, display);

#if USE_GST_GL_HELPERS
  gst_gl_handle_set_context (GST_ELEMENT_CAST (plugin), context,
      (GstGLDisplay **) & plugin->gl_display,
      (GstGLContext **) & plugin->gl_other_context);
#endif
}

void
gst_vaapi_plugin_base_init_interfaces (GType g_define_type_id)
{
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

static GstVaapiSurface *
_get_cached_surface (GstBuffer * buf)
{
  return gst_mini_object_get_qdata (GST_MINI_OBJECT (buf),
      g_quark_from_static_string ("GstVaapiDMABufSurface"));
}

static void
_set_cached_surface (GstBuffer * buf, GstVaapiSurface * surface)
{
  return gst_mini_object_set_qdata (GST_MINI_OBJECT (buf),
      g_quark_from_static_string ("GstVaapiDMABufSurface"), surface,
      (GDestroyNotify) gst_vaapi_object_unref);
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

  /* Check for a VASurface cached in the buffer */
  surface = _get_cached_surface (inbuf);
  if (!surface) {
    /* otherwise create one and cache it */
    surface =
        gst_vaapi_surface_new_with_dma_buf_handle (plugin->display, fd, vip);
    if (!surface)
      goto error_create_surface;
    _set_cached_surface (inbuf, surface);
  }

  proxy = gst_vaapi_surface_proxy_new (surface);
  if (!proxy)
    goto error_create_proxy;
  gst_vaapi_video_meta_set_surface_proxy (meta, proxy);
  gst_vaapi_surface_proxy_unref (proxy);
  gst_buffer_add_parent_buffer_meta (outbuf, inbuf);
  return TRUE;

  /* ERRORS */
error_update_sinkpad_info:
  {
    GST_ERROR_OBJECT (plugin,
        "failed to update sink pad video info from video meta");
    return FALSE;
  }
error_create_surface:
  {
    GST_ERROR_OBJECT (plugin,
        "failed to create VA surface from dma_buf handle");
    return FALSE;
  }
error_create_proxy:
  {
    GST_ERROR_OBJECT (plugin,
        "failed to create VA surface proxy from wrapped VA surface");
    return FALSE;
  }
}

static void
plugin_reset_texture_map (GstVaapiPluginBase * plugin)
{
  if (plugin->display)
    gst_vaapi_display_reset_texture_map (plugin->display);
}

void
gst_vaapi_plugin_base_class_init (GstVaapiPluginBaseClass * klass)
{
  klass->has_interface = default_has_interface;
  klass->display_changed = default_display_changed;
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
  gst_video_info_init (&plugin->sinkpad_info);

  /* src pad */
  if (!(GST_OBJECT_FLAGS (plugin) & GST_ELEMENT_FLAG_SINK))
    plugin->srcpad = gst_element_get_static_pad (GST_ELEMENT (plugin), "src");
  gst_video_info_init (&plugin->srcpad_info);

  plugin->enable_direct_rendering =
      (g_getenv ("GST_VAAPI_ENABLE_DIRECT_RENDERING") != NULL);
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
  /* Release vaapi textures first if exist, which refs display object */
  plugin_reset_texture_map (plugin);

  gst_vaapi_display_replace (&plugin->display, NULL);
  gst_object_replace (&plugin->gl_context, NULL);
  gst_object_replace (&plugin->gl_display, NULL);
  gst_object_replace (&plugin->gl_other_context, NULL);

  gst_caps_replace (&plugin->sinkpad_caps, NULL);
  gst_video_info_init (&plugin->sinkpad_info);
  if (plugin->sinkpad_buffer_pool) {
    gst_object_unref (plugin->sinkpad_buffer_pool);
    plugin->sinkpad_buffer_pool = NULL;
  }
  g_clear_object (&plugin->srcpad_buffer_pool);

  g_clear_object (&plugin->sinkpad_allocator);
  g_clear_object (&plugin->srcpad_allocator);

  gst_caps_replace (&plugin->srcpad_caps, NULL);
  gst_video_info_init (&plugin->srcpad_info);
  gst_caps_replace (&plugin->allowed_raw_caps, NULL);
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

  if (!gst_vaapi_ensure_display (GST_ELEMENT (plugin),
          plugin->display_type_req))
    return FALSE;
  plugin->display_type = gst_vaapi_display_get_display_type (plugin->display);

  GST_VAAPI_PLUGIN_BASE_GET_CLASS (plugin)->display_changed (plugin);
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

  gst_object_ref (pad);

  for (;;) {
    other_pad = gst_pad_get_peer (pad);
    gst_object_unref (pad);
    if (!other_pad)
      break;

    element = gst_pad_get_parent_element (other_pad);
    gst_object_unref (other_pad);
    if (!element)
      break;

    if (GST_IS_PUSH_SRC (element)) {
      element_name = gst_element_get_name (element);
      if (!element_name)
        break;

      if ((sscanf (element_name, "v4l2src%d", &v) != 1)
          && (sscanf (element_name, "camerasrc%d", &v) != 1))
        break;

      v = 0;
      g_object_get (element, "io-mode", &v, NULL);
      if (strncmp (element_name, "camerasrc", 9) == 0)
        is_dmabuf_capable = v == 3;
      else
        is_dmabuf_capable = v == 5;     /* "dmabuf-import" enum value */
      break;
    } else if (GST_IS_BASE_TRANSFORM (element)) {
      element_name = gst_element_get_name (element);
      if (!element_name || sscanf (element_name, "capsfilter%d", &v) != 1)
        break;

      pad = gst_element_get_static_pad (element, "sink");
      if (!pad)
        break;
    } else
      break;

    g_free (element_name);
    element_name = NULL;
    g_clear_object (&element);
  }

  g_free (element_name);
  g_clear_object (&element);
  return is_dmabuf_capable;
}

static gboolean
gst_vaapi_buffer_pool_caps_is_equal (GstBufferPool * pool, GstCaps * newcaps)
{
  GstStructure *config;
  GstCaps *caps;
  gboolean ret;

  caps = NULL;
  ret = FALSE;
  config = gst_buffer_pool_get_config (pool);
  if (gst_buffer_pool_config_get_params (config, &caps, NULL, NULL, NULL))
    ret = gst_caps_is_equal (newcaps, caps);
  gst_structure_free (config);

  return ret;
}

static inline gboolean
reset_allocator (GstAllocator * allocator, GstVideoInfo * vinfo)
{
  const GstVideoInfo *orig_vi;

  if (!allocator)
    return TRUE;

  orig_vi = gst_allocator_get_vaapi_video_info (allocator, NULL);
  if (!gst_video_info_changed (orig_vi, vinfo))
    return FALSE;

  gst_object_unref (allocator);
  return TRUE;
}

static gboolean
ensure_sinkpad_allocator (GstVaapiPluginBase * plugin, GstCaps * caps,
    guint * size)
{
  GstVideoInfo vinfo;
  GstVaapiImageUsageFlags usage_flag =
      GST_VAAPI_IMAGE_USAGE_FLAG_NATIVE_FORMATS;

  if (!gst_video_info_from_caps (&vinfo, caps))
    goto error_invalid_caps;
  gst_video_info_force_nv12_if_encoded (&vinfo);
  *size = GST_VIDEO_INFO_SIZE (&vinfo);

  if (!reset_allocator (plugin->sinkpad_allocator, &vinfo))
    return TRUE;

  if (has_dmabuf_capable_peer (plugin, plugin->sinkpad)) {
    plugin->sinkpad_allocator =
        gst_vaapi_dmabuf_allocator_new (plugin->display, &vinfo,
        GST_VAAPI_SURFACE_ALLOC_FLAG_LINEAR_STORAGE, GST_PAD_SINK);
    goto bail;
  }

  /* enable direct upload if upstream requests raw video */
  if (gst_caps_is_video_raw (caps)) {
    usage_flag = GST_VAAPI_IMAGE_USAGE_FLAG_DIRECT_UPLOAD;
    GST_INFO_OBJECT (plugin, "enabling direct upload in sink allocator");
  }
  plugin->sinkpad_allocator =
      gst_vaapi_video_allocator_new (plugin->display, &vinfo, 0, usage_flag);

bail:
  if (!plugin->sinkpad_allocator)
    goto error_create_allocator;
  return TRUE;

  /* ERRORS */
error_invalid_caps:
  {
    GST_ERROR_OBJECT (plugin, "invalid caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }
error_create_allocator:
  {
    GST_ERROR_OBJECT (plugin, "failed to create sink pad's allocator");
    return FALSE;
  }
}

static inline guint
get_dmabuf_surface_allocation_flags (void)
{
  /* @FIXME: fetch the real devices ids */
  /* Pair vendor/device identifies an unique physical device. */
  guint va_vendor_id = 0x00;
  guint va_device_id = 0x00;
  guint gl_vendor_id = 0x00;
  guint gl_device_id = 0x00;

  /* Requires linear memory only if fd export is done on a different
   * device than the device where the fd is imported. */
  gboolean same_physical_device = va_vendor_id == gl_vendor_id
      && va_device_id == gl_device_id;

  if (same_physical_device)
    return 0;
  return GST_VAAPI_SURFACE_ALLOC_FLAG_LINEAR_STORAGE;
}

static gboolean
ensure_srcpad_allocator (GstVaapiPluginBase * plugin, GstVideoInfo * vinfo,
    GstCaps * caps)
{
  gboolean different_caps;
  GstVideoInfo vi;
  GstVaapiImageUsageFlags usage_flag =
      GST_VAAPI_IMAGE_USAGE_FLAG_NATIVE_FORMATS;

  /* the received caps are the "allocation caps" which may be
   * different from the "negotiation caps". In this case, we should
   * indicate the allocator to store the negotiation caps since they
   * are the one should be used for frame mapping with GstVideoMeta */
  different_caps = GST_IS_VIDEO_DECODER (plugin) && plugin->srcpad_caps &&
      !gst_caps_is_strictly_equal (plugin->srcpad_caps, caps);

  if (different_caps) {
    vi = plugin->srcpad_info;
  } else {
    vi = *vinfo;
  }

  if (!reset_allocator (plugin->srcpad_allocator, &vi))
    return TRUE;

  plugin->srcpad_allocator = NULL;
  if (caps && gst_caps_is_video_raw (caps)) {
    if (plugin->srcpad_can_dmabuf) {
      if (GST_IS_VIDEO_DECODER (plugin) || GST_IS_BASE_TRANSFORM (plugin)) {
        plugin->srcpad_allocator =
            gst_vaapi_dmabuf_allocator_new (plugin->display, vinfo,
            get_dmabuf_surface_allocation_flags (), GST_PAD_SRC);
      }
    } else if (plugin->enable_direct_rendering) {
      usage_flag = GST_VAAPI_IMAGE_USAGE_FLAG_DIRECT_RENDER;
      GST_INFO_OBJECT (plugin, "enabling direct rendering in source allocator");
    }
  }

  if (!plugin->srcpad_allocator) {
    plugin->srcpad_allocator =
        gst_vaapi_video_allocator_new (plugin->display, vinfo, 0, usage_flag);
  }

  if (!plugin->srcpad_allocator)
    goto error_create_allocator;

  if (different_caps) {
    guint i, flags = 0;
    const GstVideoInfo *alloc_vi =
        gst_allocator_get_vaapi_video_info (plugin->srcpad_allocator, &flags);
    /* update the planes and the size with the allocator image info,
     * but not the resolution */
    if (alloc_vi) {
      for (i = 0; i < GST_VIDEO_INFO_N_PLANES (alloc_vi); i++) {
        GST_VIDEO_INFO_PLANE_OFFSET (&vi, i) =
            GST_VIDEO_INFO_PLANE_OFFSET (alloc_vi, i);
        GST_VIDEO_INFO_PLANE_STRIDE (&vi, i) =
            GST_VIDEO_INFO_PLANE_STRIDE (alloc_vi, i);
      }
      GST_VIDEO_INFO_SIZE (&vi) = GST_VIDEO_INFO_SIZE (alloc_vi);
      gst_allocator_set_vaapi_video_info (plugin->srcpad_allocator, &vi, flags);
    }
  }

  return TRUE;

  /* ERRORS */
error_create_allocator:
  {
    GST_ERROR_OBJECT (plugin, "failed to create src pad's allocator");
    return FALSE;
  }
}

/**
 * gst_vaapi_plugin_base_create_pool:
 * @plugin: a #GstVaapiPluginBase
 * @caps: the initial #GstCaps for the resulting buffer pool
 * @size: the size of each buffer, not including prefix and padding
 * @options: a set of #GstVaapiVideoBufferPoolOption encoded as bit-wise
 * @allocator: (allow-none): the #GstAllocator to use or %NULL
 *
 * Create an instance of #GstVaapiVideoBufferPool
 *
 * Returns: (transfer full): a new allocated #GstBufferPool
 **/
static GstBufferPool *
gst_vaapi_plugin_base_create_pool (GstVaapiPluginBase * plugin, GstCaps * caps,
    gsize size, guint min_buffers, guint max_buffers, guint options,
    GstAllocator * allocator)
{
  GstBufferPool *pool;
  GstStructure *config;

  if (!(pool = gst_vaapi_video_buffer_pool_new (plugin->display)))
    goto error_create_pool;

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, caps, size, min_buffers,
      max_buffers);
  gst_buffer_pool_config_add_option (config,
      GST_BUFFER_POOL_OPTION_VAAPI_VIDEO_META);
  if (options & GST_VAAPI_VIDEO_BUFFER_POOL_OPTION_VIDEO_META) {
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);
  }
  if (options & GST_VAAPI_VIDEO_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT) {
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);
  }
#if (USE_GLX || USE_EGL)
  if (options & GST_VAAPI_VIDEO_BUFFER_POOL_OPTION_GL_TEXTURE_UPLOAD) {
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_GL_TEXTURE_UPLOAD_META);
  }
#endif
  if (allocator)
    gst_buffer_pool_config_set_allocator (config, allocator, NULL);
  if (!gst_buffer_pool_set_config (pool, config)) {
    config = gst_buffer_pool_get_config (pool);

    if (!gst_buffer_pool_config_validate_params (config, caps, size,
            min_buffers, max_buffers)) {
      gst_structure_free (config);
      goto error_pool_config;
    }

    if (!gst_buffer_pool_set_config (pool, config))
      goto error_pool_config;
  }
  return pool;

  /* ERRORS */
error_create_pool:
  {
    GST_ERROR_OBJECT (plugin, "failed to create buffer pool");
    return NULL;
  }
error_pool_config:
  {
    gst_object_unref (pool);
    GST_ELEMENT_ERROR (plugin, RESOURCE, SETTINGS,
        ("Failed to configure the buffer pool"),
        ("Configuration is most likely invalid, please report this issue."));
    return NULL;
  }
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
  GstBufferPool *pool;
  guint size;

  /* video decoders don't use a buffer pool in the sink pad */
  if (GST_IS_VIDEO_DECODER (plugin))
    return TRUE;

  if (!gst_vaapi_plugin_base_ensure_display (plugin))
    return FALSE;

  if (plugin->sinkpad_buffer_pool) {
    if (gst_vaapi_buffer_pool_caps_is_equal (plugin->sinkpad_buffer_pool, caps))
      return TRUE;
    gst_buffer_pool_set_active (plugin->sinkpad_buffer_pool, FALSE);
    g_clear_object (&plugin->sinkpad_buffer_pool);
    g_clear_object (&plugin->sinkpad_allocator);
    plugin->sinkpad_buffer_size = 0;
  }

  if (!ensure_sinkpad_allocator (plugin, caps, &size))
    return FALSE;

  pool =
      gst_vaapi_plugin_base_create_pool (plugin, caps, size,
      BUFFER_POOL_SINK_MIN_BUFFERS, 0,
      GST_VAAPI_VIDEO_BUFFER_POOL_OPTION_VIDEO_META, plugin->sinkpad_allocator);
  if (!pool)
    return FALSE;

  plugin->sinkpad_buffer_pool = pool;
  plugin->sinkpad_buffer_size = size;
  return TRUE;
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
    if (!gst_video_info_from_caps (&plugin->sinkpad_info, incaps))
      return FALSE;
    gst_caps_replace (&plugin->sinkpad_caps, incaps);
    plugin->sinkpad_caps_is_raw = !gst_caps_has_vaapi_surface (incaps);
  }

  if (outcaps && outcaps != plugin->srcpad_caps) {
    if (!gst_video_info_from_caps (&plugin->srcpad_info, outcaps))
      return FALSE;
    if (plugin->srcpad_buffer_pool
        && !gst_vaapi_buffer_pool_caps_is_equal (plugin->srcpad_buffer_pool,
            outcaps)) {
      gst_buffer_pool_set_active (plugin->srcpad_buffer_pool, FALSE);
      g_clear_object (&plugin->srcpad_buffer_pool);
      g_clear_object (&plugin->srcpad_allocator);
      plugin_reset_texture_map (plugin);
    }
    gst_caps_replace (&plugin->srcpad_caps, outcaps);
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
gboolean
gst_vaapi_plugin_base_propose_allocation (GstVaapiPluginBase * plugin,
    GstQuery * query)
{
  GstCaps *caps = NULL;
  gboolean need_pool;

  gst_query_parse_allocation (query, &caps, &need_pool);
  if (!caps)
    goto error_no_caps;

  if (need_pool) {
    if (!ensure_sinkpad_buffer_pool (plugin, caps))
      return FALSE;
    gst_query_add_allocation_pool (query, plugin->sinkpad_buffer_pool,
        plugin->sinkpad_buffer_size, BUFFER_POOL_SINK_MIN_BUFFERS, 0);
    gst_query_add_allocation_param (query, plugin->sinkpad_allocator, NULL);
  }

  gst_query_add_allocation_meta (query, GST_VAAPI_VIDEO_META_API_TYPE, NULL);
  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);
  return TRUE;

  /* ERRORS */
error_no_caps:
  {
    GST_INFO_OBJECT (plugin, "no caps specified");
    return FALSE;
  }
}

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
gboolean
gst_vaapi_plugin_base_decide_allocation (GstVaapiPluginBase * plugin,
    GstQuery * query)
{
  GstCaps *caps = NULL;
  GstBufferPool *pool;
  GstVideoInfo vi;
  guint size, min, max, pool_options;
  gboolean update_pool = FALSE, update_allocator = FALSE;
#if (USE_GLX || USE_EGL)
  guint idx;
#endif

  gst_query_parse_allocation (query, &caps, NULL);
  if (!caps)
    goto error_no_caps;

  pool_options = 0;
  if (gst_query_find_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL))
    pool_options |= GST_VAAPI_VIDEO_BUFFER_POOL_OPTION_VIDEO_META;

#if (USE_GLX || USE_EGL)
  if (gst_query_find_allocation_meta (query,
          GST_VIDEO_GL_TEXTURE_UPLOAD_META_API_TYPE, &idx) &&
      gst_vaapi_caps_feature_contains (caps,
          GST_VAAPI_CAPS_FEATURE_GL_TEXTURE_UPLOAD_META))
    pool_options |= GST_VAAPI_VIDEO_BUFFER_POOL_OPTION_GL_TEXTURE_UPLOAD;

#if USE_GST_GL_HELPERS
  if (!plugin->gl_context &&
      (pool_options & GST_VAAPI_VIDEO_BUFFER_POOL_OPTION_GL_TEXTURE_UPLOAD)) {
    const GstStructure *params;
    GstObject *gl_context;

    gst_query_parse_nth_allocation_meta (query, idx, &params);
    if (params) {
      if (gst_structure_get (params, "gst.gl.GstGLContext", GST_TYPE_GL_CONTEXT,
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

  if (!gst_video_info_from_caps (&vi, caps))
    goto error_invalid_caps;
  gst_video_info_force_nv12_if_encoded (&vi);

  if (gst_query_get_n_allocation_params (query) > 0)
    update_allocator = TRUE;

  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
    update_pool = TRUE;
    size = MAX (size, GST_VIDEO_INFO_SIZE (&vi));
    if (pool) {
      /* Check whether downstream element proposed a bufferpool but did
         not provide a correct propose_allocation() implementation */
      if (gst_buffer_pool_has_option (pool,
              GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT))
        pool_options |= GST_VAAPI_VIDEO_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT;

      /* GstVaapiVideoMeta is mandatory, and this implies VA surface memory */
      if (!gst_buffer_pool_has_option (pool,
              GST_BUFFER_POOL_OPTION_VAAPI_VIDEO_META)) {
        GST_INFO_OBJECT (plugin, "ignoring non-VAAPI pool: %" GST_PTR_FORMAT,
            pool);
        g_clear_object (&pool);
      }
    }
  } else {
    pool = NULL;
    size = GST_VIDEO_INFO_SIZE (&vi);
    min = max = 0;
  }

  if (!pool) {
    if (!ensure_srcpad_allocator (plugin, &vi, caps))
      goto error;
    pool = gst_vaapi_plugin_base_create_pool (plugin, caps, size, min, max,
        pool_options, plugin->srcpad_allocator);
    if (!pool)
      goto error;
  }

  if (update_pool)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool (query, pool, size, min, max);

  if (update_allocator)
    gst_query_set_nth_allocation_param (query, 0, plugin->srcpad_allocator,
        NULL);
  else
    gst_query_add_allocation_param (query, plugin->srcpad_allocator, NULL);

  g_clear_object (&plugin->srcpad_buffer_pool);
  plugin->srcpad_buffer_pool = pool;
  return TRUE;

  /* ERRORS */
error_no_caps:
  {
    GST_ERROR_OBJECT (plugin, "no caps specified");
    return FALSE;
  }
error_invalid_caps:
  {
    GST_ERROR_OBJECT (plugin, "invalid caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }
error_ensure_display:
  {
    GST_ERROR_OBJECT (plugin, "failed to ensure display of type %d",
        plugin->display_type_req);
    return FALSE;
  }
error:
  {
    /* error message already sent */
    return FALSE;
  }
}

/**
 * gst_vaapi_plugin_base_get_input_buffer:
 * @plugin: a #GstVaapiPluginBase
 * @inbuf: the sink pad (input) buffer
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
  GstVideoFrame src_frame, out_frame;
  gboolean success;

  g_return_val_if_fail (inbuf != NULL, GST_FLOW_ERROR);
  g_return_val_if_fail (outbuf_ptr != NULL, GST_FLOW_ERROR);

  meta = gst_buffer_get_vaapi_video_meta (inbuf);
  if (meta) {
    *outbuf_ptr = gst_buffer_ref (inbuf);
    return GST_FLOW_OK;
  }

  if (!plugin->sinkpad_caps_is_raw)
    goto error_invalid_buffer;

  if (!plugin->sinkpad_buffer_pool)
    goto error_no_pool;

  if (!gst_buffer_pool_is_active (plugin->sinkpad_buffer_pool) &&
      !gst_buffer_pool_set_active (plugin->sinkpad_buffer_pool, TRUE))
    goto error_active_pool;

  outbuf = NULL;
  if (gst_buffer_pool_acquire_buffer (plugin->sinkpad_buffer_pool,
          &outbuf, NULL) != GST_FLOW_OK)
    goto error_create_buffer;

  if (is_dma_buffer (inbuf)) {
    if (!plugin_bind_dma_to_vaapi_buffer (plugin, inbuf, outbuf))
      goto error_bind_dma_buffer;
    goto done;
  }

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
        ("no buffer pool was negotiated"), ("no buffer pool was negotiated"));
    return GST_FLOW_ERROR;
  }
error_active_pool:
  {
    GST_ELEMENT_ERROR (plugin, STREAM, FAILED,
        ("failed to activate buffer pool"), ("failed to activate buffer pool"));
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
    GST_WARNING_OBJECT (plugin, "failed to upload buffer to VA surface");
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

  if (plugin->gl_context == object)
    return;

  gst_object_replace (&plugin->gl_context, object);

  switch (gst_gl_context_get_gl_platform (gl_context)) {
#if USE_GLX
    case GST_GL_PLATFORM_GLX:
      display_type = GST_VAAPI_DISPLAY_TYPE_GLX;
      break;
#endif
    case GST_GL_PLATFORM_EGL:
#if VA_CHECK_VERSION (0,36,0) && USE_GST_GL_HELPERS
      plugin->srcpad_can_dmabuf =
          (!(gst_gl_context_get_gl_api (gl_context) & GST_GL_API_GLES1)
          && gst_gl_context_check_feature (gl_context,
              "EGL_EXT_image_dma_buf_import"));
#endif
#if USE_EGL
      display_type = GST_VAAPI_DISPLAY_TYPE_EGL;
      break;
#endif
    default:
      display_type = plugin->display_type;
      break;
  }
  GST_INFO_OBJECT (plugin, "GL context: %" GST_PTR_FORMAT, plugin->gl_context);
  gst_vaapi_plugin_base_set_display_type (plugin, display_type);
#endif
}

/**
 * gst_vaapi_plugin_base_create_gl_context:
 * @plugin: a #GstVaapiPluginBase
 *
 * It queries downstream and upstream for a #GstGLDisplay and a other
 * #GstGLContext. If not found, a new #GstGLDisplay and #GstGLContext
 * are created, if it is possible.
 *
 * Returns: (transfer full) a new created #GstGLContext or %NULL
 **/
GstObject *
gst_vaapi_plugin_base_create_gl_context (GstVaapiPluginBase * plugin)
{
#if USE_GST_GL_HELPERS
  GstGLContext *gl_other_context, *gl_context = NULL;
  GstGLDisplay *gl_display;

  gst_gl_ensure_element_data (plugin, (GstGLDisplay **) & plugin->gl_display,
      (GstGLContext **) & plugin->gl_other_context);

  gl_display = (GstGLDisplay *) plugin->gl_display;
  if (!gl_display ||
      gst_gl_display_get_handle_type (gl_display) == GST_GL_DISPLAY_TYPE_ANY) {
    gst_object_replace (&plugin->gl_display, NULL);
    gst_object_replace (&plugin->gl_other_context, NULL);
    return NULL;
  }
  gl_other_context = (GstGLContext *) plugin->gl_other_context;

  GST_INFO_OBJECT (plugin, "creating a new GstGL context");

  GST_OBJECT_LOCK (gl_display);
  do {
    if (gl_context)
      gst_object_unref (gl_context);
    gl_context = gst_gl_display_get_gl_context_for_thread (gl_display, NULL);
    if (!gl_context) {
      if (!gst_gl_display_create_context (gl_display, gl_other_context,
              &gl_context, NULL))
        break;
    }
  } while (!gst_gl_display_add_context (gl_display, gl_context));
  GST_OBJECT_UNLOCK (gl_display);

  return GST_OBJECT_CAST (gl_context);
#else
  return NULL;
#endif
}

static gboolean
ensure_allowed_raw_caps (GstVaapiPluginBase * plugin)
{
  GArray *formats, *out_formats;
  GstVaapiSurface *surface;
  GstVaapiDisplay *display;
  guint i;
  GstCaps *out_caps;
  gboolean ret = FALSE;

  if (plugin->allowed_raw_caps)
    return TRUE;

  out_formats = formats = NULL;
  surface = NULL;

  display = gst_vaapi_display_ref (plugin->display);
  formats = gst_vaapi_display_get_image_formats (display);
  if (!formats)
    goto bail;

  out_formats =
      g_array_sized_new (FALSE, FALSE, sizeof (GstVideoFormat), formats->len);
  if (!out_formats)
    goto bail;

  surface =
      gst_vaapi_surface_new (display, GST_VAAPI_CHROMA_TYPE_YUV420, 64, 64);
  if (!surface)
    goto bail;

  for (i = 0; i < formats->len; i++) {
    const GstVideoFormat format = g_array_index (formats, GstVideoFormat, i);
    GstVaapiImage *image;

    if (format == GST_VIDEO_FORMAT_UNKNOWN)
      continue;
    image = gst_vaapi_image_new (display, format, 64, 64);
    if (!image)
      continue;
    if (gst_vaapi_surface_put_image (surface, image))
      g_array_append_val (out_formats, format);
    gst_vaapi_object_unref (image);
  }

  out_caps = gst_vaapi_video_format_new_template_caps_from_list (out_formats);
  if (!out_caps)
    goto bail;

  gst_caps_replace (&plugin->allowed_raw_caps, out_caps);
  gst_caps_unref (out_caps);
  ret = TRUE;

bail:
  if (formats)
    g_array_unref (formats);
  if (out_formats)
    g_array_unref (out_formats);
  if (surface)
    gst_vaapi_object_unref (surface);
  gst_vaapi_display_unref (display);

  return ret;
}

/**
 * gst_vaapi_plugin_base_get_allowed_raw_caps:
 * @plugin: a #GstVaapiPluginBase
 *
 * Returns the raw #GstCaps allowed by the element.
 *
 * Returns: the allowed raw #GstCaps or %NULL
 **/
GstCaps *
gst_vaapi_plugin_base_get_allowed_raw_caps (GstVaapiPluginBase * plugin)
{
  if (!ensure_allowed_raw_caps (plugin))
    return NULL;
  return plugin->allowed_raw_caps;
}
