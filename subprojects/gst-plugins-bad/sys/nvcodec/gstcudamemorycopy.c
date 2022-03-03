/* GStreamer
 * Copyright (C) <2019> Seungha Yang <seungha.yang@navercorp.com>
 * Copyright (C) 2022 Seungha Yang <seungha@centricular.com>
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
 * element-cudaupload:
 *
 * Uploads data to NVIDA GPU via CUDA APIs
 *
 * Since: 1.20
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "gstcudabasetransform.h"
#include "gstcudamemorycopy.h"
#include "gstcudaformat.h"
#include "gstcudautils.h"

#ifdef HAVE_NVCODEC_GST_GL
#include <gst/gl/gl.h>
#endif

GST_DEBUG_CATEGORY_STATIC (gst_cuda_memory_copy_debug);
#define GST_CAT_DEFAULT gst_cuda_memory_copy_debug

typedef struct _GstCudaMemoryCopyClassData
{
  GstCaps *sink_caps;
  GstCaps *src_caps;
} GstCudaMemoryCopyClassData;

struct _GstCudaMemoryCopy
{
  GstCudaBaseTransform parent;

#ifdef HAVE_NVCODEC_GST_GL
  GstGLDisplay *gl_display;
  GstGLContext *gl_context;
  GstGLContext *other_gl_context;
#endif
};

typedef struct _GstCudaUpload
{
  GstCudaMemoryCopy parent;
} GstCudaUpload;

typedef struct _GstCudaUploadClass
{
  GstCudaMemoryCopyClass parent_class;
} GstCudaUploadClass;

typedef struct _GstCudaDownload
{
  GstCudaMemoryCopy parent;
} GstCudaDownload;

typedef struct _GstCudaDownloadClass
{
  GstCudaMemoryCopyClass parent_class;
} GstCudaDownloadClass;

#define gst_cuda_memory_copy_parent_class parent_class
G_DEFINE_ABSTRACT_TYPE (GstCudaMemoryCopy, gst_cuda_memory_copy,
    GST_TYPE_CUDA_BASE_TRANSFORM);

static void gst_cuda_memory_copy_set_context (GstElement * element,
    GstContext * context);
static GstCaps *gst_cuda_memory_copy_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static gboolean gst_cuda_memory_copy_query (GstBaseTransform * trans,
    GstPadDirection direction, GstQuery * query);
static gboolean gst_cuda_memory_copy_propose_allocation (GstBaseTransform *
    trans, GstQuery * decide_query, GstQuery * query);
static gboolean gst_cuda_memory_copy_decide_allocation (GstBaseTransform *
    trans, GstQuery * query);
static GstFlowReturn gst_cuda_memory_copy_transform (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf);

static void
gst_cuda_memory_copy_class_init (GstCudaMemoryCopyClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *trans_class = GST_BASE_TRANSFORM_CLASS (klass);

  element_class->set_context =
      GST_DEBUG_FUNCPTR (gst_cuda_memory_copy_set_context);

  trans_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_cuda_memory_copy_transform_caps);
  trans_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_cuda_memory_copy_propose_allocation);
  trans_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_cuda_memory_copy_decide_allocation);
  trans_class->query = GST_DEBUG_FUNCPTR (gst_cuda_memory_copy_query);
}

static void
gst_cuda_memory_copy_init (GstCudaMemoryCopy * self)
{
}

static void
gst_cuda_memory_copy_set_context (GstElement * element, GstContext * context)
{
  /* CUDA context is handled by parent class, handle only non-CUDA context */
#ifdef HAVE_NVCODEC_GST_GL
  GstCudaMemoryCopy *self = GST_CUDA_MEMORY_COPY (element);

  gst_gl_handle_set_context (element, context, &self->gl_display,
      &self->other_gl_context);
#endif

  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);
}

static GstCaps *
_set_caps_features (const GstCaps * caps, const gchar * feature_name)
{
  GstCaps *tmp = gst_caps_copy (caps);
  guint n = gst_caps_get_size (tmp);
  guint i = 0;

  for (i = 0; i < n; i++)
    gst_caps_set_features (tmp, i,
        gst_caps_features_from_string (feature_name));

  return tmp;
}

static void
_remove_field (GstCaps * caps, const gchar * field)
{
  guint n = gst_caps_get_size (caps);
  guint i = 0;

  for (i = 0; i < n; i++) {
    GstStructure *s = gst_caps_get_structure (caps, i);
    gst_structure_remove_field (s, field);
  }
}

static GstCaps *
create_transform_caps (GstCaps * caps, gboolean to_cuda)
{
  GstCaps *ret = NULL;

  if (to_cuda) {
    GstCaps *sys_caps = gst_caps_simplify (_set_caps_features (caps,
            GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY));

    ret = _set_caps_features (sys_caps, GST_CAPS_FEATURE_MEMORY_CUDA_MEMORY);
    ret = gst_caps_merge (ret, sys_caps);
  } else {
    GstCaps *new_caps;

    ret = gst_caps_ref (caps);

#ifdef HAVE_NVCODEC_GST_GL
    new_caps = _set_caps_features (caps, GST_CAPS_FEATURE_MEMORY_GL_MEMORY);
    _remove_field (new_caps, "texture-target");
    ret = gst_caps_merge (ret, new_caps);
#endif

    new_caps = _set_caps_features (caps, GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY);
    _remove_field (new_caps, "texture-target");

    ret = gst_caps_merge (ret, new_caps);
  }

  return ret;
}

static GstCaps *
gst_cuda_memory_copy_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstCudaMemoryCopyClass *klass = GST_CUDA_MEMORY_COPY_GET_CLASS (trans);
  GstCaps *result, *tmp;

  GST_DEBUG_OBJECT (trans,
      "Transforming caps %" GST_PTR_FORMAT " in direction %s", caps,
      (direction == GST_PAD_SINK) ? "sink" : "src");

  if (direction == GST_PAD_SINK) {
    tmp = create_transform_caps (caps, klass->uploader);
  } else {
    tmp = create_transform_caps (caps, !klass->uploader);
  }

  if (filter) {
    result = gst_caps_intersect_full (filter, tmp, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (tmp);
  } else {
    result = tmp;
  }

  GST_DEBUG_OBJECT (trans, "returning caps: %" GST_PTR_FORMAT, result);

  return result;
}

#ifdef HAVE_NVCODEC_GST_GL
static void
gst_cuda_memory_copy_ensure_gl_interop (GstGLContext * context, gboolean * ret)
{
  guint device_count = 0;
  CUdevice device_list[1] = { 0, };
  CUresult cuda_ret;

  *ret = FALSE;

  cuda_ret = CuGLGetDevices (&device_count,
      device_list, 1, CU_GL_DEVICE_LIST_ALL);

  if (cuda_ret != CUDA_SUCCESS || device_count == 0)
    return;

  *ret = TRUE;

  return;
}

static gboolean
gst_cuda_memory_copy_ensure_gl_context (GstCudaMemoryCopy * self)
{
  GstGLDisplay *display;
  GstGLContext *context;
  gboolean ret = FALSE;

  if (!gst_gl_ensure_element_data (GST_ELEMENT (self),
          &self->gl_display, &self->other_gl_context)) {
    GST_DEBUG_OBJECT (self, "No available OpenGL display");
    return FALSE;
  }

  display = self->gl_display;

  if (!gst_gl_query_local_gl_context (GST_ELEMENT (self), GST_PAD_SRC,
          &self->gl_context) &&
      !gst_gl_query_local_gl_context (GST_ELEMENT (self), GST_PAD_SINK,
          &self->gl_context)) {
    GST_INFO_OBJECT (self, "failed to query local OpenGL context");

    gst_clear_object (&self->gl_context);
    self->gl_context = gst_gl_display_get_gl_context_for_thread (display, NULL);
    if (!self->gl_context
        || !gst_gl_display_add_context (display,
            GST_GL_CONTEXT (self->gl_context))) {
      gst_clear_object (&self->gl_context);
      if (!gst_gl_display_create_context (display,
              self->other_gl_context, &self->gl_context, NULL)) {
        GST_WARNING_OBJECT (self, "failed to create OpenGL context");
        return FALSE;
      }

      if (!gst_gl_display_add_context (display, self->gl_context)) {
        GST_WARNING_OBJECT (self,
            "failed to add the OpenGL context to the display");
        return FALSE;
      }
    }
  }

  context = self->gl_context;

  if (!gst_gl_context_check_gl_version (context,
          (GstGLAPI) (GST_GL_API_OPENGL | GST_GL_API_OPENGL3), 3, 0)) {
    GST_WARNING_OBJECT (self, "OpenGL context could not support PBO download");
    return FALSE;
  }

  gst_gl_context_thread_add (context,
      (GstGLContextThreadFunc) gst_cuda_memory_copy_ensure_gl_interop, &ret);
  if (!ret) {
    GST_WARNING_OBJECT (self, "Current GL context is not CUDA compatible");
    return FALSE;
  }

  return TRUE;
}
#endif

static gboolean
gst_cuda_memory_copy_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_query, GstQuery * query)
{
  GstCudaMemoryCopy *self = GST_CUDA_MEMORY_COPY (trans);
  GstCudaBaseTransform *ctrans = GST_CUDA_BASE_TRANSFORM (trans);
  GstVideoInfo info;
  GstBufferPool *pool = NULL;
  GstCaps *caps;
  guint size;

  if (!GST_BASE_TRANSFORM_CLASS (parent_class)->propose_allocation (trans,
          decide_query, query))
    return FALSE;

  /* passthrough, we're done */
  if (decide_query == NULL)
    return TRUE;

  gst_query_parse_allocation (query, &caps, NULL);

  if (caps == NULL)
    return FALSE;

  if (!gst_video_info_from_caps (&info, caps))
    return FALSE;

  if (gst_query_get_n_allocation_pools (query) == 0) {
    GstCapsFeatures *features;
    GstStructure *config;

    features = gst_caps_get_features (caps, 0);

    if (features && gst_caps_features_contains (features,
            GST_CAPS_FEATURE_MEMORY_CUDA_MEMORY)) {
      GST_DEBUG_OBJECT (self, "upstream support CUDA memory");
      pool = gst_cuda_buffer_pool_new (ctrans->context);
#ifdef HAVE_NVCODEC_GST_GL
    } else if (features && gst_caps_features_contains (features,
            GST_CAPS_FEATURE_MEMORY_GL_MEMORY) &&
        gst_cuda_memory_copy_ensure_gl_context (self)) {
      GST_DEBUG_OBJECT (self, "upstream support GL memory");

      pool = gst_gl_buffer_pool_new (self->gl_context);
#endif
    }

    if (!pool) {
      GST_DEBUG_OBJECT (self, "creating system buffer pool");
      pool = gst_video_buffer_pool_new ();
    }

    config = gst_buffer_pool_get_config (pool);

    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);

    size = GST_VIDEO_INFO_SIZE (&info);
    gst_buffer_pool_config_set_params (config, caps, size, 0, 0);

    if (!gst_buffer_pool_set_config (pool, config)) {
      GST_ERROR_OBJECT (ctrans, "failed to set config");
      gst_object_unref (pool);
      return FALSE;
    }

    /* Get updated size by cuda buffer pool */
    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_get_params (config, NULL, &size, NULL, NULL);
    gst_structure_free (config);

    gst_query_add_allocation_pool (query, pool, size, 0, 0);

    gst_object_unref (pool);
  }

  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

  return TRUE;
}

static gboolean
gst_cuda_memory_copy_decide_allocation (GstBaseTransform * trans,
    GstQuery * query)
{
  GstCudaMemoryCopy *self = GST_CUDA_MEMORY_COPY (trans);
  GstCudaBaseTransform *ctrans = GST_CUDA_BASE_TRANSFORM (trans);
  GstCaps *outcaps = NULL;
  GstBufferPool *pool = NULL;
  guint size, min, max;
  GstStructure *config;
  gboolean update_pool = FALSE;
  GstCapsFeatures *features;
  gboolean need_cuda = FALSE;
#ifdef HAVE_NVCODEC_GST_GL
  gboolean need_gl = FALSE;
#endif

  gst_query_parse_allocation (query, &outcaps, NULL);

  if (!outcaps)
    return FALSE;

  features = gst_caps_get_features (outcaps, 0);
  if (features && gst_caps_features_contains (features,
          GST_CAPS_FEATURE_MEMORY_CUDA_MEMORY)) {
    need_cuda = TRUE;
  }
#ifdef HAVE_NVCODEC_GST_GL
  else if (features && gst_caps_features_contains (features,
          GST_CAPS_FEATURE_MEMORY_GL_MEMORY) &&
      gst_cuda_memory_copy_ensure_gl_context (self)) {
    need_gl = TRUE;
  }
#endif

  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
    if (need_cuda && pool) {
      if (!GST_IS_CUDA_BUFFER_POOL (pool)) {
        gst_clear_object (&pool);
      } else {
        GstCudaBufferPool *cpool = GST_CUDA_BUFFER_POOL (pool);

        if (cpool->context != ctrans->context) {
          gst_clear_object (&pool);
        }
      }
    }

    update_pool = TRUE;
  } else {
    GstVideoInfo vinfo;
    gst_video_info_from_caps (&vinfo, outcaps);
    size = GST_VIDEO_INFO_SIZE (&vinfo);
    min = max = 0;
  }

  if (!pool) {
    if (need_cuda) {
      GST_DEBUG_OBJECT (self, "creating cuda pool");
      pool = gst_cuda_buffer_pool_new (ctrans->context);
    }
#ifdef HAVE_NVCODEC_GST_GL
    else if (need_gl) {
      GST_DEBUG_OBJECT (self, "creating gl pool");
      pool = gst_gl_buffer_pool_new (self->gl_context);
    }
#endif
    else {
      GST_DEBUG_OBJECT (self, "creating system pool");
      pool = gst_video_buffer_pool_new ();
    }
  }

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  gst_buffer_pool_config_set_params (config, outcaps, size, min, max);
  gst_buffer_pool_set_config (pool, config);

  /* Get updated size by cuda buffer pool */
  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_get_params (config, NULL, &size, NULL, NULL);
  gst_structure_free (config);

  if (update_pool)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool (query, pool, size, min, max);

  gst_object_unref (pool);

  return GST_BASE_TRANSFORM_CLASS (parent_class)->decide_allocation (trans,
      query);
}

static gboolean
gst_cuda_memory_copy_query (GstBaseTransform * trans,
    GstPadDirection direction, GstQuery * query)
{
#ifdef HAVE_NVCODEC_GST_GL
  GstCudaMemoryCopy *self = GST_CUDA_MEMORY_COPY (trans);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:
    {
      gboolean ret;
      ret = gst_gl_handle_context_query (GST_ELEMENT (self), query,
          self->gl_display, self->gl_context, self->other_gl_context);

      if (ret)
        return TRUE;
      break;
    }
    default:
      break;
  }
#endif

  return GST_BASE_TRANSFORM_CLASS (parent_class)->query (trans, direction,
      query);
}

static gboolean
gst_cuda_memory_copy_transform_sysmem (GstCudaMemoryCopy * self,
    GstBuffer * inbuf, GstVideoInfo * in_info, GstBuffer * outbuf,
    GstVideoInfo * out_info)
{
  GstVideoFrame in_frame, out_frame;
  gboolean ret;

  if (!gst_video_frame_map (&in_frame, in_info, inbuf, GST_MAP_READ)) {
    GST_ERROR_OBJECT (self, "Failed to map input buffer");
    return FALSE;
  }

  if (!gst_video_frame_map (&out_frame, out_info, outbuf, GST_MAP_WRITE)) {
    gst_video_frame_unmap (&in_frame);

    GST_ERROR_OBJECT (self, "Failed to map input buffer");
    return FALSE;
  }

  ret = gst_video_frame_copy (&out_frame, &in_frame);
  gst_video_frame_unmap (&out_frame);
  gst_video_frame_unmap (&in_frame);

  if (!ret)
    GST_ERROR_OBJECT (self, "Failed to copy buffer");

  return ret;
}

#ifdef HAVE_NVCODEC_GST_GL
typedef struct _GLCopyData
{
  GstCudaMemoryCopy *self;
  GstBuffer *inbuf;
  GstVideoInfo *in_info;
  GstBuffer *outbuf;
  GstVideoInfo *out_info;

  gboolean pbo_to_cuda;
  gboolean ret;
} GLCopyData;

static GstCudaGraphicsResource *
ensure_cuda_gl_graphics_resource (GstCudaMemoryCopy * self, GstMemory * mem)
{
  GstCudaBaseTransform *trans = GST_CUDA_BASE_TRANSFORM (self);
  GQuark quark;
  GstCudaGraphicsResource *ret = NULL;

  if (!gst_is_gl_memory_pbo (mem)) {
    GST_WARNING_OBJECT (self, "memory is not GL PBO memory, %s",
        mem->allocator->mem_type);
    return NULL;
  }

  quark = gst_cuda_quark_from_id (GST_CUDA_QUARK_GRAPHICS_RESOURCE);
  ret = (GstCudaGraphicsResource *)
      gst_mini_object_get_qdata (GST_MINI_OBJECT (mem), quark);

  if (!ret) {
    GstGLMemoryPBO *pbo;
    GstGLBuffer *buf;
    GstMapInfo info;

    ret = gst_cuda_graphics_resource_new (trans->context,
        GST_OBJECT (GST_GL_BASE_MEMORY_CAST (mem)->context),
        GST_CUDA_GRAPHICS_RESOURCE_GL_BUFFER);

    if (!gst_memory_map (mem, &info, (GstMapFlags) (GST_MAP_READ | GST_MAP_GL))) {
      GST_ERROR_OBJECT (self, "Failed to map gl memory");
      gst_cuda_graphics_resource_free (ret);
      return NULL;
    }

    pbo = (GstGLMemoryPBO *) mem;
    buf = pbo->pbo;

    if (!gst_cuda_graphics_resource_register_gl_buffer (ret,
            buf->id, CU_GRAPHICS_REGISTER_FLAGS_NONE)) {
      GST_ERROR_OBJECT (self, "Failed to register gl buffer");
      gst_memory_unmap (mem, &info);
      gst_cuda_graphics_resource_free (ret);

      return NULL;
    }

    gst_memory_unmap (mem, &info);

    gst_mini_object_set_qdata (GST_MINI_OBJECT (mem), quark, ret,
        (GDestroyNotify) gst_cuda_graphics_resource_free);
  }

  return ret;
}

static void
gl_copy_thread_func (GstGLContext * gl_context, GLCopyData * data)
{
  GstCudaMemoryCopy *self = data->self;
  GstCudaBaseTransform *trans = GST_CUDA_BASE_TRANSFORM (self);
  GstCudaGraphicsResource *resources[GST_VIDEO_MAX_PLANES];
  guint num_resources;
  GstBuffer *gl_buf;
  GstVideoFrame cuda_frame;
  CUDA_MEMCPY2D copy_params = { 0, };
  CUstream cuda_stream = trans->cuda_stream;
  gboolean ret = FALSE;
  guint i;

  data->ret = FALSE;

  /* Incompatible gl context */
  gst_cuda_memory_copy_ensure_gl_interop (gl_context, &ret);
  if (!ret)
    return;

  if (data->pbo_to_cuda) {
    gl_buf = data->inbuf;
    if (!gst_video_frame_map (&cuda_frame, data->out_info, data->outbuf,
            (GstMapFlags) (GST_MAP_WRITE | GST_MAP_CUDA))) {
      GST_ERROR_OBJECT (self, "Failed to map output CUDA buffer");
      return;
    }
  } else {
    gl_buf = data->outbuf;
    if (!gst_video_frame_map (&cuda_frame, data->in_info, data->inbuf,
            (GstMapFlags) (GST_MAP_READ | GST_MAP_CUDA))) {
      GST_ERROR_OBJECT (self, "Failed to map input CUDA buffer");
      return;
    }
  }

  num_resources = gst_buffer_n_memory (gl_buf);
  g_assert (num_resources >= GST_VIDEO_FRAME_N_PLANES (&cuda_frame));

  if (!gst_cuda_context_push (trans->context)) {
    GST_ERROR_OBJECT (self, "Failed to push context");
    gst_video_frame_unmap (&cuda_frame);
    return;
  }

  for (i = 0; i < GST_VIDEO_FRAME_N_PLANES (&cuda_frame); i++) {
    GstMemory *mem = gst_buffer_peek_memory (gl_buf, i);
    GstGLMemoryPBO *pbo;

    resources[i] = ensure_cuda_gl_graphics_resource (self, mem);
    if (!resources[i])
      goto out;

    pbo = (GstGLMemoryPBO *) mem;
    if (!data->pbo_to_cuda) {
      /* Need PBO -> texture */
      GST_MINI_OBJECT_FLAG_SET (mem, GST_GL_BASE_MEMORY_TRANSFER_NEED_UPLOAD);

      /* PBO -> sysmem */
      GST_MINI_OBJECT_FLAG_SET (pbo->pbo,
          GST_GL_BASE_MEMORY_TRANSFER_NEED_DOWNLOAD);
    } else {
      /* get the texture into the PBO */
      gst_gl_memory_pbo_upload_transfer (pbo);
      gst_gl_memory_pbo_download_transfer (pbo);
    }
  }

  copy_params.srcMemoryType = CU_MEMORYTYPE_DEVICE;
  copy_params.dstMemoryType = CU_MEMORYTYPE_DEVICE;

  for (i = 0; i < GST_VIDEO_FRAME_N_PLANES (&cuda_frame); i++) {
    CUgraphicsResource cuda_resource;
    CUdeviceptr dev_ptr;
    size_t size;
    gboolean copy_ret;

    if (data->pbo_to_cuda) {
      cuda_resource =
          gst_cuda_graphics_resource_map (resources[i], cuda_stream,
          CU_GRAPHICS_MAP_RESOURCE_FLAGS_READ_ONLY);
    } else {
      cuda_resource =
          gst_cuda_graphics_resource_map (resources[i], cuda_stream,
          CU_GRAPHICS_MAP_RESOURCE_FLAGS_WRITE_DISCARD);
    }

    if (!cuda_resource) {
      GST_ERROR_OBJECT (self, "Failed to map graphics resource %d", i);
      goto out;
    }

    if (!gst_cuda_result (CuGraphicsResourceGetMappedPointer (&dev_ptr, &size,
                cuda_resource))) {
      gst_cuda_graphics_resource_unmap (resources[i], cuda_stream);
      GST_ERROR_OBJECT (self, "Failed to mapped pointer");
      goto out;
    }

    copy_params.WidthInBytes = GST_VIDEO_FRAME_COMP_WIDTH (&cuda_frame, i)
        * GST_VIDEO_FRAME_COMP_PSTRIDE (&cuda_frame, i);
    copy_params.Height = GST_VIDEO_FRAME_COMP_HEIGHT (&cuda_frame, i);

    if (data->pbo_to_cuda) {
      copy_params.srcDevice = dev_ptr;
      copy_params.srcPitch = GST_VIDEO_INFO_PLANE_STRIDE (data->in_info, i);

      copy_params.dstDevice =
          (CUdeviceptr) GST_VIDEO_FRAME_PLANE_DATA (&cuda_frame, i);
      copy_params.dstPitch = GST_VIDEO_FRAME_PLANE_STRIDE (&cuda_frame, i);
    } else {
      copy_params.srcDevice =
          (CUdeviceptr) GST_VIDEO_FRAME_PLANE_DATA (&cuda_frame, i);
      copy_params.srcPitch = GST_VIDEO_FRAME_PLANE_STRIDE (&cuda_frame, i);

      copy_params.dstDevice = dev_ptr;
      copy_params.dstPitch = GST_VIDEO_INFO_PLANE_STRIDE (data->out_info, i);
    }

    copy_ret = gst_cuda_result (CuMemcpy2DAsync (&copy_params, cuda_stream));
    gst_cuda_graphics_resource_unmap (resources[i], cuda_stream);

    if (!copy_ret) {
      GST_ERROR_OBJECT (self, "Failted to copy plane %d", i);
      goto out;
    }
  }

  data->ret = TRUE;

out:
  gst_cuda_result (CuStreamSynchronize (cuda_stream));
  gst_cuda_context_pop (NULL);
  gst_video_frame_unmap (&cuda_frame);
}

static gboolean
gst_cuda_memory_copy_gl_interop (GstCudaMemoryCopy * self,
    GstBuffer * inbuf, GstVideoInfo * in_info, GstBuffer * outbuf,
    GstVideoInfo * out_info, GstGLContext * context, gboolean pbo_to_cuda)
{
  GLCopyData data;

  data.self = self;
  data.inbuf = inbuf;
  data.in_info = in_info;
  data.outbuf = outbuf;
  data.out_info = out_info;
  data.pbo_to_cuda = pbo_to_cuda;
  data.ret = FALSE;

  gst_gl_context_thread_add (context,
      (GstGLContextThreadFunc) gl_copy_thread_func, &data);

  return data.ret;
}
#endif

static gboolean
gst_cuda_memory_copy_transform_cuda (GstCudaMemoryCopy * self,
    GstBuffer * inbuf, GstVideoInfo * in_info, gboolean in_cuda,
    GstBuffer * outbuf, GstVideoInfo * out_info, gboolean out_cuda)
{
  GstCudaBaseTransform *trans = GST_CUDA_BASE_TRANSFORM (self);
  GstVideoFrame in_frame, out_frame;
  GstMapFlags in_map = GST_MAP_READ;
  GstMapFlags out_map = GST_MAP_WRITE;
  gboolean ret = TRUE;
  CUstream cuda_stream = trans->cuda_stream;
  guint i;

  g_assert (in_cuda || out_cuda);

  if (in_cuda)
    in_map |= GST_MAP_CUDA;

  if (out_cuda)
    out_map |= GST_MAP_CUDA;

  if (!gst_video_frame_map (&in_frame, in_info, inbuf, in_map)) {
    GST_ERROR_OBJECT (self, "Failed to map input buffer");
    return FALSE;
  }

  if (!gst_video_frame_map (&out_frame, out_info, outbuf, out_map)) {
    gst_video_frame_unmap (&in_frame);

    GST_ERROR_OBJECT (self, "Failed to map output buffer");
    return FALSE;
  }

  if (!gst_cuda_context_push (trans->context)) {
    GST_ERROR_OBJECT (self, "Failed to push context");
    gst_video_frame_unmap (&in_frame);
    gst_video_frame_unmap (&out_frame);
    return FALSE;
  }

  for (i = 0; i < GST_VIDEO_FRAME_N_PLANES (&in_frame); i++) {
    CUDA_MEMCPY2D copy_params = { 0, };

    if (in_cuda) {
      copy_params.srcMemoryType = CU_MEMORYTYPE_DEVICE;
      copy_params.srcDevice =
          (CUdeviceptr) GST_VIDEO_FRAME_PLANE_DATA (&in_frame, i);
    } else {
      copy_params.srcMemoryType = CU_MEMORYTYPE_HOST;
      copy_params.srcHost = GST_VIDEO_FRAME_PLANE_DATA (&in_frame, i);
    }
    copy_params.srcPitch = GST_VIDEO_FRAME_PLANE_STRIDE (&in_frame, i);

    if (out_cuda) {
      copy_params.dstMemoryType = CU_MEMORYTYPE_DEVICE;
      copy_params.dstDevice =
          (CUdeviceptr) GST_VIDEO_FRAME_PLANE_DATA (&out_frame, i);
    } else {
      copy_params.dstMemoryType = CU_MEMORYTYPE_HOST;
      copy_params.dstHost = GST_VIDEO_FRAME_PLANE_DATA (&out_frame, i);
    }
    copy_params.dstPitch = GST_VIDEO_FRAME_PLANE_STRIDE (&out_frame, i);

    copy_params.WidthInBytes = GST_VIDEO_FRAME_COMP_WIDTH (&in_frame, i)
        * GST_VIDEO_FRAME_COMP_PSTRIDE (&in_frame, i);
    copy_params.Height = GST_VIDEO_FRAME_COMP_HEIGHT (&in_frame, i);

    ret = gst_cuda_result (CuMemcpy2DAsync (&copy_params, cuda_stream));
    if (!ret) {
      GST_ERROR_OBJECT (self, "Failted to copy plane %d", i);
      break;
    }
  }

  gst_video_frame_unmap (&in_frame);
  gst_video_frame_unmap (&out_frame);

  gst_cuda_result (CuStreamSynchronize (cuda_stream));
  gst_cuda_context_pop (NULL);

  return ret;
}

static GstFlowReturn
gst_cuda_memory_copy_transform (GstBaseTransform * trans, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstCudaMemoryCopy *self = GST_CUDA_MEMORY_COPY (trans);
  GstCudaBaseTransform *ctrans = GST_CUDA_BASE_TRANSFORM (trans);
  GstMemory *in_mem;
  GstMemory *out_mem;
  gboolean in_cuda, out_cuda;
  GstVideoInfo *in_info, *out_info;
  gboolean ret = FALSE;
#ifdef HAVE_NVCODEC_GST_GL
  gboolean in_gl = FALSE;
  gboolean out_gl = FALSE;
#endif

  in_info = &ctrans->in_info;
  out_info = &ctrans->out_info;

  in_mem = gst_buffer_peek_memory (inbuf, 0);
  if (!in_mem) {
    GST_ERROR_OBJECT (self, "Empty input buffer");
    return GST_FLOW_ERROR;
  }

  out_mem = gst_buffer_peek_memory (outbuf, 0);
  if (!out_mem) {
    GST_ERROR_OBJECT (self, "Empty output buffer");
    return GST_FLOW_ERROR;
  }

  in_cuda = gst_is_cuda_memory (in_mem);
  out_cuda = gst_is_cuda_memory (out_mem);

#ifdef HAVE_NVCODEC_GST_GL
  if (self->gl_context) {
    in_gl = gst_is_gl_memory_pbo (in_mem);
    out_gl = gst_is_gl_memory_pbo (out_mem);
  }
#endif

  if (!in_cuda && !out_cuda) {
    GST_TRACE_OBJECT (self, "Both in/out buffers are not CUDA");
    if (!gst_cuda_memory_copy_transform_sysmem (self, inbuf, in_info,
            outbuf, out_info)) {
      return GST_FLOW_ERROR;
    }

    return GST_FLOW_OK;
  }
#ifdef HAVE_NVCODEC_GST_GL
  if (in_gl) {
    GstGLMemory *gl_mem = (GstGLMemory *) in_mem;
    GstGLContext *context = gl_mem->mem.context;

    GST_TRACE_OBJECT (self, "PBO -> CUDA");

    ret = gst_cuda_memory_copy_gl_interop (self, inbuf, in_info,
        outbuf, out_info, context, TRUE);
  } else if (out_gl) {
    GstGLMemory *gl_mem = (GstGLMemory *) out_mem;
    GstGLContext *context = gl_mem->mem.context;

    GST_TRACE_OBJECT (self, "CUDA -> PBO");

    ret = gst_cuda_memory_copy_gl_interop (self, inbuf, in_info,
        outbuf, out_info, context, FALSE);
  } else {
#endif
    GST_TRACE_OBJECT (self, "CUDA <-> SYSTEM");

    ret = gst_cuda_memory_copy_transform_cuda (self, inbuf, in_info, in_cuda,
        outbuf, out_info, out_cuda);
  }

  if (!ret) {
    GST_LOG_OBJECT (self, "Failed to copy using fast path, trying fallback");
    ret =
        gst_cuda_memory_copy_transform_sysmem (self, inbuf, in_info, outbuf,
        out_info);
  }

  if (ret)
    return GST_FLOW_OK;

  return GST_FLOW_ERROR;
}

static void
gst_cuda_upload_class_init (GstCudaUploadClass * klass, gpointer data)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *trans_class = GST_BASE_TRANSFORM_CLASS (klass);
  GstCudaMemoryCopyClass *copy_class = GST_CUDA_MEMORY_COPY_CLASS (klass);
  GstCudaMemoryCopyClassData *cdata = (GstCudaMemoryCopyClassData *) data;

  element_class->set_context =
      GST_DEBUG_FUNCPTR (gst_cuda_memory_copy_set_context);

  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
          cdata->sink_caps));
  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
          cdata->src_caps));

  gst_element_class_set_static_metadata (element_class,
      "CUDA uploader", "Filter/Video",
      "Uploads data into NVIDA GPU via CUDA APIs",
      "Seungha Yang <seungha.yang@navercorp.com>");

  trans_class->transform = GST_DEBUG_FUNCPTR (gst_cuda_memory_copy_transform);

  copy_class->uploader = TRUE;

  gst_caps_unref (cdata->sink_caps);
  gst_caps_unref (cdata->src_caps);
  g_free (cdata);
}

static void
gst_cuda_upload_init (GstCudaUpload * self)
{
}

static void
gst_cuda_download_class_init (GstCudaDownloadClass * klass, gpointer data)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *trans_class = GST_BASE_TRANSFORM_CLASS (klass);
  GstCudaMemoryCopyClass *copy_class = GST_CUDA_MEMORY_COPY_CLASS (klass);
  GstCudaMemoryCopyClassData *cdata = (GstCudaMemoryCopyClassData *) data;

  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
          cdata->sink_caps));
  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
          cdata->src_caps));

  gst_element_class_set_static_metadata (element_class,
      "CUDA downloader", "Filter/Video",
      "Downloads data from NVIDA GPU via CUDA APIs",
      "Seungha Yang <seungha.yang@navercorp.com>");

  trans_class->transform = GST_DEBUG_FUNCPTR (gst_cuda_memory_copy_transform);

  copy_class->uploader = FALSE;

  gst_caps_unref (cdata->sink_caps);
  gst_caps_unref (cdata->src_caps);
  g_free (cdata);
}

static void
gst_cuda_download_init (GstCudaDownload * self)
{
}

void
gst_cuda_memory_copy_register (GstPlugin * plugin, guint rank)
{
  GType upload_type, download_type;
  GTypeInfo upload_type_info = {
    sizeof (GstCudaUploadClass),
    NULL,
    NULL,
    (GClassInitFunc) gst_cuda_upload_class_init,
    NULL,
    NULL,
    sizeof (GstCudaUpload),
    0,
    (GInstanceInitFunc) gst_cuda_upload_init,
  };
  GTypeInfo download_type_info = {
    sizeof (GstCudaDownloadClass),
    NULL,
    NULL,
    (GClassInitFunc) gst_cuda_download_class_init,
    NULL,
    NULL,
    sizeof (GstCudaDownload),
    0,
    (GInstanceInitFunc) gst_cuda_download_init,
  };
  GstCaps *sys_caps;
  GstCaps *cuda_caps;
#ifdef HAVE_NVCODEC_GST_GL
  GstCaps *gl_caps;
#endif
  GstCaps *upload_sink_caps;
  GstCaps *upload_src_caps;
  GstCaps *download_sink_caps;
  GstCaps *download_src_caps;
  GstCudaMemoryCopyClassData *upload_cdata;
  GstCudaMemoryCopyClassData *download_cdata;

  GST_DEBUG_CATEGORY_INIT (gst_cuda_memory_copy_debug,
      "cudamemorycopy", 0, "cudamemorycopy");

  sys_caps = gst_caps_from_string (GST_VIDEO_CAPS_MAKE (GST_CUDA_FORMATS));
  cuda_caps =
      gst_caps_from_string (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
      (GST_CAPS_FEATURE_MEMORY_CUDA_MEMORY, GST_CUDA_FORMATS));
#ifdef HAVE_NVCODEC_GST_GL
  gl_caps =
      gst_caps_from_string (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
      (GST_CAPS_FEATURE_MEMORY_GL_MEMORY, GST_CUDA_GL_FORMATS));
#endif

  upload_sink_caps = gst_caps_copy (sys_caps);
#ifdef HAVE_NVCODEC_GST_GL
  upload_sink_caps = gst_caps_merge (upload_sink_caps, gst_caps_copy (gl_caps));
#endif
  upload_sink_caps =
      gst_caps_merge (upload_sink_caps, gst_caps_copy (cuda_caps));

  upload_src_caps = gst_caps_copy (cuda_caps);
  upload_src_caps = gst_caps_merge (upload_src_caps, gst_caps_copy (sys_caps));

  download_sink_caps = gst_caps_copy (cuda_caps);
  download_sink_caps =
      gst_caps_merge (download_sink_caps, gst_caps_copy (sys_caps));

  download_src_caps = sys_caps;
#ifdef HAVE_NVCODEC_GST_GL
  download_src_caps = gst_caps_merge (download_src_caps, gl_caps);
#endif
  download_src_caps = gst_caps_merge (download_src_caps, cuda_caps);

  GST_MINI_OBJECT_FLAG_SET (upload_sink_caps,
      GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);
  GST_MINI_OBJECT_FLAG_SET (upload_src_caps,
      GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);

  GST_MINI_OBJECT_FLAG_SET (download_sink_caps,
      GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);
  GST_MINI_OBJECT_FLAG_SET (download_src_caps,
      GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);

  upload_cdata = g_new0 (GstCudaMemoryCopyClassData, 1);
  upload_cdata->sink_caps = upload_sink_caps;
  upload_cdata->src_caps = upload_src_caps;
  upload_type_info.class_data = upload_cdata;

  download_cdata = g_new0 (GstCudaMemoryCopyClassData, 1);
  download_cdata->sink_caps = download_sink_caps;
  download_cdata->src_caps = download_src_caps;
  download_type_info.class_data = download_cdata;

  upload_type = g_type_register_static (GST_TYPE_CUDA_MEMORY_COPY,
      "GstCudaUpload", &upload_type_info, 0);
  download_type = g_type_register_static (GST_TYPE_CUDA_MEMORY_COPY,
      "GstCudaDownload", &download_type_info, 0);

  if (!gst_element_register (plugin, "cudaupload", rank, upload_type))
    GST_WARNING ("Failed to register cudaupload element");

  if (!gst_element_register (plugin, "cudadownload", rank, download_type))
    GST_WARNING ("Failed to register cudadownload element");
}
