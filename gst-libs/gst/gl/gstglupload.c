/*
 * GStreamer
 * Copyright (C) 2012-2014 Matthew Waters <ystree00@gmail.com>
 * Copyright (C) 2017 Sebastian Dröge <sebastian@centricular.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>

#include "gl.h"
#include "gstglupload.h"

#if GST_GL_HAVE_PLATFORM_EGL
#include "egl/gstglmemoryegl.h"
#include "egl/gstglcontext_egl.h"
#endif

#if GST_GL_HAVE_DMABUF
#include <gst/allocators/gstdmabuf.h>
#endif

#if GST_GL_HAVE_VIV_DIRECTVIV
#include <gst/allocators/gstphysmemory.h>
#endif

/**
 * SECTION:gstglupload
 * @title: GstGLUpload
 * @short_description: an object that uploads to GL textures
 * @see_also: #GstGLDownload, #GstGLMemory
 *
 * #GstGLUpload is an object that uploads data from system memory into GL textures.
 *
 * A #GstGLUpload can be created with gst_gl_upload_new()
 */

#define USING_OPENGL(context) (gst_gl_context_check_gl_version (context, GST_GL_API_OPENGL, 1, 0))
#define USING_OPENGL3(context) (gst_gl_context_check_gl_version (context, GST_GL_API_OPENGL3, 3, 1))
#define USING_GLES(context) (gst_gl_context_check_gl_version (context, GST_GL_API_GLES, 1, 0))
#define USING_GLES2(context) (gst_gl_context_check_gl_version (context, GST_GL_API_GLES2, 2, 0))
#define USING_GLES3(context) (gst_gl_context_check_gl_version (context, GST_GL_API_GLES2, 3, 0))

GST_DEBUG_CATEGORY_STATIC (gst_gl_upload_debug);
#define GST_CAT_DEFAULT gst_gl_upload_debug

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_gl_upload_debug, "glupload", 0, "upload");

G_DEFINE_TYPE_WITH_CODE (GstGLUpload, gst_gl_upload, GST_TYPE_OBJECT,
    DEBUG_INIT);
static void gst_gl_upload_finalize (GObject * object);

#define GST_GL_UPLOAD_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
    GST_TYPE_GL_UPLOAD, GstGLUploadPrivate))

static GstGLTextureTarget
_caps_get_texture_target (GstCaps * caps, GstGLTextureTarget default_target)
{
  GstGLTextureTarget ret = 0;
  GstStructure *s = gst_caps_get_structure (caps, 0);

  if (gst_structure_has_field_typed (s, "texture-target", G_TYPE_STRING)) {
    const gchar *target_str = gst_structure_get_string (s, "texture-target");
    ret = gst_gl_texture_target_from_string (target_str);
  }

  if (!ret)
    ret = default_target;

  return ret;
}

/* Define the maximum number of planes we can upload - handle 2 views per buffer */
#define GST_GL_UPLOAD_MAX_PLANES (GST_VIDEO_MAX_PLANES * 2)

typedef struct _UploadMethod UploadMethod;

struct _GstGLUploadPrivate
{
  GstVideoInfo in_info;
  GstVideoInfo out_info;
  GstCaps *in_caps;
  GstCaps *out_caps;

  GstBuffer *outbuf;

  /* all method impl pointers */
  gpointer *upload_impl;

  /* current method */
  const UploadMethod *method;
  gpointer method_impl;
  int method_i;
};

static GstCaps *
_set_caps_features_with_passthrough (const GstCaps * caps,
    const gchar * feature_name, GstCapsFeatures * passthrough)
{
  guint i, j, m, n;
  GstCaps *tmp;

  tmp = gst_caps_new_empty ();

  n = gst_caps_get_size (caps);
  for (i = 0; i < n; i++) {
    GstCapsFeatures *features, *orig_features;
    GstStructure *s = gst_caps_get_structure (caps, i);

    orig_features = gst_caps_get_features (caps, i);
    features = gst_caps_features_new (feature_name, NULL);

    if (gst_caps_features_is_any (orig_features)) {
      /* if we have any features, we add both the features with and without @passthrough */
      gst_caps_append_structure_full (tmp, gst_structure_copy (s),
          gst_caps_features_copy (features));

      m = gst_caps_features_get_size (passthrough);
      for (j = 0; j < m; j++) {
        const gchar *feature = gst_caps_features_get_nth (passthrough, j);

        /* if we already have the features */
        if (gst_caps_features_contains (features, feature))
          continue;

        gst_caps_features_add (features, feature);
      }
    } else {
      m = gst_caps_features_get_size (orig_features);
      for (j = 0; j < m; j++) {
        const gchar *feature = gst_caps_features_get_nth (orig_features, j);

        /* if we already have the features */
        if (gst_caps_features_contains (features, feature))
          continue;

        if (g_strcmp0 (feature, GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY) == 0)
          continue;

        if (gst_caps_features_contains (passthrough, feature)) {
          gst_caps_features_add (features, feature);
        }
      }
    }

    gst_caps_append_structure_full (tmp, gst_structure_copy (s), features);
  }

  return tmp;
}

static GstCaps *
_caps_intersect_texture_target (GstCaps * caps, GstGLTextureTarget target_mask)
{
  GValue targets = G_VALUE_INIT;
  GstCaps *ret, *target;

  target = gst_caps_copy (caps);
  gst_gl_value_set_texture_target_from_mask (&targets, target_mask);
  gst_caps_set_value (target, "texture-target", &targets);

  ret = gst_caps_intersect_full (caps, target, GST_CAPS_INTERSECT_FIRST);

  g_value_unset (&targets);
  gst_caps_unref (target);
  return ret;
}

typedef enum
{
  METHOD_FLAG_CAN_SHARE_CONTEXT = 1,
} GstGLUploadMethodFlags;

struct _UploadMethod
{
  const gchar *name;
  GstGLUploadMethodFlags flags;

  GstStaticCaps *input_template_caps;

    gpointer (*new) (GstGLUpload * upload);
  GstCaps *(*transform_caps) (gpointer impl, GstGLContext * context,
      GstPadDirection direction, GstCaps * caps);
    gboolean (*accept) (gpointer impl, GstBuffer * buffer, GstCaps * in_caps,
      GstCaps * out_caps);
  void (*propose_allocation) (gpointer impl, GstQuery * decide_query,
      GstQuery * query);
    GstGLUploadReturn (*perform) (gpointer impl, GstBuffer * buffer,
      GstBuffer ** outbuf);
  void (*free) (gpointer impl);
} _UploadMethod;

struct GLMemoryUpload
{
  GstGLUpload *upload;
  GstGLTextureTarget input_target;
  GstGLTextureTarget output_target;
};

static gpointer
_gl_memory_upload_new (GstGLUpload * upload)
{
  struct GLMemoryUpload *mem = g_new0 (struct GLMemoryUpload, 1);

  mem->upload = upload;
  mem->input_target = GST_GL_TEXTURE_TARGET_NONE;
  mem->output_target = GST_GL_TEXTURE_TARGET_NONE;

  return mem;
}

static GstCaps *
_gl_memory_upload_transform_caps (gpointer impl, GstGLContext * context,
    GstPadDirection direction, GstCaps * caps)
{
  struct GLMemoryUpload *upload = impl;
  GstCapsFeatures *passthrough =
      gst_caps_features_from_string
      (GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION);
  GstCaps *ret;

  ret =
      _set_caps_features_with_passthrough (caps,
      GST_CAPS_FEATURE_MEMORY_GL_MEMORY, passthrough);

  gst_caps_features_free (passthrough);

  if (direction == GST_PAD_SINK) {
    GstCaps *tmp;
    GstGLTextureTarget target_mask;

    if (upload->input_target != GST_GL_TEXTURE_TARGET_NONE) {
      target_mask = 1 << upload->input_target;
    } else {
      target_mask = 1 << GST_GL_TEXTURE_TARGET_2D |
          1 << GST_GL_TEXTURE_TARGET_RECTANGLE |
          1 << GST_GL_TEXTURE_TARGET_EXTERNAL_OES;
    }

    tmp = _caps_intersect_texture_target (ret, target_mask);
    gst_caps_unref (ret);
    ret = tmp;
  } else {
    gint i, n;

    n = gst_caps_get_size (ret);
    for (i = 0; i < n; i++) {
      GstStructure *s = gst_caps_get_structure (ret, i);

      gst_structure_remove_fields (s, "texture-target", NULL);
    }
  }

  return ret;
}

static gboolean
_gl_memory_upload_accept (gpointer impl, GstBuffer * buffer, GstCaps * in_caps,
    GstCaps * out_caps)
{
  struct GLMemoryUpload *upload = impl;
  GstCapsFeatures *features;
  int i;

  features = gst_caps_get_features (out_caps, 0);
  if (!gst_caps_features_contains (features, GST_CAPS_FEATURE_MEMORY_GL_MEMORY))
    return FALSE;

  features = gst_caps_get_features (in_caps, 0);
  if (!gst_caps_features_contains (features, GST_CAPS_FEATURE_MEMORY_GL_MEMORY)
      && !gst_caps_features_contains (features,
          GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY))
    return FALSE;

  if (buffer) {
    GstVideoInfo *in_info = &upload->upload->priv->in_info;
    guint expected_memories = GST_VIDEO_INFO_N_PLANES (in_info);

    /* Support stereo views for separated multiview mode */
    if (GST_VIDEO_INFO_MULTIVIEW_MODE (in_info) ==
        GST_VIDEO_MULTIVIEW_MODE_SEPARATED)
      expected_memories *= GST_VIDEO_INFO_VIEWS (in_info);

    if (gst_buffer_n_memory (buffer) != expected_memories)
      return FALSE;

    for (i = 0; i < expected_memories; i++) {
      GstMemory *mem = gst_buffer_peek_memory (buffer, i);

      if (!gst_is_gl_memory (mem))
        return FALSE;
    }
  }

  return TRUE;
}

static void
_gl_memory_upload_propose_allocation (gpointer impl, GstQuery * decide_query,
    GstQuery * query)
{
  struct GLMemoryUpload *upload = impl;
  GstBufferPool *pool = NULL;
  guint n_pools, i;
  GstCaps *caps;
  GstCapsFeatures *features;

  gst_query_parse_allocation (query, &caps, NULL);
  features = gst_caps_get_features (caps, 0);

  /* Only offer our custom allocator if that type of memory was negotiated. */
  if (gst_caps_features_contains (features, GST_CAPS_FEATURE_MEMORY_GL_MEMORY)) {
    GstAllocator *allocator;
    GstAllocationParams params;
    gst_allocation_params_init (&params);

    allocator =
        GST_ALLOCATOR (gst_gl_memory_allocator_get_default (upload->
            upload->context));
    gst_query_add_allocation_param (query, allocator, &params);
    gst_object_unref (allocator);

#if GST_GL_HAVE_PLATFORM_EGL
    if (upload->upload->context
        && gst_gl_context_get_gl_platform (upload->upload->context) ==
        GST_GL_PLATFORM_EGL) {
      allocator =
          GST_ALLOCATOR (gst_allocator_find (GST_GL_MEMORY_EGL_ALLOCATOR_NAME));
      gst_query_add_allocation_param (query, allocator, &params);
      gst_object_unref (allocator);
    }
#endif
  }

  n_pools = gst_query_get_n_allocation_pools (query);
  for (i = 0; i < n_pools; i++) {
    gst_query_parse_nth_allocation_pool (query, i, &pool, NULL, NULL, NULL);
    if (!GST_IS_GL_BUFFER_POOL (pool)) {
      gst_object_unref (pool);
      pool = NULL;
    }
  }

  if (!pool) {
    GstStructure *config;
    GstVideoInfo info;
    gsize size;


    if (!gst_video_info_from_caps (&info, caps))
      goto invalid_caps;

    pool = gst_gl_buffer_pool_new (upload->upload->context);
    config = gst_buffer_pool_get_config (pool);

    /* the normal size of a frame */
    size = info.size;
    gst_buffer_pool_config_set_params (config, caps, size, 0, 0);
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_GL_SYNC_META);
    if (upload->upload->priv->out_caps) {
      GstGLTextureTarget target;
      const gchar *target_pool_option_str;

      target =
          _caps_get_texture_target (upload->upload->priv->out_caps,
          GST_GL_TEXTURE_TARGET_2D);
      target_pool_option_str =
          gst_gl_texture_target_to_buffer_pool_option (target);
      gst_buffer_pool_config_add_option (config, target_pool_option_str);
    }

    if (!gst_buffer_pool_set_config (pool, config)) {
      gst_object_unref (pool);
      goto config_failed;
    }

    gst_query_add_allocation_pool (query, pool, size, 1, 0);
  }

  if (pool)
    gst_object_unref (pool);

  return;

invalid_caps:
  {
    GST_WARNING_OBJECT (upload->upload, "invalid caps specified");
    return;
  }
config_failed:
  {
    GST_WARNING_OBJECT (upload->upload, "failed setting config");
    return;
  }
}

static GstGLUploadReturn
_gl_memory_upload_perform (gpointer impl, GstBuffer * buffer,
    GstBuffer ** outbuf)
{
  struct GLMemoryUpload *upload = impl;
  GstGLMemory *gl_mem;
  int i, n;

  n = gst_buffer_n_memory (buffer);
  for (i = 0; i < n; i++) {
    GstMemory *mem = gst_buffer_peek_memory (buffer, i);

    gl_mem = (GstGLMemory *) mem;
    if (!gst_gl_context_can_share (upload->upload->context,
            gl_mem->mem.context))
      return GST_GL_UPLOAD_UNSHARED_GL_CONTEXT;

    if (upload->output_target == GST_GL_TEXTURE_TARGET_NONE &&
        upload->upload->priv->out_caps) {
      upload->output_target =
          _caps_get_texture_target (upload->upload->priv->out_caps,
          GST_GL_TEXTURE_TARGET_NONE);
    }

    /* always track the last input texture target so ::transform_caps() can
     * use it to build the output caps */
    upload->input_target = gl_mem->tex_target;
    if (upload->output_target != gl_mem->tex_target) {
      *outbuf = NULL;
      return GST_GL_UPLOAD_RECONFIGURE;
    }

    if (gst_is_gl_memory_pbo (mem))
      gst_gl_memory_pbo_upload_transfer ((GstGLMemoryPBO *) mem);
  }

  *outbuf = gst_buffer_ref (buffer);

  return GST_GL_UPLOAD_DONE;
}

static void
_gl_memory_upload_free (gpointer impl)
{
  g_free (impl);
}


static GstStaticCaps _gl_memory_upload_caps =
GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
    (GST_CAPS_FEATURE_MEMORY_GL_MEMORY, GST_GL_MEMORY_VIDEO_FORMATS_STR));

static const UploadMethod _gl_memory_upload = {
  "GLMemory",
  METHOD_FLAG_CAN_SHARE_CONTEXT,
  &_gl_memory_upload_caps,
  &_gl_memory_upload_new,
  &_gl_memory_upload_transform_caps,
  &_gl_memory_upload_accept,
  &_gl_memory_upload_propose_allocation,
  &_gl_memory_upload_perform,
  &_gl_memory_upload_free
};

#if GST_GL_HAVE_DMABUF
struct DmabufUpload
{
  GstGLUpload *upload;

  GstEGLImage *eglimage[GST_VIDEO_MAX_PLANES];
  GstBuffer *outbuf;
  GstGLVideoAllocationParams *params;
};

static GstStaticCaps _dma_buf_upload_caps =
GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (GST_GL_MEMORY_VIDEO_FORMATS_STR));

static gpointer
_dma_buf_upload_new (GstGLUpload * upload)
{
  struct DmabufUpload *dmabuf = g_new0 (struct DmabufUpload, 1);
  dmabuf->upload = upload;
  return dmabuf;
}

static GstCaps *
_dma_buf_upload_transform_caps (gpointer impl, GstGLContext * context,
    GstPadDirection direction, GstCaps * caps)
{
  GstCapsFeatures *passthrough =
      gst_caps_features_from_string
      (GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION);
  GstCaps *ret;

  if (direction == GST_PAD_SINK) {
    GstCaps *tmp;

    ret =
        _set_caps_features_with_passthrough (caps,
        GST_CAPS_FEATURE_MEMORY_GL_MEMORY, passthrough);

    tmp = _caps_intersect_texture_target (ret, 1 << GST_GL_TEXTURE_TARGET_2D);
    gst_caps_unref (ret);
    ret = tmp;
  } else {
    gint i, n;

    ret =
        _set_caps_features_with_passthrough (caps,
        GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY, passthrough);

    n = gst_caps_get_size (ret);
    for (i = 0; i < n; i++) {
      GstStructure *s = gst_caps_get_structure (ret, i);

      gst_structure_remove_fields (s, "texture-target", NULL);
    }
  }

  gst_caps_features_free (passthrough);

  return ret;
}

static GQuark
_eglimage_quark (gint plane)
{
  static GQuark quark[4] = { 0 };
  static const gchar *quark_str[] = {
    "GstGLDMABufEGLImage0",
    "GstGLDMABufEGLImage1",
    "GstGLDMABufEGLImage2",
    "GstGLDMABufEGLImage3",
  };

  if (!quark[plane])
    quark[plane] = g_quark_from_static_string (quark_str[plane]);

  return quark[plane];
}

static GstEGLImage *
_get_cached_eglimage (GstMemory * mem, gint plane)
{
  return gst_mini_object_get_qdata (GST_MINI_OBJECT (mem),
      _eglimage_quark (plane));
}

static void
_set_cached_eglimage (GstMemory * mem, GstEGLImage * eglimage, gint plane)
{
  return gst_mini_object_set_qdata (GST_MINI_OBJECT (mem),
      _eglimage_quark (plane), eglimage, (GDestroyNotify) gst_egl_image_unref);
}

static gboolean
_dma_buf_upload_accept (gpointer impl, GstBuffer * buffer, GstCaps * in_caps,
    GstCaps * out_caps)
{
  struct DmabufUpload *dmabuf = impl;
  GstVideoInfo *in_info = &dmabuf->upload->priv->in_info;
  guint n_planes = GST_VIDEO_INFO_N_PLANES (in_info);
  GstVideoMeta *meta;
  guint n_mem;
  guint mems_idx[GST_VIDEO_MAX_PLANES];
  gsize mems_skip[GST_VIDEO_MAX_PLANES];
  GstMemory *mems[GST_VIDEO_MAX_PLANES];
  guint i;

  n_mem = gst_buffer_n_memory (buffer);
  meta = gst_buffer_get_video_meta (buffer);

  /* dmabuf upload is only supported with EGL contexts. */
  if (gst_gl_context_get_gl_platform (dmabuf->upload->context) !=
      GST_GL_PLATFORM_EGL)
    return FALSE;

  if (!gst_gl_context_check_feature (dmabuf->upload->context,
          "EGL_KHR_image_base"))
    return FALSE;

  /* This will eliminate most non-dmabuf out there */
  if (!gst_is_dmabuf_memory (gst_buffer_peek_memory (buffer, 0)))
    return FALSE;

  /* We cannot have multiple dmabuf per plane */
  if (n_mem > n_planes)
    return FALSE;

  /* Update video info based on video meta */
  if (meta) {
    in_info->width = meta->width;
    in_info->height = meta->height;

    for (i = 0; i < meta->n_planes; i++) {
      in_info->offset[i] = meta->offset[i];
      in_info->stride[i] = meta->stride[i];
    }
  }

  if (dmabuf->params)
    gst_gl_allocation_params_free ((GstGLAllocationParams *) dmabuf->params);
  if (!(dmabuf->params =
          gst_gl_video_allocation_params_new_wrapped_gl_handle (dmabuf->
              upload->context, NULL, &dmabuf->upload->priv->in_info, -1, NULL,
              GST_GL_TEXTURE_TARGET_2D, 0, NULL, NULL, NULL)))
    return FALSE;

  /* Find and validate all memories */
  for (i = 0; i < n_planes; i++) {
    guint plane_size;
    guint length;

    plane_size = gst_gl_get_plane_data_size (in_info, NULL, i);

    if (!gst_buffer_find_memory (buffer, in_info->offset[i], plane_size,
            &mems_idx[i], &length, &mems_skip[i]))
      return FALSE;

    /* We can't have more then one dmabuf per plane */
    if (length != 1)
      return FALSE;

    mems[i] = gst_buffer_peek_memory (buffer, mems_idx[i]);

    /* And all memory found must be dmabuf */
    if (!gst_is_dmabuf_memory (mems[i]))
      return FALSE;
  }

  /* Now create an EGLImage for each dmabufs */
  for (i = 0; i < n_planes; i++) {
    /* check if one is cached */
    dmabuf->eglimage[i] = _get_cached_eglimage (mems[i], i);
    if (dmabuf->eglimage[i])
      continue;

    /* otherwise create one and cache it */
    dmabuf->eglimage[i] =
        gst_egl_image_from_dmabuf (dmabuf->upload->context,
        gst_dmabuf_memory_get_fd (mems[i]), in_info, i,
        mems[i]->offset + mems_skip[i]);

    if (!dmabuf->eglimage[i])
      return FALSE;

    _set_cached_eglimage (mems[i], dmabuf->eglimage[i], i);
  }

  return TRUE;
}

static void
_dma_buf_upload_propose_allocation (gpointer impl, GstQuery * decide_query,
    GstQuery * query)
{
  /* nothing to do for now. */
}

static void
_dma_buf_upload_perform_gl_thread (GstGLContext * context,
    struct DmabufUpload *dmabuf)
{
  GstGLMemoryAllocator *allocator;

  allocator =
      GST_GL_MEMORY_ALLOCATOR (gst_allocator_find
      (GST_GL_MEMORY_EGL_ALLOCATOR_NAME));

  /* FIXME: buffer pool */
  dmabuf->outbuf = gst_buffer_new ();
  gst_gl_memory_setup_buffer (allocator, dmabuf->outbuf, dmabuf->params, NULL,
      (gpointer *) dmabuf->eglimage, gst_buffer_n_memory (dmabuf->outbuf));
  gst_object_unref (allocator);
}

static GstGLUploadReturn
_dma_buf_upload_perform (gpointer impl, GstBuffer * buffer, GstBuffer ** outbuf)
{
  struct DmabufUpload *dmabuf = impl;

  gst_gl_context_thread_add (dmabuf->upload->context,
      (GstGLContextThreadFunc) _dma_buf_upload_perform_gl_thread, dmabuf);

  if (!dmabuf->outbuf)
    return GST_GL_UPLOAD_ERROR;

  gst_buffer_add_parent_buffer_meta (dmabuf->outbuf, buffer);

  *outbuf = dmabuf->outbuf;
  dmabuf->outbuf = NULL;

  return GST_GL_UPLOAD_DONE;
}

static void
_dma_buf_upload_free (gpointer impl)
{
  struct DmabufUpload *dmabuf = impl;

  if (dmabuf->params)
    gst_gl_allocation_params_free ((GstGLAllocationParams *) dmabuf->params);

  g_free (impl);
}

static const UploadMethod _dma_buf_upload = {
  "Dmabuf",
  0,
  &_dma_buf_upload_caps,
  &_dma_buf_upload_new,
  &_dma_buf_upload_transform_caps,
  &_dma_buf_upload_accept,
  &_dma_buf_upload_propose_allocation,
  &_dma_buf_upload_perform,
  &_dma_buf_upload_free
};

#endif /* GST_GL_HAVE_DMABUF */

struct GLUploadMeta
{
  GstGLUpload *upload;

  gboolean result;
  GstVideoGLTextureUploadMeta *meta;
  guint texture_ids[GST_GL_UPLOAD_MAX_PLANES];
  GstBufferPool *pool;
};

static gpointer
_upload_meta_upload_new (GstGLUpload * upload)
{
  struct GLUploadMeta *meta = g_new0 (struct GLUploadMeta, 1);

  meta->upload = upload;
  meta->pool = NULL;

  return meta;
}

static GstCaps *
_upload_meta_upload_transform_caps (gpointer impl, GstGLContext * context,
    GstPadDirection direction, GstCaps * caps)
{
  GstCapsFeatures *passthrough =
      gst_caps_features_from_string
      (GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION);
  GstCaps *ret;

  if (direction == GST_PAD_SINK) {
    GstCaps *tmp;

    ret =
        _set_caps_features_with_passthrough (caps,
        GST_CAPS_FEATURE_MEMORY_GL_MEMORY, passthrough);

    tmp = _caps_intersect_texture_target (ret, 1 << GST_GL_TEXTURE_TARGET_2D);
    gst_caps_unref (ret);
    ret = tmp;
  } else {
    gint i, n;

    ret =
        _set_caps_features_with_passthrough (caps,
        GST_CAPS_FEATURE_META_GST_VIDEO_GL_TEXTURE_UPLOAD_META, passthrough);
    gst_caps_set_simple (ret, "format", G_TYPE_STRING, "RGBA", NULL);

    n = gst_caps_get_size (ret);
    for (i = 0; i < n; i++) {
      GstStructure *s = gst_caps_get_structure (ret, i);

      gst_structure_remove_fields (s, "texture-target", NULL);
    }
  }

  gst_caps_features_free (passthrough);

  return ret;
}

static gboolean
_upload_meta_upload_accept (gpointer impl, GstBuffer * buffer,
    GstCaps * in_caps, GstCaps * out_caps)
{
  struct GLUploadMeta *upload = impl;
  GstCapsFeatures *features;
  GstVideoGLTextureUploadMeta *meta;
  gboolean ret = TRUE;
  GstStructure *config;
  gsize size;

  features = gst_caps_get_features (in_caps, 0);

  if (!gst_caps_features_contains (features,
          GST_CAPS_FEATURE_META_GST_VIDEO_GL_TEXTURE_UPLOAD_META))
    ret = FALSE;

  features = gst_caps_get_features (out_caps, 0);
  if (!gst_caps_features_contains (features, GST_CAPS_FEATURE_MEMORY_GL_MEMORY))
    ret = FALSE;

  if (!ret)
    return ret;

  if (upload->pool == NULL)
    upload->pool = gst_gl_buffer_pool_new (upload->upload->context);

  if (!gst_buffer_pool_is_active (upload->pool)) {
    config = gst_buffer_pool_get_config (upload->pool);

    size = upload->upload->priv->in_info.size;
    gst_buffer_pool_config_set_params (config, in_caps, size, 0, 0);

    if (!gst_buffer_pool_set_config (upload->pool, config)) {
      GST_WARNING_OBJECT (upload->upload, "failed to set bufferpool config");
      return FALSE;
    }
    gst_buffer_pool_set_active (upload->pool, TRUE);
  }

  if (buffer) {
    if ((meta = gst_buffer_get_video_gl_texture_upload_meta (buffer)) == NULL)
      return FALSE;

    if (meta->texture_type[0] != GST_VIDEO_GL_TEXTURE_TYPE_RGBA) {
      GST_FIXME_OBJECT (upload, "only single rgba texture supported");
      return FALSE;
    }

    if (meta->texture_orientation !=
        GST_VIDEO_GL_TEXTURE_ORIENTATION_X_NORMAL_Y_NORMAL) {
      GST_FIXME_OBJECT (upload, "only x-normal, y-normal textures supported");
      return FALSE;
    }
  }

  return TRUE;
}

static void
_upload_meta_upload_propose_allocation (gpointer impl, GstQuery * decide_query,
    GstQuery * query)
{
  struct GLUploadMeta *upload = impl;
  GstStructure *gl_context;
  gchar *platform, *gl_apis;
  gpointer handle;

  gl_apis =
      gst_gl_api_to_string (gst_gl_context_get_gl_api (upload->upload->
          context));
  platform =
      gst_gl_platform_to_string (gst_gl_context_get_gl_platform (upload->
          upload->context));
  handle = (gpointer) gst_gl_context_get_gl_context (upload->upload->context);

  gl_context =
      gst_structure_new ("GstVideoGLTextureUploadMeta", "gst.gl.GstGLContext",
      GST_TYPE_GL_CONTEXT, upload->upload->context, "gst.gl.context.handle",
      G_TYPE_POINTER, handle, "gst.gl.context.type", G_TYPE_STRING, platform,
      "gst.gl.context.apis", G_TYPE_STRING, gl_apis, NULL);
  gst_query_add_allocation_meta (query,
      GST_VIDEO_GL_TEXTURE_UPLOAD_META_API_TYPE, gl_context);

  g_free (gl_apis);
  g_free (platform);
  gst_structure_free (gl_context);
}

/*
 * Uploads using gst_video_gl_texture_upload_meta_upload().
 * i.e. consumer of GstVideoGLTextureUploadMeta
 */
static void
_do_upload_with_meta (GstGLContext * context, struct GLUploadMeta *upload)
{
  if (!gst_video_gl_texture_upload_meta_upload (upload->meta,
          upload->texture_ids)) {
    upload->result = FALSE;
    return;
  }

  upload->result = TRUE;
}

static GstGLUploadReturn
_upload_meta_upload_perform (gpointer impl, GstBuffer * buffer,
    GstBuffer ** outbuf)
{
  struct GLUploadMeta *upload = impl;
  int i;
  GstVideoInfo *in_info = &upload->upload->priv->in_info;
  guint max_planes = GST_VIDEO_INFO_N_PLANES (in_info);

  /* Support stereo views for separated multiview mode */
  if (GST_VIDEO_INFO_MULTIVIEW_MODE (in_info) ==
      GST_VIDEO_MULTIVIEW_MODE_SEPARATED)
    max_planes *= GST_VIDEO_INFO_VIEWS (in_info);

  GST_LOG_OBJECT (upload, "Attempting upload with GstVideoGLTextureUploadMeta");

  upload->meta = gst_buffer_get_video_gl_texture_upload_meta (buffer);

  if (gst_buffer_pool_acquire_buffer (upload->pool, outbuf,
          NULL) != GST_FLOW_OK) {
    GST_WARNING_OBJECT (upload, "failed to acquire buffer from bufferpool");
    return GST_GL_UPLOAD_ERROR;
  }

  for (i = 0; i < GST_GL_UPLOAD_MAX_PLANES; i++) {
    guint tex_id = 0;

    if (i < max_planes) {
      GstMemory *mem = gst_buffer_peek_memory (*outbuf, i);
      tex_id = ((GstGLMemory *) mem)->tex_id;
    }

    upload->texture_ids[i] = tex_id;
  }

  GST_LOG ("Uploading with GLTextureUploadMeta with textures "
      "%i,%i,%i,%i / %i,%i,%i,%i",
      upload->texture_ids[0], upload->texture_ids[1],
      upload->texture_ids[2], upload->texture_ids[3],
      upload->texture_ids[4], upload->texture_ids[5],
      upload->texture_ids[6], upload->texture_ids[7]);

  gst_gl_context_thread_add (upload->upload->context,
      (GstGLContextThreadFunc) _do_upload_with_meta, upload);

  if (!upload->result)
    return GST_GL_UPLOAD_ERROR;

  return GST_GL_UPLOAD_DONE;
}

static void
_upload_meta_upload_free (gpointer impl)
{
  struct GLUploadMeta *upload = impl;

  g_return_if_fail (impl != NULL);

  if (upload->pool)
    gst_object_unref (upload->pool);

  g_free (upload);
}

static GstStaticCaps _upload_meta_upload_caps =
GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
    (GST_CAPS_FEATURE_META_GST_VIDEO_GL_TEXTURE_UPLOAD_META, "RGBA"));

static const UploadMethod _upload_meta_upload = {
  "UploadMeta",
  METHOD_FLAG_CAN_SHARE_CONTEXT,
  &_upload_meta_upload_caps,
  &_upload_meta_upload_new,
  &_upload_meta_upload_transform_caps,
  &_upload_meta_upload_accept,
  &_upload_meta_upload_propose_allocation,
  &_upload_meta_upload_perform,
  &_upload_meta_upload_free
};

struct RawUploadFrame
{
  gint ref_count;
  GstVideoFrame frame;
};

struct RawUpload
{
  GstGLUpload *upload;
  struct RawUploadFrame *in_frame;
  GstGLVideoAllocationParams *params;
};

static struct RawUploadFrame *
_raw_upload_frame_new (struct RawUpload *raw, GstBuffer * buffer)
{
  struct RawUploadFrame *frame;
  GstVideoInfo *info;
  gint i;

  if (!buffer)
    return NULL;

  frame = g_slice_new (struct RawUploadFrame);
  frame->ref_count = 1;

  if (!gst_video_frame_map (&frame->frame, &raw->upload->priv->in_info,
          buffer, GST_MAP_READ)) {
    g_slice_free (struct RawUploadFrame, frame);
    return NULL;
  }

  raw->upload->priv->in_info = frame->frame.info;
  info = &raw->upload->priv->in_info;

  /* Recalculate the offsets (and size) */
  info->size = 0;
  for (i = 0; i < GST_VIDEO_INFO_N_PLANES (info); i++) {
    info->offset[i] = info->size;
    info->size += gst_gl_get_plane_data_size (info, NULL, i);
  }

  return frame;
}

static void
_raw_upload_frame_ref (struct RawUploadFrame *frame)
{
  g_atomic_int_inc (&frame->ref_count);
}

static void
_raw_upload_frame_unref (struct RawUploadFrame *frame)
{
  if (g_atomic_int_dec_and_test (&frame->ref_count)) {
    gst_video_frame_unmap (&frame->frame);
    g_slice_free (struct RawUploadFrame, frame);
  }
}

static gpointer
_raw_data_upload_new (GstGLUpload * upload)
{
  struct RawUpload *raw = g_new0 (struct RawUpload, 1);

  raw->upload = upload;

  return raw;
}

static GstCaps *
_raw_data_upload_transform_caps (gpointer impl, GstGLContext * context,
    GstPadDirection direction, GstCaps * caps)
{
  GstCapsFeatures *passthrough =
      gst_caps_features_from_string
      (GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION);
  GstCaps *ret;

  if (direction == GST_PAD_SINK) {
    GstGLTextureTarget target_mask = 0;
    GstCaps *tmp;

    ret =
        _set_caps_features_with_passthrough (caps,
        GST_CAPS_FEATURE_MEMORY_GL_MEMORY, passthrough);

    target_mask |= 1 << GST_GL_TEXTURE_TARGET_2D;
    target_mask |= 1 << GST_GL_TEXTURE_TARGET_RECTANGLE;
    tmp = _caps_intersect_texture_target (ret, target_mask);
    gst_caps_unref (ret);
    ret = tmp;
  } else {
    gint i, n;

    ret =
        _set_caps_features_with_passthrough (caps,
        GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY, passthrough);

    n = gst_caps_get_size (ret);
    for (i = 0; i < n; i++) {
      GstStructure *s = gst_caps_get_structure (ret, i);

      gst_structure_remove_fields (s, "texture-target", NULL);
    }
  }

  gst_caps_features_free (passthrough);

  return ret;
}

static gboolean
_raw_data_upload_accept (gpointer impl, GstBuffer * buffer, GstCaps * in_caps,
    GstCaps * out_caps)
{
  struct RawUpload *raw = impl;
  GstCapsFeatures *features;

  features = gst_caps_get_features (out_caps, 0);
  if (!gst_caps_features_contains (features, GST_CAPS_FEATURE_MEMORY_GL_MEMORY))
    return FALSE;

  if (raw->in_frame)
    _raw_upload_frame_unref (raw->in_frame);
  raw->in_frame = _raw_upload_frame_new (raw, buffer);

  if (raw->params)
    gst_gl_allocation_params_free ((GstGLAllocationParams *) raw->params);
  if (!(raw->params =
          gst_gl_video_allocation_params_new_wrapped_data (raw->upload->context,
              NULL, &raw->upload->priv->in_info, -1, NULL,
              GST_GL_TEXTURE_TARGET_2D, 0, NULL, raw->in_frame,
              (GDestroyNotify) _raw_upload_frame_unref)))
    return FALSE;

  return (raw->in_frame != NULL);
}

static void
_raw_data_upload_propose_allocation (gpointer impl, GstQuery * decide_query,
    GstQuery * query)
{
  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, 0);
}

static GstGLUploadReturn
_raw_data_upload_perform (gpointer impl, GstBuffer * buffer,
    GstBuffer ** outbuf)
{
  GstGLBaseMemoryAllocator *allocator;
  struct RawUpload *raw = impl;
  int i;
  GstVideoInfo *in_info = &raw->upload->priv->in_info;
  guint n_mem = GST_VIDEO_INFO_N_PLANES (in_info);

  allocator =
      GST_GL_BASE_MEMORY_ALLOCATOR (gst_gl_memory_allocator_get_default
      (raw->upload->context));

  /* FIXME Use a buffer pool to cache the generated textures */
  /* FIXME: multiview support with separated left/right frames? */
  *outbuf = gst_buffer_new ();
  for (i = 0; i < n_mem; i++) {
    GstGLBaseMemory *tex;

    raw->params->parent.wrapped_data = raw->in_frame->frame.data[i];
    raw->params->plane = i;
    raw->params->tex_format =
        gst_gl_format_from_video_info (raw->upload->context, in_info, i);

    tex =
        gst_gl_base_memory_alloc (allocator,
        (GstGLAllocationParams *) raw->params);
    if (!tex) {
      gst_buffer_unref (*outbuf);
      *outbuf = NULL;
      GST_ERROR_OBJECT (raw->upload, "Failed to allocate wrapped texture");
      return GST_GL_UPLOAD_ERROR;
    }

    _raw_upload_frame_ref (raw->in_frame);
    gst_buffer_append_memory (*outbuf, (GstMemory *) tex);
  }
  gst_object_unref (allocator);

  _raw_upload_frame_unref (raw->in_frame);
  raw->in_frame = NULL;
  return GST_GL_UPLOAD_DONE;
}

static void
_raw_data_upload_free (gpointer impl)
{
  struct RawUpload *raw = impl;

  if (raw->params)
    gst_gl_allocation_params_free ((GstGLAllocationParams *) raw->params);

  g_free (raw);
}

static GstStaticCaps _raw_data_upload_caps =
GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (GST_GL_MEMORY_VIDEO_FORMATS_STR));

static const UploadMethod _raw_data_upload = {
  "Raw Data",
  0,
  &_raw_data_upload_caps,
  &_raw_data_upload_new,
  &_raw_data_upload_transform_caps,
  &_raw_data_upload_accept,
  &_raw_data_upload_propose_allocation,
  &_raw_data_upload_perform,
  &_raw_data_upload_free
};

#if GST_GL_HAVE_VIV_DIRECTVIV
#ifndef GL_BGRA_EXT
#define GL_BGRA_EXT                                             0x80E1
#endif
#ifndef GL_VIV_YV12
#define GL_VIV_YV12                                             0x8FC0
#endif
#ifndef GL_VIV_NV12
#define GL_VIV_NV12                                             0x8FC1
#endif
#ifndef GL_VIV_YUY2
#define GL_VIV_YUY2                                             0x8FC2
#endif
#ifndef GL_VIV_UYVY
#define GL_VIV_UYVY                                             0x8FC3
#endif
#ifndef GL_VIV_NV21
#define GL_VIV_NV21                                             0x8FC4
#endif
#ifndef GL_VIV_I420
#define GL_VIV_I420                                             0x8FC5
#endif

struct DirectVIVUpload
{
  GstGLUpload *upload;

  GstGLVideoAllocationParams *params;
  GstBuffer *inbuf, *outbuf;
  void (*TexDirectVIVMap) (GLenum Target, GLsizei Width, GLsizei Height,
      GLenum Format, GLvoid ** Logical, const GLuint * Physical);
  void (*TexDirectInvalidateVIV) (GLenum Target);
  gboolean loaded_functions;
};

#define GST_GL_DIRECTVIV_FORMAT "{RGBA, I420, YV12, NV12, NV21, YUY2, UYVY, BGRA, RGB16}"

static GstStaticCaps _directviv_upload_caps =
GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (GST_GL_DIRECTVIV_FORMAT));

static gpointer
_directviv_upload_new (GstGLUpload * upload)
{
  struct DirectVIVUpload *directviv = g_new0 (struct DirectVIVUpload, 1);
  directviv->upload = upload;
  directviv->loaded_functions = FALSE;

  return directviv;
}

static GstCaps *
_directviv_upload_transform_caps (gpointer impl, GstGLContext * context,
    GstPadDirection direction, GstCaps * caps)
{
  GstCapsFeatures *passthrough =
      gst_caps_features_from_string
      (GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION);
  GstCaps *ret;

  if (direction == GST_PAD_SINK) {
    GstCaps *tmp;

    ret =
        _set_caps_features_with_passthrough (caps,
        GST_CAPS_FEATURE_MEMORY_GL_MEMORY, passthrough);

    gst_caps_set_simple (ret, "format", G_TYPE_STRING, "RGBA", NULL);
    tmp = _caps_intersect_texture_target (ret, 1 << GST_GL_TEXTURE_TARGET_2D);
    gst_caps_unref (ret);
    ret = tmp;
  } else {
    GstCaps *tmp;
    tmp = gst_caps_from_string (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY, GST_GL_DIRECTVIV_FORMAT));
    ret =
        _set_caps_features_with_passthrough (tmp,
        GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY, passthrough);
    gst_caps_unref (tmp);
  }

  gst_caps_features_free (passthrough);
  return ret;
}


static void
_directviv_upload_load_functions_gl_thread (GstGLContext * context,
    struct DirectVIVUpload *directviv)
{
  directviv->TexDirectVIVMap =
      gst_gl_context_get_proc_address (context, "glTexDirectVIVMap");
  directviv->TexDirectInvalidateVIV =
      gst_gl_context_get_proc_address (context, "glTexDirectInvalidateVIV");
}

static gboolean
_directviv_upload_accept (gpointer impl, GstBuffer * buffer, GstCaps * in_caps,
    GstCaps * out_caps)
{
  struct DirectVIVUpload *directviv = impl;
  GstCapsFeatures *features;
  guint n_mem;
  GstMemory *mem;

  if (!directviv->loaded_functions && (!directviv->TexDirectInvalidateVIV ||
          !directviv->TexDirectVIVMap)) {
    gst_gl_context_thread_add (directviv->upload->context,
        (GstGLContextThreadFunc) _directviv_upload_load_functions_gl_thread,
        directviv);
    directviv->loaded_functions = TRUE;
  }
  if (!directviv->TexDirectInvalidateVIV || !directviv->TexDirectVIVMap)
    return FALSE;

  features = gst_caps_get_features (out_caps, 0);
  if (!gst_caps_features_contains (features, GST_CAPS_FEATURE_MEMORY_GL_MEMORY))
    return FALSE;

  if (directviv->params)
    gst_gl_allocation_params_free ((GstGLAllocationParams *) directviv->params);
  if (!(directviv->params =
          gst_gl_video_allocation_params_new (directviv->upload->context, NULL,
              &directviv->upload->priv->out_info, -1, NULL,
              GST_GL_TEXTURE_TARGET_2D, GST_VIDEO_GL_TEXTURE_TYPE_RGBA)))
    return FALSE;

  /* We only support a single memory per buffer at this point */
  n_mem = gst_buffer_n_memory (buffer);
  if (n_mem == 1) {
    mem = gst_buffer_peek_memory (buffer, 0);
  } else {
    mem = NULL;
  }

  return n_mem == 1 && mem && gst_is_phys_memory (mem);
}

static void
_directviv_upload_propose_allocation (gpointer impl, GstQuery * decide_query,
    GstQuery * query)
{
}

static GLenum
_directviv_upload_video_format_to_gl_format (GstVideoFormat format)
{
  switch (format) {
    case GST_VIDEO_FORMAT_I420:
      return GL_VIV_I420;
    case GST_VIDEO_FORMAT_YV12:
      return GL_VIV_YV12;
    case GST_VIDEO_FORMAT_NV12:
      return GL_VIV_NV12;
    case GST_VIDEO_FORMAT_NV21:
      return GL_VIV_NV21;
    case GST_VIDEO_FORMAT_YUY2:
      return GL_VIV_YUY2;
    case GST_VIDEO_FORMAT_UYVY:
      return GL_VIV_UYVY;
    case GST_VIDEO_FORMAT_RGB16:
      return GL_RGB565;
    case GST_VIDEO_FORMAT_RGBA:
      return GL_RGBA;
    case GST_VIDEO_FORMAT_BGRA:
      return GL_BGRA_EXT;
    case GST_VIDEO_FORMAT_RGBx:
      return GL_RGBA;
    case GST_VIDEO_FORMAT_BGRx:
      return GL_BGRA_EXT;
    default:
      return 0;
  }
}

typedef struct
{
  GstBuffer *buffer;
  GstMemory *memory;
  GstMapInfo map;
  guintptr phys_addr;
} DirectVIVUnmapData;

static void
_directviv_memory_unmap (DirectVIVUnmapData * data)
{
  gst_memory_unmap (data->memory, &data->map);
  gst_memory_unref (data->memory);
  gst_buffer_unref (data->buffer);
  g_free (data);
}

static void
_directviv_upload_perform_gl_thread (GstGLContext * context,
    struct DirectVIVUpload *directviv)
{
  static GQuark directviv_unmap_quark = 0;
  GstGLMemoryAllocator *allocator;
  GstMemory *in_mem;
  GstGLMemory *out_gl_mem;
  GstVideoInfo *in_info;
  DirectVIVUnmapData *unmap_data;
  GstVideoMeta *vmeta;
  gint width, height, gl_format;
  const GstGLFuncs *gl;

  if (!directviv_unmap_quark)
    directviv_unmap_quark = g_quark_from_static_string ("GstGLDirectVIVUnmap");

  gl = context->gl_vtable;

  g_assert (gst_buffer_n_memory (directviv->inbuf) == 1);
  in_info = &directviv->upload->priv->in_info;
  in_mem = gst_buffer_peek_memory (directviv->inbuf, 0);
  unmap_data = g_new0 (DirectVIVUnmapData, 1);
  if (!gst_memory_map (in_mem, &unmap_data->map, GST_MAP_READ)) {
    g_free (unmap_data);
    return;
  }
  unmap_data->phys_addr = gst_phys_memory_get_phys_addr (in_mem);
  if (!unmap_data->phys_addr) {
    gst_memory_unmap (in_mem, &unmap_data->map);
    g_free (unmap_data);
    return;
  }
  unmap_data->memory = gst_memory_ref (in_mem);
  unmap_data->buffer = gst_buffer_ref (directviv->inbuf);

  allocator =
      GST_GL_MEMORY_ALLOCATOR (gst_allocator_find
      (GST_GL_MEMORY_PBO_ALLOCATOR_NAME));

  /* FIXME: buffer pool */
  directviv->outbuf = gst_buffer_new ();
  gst_gl_memory_setup_buffer (allocator, directviv->outbuf, directviv->params,
      NULL, NULL, 0);
  gst_object_unref (allocator);

  out_gl_mem = (GstGLMemory *) gst_buffer_peek_memory (directviv->outbuf, 0);

  /* Need to keep the input memory and buffer mapped and valid until
   * the GL memory is not used anymore */
  gst_mini_object_set_qdata ((GstMiniObject *) out_gl_mem,
      directviv_unmap_quark, unmap_data,
      (GDestroyNotify) _directviv_memory_unmap);
  gst_buffer_add_parent_buffer_meta (directviv->outbuf, directviv->inbuf);

  /* width/height need to compensate for stride/padding */
  vmeta = gst_buffer_get_video_meta (directviv->inbuf);
  if (vmeta) {
    width = vmeta->stride[0];
    if (GST_VIDEO_INFO_N_PLANES (in_info) == 1)
      height = gst_memory_get_sizes (in_mem, NULL, NULL) / width;
    else
      height = vmeta->offset[1] / width;
  } else {
    width = GST_VIDEO_INFO_PLANE_STRIDE (in_info, 0);
    if (GST_VIDEO_INFO_N_PLANES (in_info) == 1)
      height = gst_memory_get_sizes (in_mem, NULL, NULL) / width;
    else
      height = GST_VIDEO_INFO_PLANE_OFFSET (in_info, 1) / width;
  }
  width /= GST_VIDEO_INFO_COMP_PSTRIDE (in_info, 0);

  gl_format =
      _directviv_upload_video_format_to_gl_format (GST_VIDEO_INFO_FORMAT
      (in_info));

  gl->BindTexture (GL_TEXTURE_2D, out_gl_mem->tex_id);
  directviv->TexDirectVIVMap (GL_TEXTURE_2D, width, height,
      gl_format, (void **) &unmap_data->map.data, &unmap_data->phys_addr);
  directviv->TexDirectInvalidateVIV (GL_TEXTURE_2D);
}

static GstGLUploadReturn
_directviv_upload_perform (gpointer impl, GstBuffer * buffer,
    GstBuffer ** outbuf)
{
  struct DirectVIVUpload *directviv = impl;

  directviv->inbuf = buffer;
  directviv->outbuf = NULL;
  gst_gl_context_thread_add (directviv->upload->context,
      (GstGLContextThreadFunc) _directviv_upload_perform_gl_thread, directviv);
  directviv->inbuf = NULL;

  if (!directviv->outbuf)
    return GST_GL_UPLOAD_ERROR;

  *outbuf = directviv->outbuf;
  directviv->outbuf = NULL;

  return GST_GL_UPLOAD_DONE;
}

static void
_directviv_upload_free (gpointer impl)
{
  struct DirectVIVUpload *directviv = impl;

  if (directviv->params)
    gst_gl_allocation_params_free ((GstGLAllocationParams *) directviv->params);

  g_free (impl);
}

static const UploadMethod _directviv_upload = {
  "DirectVIV",
  0,
  &_directviv_upload_caps,
  &_directviv_upload_new,
  &_directviv_upload_transform_caps,
  &_directviv_upload_accept,
  &_directviv_upload_propose_allocation,
  &_directviv_upload_perform,
  &_directviv_upload_free
};

#endif /* GST_GL_HAVE_VIV_DIRECTVIV */

static const UploadMethod *upload_methods[] = { &_gl_memory_upload,
#if GST_GL_HAVE_DMABUF
  &_dma_buf_upload,
#endif
#if GST_GL_HAVE_VIV_DIRECTVIV
  &_directviv_upload,
#endif
  &_upload_meta_upload, &_raw_data_upload
};

static GMutex upload_global_lock;

GstCaps *
gst_gl_upload_get_input_template_caps (void)
{
  GstCaps *ret = NULL;
  gint i;

  g_mutex_lock (&upload_global_lock);

  /* FIXME: cache this and invalidate on changes to upload_methods */
  for (i = 0; i < G_N_ELEMENTS (upload_methods); i++) {
    GstCaps *template =
        gst_static_caps_get (upload_methods[i]->input_template_caps);
    ret = ret == NULL ? template : gst_caps_merge (ret, template);
  }

  ret = gst_caps_simplify (ret);
  ret = gst_gl_overlay_compositor_add_caps (ret);
  g_mutex_unlock (&upload_global_lock);

  return ret;
}

static void
gst_gl_upload_class_init (GstGLUploadClass * klass)
{
  g_type_class_add_private (klass, sizeof (GstGLUploadPrivate));

  G_OBJECT_CLASS (klass)->finalize = gst_gl_upload_finalize;
}

static void
gst_gl_upload_init (GstGLUpload * upload)
{
  upload->priv = GST_GL_UPLOAD_GET_PRIVATE (upload);
}

/**
 * gst_gl_upload_new:
 * @context: a #GstGLContext
 *
 * Returns: a new #GstGLUpload object
 */
GstGLUpload *
gst_gl_upload_new (GstGLContext * context)
{
  GstGLUpload *upload = g_object_new (GST_TYPE_GL_UPLOAD, NULL);
  gint i, n;

  if (context)
    gst_gl_upload_set_context (upload, context);
  else
    upload->context = NULL;

  n = G_N_ELEMENTS (upload_methods);
  upload->priv->upload_impl = g_malloc (sizeof (gpointer) * n);
  for (i = 0; i < n; i++) {
    upload->priv->upload_impl[i] = upload_methods[i]->new (upload);
  }

  GST_DEBUG_OBJECT (upload, "Created new GLUpload for context %" GST_PTR_FORMAT,
      context);

  return upload;
}

void
gst_gl_upload_set_context (GstGLUpload * upload, GstGLContext * context)
{
  g_return_if_fail (upload != NULL);

  gst_object_replace ((GstObject **) & upload->context, (GstObject *) context);
}

static void
gst_gl_upload_finalize (GObject * object)
{
  GstGLUpload *upload;
  gint i, n;

  upload = GST_GL_UPLOAD (object);

  upload->priv->method_i = 0;

  if (upload->context) {
    gst_object_unref (upload->context);
    upload->context = NULL;
  }

  if (upload->priv->in_caps) {
    gst_caps_unref (upload->priv->in_caps);
    upload->priv->in_caps = NULL;
  }

  if (upload->priv->out_caps) {
    gst_caps_unref (upload->priv->out_caps);
    upload->priv->out_caps = NULL;
  }

  n = G_N_ELEMENTS (upload_methods);
  for (i = 0; i < n; i++) {
    if (upload->priv->upload_impl[i])
      upload_methods[i]->free (upload->priv->upload_impl[i]);
  }
  g_free (upload->priv->upload_impl);

  G_OBJECT_CLASS (gst_gl_upload_parent_class)->finalize (object);
}

GstCaps *
gst_gl_upload_transform_caps (GstGLUpload * upload, GstGLContext * context,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstCaps *result, *tmp;
  gint i;

  tmp = gst_caps_new_empty ();

  for (i = 0; i < G_N_ELEMENTS (upload_methods); i++) {
    GstCaps *tmp2;

    tmp2 =
        upload_methods[i]->transform_caps (upload->priv->upload_impl[i],
        context, direction, caps);

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

/**
 * gst_gl_upload_propose_allocation:
 * @upload: a #GstGLUpload
 * @decide_query: (allow-none): a #GstQuery from a decide allocation
 * @query: the proposed allocation query
 *
 * Adds the required allocation parameters to support uploading.
 */
void
gst_gl_upload_propose_allocation (GstGLUpload * upload, GstQuery * decide_query,
    GstQuery * query)
{
  gint i;

  for (i = 0; i < G_N_ELEMENTS (upload_methods); i++)
    upload_methods[i]->propose_allocation (upload->priv->upload_impl[i],
        decide_query, query);
}

static gboolean
_gst_gl_upload_set_caps_unlocked (GstGLUpload * upload, GstCaps * in_caps,
    GstCaps * out_caps)
{
  g_return_val_if_fail (upload != NULL, FALSE);
  g_return_val_if_fail (gst_caps_is_fixed (in_caps), FALSE);

  if (upload->priv->in_caps && upload->priv->out_caps
      && gst_caps_is_equal (upload->priv->in_caps, in_caps)
      && gst_caps_is_equal (upload->priv->out_caps, out_caps))
    return TRUE;

  gst_caps_replace (&upload->priv->in_caps, in_caps);
  gst_caps_replace (&upload->priv->out_caps, out_caps);

  gst_video_info_from_caps (&upload->priv->in_info, in_caps);
  gst_video_info_from_caps (&upload->priv->out_info, out_caps);

  upload->priv->method_impl = NULL;
  upload->priv->method_i = 0;

  return TRUE;
}

/**
 * gst_gl_upload_set_caps:
 * @upload: a #GstGLUpload
 * @in_caps: input #GstCaps
 * @out_caps: output #GstCaps
 *
 * Initializes @upload with the information required for upload.
 *
 * Returns: whether @in_caps and @out_caps could be set on @upload
 */
gboolean
gst_gl_upload_set_caps (GstGLUpload * upload, GstCaps * in_caps,
    GstCaps * out_caps)
{
  gboolean ret;

  GST_OBJECT_LOCK (upload);
  ret = _gst_gl_upload_set_caps_unlocked (upload, in_caps, out_caps);
  GST_OBJECT_UNLOCK (upload);

  return ret;
}

/**
 * gst_gl_upload_get_caps:
 * @upload: a #GstGLUpload
 * @in_caps: (transfer full) (allow-none) (out): the input #GstCaps
 * @out_caps: (transfer full) (allow-none) (out): the output #GstCaps
 */
void
gst_gl_upload_get_caps (GstGLUpload * upload, GstCaps ** in_caps,
    GstCaps ** out_caps)
{
  GST_OBJECT_LOCK (upload);
  if (in_caps)
    *in_caps =
        upload->priv->in_caps ? gst_caps_ref (upload->priv->in_caps) : NULL;
  if (out_caps)
    *out_caps =
        upload->priv->out_caps ? gst_caps_ref (upload->priv->out_caps) : NULL;
  GST_OBJECT_UNLOCK (upload);
}

static gboolean
_upload_find_method (GstGLUpload * upload)
{
  gint method_i;

  if (upload->priv->method_i >= G_N_ELEMENTS (upload_methods))
    return FALSE;

  method_i = upload->priv->method_i;
  upload->priv->method = upload_methods[method_i];
  upload->priv->method_impl = upload->priv->upload_impl[method_i];

  GST_DEBUG_OBJECT (upload, "attempting upload with uploader %s",
      upload->priv->method->name);

  upload->priv->method_i++;

  return TRUE;
}

/**
 * gst_gl_upload_perform_with_buffer:
 * @upload: a #GstGLUpload
 * @buffer: input #GstBuffer
 * @outbuf_ptr: resulting #GstBuffer
 *
 * Uploads @buffer using the transformation specified by
 * gst_gl_upload_set_caps() creating a new #GstBuffer in @outbuf_ptr.
 *
 * Returns: whether the upload was successful
 */
GstGLUploadReturn
gst_gl_upload_perform_with_buffer (GstGLUpload * upload, GstBuffer * buffer,
    GstBuffer ** outbuf_ptr)
{
  GstGLUploadReturn ret = GST_GL_UPLOAD_ERROR;
  GstBuffer *outbuf;

  g_return_val_if_fail (GST_IS_GL_UPLOAD (upload), FALSE);
  g_return_val_if_fail (GST_IS_BUFFER (buffer), FALSE);
  g_return_val_if_fail (outbuf_ptr != NULL, FALSE);

  GST_OBJECT_LOCK (upload);

#define NEXT_METHOD \
do { \
  if (!_upload_find_method (upload)) { \
    GST_OBJECT_UNLOCK (upload); \
    return FALSE; \
  } \
  goto restart; \
} while (0)

  if (!upload->priv->method_impl)
    _upload_find_method (upload);

restart:
  if (!upload->priv->method->accept (upload->priv->method_impl, buffer,
          upload->priv->in_caps, upload->priv->out_caps))
    NEXT_METHOD;

  ret =
      upload->priv->method->perform (upload->priv->method_impl, buffer,
      &outbuf);
  if (ret == GST_GL_UPLOAD_UNSHARED_GL_CONTEXT) {
    gint i;

    for (i = 0; i < G_N_ELEMENTS (upload_methods); i++) {
      if (upload_methods[i] == &_raw_data_upload) {
        upload->priv->method = &_raw_data_upload;
        upload->priv->method_impl = upload->priv->upload_impl[i];
        upload->priv->method_i = i;

        break;
      }
    }
    goto restart;
  } else if (ret == GST_GL_UPLOAD_DONE || ret == GST_GL_UPLOAD_RECONFIGURE) {
    /* we are done */
  } else {
    upload->priv->method_impl = NULL;
    NEXT_METHOD;
  }

  if (outbuf && buffer != outbuf)
    gst_buffer_copy_into (outbuf, buffer,
        GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS, 0, -1);
  *outbuf_ptr = outbuf;

  GST_OBJECT_UNLOCK (upload);

  return ret;

#undef NEXT_METHOD
}
