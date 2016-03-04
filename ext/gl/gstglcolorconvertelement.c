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
        "glconvertelement", 0, "convert");
    );

static gboolean gst_gl_color_convert_element_set_caps (GstBaseTransform * bt,
    GstCaps * in_caps, GstCaps * out_caps);
static GstCaps *gst_gl_color_convert_element_transform_caps (GstBaseTransform *
    bt, GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static gboolean gst_gl_color_convert_element_get_unit_size (GstBaseTransform *
    trans, GstCaps * caps, gsize * size);
static gboolean
gst_gl_color_convert_element_filter_meta (GstBaseTransform * trans,
    GstQuery * query, GType api, const GstStructure * params);
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
    GST_STATIC_CAPS (GST_GL_COLOR_CONVERT_VIDEO_CAPS));

static GstStaticPadTemplate gst_gl_color_convert_element_sink_pad_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_GL_COLOR_CONVERT_VIDEO_CAPS));

static gboolean
gst_gl_color_convert_element_stop (GstBaseTransform * bt)
{
  GstGLColorConvertElement *convert = GST_GL_COLOR_CONVERT_ELEMENT (bt);

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

  bt_class->transform_caps = gst_gl_color_convert_element_transform_caps;
  bt_class->set_caps = gst_gl_color_convert_element_set_caps;
  bt_class->get_unit_size = gst_gl_color_convert_element_get_unit_size;
  bt_class->filter_meta = gst_gl_color_convert_element_filter_meta;
  bt_class->decide_allocation = gst_gl_color_convert_element_decide_allocation;
  bt_class->prepare_output_buffer =
      gst_gl_color_convert_element_prepare_output_buffer;
  bt_class->transform = gst_gl_color_convert_element_transform;
  bt_class->stop = gst_gl_color_convert_element_stop;
  bt_class->fixate_caps = gst_gl_color_convert_element_fixate_caps;

  bt_class->passthrough_on_same_caps = TRUE;

  gst_element_class_add_static_pad_template (element_class,
      &gst_gl_color_convert_element_src_pad_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_gl_color_convert_element_sink_pad_template);

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

  if (convert->convert)
    gst_gl_color_convert_set_caps (convert->convert, in_caps, out_caps);

  return TRUE;
}

static GstCaps *
gst_gl_color_convert_element_transform_caps (GstBaseTransform * bt,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstGLContext *context = GST_GL_BASE_FILTER (bt)->context;

  return gst_gl_color_convert_transform_caps (context, direction, caps, filter);
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
gst_gl_color_convert_element_filter_meta (GstBaseTransform * trans,
    GstQuery * query, GType api, const GstStructure * params)
{
  /* propose all metadata upstream */
  return TRUE;
}

static gboolean
gst_gl_color_convert_element_decide_allocation (GstBaseTransform * trans,
    GstQuery * query)
{
  GstGLColorConvertElement *convert = GST_GL_COLOR_CONVERT_ELEMENT (trans);
  GstGLContext *context;

  /* get gl context */
  if (!GST_BASE_TRANSFORM_CLASS
      (gst_gl_color_convert_element_parent_class)->decide_allocation (trans,
          query))
    return FALSE;

  context = GST_GL_BASE_FILTER (trans)->context;

  if (!convert->convert)
    convert->convert = gst_gl_color_convert_new (context);

  if (!gst_gl_color_convert_set_caps (convert->convert, convert->in_caps,
          convert->out_caps))
    return FALSE;

  if (!gst_gl_color_convert_decide_allocation (convert->convert, query))
    return FALSE;

  return TRUE;
}

static GstFlowReturn
gst_gl_color_convert_element_prepare_output_buffer (GstBaseTransform * bt,
    GstBuffer * inbuf, GstBuffer ** outbuf)
{
  GstGLColorConvertElement *convert = GST_GL_COLOR_CONVERT_ELEMENT (bt);
  GstBaseTransformClass *bclass;

  bclass = GST_BASE_TRANSFORM_GET_CLASS (bt);

  if (gst_base_transform_is_passthrough (bt)) {
    *outbuf = inbuf;
    return GST_FLOW_OK;
  }

  if (!convert->convert)
    return GST_FLOW_NOT_NEGOTIATED;

  *outbuf = gst_gl_color_convert_perform (convert->convert, inbuf);
  if (!*outbuf) {
    GST_ELEMENT_ERROR (bt, RESOURCE, NOT_FOUND,
        ("%s", "Failed to convert video buffer"), (NULL));
    return GST_FLOW_ERROR;
  }

  /* basetransform doesn't unref if they're the same */
  if (inbuf == *outbuf)
    gst_buffer_unref (*outbuf);
  else
    bclass->copy_metadata (bt, inbuf, *outbuf);

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
  GstGLContext *context = GST_GL_BASE_FILTER (bt)->context;

  return gst_gl_color_convert_fixate_caps (context, direction, caps, othercaps);
}
