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
 * SECTION:element-glupload
 *
 * upload video frames video frames into opengl textures.
 *
 * <refsect2>
 * <title>Color space conversion</title>
 * <para>
 * Depends on the driver and when needed, the color space conversion is made
 * in a fragment shader using one frame buffer object instance, or using
 * mesa ycbcr .
 * </para>
 * </refsect2>
 * <refsect2>
 * <title>Examples</title>
 * |[
 * gst-launch -v videotestsrc ! "video/x-raw-rgb" ! glupload ! glimagesink
 * ]| A pipeline to test hardware scaling.
 * No special opengl extension is used in this pipeline, that's why it should work
 * with OpenGL >= 1.1. That's the case if you are using the MESA3D driver v1.3.
 * |[
 * gst-launch -v videotestsrc ! "video/x-raw-yuv, format=(fourcc)I420" ! glupload ! glimagesink
 * ]| A pipeline to test hardware scaling and hardware colorspace conversion.
 * When your driver supports GLSL (OpenGL Shading Language needs OpenGL >= 2.1),
 * the 4 following format YUY2, UYVY, I420, YV12 and AYUV are converted to RGB32
 * through some fragment shaders and using one framebuffer (FBO extension OpenGL >= 1.4).
 * If your driver does not support GLSL but supports MESA_YCbCr extension then
 * the you can use YUY2 and UYVY. In this case the colorspace conversion is automatically
 * made when loading the texture and therefore no framebuffer is used.
 * |[
 * gst-launch -v videotestsrc ! "video/x-raw-rgb, width=320, height=240" ! glupload ! \
 *    "video/x-raw-gl, width=640, height=480" ! glimagesink
 * ]| A pipeline to test hardware scaling.
 * Frame buffer extension is required. Inded one FBO is used bettween glupload and glimagesink,
 * because the texture needs to be resized.
 * |[
 * gst-launch -v videotestsrc ! "video/x-raw-yuv, width=320, height=240" ! glupload ! \
 *    "video/x-raw-gl, width=640, height=480" ! glimagesink
 * ]| A pipeline to test hardware scaling.
 * Frame buffer extension is required. Inded one FBO is used bettween glupload and glimagesink,
 * because the texture needs to be resized. Depends on your driver the color space conversion
 * is made in a fragment shader using one frame buffer object instance, or using mesa ycbcr .
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstglupload.h"


#define GST_CAT_DEFAULT gst_gl_upload_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

/* Source pad definition */
static GstStaticPadTemplate gst_gl_upload_src_pad_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_GL_VIDEO_CAPS)
    );

/* Source pad definition */
#ifndef OPENGL_ES2
static GstStaticPadTemplate gst_gl_upload_sink_pad_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_RGB ";"
        GST_VIDEO_CAPS_RGBx ";"
        GST_VIDEO_CAPS_RGBA ";"
        GST_VIDEO_CAPS_BGR ";"
        GST_VIDEO_CAPS_BGRx ";"
        GST_VIDEO_CAPS_BGRA ";"
        GST_VIDEO_CAPS_xRGB ";"
        GST_VIDEO_CAPS_xBGR ";"
        GST_VIDEO_CAPS_ARGB ";"
        GST_VIDEO_CAPS_ABGR ";"
        GST_VIDEO_CAPS_YUV ("{ I420, YV12, YUY2, UYVY, AYUV }"))
    );
#else
static GstStaticPadTemplate gst_gl_upload_sink_pad_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_RGB ";"
        GST_VIDEO_CAPS_RGBx ";"
        GST_VIDEO_CAPS_RGBA ";"
        GST_VIDEO_CAPS_YUV ("{ I420, YV12, YUY2, UYVY, AYUV }"))
    );
#endif

/* Properties */
enum
{
  PROP_0,
  PROP_EXTERNAL_OPENGL_CONTEXT
};

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_gl_upload_debug, "glupload", 0, "glupload element");

GST_BOILERPLATE_FULL (GstGLUpload, gst_gl_upload, GstBaseTransform,
    GST_TYPE_BASE_TRANSFORM, DEBUG_INIT);

static void gst_gl_upload_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gl_upload_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_gl_upload_src_query (GstPad *pad, GstQuery * query);

static void gst_gl_upload_reset (GstGLUpload * upload);
static gboolean gst_gl_upload_set_caps (GstBaseTransform * bt,
    GstCaps * incaps, GstCaps * outcaps);
static GstCaps *gst_gl_upload_transform_caps (GstBaseTransform * bt,
    GstPadDirection direction, GstCaps * caps);
static void gst_gl_upload_fixate_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps);
static gboolean gst_gl_upload_start (GstBaseTransform * bt);
static gboolean gst_gl_upload_stop (GstBaseTransform * bt);
static GstFlowReturn gst_gl_upload_prepare_output_buffer (GstBaseTransform *
    trans, GstBuffer * input, gint size, GstCaps * caps, GstBuffer ** buf);
static GstFlowReturn gst_gl_upload_transform (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf);
static gboolean gst_gl_upload_get_unit_size (GstBaseTransform * trans,
    GstCaps * caps, guint * size);


static void
gst_gl_upload_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_details_simple (element_class, "OpenGL upload",
      "Filter/Effect", "A from video to GL flow filter",
      "Julien Isorce <julien.isorce@gmail.com>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_gl_upload_src_pad_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_gl_upload_sink_pad_template));
}

static void
gst_gl_upload_class_init (GstGLUploadClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;
  gobject_class->set_property = gst_gl_upload_set_property;
  gobject_class->get_property = gst_gl_upload_get_property;

  GST_BASE_TRANSFORM_CLASS (klass)->transform_caps =
      gst_gl_upload_transform_caps;
  GST_BASE_TRANSFORM_CLASS (klass)->fixate_caps = gst_gl_upload_fixate_caps;
  GST_BASE_TRANSFORM_CLASS (klass)->transform = gst_gl_upload_transform;
  GST_BASE_TRANSFORM_CLASS (klass)->start = gst_gl_upload_start;
  GST_BASE_TRANSFORM_CLASS (klass)->stop = gst_gl_upload_stop;
  GST_BASE_TRANSFORM_CLASS (klass)->set_caps = gst_gl_upload_set_caps;
  GST_BASE_TRANSFORM_CLASS (klass)->get_unit_size = gst_gl_upload_get_unit_size;
  GST_BASE_TRANSFORM_CLASS (klass)->prepare_output_buffer =
      gst_gl_upload_prepare_output_buffer;

  g_object_class_install_property (gobject_class, PROP_EXTERNAL_OPENGL_CONTEXT,
      g_param_spec_ulong ("external_opengl_context",
          "External OpenGL context",
          "Give an external OpenGL context with which to share textures",
          0, G_MAXULONG, 0, G_PARAM_WRITABLE));
}

static void
gst_gl_upload_init (GstGLUpload * upload, GstGLUploadClass * klass)
{
  GstBaseTransform *base_trans = GST_BASE_TRANSFORM (upload);
  
  gst_pad_set_query_function (base_trans->srcpad,
      GST_DEBUG_FUNCPTR (gst_gl_upload_src_query));

  gst_gl_upload_reset (upload);
}

static void
gst_gl_upload_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGLUpload *upload = GST_GL_UPLOAD (object);

  switch (prop_id) {
    case PROP_EXTERNAL_OPENGL_CONTEXT:
    {
      upload->external_gl_context = g_value_get_ulong (value);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gl_upload_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  //GstGLUpload *upload = GST_GL_UPLOAD (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_gl_upload_src_query (GstPad * pad, GstQuery * query)
{
  gboolean res = FALSE;
  GstElement *parent = GST_ELEMENT (gst_pad_get_parent (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CUSTOM:
    {
      GstStructure *structure = gst_query_get_structure (query);
      res = g_strcmp0 (gst_element_get_name (parent), gst_structure_get_name (structure)) == 0;
      if (!res)
        res = gst_pad_query_default (pad, query);
      break;
    }
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }

  return res;
}

static void
gst_gl_upload_reset (GstGLUpload * upload)
{
  if (upload->display) {
    g_object_unref (upload->display);
    upload->display = NULL;
  }
  upload->external_gl_context = 0;
}

static gboolean
gst_gl_upload_start (GstBaseTransform * bt)
{
  GstGLUpload* upload = GST_GL_UPLOAD (bt);
  GstElement *parent = GST_ELEMENT (gst_element_get_parent (upload));
  GstStructure *structure = gst_structure_new (gst_element_get_name (upload), NULL);
  GstQuery *query = gst_query_new_application (GST_QUERY_CUSTOM, structure);

  gboolean isPerformed = gst_element_query (parent, query);

  if (isPerformed) {
    const GValue *id_value = gst_structure_get_value (structure, "gstgldisplay");
    if (G_VALUE_HOLDS_POINTER (id_value))
      /* at least one gl element is after in our gl chain */
      upload->display = g_object_ref (GST_GL_DISPLAY (g_value_get_pointer (id_value)));
    else {
      /* this gl filter is a sink in terms of the gl chain */
      upload->display = gst_gl_display_new ();
      gst_gl_display_create_context (upload->display, upload->external_gl_context);
    }
  }

  gst_query_unref (query);
  gst_object_unref (GST_OBJECT (parent));

  return isPerformed;
}

static gboolean
gst_gl_upload_stop (GstBaseTransform * bt)
{
  GstGLUpload *upload = GST_GL_UPLOAD (bt);

  gst_gl_upload_reset (upload);

  return TRUE;
}

static GstCaps *
gst_gl_upload_transform_caps (GstBaseTransform * bt,
    GstPadDirection direction, GstCaps * caps)
{
  //GstGLUpload* upload = GST_GL_UPLOAD (bt);
  GstStructure *structure = gst_caps_get_structure (caps, 0);
  GstCaps *newcaps = NULL;
  const GValue *framerate_value = NULL;
  const GValue *par_value = NULL;

  GST_DEBUG ("transform caps %" GST_PTR_FORMAT, caps);

  framerate_value = gst_structure_get_value (structure, "framerate");
  par_value = gst_structure_get_value (structure, "pixel-aspect-ratio");

  if (direction == GST_PAD_SRC) {
    GstCaps *newothercaps = gst_caps_new_simple ("video/x-raw-rgb", NULL);
    newcaps = gst_caps_new_simple ("video/x-raw-yuv", NULL);
    gst_caps_append (newcaps, newothercaps);
  } else
    newcaps = gst_caps_new_simple ("video/x-raw-gl", NULL);

  structure = gst_caps_get_structure (newcaps, 0);

  gst_structure_set (structure,
      "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
      "height", GST_TYPE_INT_RANGE, 1, G_MAXINT, NULL);

  gst_structure_set_value (structure, "framerate", framerate_value);
  if (par_value)
    gst_structure_set_value (structure, "pixel-aspect-ratio", par_value);
  else
    gst_structure_set (structure, "pixel-aspect-ratio", GST_TYPE_FRACTION,
        1, 1, NULL);

  gst_caps_merge_structure (newcaps, gst_structure_copy (structure));

  GST_DEBUG ("new caps %" GST_PTR_FORMAT, newcaps);

  return newcaps;
}

/* from gst-plugins-base "videoscale" code */
static void
gst_gl_upload_fixate_caps (GstBaseTransform * base, GstPadDirection direction,
    GstCaps * caps, GstCaps * othercaps)
{
  GstStructure *ins, *outs;

  const GValue *from_par, *to_par;

  g_return_if_fail (gst_caps_is_fixed (caps));

  GST_DEBUG_OBJECT (base, "trying to fixate othercaps %" GST_PTR_FORMAT
      " based on caps %" GST_PTR_FORMAT, othercaps, caps);

  ins = gst_caps_get_structure (caps, 0);
  outs = gst_caps_get_structure (othercaps, 0);

  from_par = gst_structure_get_value (ins, "pixel-aspect-ratio");
  to_par = gst_structure_get_value (outs, "pixel-aspect-ratio");

  //we have both PAR but they might not be fixated
  if (from_par && to_par) {
    gint from_w, from_h, from_par_n, from_par_d, to_par_n, to_par_d;

    gint count = 0, w = 0, h = 0;

    guint num, den;

    //from_par should be fixed
    g_return_if_fail (gst_value_is_fixed (from_par));

    from_par_n = gst_value_get_fraction_numerator (from_par);
    from_par_d = gst_value_get_fraction_denominator (from_par);

    //fixate the out PAR
    if (!gst_value_is_fixed (to_par)) {
      GST_DEBUG_OBJECT (base, "fixating to_par to %dx%d", from_par_n,
          from_par_d);
      gst_structure_fixate_field_nearest_fraction (outs, "pixel-aspect-ratio",
          from_par_n, from_par_d);
    }

    to_par_n = gst_value_get_fraction_numerator (to_par);
    to_par_d = gst_value_get_fraction_denominator (to_par);

    //f both width and height are already fixed, we can't do anything
    //about it anymore
    if (gst_structure_get_int (outs, "width", &w))
      ++count;
    if (gst_structure_get_int (outs, "height", &h))
      ++count;
    if (count == 2) {
      GST_DEBUG_OBJECT (base, "dimensions already set to %dx%d, not fixating",
          w, h);
      return;
    }

    gst_structure_get_int (ins, "width", &from_w);
    gst_structure_get_int (ins, "height", &from_h);

    if (!gst_video_calculate_display_ratio (&num, &den, from_w, from_h,
            from_par_n, from_par_d, to_par_n, to_par_d)) {
      GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (NULL),
          ("Error calculating the output scaled size - integer overflow"));
      return;
    }

    GST_DEBUG_OBJECT (base,
        "scaling input with %dx%d and PAR %d/%d to output PAR %d/%d",
        from_w, from_h, from_par_n, from_par_d, to_par_n, to_par_d);
    GST_DEBUG_OBJECT (base, "resulting output should respect ratio of %d/%d",
        num, den);

    //now find a width x height that respects this display ratio.
    //prefer those that have one of w/h the same as the incoming video
    //using wd / hd = num / den

    //if one of the output width or height is fixed, we work from there
    if (h) {
      GST_DEBUG_OBJECT (base, "height is fixed,scaling width");
      w = (guint) gst_util_uint64_scale_int (h, num, den);
    } else if (w) {
      GST_DEBUG_OBJECT (base, "width is fixed, scaling height");
      h = (guint) gst_util_uint64_scale_int (w, den, num);
    } else {
      //none of width or height is fixed, figure out both of them based only on
      //the input width and height
      //check hd / den is an integer scale factor, and scale wd with the PAR
      if (from_h % den == 0) {
        GST_DEBUG_OBJECT (base, "keeping video height");
        h = from_h;
        w = (guint) gst_util_uint64_scale_int (h, num, den);
      } else if (from_w % num == 0) {
        GST_DEBUG_OBJECT (base, "keeping video width");
        w = from_w;
        h = (guint) gst_util_uint64_scale_int (w, den, num);
      } else {
        GST_DEBUG_OBJECT (base, "approximating but keeping video height");
        h = from_h;
        w = (guint) gst_util_uint64_scale_int (h, num, den);
      }
    }
    GST_DEBUG_OBJECT (base, "scaling to %dx%d", w, h);

    //now fixate
    gst_structure_fixate_field_nearest_int (outs, "width", w);
    gst_structure_fixate_field_nearest_int (outs, "height", h);
  } else {
    gint width, height;

    if (gst_structure_get_int (ins, "width", &width)) {
      if (gst_structure_has_field (outs, "width"))
        gst_structure_fixate_field_nearest_int (outs, "width", width);
    }
    if (gst_structure_get_int (ins, "height", &height)) {
      if (gst_structure_has_field (outs, "height")) {
        gst_structure_fixate_field_nearest_int (outs, "height", height);
      }
    }
  }

  GST_DEBUG_OBJECT (base, "fixated othercaps to %" GST_PTR_FORMAT, othercaps);
}

static gboolean
gst_gl_upload_set_caps (GstBaseTransform * bt, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstGLUpload *upload = GST_GL_UPLOAD (bt);
  gboolean ret = FALSE;
  GstVideoFormat video_format = GST_VIDEO_FORMAT_UNKNOWN;

  GST_DEBUG ("called with %" GST_PTR_FORMAT, incaps);

  ret = gst_video_format_parse_caps (outcaps, &video_format,
      &upload->gl_width, &upload->gl_height);

  ret |= gst_video_format_parse_caps (incaps, &upload->video_format,
      &upload->video_width, &upload->video_height);

  if (!ret) {
    GST_DEBUG ("caps connot be parsed");
    return FALSE;
  }

  //init colorspace conversion if needed
  gst_gl_display_init_upload (upload->display, upload->video_format,
      upload->gl_width, upload->gl_height,
      upload->video_width, upload->video_height);

  return ret;
}

static gboolean
gst_gl_upload_get_unit_size (GstBaseTransform * trans, GstCaps * caps,
    guint * size)
{
  gboolean ret = FALSE;
  GstStructure *structure = NULL;
  gint width = 0;
  gint height = 0;

  structure = gst_caps_get_structure (caps, 0);
  if (gst_structure_has_name (structure, "video/x-raw-gl")) {
    ret = gst_gl_buffer_parse_caps (caps, &width, &height);
    if (ret)
      *size = gst_gl_buffer_get_size (width, height);
  } else {
    GstVideoFormat video_format = GST_VIDEO_FORMAT_UNKNOWN;

    ret = gst_video_format_parse_caps (caps, &video_format, &width, &height);
    if (ret)
      *size = gst_video_format_get_size (video_format, width, height);
  }

  return TRUE;
}

static GstFlowReturn
gst_gl_upload_prepare_output_buffer (GstBaseTransform * trans,
    GstBuffer * input, gint size, GstCaps * caps, GstBuffer ** buf)
{
  GstGLUpload *upload = GST_GL_UPLOAD (trans);

  //blocking call, request a texture and attach it to the upload FBO
  GstGLBuffer *gl_outbuf = gst_gl_buffer_new (upload->display,
      upload->gl_width, upload->gl_height);

  *buf = GST_BUFFER (gl_outbuf);
  gst_buffer_set_caps (*buf, caps);

  if (gl_outbuf->texture)
    return GST_FLOW_OK;
  else
    return GST_FLOW_UNEXPECTED;
}


static GstFlowReturn
gst_gl_upload_transform (GstBaseTransform * trans, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstGLUpload *upload = GST_GL_UPLOAD (trans);
  GstGLBuffer *gl_outbuf = GST_GL_BUFFER (outbuf);

  GST_DEBUG ("Upload %p size %d",
      GST_BUFFER_DATA (inbuf), GST_BUFFER_SIZE (inbuf));

  //blocking call.
  //Depending on the colorspace, video is upload into several textures.
  //However, there is only one output texture. The one attached
  //to the upload FBO.
  if (gst_gl_display_do_upload (upload->display, gl_outbuf->texture,
          upload->video_width, upload->video_height, GST_BUFFER_DATA (inbuf)))
    return GST_FLOW_OK;
  else
    return GST_FLOW_UNEXPECTED;
}
