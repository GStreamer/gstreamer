/*
 * GStreamer
 * Copyright (C) 2012 Matthew Waters <ystree00@gmail.com>
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

#include <gst/gl/gl.h>
#if GST_GL_HAVE_PLATFORM_EGL && GST_GL_HAVE_DMABUF
#include <gst/gl/egl/gsteglimage.h>
#include <gst/allocators/gstdmabuf.h>
#endif

#include "gstgldownloadelement.h"

GST_DEBUG_CATEGORY_STATIC (gst_gl_download_element_debug);
#define GST_CAT_DEFAULT gst_gl_download_element_debug

#define gst_gl_download_element_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstGLDownloadElement, gst_gl_download_element,
    GST_TYPE_GL_BASE_FILTER,
    GST_DEBUG_CATEGORY_INIT (gst_gl_download_element_debug, "gldownloadelement",
        0, "download element"););

static gboolean gst_gl_download_element_get_unit_size (GstBaseTransform * trans,
    GstCaps * caps, gsize * size);
static GstCaps *gst_gl_download_element_transform_caps (GstBaseTransform * bt,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static gboolean gst_gl_download_element_set_caps (GstBaseTransform * bt,
    GstCaps * in_caps, GstCaps * out_caps);
static GstFlowReturn
gst_gl_download_element_prepare_output_buffer (GstBaseTransform * bt,
    GstBuffer * buffer, GstBuffer ** outbuf);
static GstFlowReturn gst_gl_download_element_transform (GstBaseTransform * bt,
    GstBuffer * buffer, GstBuffer * outbuf);
static gboolean gst_gl_download_element_decide_allocation (GstBaseTransform *
    trans, GstQuery * query);
static void gst_gl_download_element_finalize (GObject * object);

#if GST_GL_HAVE_PLATFORM_EGL && GST_GL_HAVE_DMABUF
#define EXTRA_CAPS_TEMPLATE "video/x-raw(" GST_CAPS_FEATURE_MEMORY_DMABUF "); "
#else
#define EXTRA_CAPS_TEMPLATE
#endif

static GstStaticPadTemplate gst_gl_download_element_src_pad_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (
        EXTRA_CAPS_TEMPLATE
        "video/x-raw; video/x-raw(memory:GLMemory)"));

static GstStaticPadTemplate gst_gl_download_element_sink_pad_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw(memory:GLMemory); video/x-raw"));

static void
gst_gl_download_element_class_init (GstGLDownloadElementClass * klass)
{
  GstBaseTransformClass *bt_class = GST_BASE_TRANSFORM_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  bt_class->transform_caps = gst_gl_download_element_transform_caps;
  bt_class->set_caps = gst_gl_download_element_set_caps;
  bt_class->get_unit_size = gst_gl_download_element_get_unit_size;
  bt_class->prepare_output_buffer =
      gst_gl_download_element_prepare_output_buffer;
  bt_class->transform = gst_gl_download_element_transform;
  bt_class->decide_allocation = gst_gl_download_element_decide_allocation;

  bt_class->passthrough_on_same_caps = TRUE;

  gst_element_class_add_static_pad_template (element_class,
      &gst_gl_download_element_src_pad_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_gl_download_element_sink_pad_template);

  gst_element_class_set_metadata (element_class,
      "OpenGL downloader", "Filter/Video",
      "Downloads data from OpenGL", "Matthew Waters <matthew@centricular.com>");

  object_class->finalize = gst_gl_download_element_finalize;
}

static void
gst_gl_download_element_init (GstGLDownloadElement * download)
{
  gst_base_transform_set_prefer_passthrough (GST_BASE_TRANSFORM (download),
      TRUE);
}

static gboolean
gst_gl_download_element_set_caps (GstBaseTransform * bt, GstCaps * in_caps,
    GstCaps * out_caps)
{
  GstGLDownloadElement *dl = GST_GL_DOWNLOAD_ELEMENT (bt);
  GstVideoInfo out_info;
  GstCapsFeatures *features = NULL;

  if (!gst_video_info_from_caps (&out_info, out_caps))
    return FALSE;

  features = gst_caps_get_features (out_caps, 0);

  dl->do_pbo_transfers = FALSE;
  if (dl->dmabuf_allocator) {
    gst_object_unref (GST_OBJECT (dl->dmabuf_allocator));
    dl->dmabuf_allocator = NULL;
  }

  if (!features) {
    dl->do_pbo_transfers = TRUE;
    return TRUE;
  }

  if (gst_caps_features_contains (features, GST_CAPS_FEATURE_MEMORY_GL_MEMORY)) {
    /* do nothing with the buffer */
#if GST_GL_HAVE_PLATFORM_EGL && GST_GL_HAVE_DMABUF
  } else if (gst_caps_features_contains (features,
          GST_CAPS_FEATURE_MEMORY_DMABUF)) {
    dl->dmabuf_allocator = gst_dmabuf_allocator_new ();
#endif
  } else if (gst_caps_features_contains (features,
          GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY)) {
    dl->do_pbo_transfers = TRUE;
  }

  return TRUE;
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
gst_gl_download_element_transform_caps (GstBaseTransform * bt,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstCaps *result, *tmp;

  if (direction == GST_PAD_SRC) {
    tmp = _set_caps_features (caps, GST_CAPS_FEATURE_MEMORY_GL_MEMORY);
    tmp = gst_caps_merge (gst_caps_ref (caps), tmp);
  } else {
    GstCaps *newcaps;
    tmp = gst_caps_ref (caps);

#if GST_GL_HAVE_PLATFORM_EGL && GST_GL_HAVE_DMABUF
    newcaps = _set_caps_features (caps, GST_CAPS_FEATURE_MEMORY_DMABUF);
    _remove_field (newcaps, "texture-target");
    tmp = gst_caps_merge (tmp, newcaps);
#endif

    newcaps = _set_caps_features (caps, GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY);
    _remove_field (newcaps, "texture-target");
    tmp = gst_caps_merge (tmp, newcaps);
  }

  if (filter) {
    result = gst_caps_intersect_full (filter, tmp, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (tmp);
  } else {
    result = tmp;
  }

  GST_DEBUG_OBJECT (bt, "returning caps %" GST_PTR_FORMAT, result);

  return result;
}

static gboolean
gst_gl_download_element_get_unit_size (GstBaseTransform * trans, GstCaps * caps,
    gsize * size)
{
  gboolean ret = FALSE;
  GstVideoInfo info;

  ret = gst_video_info_from_caps (&info, caps);
  if (ret)
    *size = GST_VIDEO_INFO_SIZE (&info);

  return TRUE;
}

#if GST_GL_HAVE_PLATFORM_EGL && GST_GL_HAVE_DMABUF

struct DmabufInfo
{
  GstMemory *dmabuf;
  gint stride;
  gsize offset;
};

static void
_free_dmabuf_info (struct DmabufInfo *info)
{
  gst_memory_unref (info->dmabuf);
  g_free (info);
}

static GQuark
_dmabuf_info_quark (void)
{
  static GQuark quark = 0;

  if (!quark)
    quark = g_quark_from_static_string ("GstGLDownloadDmabufInfo");
  return quark;
}

static struct DmabufInfo *
_get_cached_dmabuf_info (GstGLMemory * mem)
{
  return gst_mini_object_get_qdata (GST_MINI_OBJECT (mem),
      _dmabuf_info_quark ());
}

static void
_set_cached_dmabuf_info (GstGLMemory * mem, struct DmabufInfo *info)
{
  return gst_mini_object_set_qdata (GST_MINI_OBJECT (mem),
      _dmabuf_info_quark (), info, (GDestroyNotify) _free_dmabuf_info);
}

struct DmabufTransfer
{
  GstGLDownloadElement *download;
  GstGLMemory *glmem;
  struct DmabufInfo *info;
};

static void
_create_cached_dmabuf_info (GstGLContext * context, gpointer data)
{
  struct DmabufTransfer *transfer = (struct DmabufTransfer *) data;
  GstEGLImage *image;

  image = gst_egl_image_from_texture (context, transfer->glmem, NULL);
  if (image) {
    int fd;
    gint stride;
    gsize offset;

    if (gst_egl_image_export_dmabuf (image, &fd, &stride, &offset)) {
      GstGLDownloadElement *download = transfer->download;
      struct DmabufInfo *info;
      gsize maxsize;

      gst_memory_get_sizes (GST_MEMORY_CAST (transfer->glmem), NULL, &maxsize);

      info = g_new0 (struct DmabufInfo, 1);
      info->dmabuf =
          gst_dmabuf_allocator_alloc (download->dmabuf_allocator, fd, maxsize);
      info->stride = stride;
      info->offset = offset;

      transfer->info = info;
    }

    gst_egl_image_unref (image);
  }
}

static GstBuffer *
_try_export_dmabuf (GstGLDownloadElement * download, GstBuffer * inbuf)
{
  GstGLMemory *glmem;
  GstBuffer *buffer = NULL;
  int i;
  gsize offset[GST_VIDEO_MAX_PLANES];
  gint stride[GST_VIDEO_MAX_PLANES];
  GstCaps *src_caps;
  GstVideoInfo out_info;
  gsize total_offset;

  glmem = GST_GL_MEMORY_CAST (gst_buffer_peek_memory (inbuf, 0));
  if (glmem) {
    GstGLContext *context = GST_GL_BASE_MEMORY_CAST (glmem)->context;
    if (gst_gl_context_get_gl_platform (context) != GST_GL_PLATFORM_EGL)
      return NULL;
  }

  buffer = gst_buffer_new ();
  total_offset = 0;

  for (i = 0; i < gst_buffer_n_memory (inbuf); i++) {
    struct DmabufInfo *info;

    glmem = GST_GL_MEMORY_CAST (gst_buffer_peek_memory (inbuf, i));
    info = _get_cached_dmabuf_info (glmem);
    if (!info) {
      GstGLContext *context = GST_GL_BASE_MEMORY_CAST (glmem)->context;
      struct DmabufTransfer transfer;

      transfer.download = download;
      transfer.glmem = glmem;
      transfer.info = NULL;
      gst_gl_context_thread_add (context, _create_cached_dmabuf_info,
          &transfer);
      info = transfer.info;

      if (info)
        _set_cached_dmabuf_info (glmem, info);
    }

    if (info) {
      offset[i] = total_offset + info->offset;
      stride[i] = info->stride;
      total_offset += gst_memory_get_sizes (info->dmabuf, NULL, NULL);
      gst_buffer_insert_memory (buffer, -1, gst_memory_ref (info->dmabuf));
    } else {
      gst_buffer_unref (buffer);
      buffer = NULL;
      goto export_complete;
    }
  }

  src_caps = gst_pad_get_current_caps (GST_BASE_TRANSFORM (download)->srcpad);
  gst_video_info_from_caps (&out_info, src_caps);

  if (download->add_videometa) {
    gst_buffer_add_video_meta_full (buffer, GST_VIDEO_FRAME_FLAG_NONE,
        out_info.finfo->format, out_info.width, out_info.height,
        out_info.finfo->n_planes, offset, stride);
  } else {
    int i;
    gboolean match = TRUE;
    for (i = 0; i < gst_buffer_n_memory (inbuf); i++) {
      if (offset[i] != out_info.offset[i] || stride[i] != out_info.stride[i]) {
        match = FALSE;
        break;
      }
    }

    if (!match) {
      gst_buffer_unref (buffer);
      buffer = NULL;
    }
  }

export_complete:

  return buffer;
}
#endif /* GST_GL_HAVE_PLATFORM_EGL && GST_GL_HAVE_DMABUF */

static GstFlowReturn
gst_gl_download_element_prepare_output_buffer (GstBaseTransform * bt,
    GstBuffer * inbuf, GstBuffer ** outbuf)
{
  GstGLDownloadElement *dl = GST_GL_DOWNLOAD_ELEMENT (bt);
  gint i, n;

  *outbuf = inbuf;

  if (dl->do_pbo_transfers) {
    n = gst_buffer_n_memory (*outbuf);
    for (i = 0; i < n; i++) {
      GstMemory *mem = gst_buffer_peek_memory (*outbuf, i);

      if (gst_is_gl_memory_pbo (mem))
        gst_gl_memory_pbo_download_transfer ((GstGLMemoryPBO *) mem);
    }
  }
#if GST_GL_HAVE_PLATFORM_EGL && GST_GL_HAVE_DMABUF
  else if (dl->dmabuf_allocator) {
    GstBuffer *buffer = _try_export_dmabuf (dl, inbuf);
    if (buffer) {
      if (GST_BASE_TRANSFORM_GET_CLASS (bt)->copy_metadata)
        if (!GST_BASE_TRANSFORM_GET_CLASS (bt)->copy_metadata (bt, inbuf,
                buffer)) {
          GST_ELEMENT_WARNING (GST_ELEMENT (bt), STREAM, NOT_IMPLEMENTED,
              ("could not copy metadata"), (NULL));
        }

      *outbuf = buffer;
    } else {
      GstCaps *src_caps;
      GstCapsFeatures *features;

      gst_object_unref (dl->dmabuf_allocator);
      dl->dmabuf_allocator = NULL;

      src_caps = gst_pad_get_current_caps (bt->srcpad);
      src_caps = gst_caps_make_writable (src_caps);
      features = gst_caps_get_features (src_caps, 0);
      gst_caps_features_remove (features, GST_CAPS_FEATURE_MEMORY_DMABUF);

      if (!gst_base_transform_update_src_caps (bt, src_caps)) {
        GST_ERROR_OBJECT (bt, "DMABuf exportation didn't work and system "
            "memory is not supported.");
        return GST_FLOW_NOT_NEGOTIATED;
      }
    }
  }
#endif

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_gl_download_element_transform (GstBaseTransform * bt,
    GstBuffer * inbuf, GstBuffer * outbuf)
{
  return GST_FLOW_OK;
}

static gboolean
gst_gl_download_element_decide_allocation (GstBaseTransform * trans,
    GstQuery * query)
{
  GstGLDownloadElement *download = GST_GL_DOWNLOAD_ELEMENT_CAST (trans);

  if (gst_query_find_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL)) {
    download->add_videometa = TRUE;
  } else {
    download->add_videometa = FALSE;
  }

  return GST_BASE_TRANSFORM_CLASS (parent_class)->decide_allocation (trans,
      query);
}

static void
gst_gl_download_element_finalize (GObject * object)
{
  GstGLDownloadElement *download = GST_GL_DOWNLOAD_ELEMENT_CAST (object);

  if (download->dmabuf_allocator) {
    gst_object_unref (GST_OBJECT (download->dmabuf_allocator));
    download->dmabuf_allocator = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}
