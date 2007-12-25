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
  GstElement element;

  GstPad *srcpad;
  GstPad *sinkpad;

  /* < private > */

  GstGLDisplay *display;
  GstVideoFormat format;
  int width;
  int height;

  gboolean peek;
};

struct _GstGLUploadClass
{
  GstElementClass element_class;
};

static const GstElementDetails element_details = GST_ELEMENT_DETAILS ("FIXME",
    "Filter/Effect",
    "FIXME example filter",
    "FIXME <fixme@fixme.com>");

#define GST_GL_VIDEO_CAPS "video/x-raw-gl"

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
    GST_STATIC_CAPS (GST_VIDEO_CAPS_RGBx)
    );

enum
{
  PROP_0
};

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_gl_upload_debug, "glupload", 0, "glupload element");

GST_BOILERPLATE_FULL (GstGLUpload, gst_gl_upload, GstElement,
    GST_TYPE_ELEMENT, DEBUG_INIT);

static void gst_gl_upload_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gl_upload_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_gl_upload_chain (GstPad * pad, GstBuffer * buf);
static void gst_gl_upload_reset (GstGLUpload * upload);
static GstStateChangeReturn
gst_gl_upload_change_state (GstElement * element, GstStateChange transition);
static gboolean gst_gl_upload_sink_setcaps (GstPad * pad, GstCaps * caps);


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

  GST_ELEMENT_CLASS (klass)->change_state = gst_gl_upload_change_state;
}

static void
gst_gl_upload_init (GstGLUpload * upload, GstGLUploadClass * klass)
{
  gst_element_create_all_pads (GST_ELEMENT (upload));

  upload->sinkpad = gst_element_get_static_pad (GST_ELEMENT (upload), "sink");
  upload->srcpad = gst_element_get_static_pad (GST_ELEMENT (upload), "src");

  gst_pad_set_setcaps_function (upload->sinkpad, gst_gl_upload_sink_setcaps);
  gst_pad_set_chain_function (upload->sinkpad, gst_gl_upload_chain);

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
  upload->format = GST_VIDEO_FORMAT_RGBx;
  upload->peek = FALSE;
}

static gboolean
gst_gl_upload_start (GstGLUpload * upload)
{
  gboolean ret;

  upload->format = GST_VIDEO_FORMAT_RGBx;
  upload->display = gst_gl_display_new ();
  ret = gst_gl_display_connect (upload->display, NULL);

  return ret;
}

static gboolean
gst_gl_upload_stop (GstGLUpload * upload)
{
  gst_gl_upload_reset (upload);

  return TRUE;
}

static gboolean
gst_gl_upload_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstGLUpload *upload;
  GstVideoFormat format;
  int height;
  int width;
  gboolean ret;
  GstCaps *srccaps;

  upload = GST_GL_UPLOAD (gst_pad_get_parent (pad));

  ret = gst_video_format_parse_caps (caps, &format, &width, &height);
  if (!ret)
    return FALSE;

  upload->format = format;
  upload->width = width;
  upload->height = height;

  GST_ERROR ("setcaps %d %d %d", format, width, height);

  srccaps = gst_caps_new_simple ("video/x-raw-gl",
      "width", G_TYPE_INT, width, "height", G_TYPE_INT, height, NULL);
  ret = gst_pad_set_caps (upload->srcpad, srccaps);
  gst_caps_unref (srccaps);

  return ret;
}

static GstFlowReturn
gst_gl_upload_chain (GstPad * pad, GstBuffer * buf)
{
  GstGLUpload *upload;
  GstGLBuffer *outbuf;

  upload = GST_GL_UPLOAD (gst_pad_get_parent (pad));

  outbuf = gst_gl_buffer_new (upload->display, upload->format,
      upload->width, upload->height);

  gst_buffer_copy_metadata (GST_BUFFER (outbuf), buf,
      GST_BUFFER_COPY_TIMESTAMPS | GST_BUFFER_COPY_FLAGS);
  gst_buffer_set_caps (GST_BUFFER (outbuf), GST_PAD_CAPS (upload->srcpad));

  GST_DEBUG ("uploading %p size %d", GST_BUFFER_DATA (buf),
      GST_BUFFER_SIZE (buf));
  gst_gl_buffer_upload (outbuf, GST_BUFFER_DATA (buf));
  gst_buffer_unref (buf);

  if (upload->peek) {
    gst_gl_display_draw_texture (outbuf->display, outbuf->texture,
        outbuf->width, outbuf->height);
  }

  gst_pad_push (upload->srcpad, GST_BUFFER (outbuf));

  gst_object_unref (upload);
  return GST_FLOW_OK;
}

static GstStateChangeReturn
gst_gl_upload_change_state (GstElement * element, GstStateChange transition)
{
  GstGLUpload *upload;
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  GST_DEBUG ("change state");

  upload = GST_GL_UPLOAD (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_gl_upload_start (upload);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_gl_upload_stop (upload);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}
