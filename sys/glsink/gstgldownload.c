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

#define GST_CAT_DEFAULT gst_gl_download_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

#define GST_TYPE_GL_DOWNLOAD            (gst_gl_download_get_type())
#define GST_GL_DOWNLOAD(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_GL_DOWNLOAD,GstGLDownload))
#define GST_IS_GL_DOWNLOAD(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_GL_DOWNLOAD))
#define GST_GL_DOWNLOAD_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass) ,GST_TYPE_GL_DOWNLOAD,GstGLDownloadClass))
#define GST_IS_GL_DOWNLOAD_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass) ,GST_TYPE_GL_DOWNLOAD))
#define GST_GL_DOWNLOAD_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj) ,GST_TYPE_GL_DOWNLOAD,GstGLDownloadClass))
typedef struct _GstGLDownload GstGLDownload;
typedef struct _GstGLDownloadClass GstGLDownloadClass;

typedef void (*GstGLDownloadProcessFunc) (GstGLDownload *, guint8 *, guint);

struct _GstGLDownload
{
  GstElement element;

  GstPad *srcpad;
  GstPad *sinkpad;

  /* < private > */

  GstGLDisplay *display;
  GstVideoFormat format;
  int width;
  int height;
};

struct _GstGLDownloadClass
{
  GstElementClass element_class;
};

static const GstElementDetails element_details = GST_ELEMENT_DETAILS ("FIXME",
    "Filter/Effect",
    "FIXME example filter",
    "FIXME <fixme@fixme.com>");

#define GST_GL_VIDEO_CAPS "video/x-raw-gl"

static GstStaticPadTemplate gst_gl_download_src_pad_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_RGBx)
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

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_gl_download_debug, "gldownload", 0, "gldownload element");

GST_BOILERPLATE_FULL (GstGLDownload, gst_gl_download, GstElement,
    GST_TYPE_ELEMENT, DEBUG_INIT);

static void gst_gl_download_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gl_download_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_gl_download_chain (GstPad * pad, GstBuffer * buf);
static void gst_gl_download_reset (GstGLDownload * download);
static GstStateChangeReturn
gst_gl_download_change_state (GstElement * element, GstStateChange transition);
static gboolean gst_gl_download_sink_setcaps (GstPad * pad, GstCaps * caps);


static void
gst_gl_download_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_details (element_class, &element_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_gl_download_src_pad_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_gl_download_sink_pad_template));
}

static void
gst_gl_download_class_init (GstGLDownloadClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;
  gobject_class->set_property = gst_gl_download_set_property;
  gobject_class->get_property = gst_gl_download_get_property;

  GST_ELEMENT_CLASS (klass)->change_state = gst_gl_download_change_state;
}

static void
gst_gl_download_init (GstGLDownload * download, GstGLDownloadClass * klass)
{
  gst_element_create_all_pads (GST_ELEMENT (download));

  download->sinkpad =
      gst_element_get_static_pad (GST_ELEMENT (download), "sink");
  download->srcpad = gst_element_get_static_pad (GST_ELEMENT (download), "src");

  gst_pad_set_setcaps_function (download->sinkpad,
      gst_gl_download_sink_setcaps);
  gst_pad_set_chain_function (download->sinkpad, gst_gl_download_chain);

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

static void
gst_gl_download_reset (GstGLDownload * download)
{
  if (download->display) {
    g_object_unref (download->display);
    download->display = NULL;
  }
  download->format = GST_VIDEO_FORMAT_RGBx;
}

static gboolean
gst_gl_download_start (GstGLDownload * download)
{
  download->format = GST_VIDEO_FORMAT_RGBx;

  return TRUE;
}

static gboolean
gst_gl_download_stop (GstGLDownload * download)
{
  gst_gl_download_reset (download);

  return TRUE;
}

static gboolean
gst_gl_download_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstGLDownload *download;
  gboolean ret;
  GstStructure *structure;
  GstCaps *srccaps;

  download = GST_GL_DOWNLOAD (gst_pad_get_parent (pad));

  GST_DEBUG ("called with %" GST_PTR_FORMAT, caps);

  structure = gst_caps_get_structure (caps, 0);

  ret = gst_structure_get_int (structure, "width", &download->width);
  ret &= gst_structure_get_int (structure, "height", &download->height);
  if (!ret) {
    GST_DEBUG ("bad caps");
    return FALSE;
  }

  srccaps = gst_video_format_new_caps (download->format,
      download->width, download->height, 30, 1, 1, 1);
  GST_DEBUG ("srccaps %" GST_PTR_FORMAT, srccaps);
  ret = gst_pad_set_caps (download->srcpad, srccaps);
  gst_caps_unref (srccaps);

  return ret;
}

static GstFlowReturn
gst_gl_download_chain (GstPad * pad, GstBuffer * buf)
{
  GstGLDownload *download;
  GstGLBuffer *inbuf = GST_GL_BUFFER (buf);
  GstBuffer *outbuf;

  download = GST_GL_DOWNLOAD (gst_pad_get_parent (pad));

  outbuf =
      gst_buffer_new_and_alloc (gst_video_format_get_size (download->format,
          inbuf->width, inbuf->height));

  gst_buffer_copy_metadata (GST_BUFFER (outbuf), buf,
      GST_BUFFER_COPY_TIMESTAMPS | GST_BUFFER_COPY_FLAGS);
  gst_buffer_set_caps (GST_BUFFER (outbuf), GST_PAD_CAPS (download->srcpad));

  GST_DEBUG ("downloading %p size %d",
      GST_BUFFER_DATA (outbuf), GST_BUFFER_SIZE (outbuf));
  gst_gl_buffer_download (inbuf, GST_BUFFER_DATA (outbuf));

  gst_pad_push (download->srcpad, GST_BUFFER (outbuf));

  gst_buffer_unref (buf);
  gst_object_unref (download);
  return GST_FLOW_OK;
}

static GstStateChangeReturn
gst_gl_download_change_state (GstElement * element, GstStateChange transition)
{
  GstGLDownload *download;
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  GST_DEBUG ("change state");

  download = GST_GL_DOWNLOAD (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_gl_download_start (download);
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
      gst_gl_download_stop (download);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}
