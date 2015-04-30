/*
 * GStreamer
 * Copyright (C) 2012-2014 Matthew Waters <ystree00@gmail.com>
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

#include <gst/gl/gl.h>
#include "gstglcolorconvertelement.h"

GST_DEBUG_CATEGORY_STATIC (gst_gl_color_convert_element_debug);
#define GST_CAT_DEFAULT gst_gl_color_convert_element_debug

G_DEFINE_TYPE_WITH_CODE (GstGLColorConvertElement, gst_gl_color_convert_element,
    GST_TYPE_GL_BASE_FILTER,
    GST_DEBUG_CATEGORY_INIT (gst_gl_color_convert_element_debug,
        "glconvertelement", 0, "convert"););

static gboolean gst_gl_color_convert_element_set_caps (GstBaseTransform * bt,
    GstCaps * in_caps, GstCaps * out_caps);
static GstCaps *gst_gl_color_convert_element_transform_caps (GstBaseTransform *
    bt, GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static gboolean gst_gl_color_convert_element_get_unit_size (GstBaseTransform *
    trans, GstCaps * caps, gsize * size);
static gboolean gst_gl_color_convert_element_decide_allocation (GstBaseTransform
    * trans, GstQuery * query);
static GstFlowReturn
gst_gl_color_convert_element_prepare_output_buffer (GstBaseTransform * bt,
    GstBuffer * inbuf, GstBuffer ** outbuf);
static GstFlowReturn gst_gl_color_convert_element_transform (GstBaseTransform *
    bt, GstBuffer * inbuf, GstBuffer * outbuf);
static GstCaps *gst_gl_color_convert_element_fixate_caps (GstBaseTransform *
    bt, GstPadDirection direction, GstCaps * caps, GstCaps * othercaps);

static GstStaticPadTemplate gst_gl_color_convert_element_src_pad_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_GL_MEMORY,
            GST_GL_COLOR_CONVERT_FORMATS) ";"
        GST_VIDEO_CAPS_MAKE (GST_GL_COLOR_CONVERT_FORMATS)));

static gboolean
gst_gl_color_convert_element_stop (GstBaseTransform * bt)
{
  GstGLColorConvertElement *convert = GST_GL_COLOR_CONVERT_ELEMENT (bt);

  if (convert->upload) {
    gst_object_unref (convert->upload);
    convert->upload = NULL;
  }

  if (convert->convert) {
    gst_object_unref (convert->convert);
    convert->convert = NULL;
  }

  gst_caps_replace (&convert->in_caps, NULL);
  gst_caps_replace (&convert->out_caps, NULL);

  return
      GST_BASE_TRANSFORM_CLASS (gst_gl_color_convert_element_parent_class)->stop
      (bt);
}

static void
gst_gl_color_convert_element_class_init (GstGLColorConvertElementClass * klass)
{
  GstBaseTransformClass *bt_class = GST_BASE_TRANSFORM_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstCaps *upload_caps;

  bt_class->transform_caps = gst_gl_color_convert_element_transform_caps;
  bt_class->set_caps = gst_gl_color_convert_element_set_caps;
  bt_class->get_unit_size = gst_gl_color_convert_element_get_unit_size;
  bt_class->decide_allocation = gst_gl_color_convert_element_decide_allocation;
  bt_class->prepare_output_buffer =
      gst_gl_color_convert_element_prepare_output_buffer;
  bt_class->transform = gst_gl_color_convert_element_transform;
  bt_class->stop = gst_gl_color_convert_element_stop;
  bt_class->fixate_caps = gst_gl_color_convert_element_fixate_caps;

  bt_class->passthrough_on_same_caps = TRUE;

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get
      (&gst_gl_color_convert_element_src_pad_template));

  upload_caps = gst_gl_upload_get_input_template_caps ();
  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS, upload_caps));
  gst_caps_unref (upload_caps);

  gst_element_class_set_metadata (element_class,
      "OpenGL color converter", "Filter/Converter/Video",
      "Converts between color spaces using OpenGL shaders",
      "Matthew Waters <matthew@centricular.com>");
}

static void
gst_gl_color_convert_element_init (GstGLColorConvertElement * convert)
{
  gst_base_transform_set_prefer_passthrough (GST_BASE_TRANSFORM (convert),
      TRUE);
}

static gboolean
gst_gl_color_convert_element_set_caps (GstBaseTransform * bt,
    GstCaps * in_caps, GstCaps * out_caps)
{
  GstGLColorConvertElement *convert = GST_GL_COLOR_CONVERT_ELEMENT (bt);

  gst_caps_replace (&convert->in_caps, in_caps);
  gst_caps_replace (&convert->out_caps, out_caps);

  return TRUE;
}

static GstCaps *
gst_gl_color_convert_element_transform_caps (GstBaseTransform * bt,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstGLContext *context = GST_GL_BASE_FILTER (bt)->context;
  GstCaps *tmp, *ret;

  if (direction == GST_PAD_SINK) {
    ret = gst_gl_upload_transform_caps (context, direction, caps, NULL);
  } else {
    tmp =
        gst_gl_caps_replace_all_caps_features (caps,
        GST_CAPS_FEATURE_MEMORY_GL_MEMORY);
    ret = gst_caps_merge (gst_caps_ref (caps), tmp);
  }

  GST_DEBUG_OBJECT (bt, "transfer returned %" GST_PTR_FORMAT, ret);

  tmp = gst_gl_color_convert_transform_caps (context, direction, ret, NULL);
  gst_caps_unref (ret);
  ret = tmp;
  GST_DEBUG_OBJECT (bt, "convert returned %" GST_PTR_FORMAT, ret);

  if (direction == GST_PAD_SRC) {
    tmp = gst_gl_upload_transform_caps (context, direction, ret, NULL);
    gst_caps_unref (ret);
    ret = tmp;
  } else {
    tmp =
        gst_gl_caps_replace_all_caps_features (ret,
        GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY);
    ret = gst_caps_merge (ret, tmp);
  }
  GST_DEBUG_OBJECT (bt, "transfer returned %" GST_PTR_FORMAT, ret);

  if (filter) {
    tmp = gst_caps_intersect_full (filter, ret, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (ret);
    ret = tmp;
  }

  return ret;
}

static gboolean
gst_gl_color_convert_element_get_unit_size (GstBaseTransform * trans,
    GstCaps * caps, gsize * size)
{
  gboolean ret = FALSE;
  GstVideoInfo info;

  ret = gst_video_info_from_caps (&info, caps);
  if (ret)
    *size = GST_VIDEO_INFO_SIZE (&info);

  return TRUE;
}

static gboolean
gst_gl_color_convert_element_decide_allocation (GstBaseTransform * trans,
    GstQuery * query)
{
  GstGLColorConvertElement *convert = GST_GL_COLOR_CONVERT_ELEMENT (trans);
  GstGLContext *context;
  GstCaps *converted_caps;

  /* get gl context */
  if (!GST_BASE_TRANSFORM_CLASS
      (gst_gl_color_convert_element_parent_class)->decide_allocation (trans,
          query))
    return FALSE;

  context = GST_GL_BASE_FILTER (trans)->context;

  if (!convert->upload)
    convert->upload = gst_gl_upload_new (context);

  if (convert->upload_caps)
    gst_caps_unref (convert->upload_caps);
  convert->upload_caps = gst_caps_copy (convert->in_caps);
  gst_caps_set_features (convert->upload_caps, 0,
      gst_caps_features_from_string (GST_CAPS_FEATURE_MEMORY_GL_MEMORY));

  if (!gst_gl_upload_set_caps (convert->upload, convert->in_caps,
          convert->upload_caps)) {
    GST_ERROR_OBJECT (trans, "failed to set caps for upload");
    return FALSE;
  }

  if (!convert->convert)
    convert->convert = gst_gl_color_convert_new (context);

  converted_caps = gst_caps_copy (convert->out_caps);
  gst_caps_set_features (converted_caps, 0,
      gst_caps_features_from_string (GST_CAPS_FEATURE_MEMORY_GL_MEMORY));

  if (!gst_gl_color_convert_set_caps (convert->convert, convert->upload_caps,
          converted_caps)) {
    gst_caps_unref (converted_caps);
    GST_ERROR_OBJECT (trans, "failed to set caps for conversion");
    return FALSE;
  }
  gst_caps_unref (converted_caps);

  return TRUE;
}

static GstFlowReturn
gst_gl_color_convert_element_prepare_output_buffer (GstBaseTransform * bt,
    GstBuffer * inbuf, GstBuffer ** outbuf)
{
  GstGLColorConvertElement *convert = GST_GL_COLOR_CONVERT_ELEMENT (bt);
  GstBuffer *uploaded_buffer;

  if (gst_base_transform_is_passthrough (bt)) {
    *outbuf = inbuf;
    return GST_FLOW_OK;
  }

  if (!convert->upload || !convert->convert)
    return GST_FLOW_NOT_NEGOTIATED;

  if (GST_GL_UPLOAD_DONE != gst_gl_upload_perform_with_buffer (convert->upload,
          inbuf, &uploaded_buffer) || !uploaded_buffer) {
    GST_ELEMENT_ERROR (bt, RESOURCE, NOT_FOUND, ("%s",
            "failed to upload buffer"), (NULL));
    return GST_FLOW_ERROR;
  }

  *outbuf = gst_gl_color_convert_perform (convert->convert, uploaded_buffer);
  gst_buffer_unref (uploaded_buffer);
  if (!*outbuf) {
    GST_ELEMENT_ERROR (bt, RESOURCE, NOT_FOUND,
        ("%s", "failed to convert buffer"), (NULL));
    return GST_FLOW_ERROR;
  }

  /* basetransform doesn't unref if they're the same */
  if (inbuf == *outbuf)
    gst_buffer_unref (*outbuf);
  else if (*outbuf)
    gst_buffer_copy_into (*outbuf, inbuf,
        GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS, 0, -1);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_gl_color_convert_element_transform (GstBaseTransform * bt,
    GstBuffer * inbuf, GstBuffer * outbuf)
{
  return GST_FLOW_OK;
}

static GstCaps *
gst_gl_color_convert_element_fixate_caps (GstBaseTransform *
    bt, GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
{
  GstCaps *ret;

  ret =
      GST_BASE_TRANSFORM_CLASS
      (gst_gl_color_convert_element_parent_class)->fixate_caps (bt, direction,
      caps, othercaps);

  if (direction == GST_PAD_SINK) {
    if (gst_caps_is_subset (caps, ret)) {
      gst_caps_replace (&ret, caps);
    }
  }

  return ret;
}
