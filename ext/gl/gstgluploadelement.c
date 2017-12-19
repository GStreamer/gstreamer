/*
 * GStreamer
 * Copyright (C) 2015 Matthew Waters <matthew@centricular.com>
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

#include <gst/gl/gl.h>
#include "gstgluploadelement.h"

GST_DEBUG_CATEGORY_STATIC (gst_gl_upload_element_debug);
#define GST_CAT_DEFAULT gst_gl_upload_element_debug

#define gst_gl_upload_element_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstGLUploadElement, gst_gl_upload_element,
    GST_TYPE_GL_BASE_FILTER,
    GST_DEBUG_CATEGORY_INIT (gst_gl_upload_element_debug, "gluploadelement", 0,
        "glupload Element"););

static gboolean gst_gl_upload_element_get_unit_size (GstBaseTransform * trans,
    GstCaps * caps, gsize * size);
static GstCaps *_gst_gl_upload_element_transform_caps (GstBaseTransform * bt,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static gboolean _gst_gl_upload_element_set_caps (GstBaseTransform * bt,
    GstCaps * in_caps, GstCaps * out_caps);
static gboolean gst_gl_upload_element_filter_meta (GstBaseTransform * trans,
    GstQuery * query, GType api, const GstStructure * params);
static gboolean _gst_gl_upload_element_propose_allocation (GstBaseTransform *
    bt, GstQuery * decide_query, GstQuery * query);
static gboolean _gst_gl_upload_element_decide_allocation (GstBaseTransform *
    trans, GstQuery * query);
static GstFlowReturn
gst_gl_upload_element_prepare_output_buffer (GstBaseTransform * bt,
    GstBuffer * buffer, GstBuffer ** outbuf);
static GstFlowReturn gst_gl_upload_element_transform (GstBaseTransform * bt,
    GstBuffer * buffer, GstBuffer * outbuf);
static gboolean gst_gl_upload_element_stop (GstBaseTransform * bt);

static GstStaticPadTemplate gst_gl_upload_element_src_pad_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw(ANY)"));

static void
gst_gl_upload_element_finalize (GObject * object)
{
  GstGLUploadElement *upload = GST_GL_UPLOAD_ELEMENT (object);

  if (upload->upload)
    gst_object_unref (upload->upload);
  upload->upload = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_gl_upload_element_class_init (GstGLUploadElementClass * klass)
{
  GstBaseTransformClass *bt_class = GST_BASE_TRANSFORM_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstCaps *upload_caps;

  bt_class->transform_caps = _gst_gl_upload_element_transform_caps;
  bt_class->set_caps = _gst_gl_upload_element_set_caps;
  bt_class->filter_meta = gst_gl_upload_element_filter_meta;
  bt_class->propose_allocation = _gst_gl_upload_element_propose_allocation;
  bt_class->decide_allocation = _gst_gl_upload_element_decide_allocation;
  bt_class->get_unit_size = gst_gl_upload_element_get_unit_size;
  bt_class->prepare_output_buffer = gst_gl_upload_element_prepare_output_buffer;
  bt_class->transform = gst_gl_upload_element_transform;
  bt_class->stop = gst_gl_upload_element_stop;

  bt_class->passthrough_on_same_caps = TRUE;

  gst_element_class_add_static_pad_template (element_class,
      &gst_gl_upload_element_src_pad_template);

  upload_caps = gst_gl_upload_get_input_template_caps ();
  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS, upload_caps));
  gst_caps_unref (upload_caps);

  gst_element_class_set_metadata (element_class,
      "OpenGL uploader", "Filter/Video",
      "Uploads data into OpenGL", "Matthew Waters <matthew@centricular.com>");

  gobject_class->finalize = gst_gl_upload_element_finalize;
}

static void
gst_gl_upload_element_init (GstGLUploadElement * upload)
{
  gst_base_transform_set_prefer_passthrough (GST_BASE_TRANSFORM (upload), TRUE);
}

static gboolean
gst_gl_upload_element_stop (GstBaseTransform * bt)
{
  GstGLUploadElement *upload = GST_GL_UPLOAD_ELEMENT (bt);

  if (upload->upload) {
    gst_object_unref (upload->upload);
    upload->upload = NULL;
  }

  gst_caps_replace (&upload->in_caps, NULL);
  gst_caps_replace (&upload->out_caps, NULL);

  return GST_BASE_TRANSFORM_CLASS (parent_class)->stop (bt);
}

static gboolean
gst_gl_upload_element_get_unit_size (GstBaseTransform * trans, GstCaps * caps,
    gsize * size)
{
  gboolean ret = FALSE;
  GstVideoInfo info;

  ret = gst_video_info_from_caps (&info, caps);
  if (ret)
    *size = GST_VIDEO_INFO_SIZE (&info);

  return TRUE;
}

static GstCaps *
_gst_gl_upload_element_transform_caps (GstBaseTransform * bt,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstGLUploadElement *upload = GST_GL_UPLOAD_ELEMENT (bt);
  GstGLContext *context = GST_GL_BASE_FILTER (bt)->context;

  if (upload->upload == NULL)
    upload->upload = gst_gl_upload_new (NULL);

  return gst_gl_upload_transform_caps (upload->upload, context, direction, caps,
      filter);
}

static gboolean
gst_gl_upload_element_filter_meta (GstBaseTransform * trans, GstQuery * query,
    GType api, const GstStructure * params)
{
  /* propose all metadata upstream */
  return TRUE;
}

static gboolean
_gst_gl_upload_element_propose_allocation (GstBaseTransform * bt,
    GstQuery * decide_query, GstQuery * query)
{
  GstGLUploadElement *upload = GST_GL_UPLOAD_ELEMENT (bt);
  gboolean ret;

  if (!upload->upload)
    return FALSE;

  ret = GST_BASE_TRANSFORM_CLASS (parent_class)->propose_allocation (bt,
      decide_query, query);
  gst_gl_upload_propose_allocation (upload->upload, decide_query, query);

  return ret;
}

static gboolean
_gst_gl_upload_element_decide_allocation (GstBaseTransform * trans,
    GstQuery * query)
{
  GstGLUploadElement *upload = GST_GL_UPLOAD_ELEMENT (trans);
  GstGLContext *context;
  gboolean ret;

  ret =
      GST_BASE_TRANSFORM_CLASS
      (gst_gl_upload_element_parent_class)->decide_allocation (trans, query);
  if (!ret)
    return FALSE;

  /* GstGLBaseFilter populates ->context in ::decide_allocation so now it's the
   * time to set the ->upload context */
  context = GST_GL_BASE_FILTER (trans)->context;
  gst_gl_upload_set_context (upload->upload, context);

  return gst_gl_upload_set_caps (upload->upload, upload->in_caps,
      upload->out_caps);
}

static gboolean
_gst_gl_upload_element_set_caps (GstBaseTransform * bt, GstCaps * in_caps,
    GstCaps * out_caps)
{
  GstGLUploadElement *upload = GST_GL_UPLOAD_ELEMENT (bt);

  gst_caps_replace (&upload->in_caps, in_caps);
  gst_caps_replace (&upload->out_caps, out_caps);

  if (upload->upload)
    return gst_gl_upload_set_caps (upload->upload, in_caps, out_caps);

  return TRUE;
}

GstFlowReturn
gst_gl_upload_element_prepare_output_buffer (GstBaseTransform * bt,
    GstBuffer * buffer, GstBuffer ** outbuf)
{
  GstGLUploadElement *upload = GST_GL_UPLOAD_ELEMENT (bt);
  GstGLUploadReturn ret;
  GstBaseTransformClass *bclass;

  bclass = GST_BASE_TRANSFORM_GET_CLASS (bt);

  if (gst_base_transform_is_passthrough (bt)) {
    *outbuf = buffer;
    return GST_FLOW_OK;
  }

  if (!upload->upload)
    return GST_FLOW_NOT_NEGOTIATED;

  ret = gst_gl_upload_perform_with_buffer (upload->upload, buffer, outbuf);
  if (ret == GST_GL_UPLOAD_RECONFIGURE) {
    gst_base_transform_reconfigure_src (bt);
    return GST_FLOW_OK;
  }

  if (ret != GST_GL_UPLOAD_DONE || *outbuf == NULL) {
    GST_ELEMENT_ERROR (bt, RESOURCE, NOT_FOUND, ("%s",
            "Failed to upload buffer"), (NULL));
    if (*outbuf)
      gst_buffer_unref (*outbuf);
    return GST_FLOW_ERROR;
  }

  /* basetransform doesn't unref if they're the same */
  if (buffer == *outbuf)
    gst_buffer_unref (*outbuf);
  else
    bclass->copy_metadata (bt, buffer, *outbuf);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_gl_upload_element_transform (GstBaseTransform * bt, GstBuffer * buffer,
    GstBuffer * outbuf)
{
  return GST_FLOW_OK;
}
