/* 
 * GStreamer
 * Copyright (C) 2007 David Schleef <ds@schleef.org>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/video/video.h>
#include <gstglbuffer.h>

#define GST_CAT_DEFAULT gst_gl_upload_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

#define GST_TYPE_GL_UPLOAD            (gst_gl_upload_get_type())
#define GST_GL_UPLOAD(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_GL_UPLOAD,GstGLUpload))
#define GST_IS_GL_UPLOAD(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_GL_UPLOAD))
#define GST_GL_UPLOAD_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass) ,GST_TYPE_GL_UPLOAD,GstGLUploadClass))
#define GST_IS_GL_UPLOAD_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass) ,GST_TYPE_GL_UPLOAD))
#define GST_GL_UPLOAD_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj) ,GST_TYPE_GL_UPLOAD,GstGLUploadClass))
typedef struct _GstGLUpload GstGLUpload;
typedef struct _GstGLUploadClass GstGLUploadClass;

typedef void (*GstGLUploadProcessFunc) (GstGLUpload *, guint8 *, guint);

struct _GstGLUpload
{
  GstBaseTransform base_transform;

  GstPad *srcpad;
  GstPad *sinkpad;

  /* < private > */

  GstGLDisplay *display;
  GstVideoFormat video_format;
  GstGLBufferFormat format;
  int width;
  int height;

  gboolean peek;
};

struct _GstGLUploadClass
{
  GstBaseTransformClass base_transform_class;
};

static const GstElementDetails element_details = GST_ELEMENT_DETAILS ("FIXME",
    "Filter/Effect",
    "FIXME example filter",
    "FIXME <fixme@fixme.com>");

static GstStaticPadTemplate gst_gl_upload_src_pad_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_GL_VIDEO_CAPS)
    );

static GstStaticPadTemplate gst_gl_upload_sink_pad_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_RGBx ";"
        GST_VIDEO_CAPS_BGRx ";"
        GST_VIDEO_CAPS_xRGB ";"
        GST_VIDEO_CAPS_xBGR ";"
        GST_VIDEO_CAPS_YUV ("{ YUY2, UYVY, AYUV, YV12, I420 }"))
    );

enum
{
  PROP_0
};

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_gl_upload_debug, "glupload", 0, "glupload element");

GST_BOILERPLATE_FULL (GstGLUpload, gst_gl_upload, GstBaseTransform,
    GST_TYPE_BASE_TRANSFORM, DEBUG_INIT);

static void gst_gl_upload_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gl_upload_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_gl_upload_reset (GstGLUpload * upload);

static void gst_gl_upload_reset (GstGLUpload * upload);
static gboolean gst_gl_upload_set_caps (GstBaseTransform * bt,
    GstCaps * incaps, GstCaps * outcaps);
static GstCaps *gst_gl_upload_transform_caps (GstBaseTransform * bt,
    GstPadDirection direction, GstCaps * caps);
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

  gst_element_class_set_details (element_class, &element_details);

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
  GST_BASE_TRANSFORM_CLASS (klass)->transform = gst_gl_upload_transform;
  GST_BASE_TRANSFORM_CLASS (klass)->start = gst_gl_upload_start;
  GST_BASE_TRANSFORM_CLASS (klass)->stop = gst_gl_upload_stop;
  GST_BASE_TRANSFORM_CLASS (klass)->set_caps = gst_gl_upload_set_caps;
  GST_BASE_TRANSFORM_CLASS (klass)->get_unit_size = gst_gl_upload_get_unit_size;
  GST_BASE_TRANSFORM_CLASS (klass)->prepare_output_buffer =
      gst_gl_upload_prepare_output_buffer;
}

static void
gst_gl_upload_init (GstGLUpload * upload, GstGLUploadClass * klass)
{

  gst_gl_upload_reset (upload);
}

static void
gst_gl_upload_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  //GstGLUpload *upload = GST_GL_UPLOAD (object);

  switch (prop_id) {
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

static void
gst_gl_upload_reset (GstGLUpload * upload)
{
  if (upload->display) {
    g_object_unref (upload->display);
    upload->display = NULL;
  }
  upload->format = GST_GL_BUFFER_FORMAT_RGB;
  upload->peek = FALSE;
}

static gboolean
gst_gl_upload_start (GstBaseTransform * bt)
{
  GstGLUpload *upload = GST_GL_UPLOAD (bt);
  gboolean ret;

  upload->format = GST_GL_BUFFER_FORMAT_RGB;
  upload->display = gst_gl_display_new ();
  ret = gst_gl_display_connect (upload->display, NULL);
  //upload->format = GST_VIDEO_FORMAT_RGBx;

  return TRUE;
}

static gboolean
gst_gl_upload_stop (GstBaseTransform * bt)
{
  GstGLUpload *upload = GST_GL_UPLOAD (bt);

  gst_gl_upload_reset (upload);

  return TRUE;
}

#if 0
static gboolean
gst_gl_upload_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstGLUpload *upload;
  GstVideoFormat video_format;
  int height;
  int width;
  gboolean ret;
  GstCaps *srccaps;
  int fps_n, fps_d;
  int par_n, par_d;

  upload = GST_GL_UPLOAD (gst_pad_get_parent (pad));

  ret = gst_video_format_parse_caps (caps, &video_format, &width, &height);
  ret &= gst_video_parse_caps_framerate (caps, &fps_n, &fps_d);

  if (!ret)
    return FALSE;

  upload->video_format = video_format;
  upload->width = width;
  upload->height = height;

  GST_DEBUG ("setcaps %d %d %d", video_format, width, height);

  par_n = 1;
  par_d = 1;
  gst_video_parse_caps_pixel_aspect_ratio (caps, &par_n, &par_d);

  srccaps = gst_caps_new_simple ("video/x-raw-gl",
      "format", G_TYPE_INT, 0,
      "width", G_TYPE_INT, width, "height", G_TYPE_INT, height,
      "framerate", GST_TYPE_FRACTION, fps_n, fps_d,
      "pixel-aspect-ratio", GST_TYPE_FRACTION, par_n, par_d, NULL);

  ret = gst_pad_set_caps (upload->srcpad, srccaps);
  gst_caps_unref (srccaps);

  return ret;
}
#endif

#if 0
static GstFlowReturn
gst_gl_upload_chain (GstPad * pad, GstBuffer * buf)
{
  GstGLUpload *upload;
  GstGLBuffer *outbuf;

  upload = GST_GL_UPLOAD (gst_pad_get_parent (pad));

  outbuf = gst_gl_buffer_new_from_data (upload->display,
      upload->video_format, upload->width, upload->height,
      GST_BUFFER_DATA (buf));

  gst_buffer_copy_metadata (GST_BUFFER (outbuf), buf,
      GST_BUFFER_COPY_TIMESTAMPS | GST_BUFFER_COPY_FLAGS);
  gst_buffer_set_caps (GST_BUFFER (outbuf), GST_PAD_CAPS (upload->srcpad));

  GST_DEBUG ("uploading %p size %d", GST_BUFFER_DATA (buf),
      GST_BUFFER_SIZE (buf));
  gst_buffer_unref (buf);

  if (upload->peek) {
    gst_gl_display_draw_texture (outbuf->display, outbuf->texture,
        outbuf->width, outbuf->height);
  }

  gst_pad_push (upload->srcpad, GST_BUFFER (outbuf));

  gst_object_unref (upload);
  return GST_FLOW_OK;
}
#endif

static GstCaps *
gst_gl_upload_transform_caps (GstBaseTransform * bt,
    GstPadDirection direction, GstCaps * caps)
{
  GstGLUpload *upload;
  GstStructure *structure;
  GstCaps *newcaps;
  GstStructure *newstruct;
  const GValue *width_value;
  const GValue *height_value;
  const GValue *framerate_value;
  const GValue *par_value;

  upload = GST_GL_UPLOAD (bt);

  GST_ERROR ("transform caps %" GST_PTR_FORMAT, caps);

  structure = gst_caps_get_structure (caps, 0);

  width_value = gst_structure_get_value (structure, "width");
  height_value = gst_structure_get_value (structure, "height");
  framerate_value = gst_structure_get_value (structure, "framerate");
  par_value = gst_structure_get_value (structure, "pixel-aspect-ratio");

  if (direction == GST_PAD_SRC) {
    newcaps = gst_caps_new_simple ("video/x-raw-rgb", NULL);
  } else {
    newcaps = gst_caps_new_simple ("video/x-raw-gl",
        "format", G_TYPE_INT, GST_GL_BUFFER_FORMAT_RGBA,
        "is_yuv", G_TYPE_BOOLEAN, FALSE, NULL);
  }
  newstruct = gst_caps_get_structure (newcaps, 0);
  gst_structure_set_value (newstruct, "width", width_value);
  gst_structure_set_value (newstruct, "height", height_value);
  gst_structure_set_value (newstruct, "framerate", framerate_value);
  if (par_value) {
    gst_structure_set_value (newstruct, "pixel-aspect-ratio", par_value);
  } else {
    gst_structure_set (newstruct, "pixel-aspect-ratio", GST_TYPE_FRACTION,
        1, 1, NULL);
  }

  GST_ERROR ("new caps %" GST_PTR_FORMAT, newcaps);

  return newcaps;
}

static gboolean
gst_gl_upload_set_caps (GstBaseTransform * bt, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstGLUpload *upload;
  gboolean ret;

  upload = GST_GL_UPLOAD (bt);

  GST_DEBUG ("called with %" GST_PTR_FORMAT, incaps);

  ret = gst_video_format_parse_caps (incaps, &upload->video_format,
      &upload->width, &upload->height);

  if (!ret) {
    GST_DEBUG ("bad caps");
    return FALSE;
  }

  return ret;
}

static gboolean
gst_gl_upload_get_unit_size (GstBaseTransform * trans, GstCaps * caps,
    guint * size)
{
  gboolean ret;
  GstStructure *structure;
  int width;
  int height;

  structure = gst_caps_get_structure (caps, 0);
  if (gst_structure_has_name (structure, "video/x-raw-gl")) {
    GstGLBufferFormat format;

    ret = gst_gl_buffer_format_parse_caps (caps, &format, &width, &height);
    if (ret) {
      *size = gst_gl_buffer_format_get_size (format, width, height);
    }
  } else {
    GstVideoFormat format;

    ret = gst_video_format_parse_caps (caps, &format, &width, &height);
    if (ret) {
      *size = gst_video_format_get_size (format, width, height);
    }
  }

  return TRUE;
}

static GstFlowReturn
gst_gl_upload_prepare_output_buffer (GstBaseTransform * trans,
    GstBuffer * input, gint size, GstCaps * caps, GstBuffer ** buf)
{
  GstGLUpload *upload;
  GstGLBuffer *gl_outbuf;

  upload = GST_GL_UPLOAD (trans);

  gl_outbuf = gst_gl_buffer_new_from_video_format (upload->display,
      upload->video_format, upload->width, upload->height);

  *buf = GST_BUFFER (gl_outbuf);
  gst_buffer_set_caps (*buf, caps);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_gl_upload_transform (GstBaseTransform * trans, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstGLUpload *upload;
  GstGLBuffer *gl_outbuf = GST_GL_BUFFER (outbuf);

  upload = GST_GL_UPLOAD (trans);

  GST_DEBUG ("uploading %p size %d",
      GST_BUFFER_DATA (inbuf), GST_BUFFER_SIZE (inbuf));
  gst_gl_buffer_upload (gl_outbuf, upload->video_format,
      GST_BUFFER_DATA (inbuf));

  if (upload->peek) {
    gst_gl_display_draw_texture (gl_outbuf->display, gl_outbuf->texture,
        gl_outbuf->width, gl_outbuf->height);
  }

  return GST_FLOW_OK;
}
