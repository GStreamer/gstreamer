/*
 * GStreamer
 * Copyright (C) 2007 David Schleef <ds@schleef.org>
 * Copyright (C) 2008 Julien Isorce <julien.isorce@gmail.com>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:element-gldownload
 *
 * download opengl textures into video frames.
 *
 * <refsect2>
 * <title>Color space conversion</title>
 * <para>
 * When needed, the color space conversion is made in a fragment shader using
 * one frame buffer object instance.
 * </para>
 * </refsect2>
 * <refsect2>
 * <title>Examples</title>
 * |[
 * gst-launch -v videotestsrc ! "video/x-raw-rgb" ! glupload ! gldownload ! \
 *   "video/x-raw-rgb" ! ximagesink
 * ]| A pipeline to test downloading.
 * No special opengl extension is used in this pipeline, that's why it should work
 * with OpenGL >= 1.1. That's the case if you are using the MESA3D driver v1.3.
  |[
 * gst-launch -v videotestsrc ! "video/x-raw-rgb, width=640, height=480" ! glupload ! gldownload ! \
 *   "video/x-raw-rgb, width=320, height=240" ! ximagesink
 * ]| A pipeline to test hardware scaling.
 * Frame buffer extension is required. Inded one FBO is used bettween glupload and gldownload,
 * because the texture needs to be resized.
 * |[
 * gst-launch -v gltestsrc ! gldownload ! xvimagesink
 * ]| A pipeline to test hardware colorspace conversion.
 * Your driver must support GLSL (OpenGL Shading Language needs OpenGL >= 2.1).
 * Texture RGB32 is converted to one of the 4 following format YUY2, UYVY, I420, YV12 and AYUV,
 * through some fragment shaders and using one framebuffer (FBO extension OpenGL >= 1.4).
 * MESA >= 7.1 supports GLSL but it's made in software.
 * |[
 * gst-launch -v videotestsrc ! glupload ! gldownload ! "video/x-raw-yuv, format=(fourcc)YUY2" ! glimagesink
 * ]| A pipeline to test hardware colorspace conversion
 * FBO and GLSL are required.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstgldownload.h"

#define GST_CAT_DEFAULT gst_gl_download_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

static GstStaticPadTemplate gst_gl_download_src_pad_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_GL_DOWNLOAD_VIDEO_CAPS)
    );

static GstStaticPadTemplate gst_gl_download_sink_pad_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_GL_VIDEO_CAPS)
    );

enum
{
  PROP_0
};

#define gst_gl_download_parent_class parent_class
#define DEBUG_INIT \
    GST_DEBUG_CATEGORY_INIT (gst_gl_download_debug, "gldownload", 0, "gldownload element");
G_DEFINE_TYPE_WITH_CODE (GstGLDownload, gst_gl_download,
    GST_TYPE_BASE_TRANSFORM, DEBUG_INIT);

static void gst_gl_download_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gl_download_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_gl_download_query (GstBaseTransform * trans,
    GstPadDirection direction, GstQuery * query);

static void gst_gl_download_reset (GstGLDownload * download);
static gboolean gst_gl_download_set_caps (GstBaseTransform * bt,
    GstCaps * incaps, GstCaps * outcaps);
static GstCaps *gst_gl_download_transform_caps (GstBaseTransform * bt,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static gboolean gst_gl_download_start (GstBaseTransform * bt);
static gboolean gst_gl_download_stop (GstBaseTransform * bt);
static GstFlowReturn gst_gl_download_transform (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf);
static gboolean gst_gl_download_get_unit_size (GstBaseTransform * trans,
    GstCaps * caps, gsize * size);
static gboolean gst_gl_download_decide_allocation (GstBaseTransform * trans,
    GstQuery * query);
static gboolean gst_gl_download_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_allocation, GstQuery * query);


static void
gst_gl_download_class_init (GstGLDownloadClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;

  gobject_class = (GObjectClass *) klass;
  element_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_gl_download_set_property;
  gobject_class->get_property = gst_gl_download_get_property;

  gst_element_class_set_details_simple (element_class, "OpenGL video maker",
      "Filter/Effect", "A from GL to video flow filter",
      "Julien Isorce <julien.isorce@gmail.com>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_gl_download_src_pad_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_gl_download_sink_pad_template));

  GST_BASE_TRANSFORM_CLASS (klass)->query = gst_gl_download_query;
  GST_BASE_TRANSFORM_CLASS (klass)->transform_caps =
      gst_gl_download_transform_caps;
  GST_BASE_TRANSFORM_CLASS (klass)->transform = gst_gl_download_transform;
  GST_BASE_TRANSFORM_CLASS (klass)->start = gst_gl_download_start;
  GST_BASE_TRANSFORM_CLASS (klass)->stop = gst_gl_download_stop;
  GST_BASE_TRANSFORM_CLASS (klass)->set_caps = gst_gl_download_set_caps;
  GST_BASE_TRANSFORM_CLASS (klass)->get_unit_size =
      gst_gl_download_get_unit_size;
  GST_BASE_TRANSFORM_CLASS (klass)->propose_allocation =
      gst_gl_download_propose_allocation;
  GST_BASE_TRANSFORM_CLASS (klass)->decide_allocation =
      gst_gl_download_decide_allocation;
}


static void
gst_gl_download_init (GstGLDownload * download)
{
  gst_gl_download_reset (download);
}


static void
gst_gl_download_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  //GstGLDownload *download = GST_GL_DOWNLOAD (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static void
gst_gl_download_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  //GstGLDownload *download = GST_GL_DOWNLOAD (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_gl_download_query (GstBaseTransform * trans, GstPadDirection direction,
    GstQuery * query)
{
  GstGLDownload *download;
  gboolean res;

  download = GST_GL_DOWNLOAD (trans);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CUSTOM:
    {
      GstStructure *structure = gst_query_writable_structure (query);
      if (direction == GST_PAD_SINK &&
          gst_structure_has_name (structure, "gstgldisplay")) {
        gst_structure_set (structure, "gstgldisplay", G_TYPE_POINTER,
            download->display, NULL);
        res = TRUE;
      } else
        res =
            GST_BASE_TRANSFORM_CLASS (parent_class)->query (trans, direction,
            query);
      break;
    }
    default:
      res =
          GST_BASE_TRANSFORM_CLASS (parent_class)->query (trans, direction,
          query);
      break;
  }

  return res;
}

static void
gst_gl_download_reset (GstGLDownload * download)
{
  if (download->display) {
    g_object_unref (download->display);
    download->display = NULL;
  }
}


static gboolean
gst_gl_download_start (GstBaseTransform * bt)
{
  GstGLDownload *download = GST_GL_DOWNLOAD (bt);

  GST_INFO ("Creating GstGLDisplay");
  download->display = gst_gl_display_new ();
  if (!gst_gl_display_create_context (download->display, 0)) {
    GST_ELEMENT_ERROR (download, RESOURCE, NOT_FOUND,
        GST_GL_DISPLAY_ERR_MSG (download->display), (NULL));
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_gl_download_stop (GstBaseTransform * bt)
{
  GstGLDownload *download = GST_GL_DOWNLOAD (bt);

  gst_gl_download_reset (download);

  return TRUE;
}

/* from videoconvert code */
/* copies the given caps */
static GstCaps *
gst_gl_download_caps_remove_format_info (GstCaps * caps)
{
  GstStructure *st;
  gint i, n;
  GstCaps *res;

  res = gst_caps_new_empty ();

  n = gst_caps_get_size (caps);
  for (i = 0; i < n; i++) {
    st = gst_caps_get_structure (caps, i);

    /* If this is already expressed by the existing caps
     * skip this structure */
    if (i > 0 && gst_caps_is_subset_structure (res, st))
      continue;

    st = gst_structure_copy (st);
    gst_structure_remove_fields (st, "format", "palette_data",
        "colorimetry", "chroma-site", NULL);
    gst_caps_append_structure (res, st);
  }

  return res;
}

/* from videoconvert code */
static GstCaps *
gst_gl_download_transform_caps (GstBaseTransform * bt,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstCaps *tmp, *tmp2;
  GstCaps *result;

  /* Get all possible caps that we can transform to */
  tmp = gst_gl_download_caps_remove_format_info (caps);

  if (filter) {
    tmp2 = gst_caps_intersect_full (filter, tmp, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (tmp);
    tmp = tmp2;
  }

  result = tmp;

  GST_DEBUG_OBJECT (bt, "transformed %" GST_PTR_FORMAT " into %"
      GST_PTR_FORMAT, caps, result);

  return result;
}

static gboolean
gst_gl_download_set_caps (GstBaseTransform * bt, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstGLDownload *download;
  GstVideoInfo in_vinfo, out_vinfo;
  GstVideoFormat video_format;
  gint width, height;
  gboolean ret;

  download = GST_GL_DOWNLOAD (bt);

  GST_DEBUG ("called with in: %" GST_PTR_FORMAT " and out: %" GST_PTR_FORMAT,
      incaps, outcaps);

  ret = gst_video_info_from_caps (&in_vinfo, incaps);
  ret |= gst_video_info_from_caps (&out_vinfo, outcaps);

  if (!ret) {
    GST_ERROR ("bad caps");
    return FALSE;
  }

  download->out_info = out_vinfo;
  download->in_info = in_vinfo;
  video_format = GST_VIDEO_INFO_FORMAT (&out_vinfo);
  width = GST_VIDEO_INFO_WIDTH (&out_vinfo);
  height = GST_VIDEO_INFO_HEIGHT (&out_vinfo);

  if (!download->display) {
    GST_ERROR ("display is null");
    return FALSE;
  }
  //blocking call, init color space conversion if needed
  ret = gst_gl_display_init_download (download->display, video_format,
      width, height);
  if (!ret)
    GST_ELEMENT_ERROR (download, RESOURCE, NOT_FOUND,
        GST_GL_DISPLAY_ERR_MSG (download->display), (NULL));

  return ret;
}

static gboolean
gst_gl_download_get_unit_size (GstBaseTransform * trans, GstCaps * caps,
    gsize * size)
{
  gboolean ret;
  GstVideoInfo vinfo;

  ret = gst_video_info_from_caps (&vinfo, caps);
  if (ret)
    *size = GST_VIDEO_INFO_SIZE (&vinfo);

  return ret;
}

static GstFlowReturn
gst_gl_download_transform (GstBaseTransform * trans, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstGLDownload *download;
  GstVideoMeta *smeta, *dmeta;
  GstGLMeta *gl_meta;
  GstVideoFrame frame;

  download = GST_GL_DOWNLOAD (trans);
  smeta = gst_buffer_get_video_meta (inbuf);
  gl_meta = gst_buffer_get_gl_meta (inbuf);
  dmeta = gst_buffer_get_video_meta (outbuf);

  if (!smeta || !gl_meta) {
    GST_ERROR ("Input buffer does not have required GstVideoMeta or GstGLMeta");
    goto error;
  }
  if (!dmeta) {
    GST_ERROR ("Output buffer does not have required GstVideoMeta");
    goto error;
  }

  if (!gst_video_frame_map (&frame, &download->out_info, outbuf, GST_MAP_WRITE)) {
    GST_WARNING ("Could not map data for writing");
    goto error;
  }

  if (!gst_gl_display_do_download (download->display, gl_meta->memory->tex_id,
          &frame)) {
    GST_WARNING ("Failed to download data");
  }

  gst_video_frame_unmap (&frame);

  return GST_FLOW_OK;

/* ERRORS */
error:
  {
    return GST_FLOW_ERROR;
  }
}

static gboolean
gst_gl_download_decide_allocation (GstBaseTransform * trans, GstQuery * query)
{
  GstBufferPool *pool = NULL;
  GstStructure *config;
  GstCaps *caps;
  guint min, max, size;
  gboolean update_pool;

  gst_query_parse_allocation (query, &caps, NULL);

  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);

    update_pool = TRUE;
  } else {
    GstVideoInfo vinfo;

    gst_video_info_init (&vinfo);
    gst_video_info_from_caps (&vinfo, caps);
    size = vinfo.size;
    min = max = 0;
    update_pool = FALSE;
  }

  if (!pool)
    pool = gst_video_buffer_pool_new ();

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, caps, size, min, max);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  gst_buffer_pool_set_config (pool, config);

  if (update_pool)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool (query, pool, size, min, max);

  gst_object_unref (pool);

  return TRUE;
}

static gboolean
gst_gl_download_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_query, GstQuery * query)
{
  GstGLDownload *download = GST_GL_DOWNLOAD (trans);
  GstBufferPool *pool;
  GstStructure *config;
  GstCaps *caps;
  guint size;
  gboolean need_pool;

  gst_query_parse_allocation (query, &caps, &need_pool);

  if (caps == NULL)
    goto no_caps;

  if ((pool = download->pool))
    gst_object_ref (pool);

  if (pool != NULL) {
    GstCaps *pcaps;

    /* we had a pool, check caps */
    GST_DEBUG_OBJECT (download, "check existing pool caps");
    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_get_params (config, &pcaps, &size, NULL, NULL);

    if (!gst_caps_is_equal (caps, pcaps)) {
      GST_DEBUG_OBJECT (download, "pool has different caps");
      /* different caps, we can't use this pool */
      gst_object_unref (pool);
      pool = NULL;
    }
    gst_structure_free (config);
  }
  if (pool == NULL && need_pool) {
    GstVideoInfo info;

    if (!gst_video_info_from_caps (&info, caps))
      goto invalid_caps;

    GST_DEBUG_OBJECT (download, "create new pool");
    pool = gst_gl_buffer_pool_new (download->display);

    /* the normal size of a frame */
    size = info.size;

    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_set_params (config, caps, size, 0, 0);
    if (!gst_buffer_pool_set_config (pool, config))
      goto config_failed;
  }
  /* we need at least 2 buffer because we hold on to the last one */
  gst_query_add_allocation_pool (query, pool, size, 2, 0);

  /* we also support various metadata */
  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, 0);
  gst_query_add_allocation_meta (query, GST_GL_META_API_TYPE, 0);

  gst_object_unref (pool);

  return TRUE;

  /* ERRORS */
no_caps:
  {
    GST_DEBUG_OBJECT (trans, "no caps specified");
    return FALSE;
  }
invalid_caps:
  {
    GST_DEBUG_OBJECT (trans, "invalid caps specified");
    return FALSE;
  }
config_failed:
  {
    GST_DEBUG_OBJECT (trans, "failed setting config");
    return FALSE;
  }
}
