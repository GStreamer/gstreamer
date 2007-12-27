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
  GstBaseTransform base_transform;

  /* < private > */

  GstGLDisplay *display;
  GstVideoFormat format;
  int width;
  int height;
};

struct _GstGLDownloadClass
{
  GstBaseTransformClass base_transform_class;
};

static const GstElementDetails element_details = GST_ELEMENT_DETAILS ("FIXME",
    "Filter/Effect",
    "FIXME example filter",
    "FIXME <fixme@fixme.com>");

static GstStaticPadTemplate gst_gl_download_src_pad_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_xRGB ";"
        GST_VIDEO_CAPS_RGBx ";" GST_VIDEO_CAPS_BGRx ";" GST_VIDEO_CAPS_xBGR)
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

GST_BOILERPLATE_FULL (GstGLDownload, gst_gl_download, GstBaseTransform,
    GST_TYPE_BASE_TRANSFORM, DEBUG_INIT);

static void gst_gl_download_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gl_download_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_gl_download_reset (GstGLDownload * download);
static gboolean gst_gl_download_set_caps (GstBaseTransform * bt,
    GstCaps * incaps, GstCaps * outcaps);
static GstCaps *gst_gl_download_transform_caps (GstBaseTransform * bt,
    GstPadDirection direction, GstCaps * caps);
static gboolean gst_gl_download_start (GstBaseTransform * bt);
static gboolean gst_gl_download_stop (GstBaseTransform * bt);
static GstFlowReturn gst_gl_download_transform (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf);
static gboolean
gst_gl_download_get_unit_size (GstBaseTransform * trans, GstCaps * caps,
    guint * size);


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

  GST_BASE_TRANSFORM_CLASS (klass)->transform_caps =
      gst_gl_download_transform_caps;
  GST_BASE_TRANSFORM_CLASS (klass)->transform = gst_gl_download_transform;
  GST_BASE_TRANSFORM_CLASS (klass)->start = gst_gl_download_start;
  GST_BASE_TRANSFORM_CLASS (klass)->stop = gst_gl_download_stop;
  GST_BASE_TRANSFORM_CLASS (klass)->set_caps = gst_gl_download_set_caps;
  GST_BASE_TRANSFORM_CLASS (klass)->get_unit_size =
      gst_gl_download_get_unit_size;
}

static void
gst_gl_download_init (GstGLDownload * download, GstGLDownloadClass * klass)
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
gst_gl_download_start (GstBaseTransform * bt)
{
  GstGLDownload *download = GST_GL_DOWNLOAD (bt);

  download->format = GST_VIDEO_FORMAT_RGBx;

  return TRUE;
}

static gboolean
gst_gl_download_stop (GstBaseTransform * bt)
{
  GstGLDownload *download = GST_GL_DOWNLOAD (bt);

  gst_gl_download_reset (download);

  return TRUE;
}

static GstCaps *
gst_gl_download_transform_caps (GstBaseTransform * bt,
    GstPadDirection direction, GstCaps * caps)
{
  GstGLDownload *download;
  GstStructure *structure;
  GstCaps *newcaps;
  GstStructure *newstruct;
  const GValue *width_value;
  const GValue *height_value;
  const GValue *framerate_value;
  const GValue *par_value;

  download = GST_GL_DOWNLOAD (bt);

  structure = gst_caps_get_structure (caps, 0);

  width_value = gst_structure_get_value (structure, "width");
  height_value = gst_structure_get_value (structure, "height");
  framerate_value = gst_structure_get_value (structure, "framerate");
  par_value = gst_structure_get_value (structure, "pixel-aspect-ratio");

  if (direction == GST_PAD_SINK) {
    newcaps = gst_caps_new_simple ("video/x-raw-rgb", NULL);
  } else {
    newcaps = gst_caps_new_simple ("video/x-raw-gl", NULL);
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

  return newcaps;
}

static gboolean
gst_gl_download_set_caps (GstBaseTransform * bt, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstGLDownload *download;
  gboolean ret;

  download = GST_GL_DOWNLOAD (bt);

  GST_DEBUG ("called with %" GST_PTR_FORMAT, incaps);

  ret = gst_video_format_parse_caps (outcaps, &download->format,
      &download->width, &download->height);

  if (!ret) {
    GST_DEBUG ("bad caps");
    return FALSE;
  }

  return ret;
}

static gboolean
gst_gl_download_get_unit_size (GstBaseTransform * trans, GstCaps * caps,
    guint * size)
{
  gboolean ret;
  GstStructure *structure;

  structure = gst_caps_get_structure (caps, 0);
  if (gst_structure_has_name (structure, "video/x-raw-gl")) {
    int width;
    int height;

    ret = gst_structure_get_int (structure, "width", &width);
    ret &= gst_structure_get_int (structure, "height", &height);

    /* FIXME */
    *size = width * height * 4;
  } else {
    int width;
    int height;

    ret = gst_structure_get_int (structure, "width", &width);
    ret &= gst_structure_get_int (structure, "height", &height);

    /* FIXME */
    *size = width * height * 4;
  }


  return TRUE;
}

static GstFlowReturn
gst_gl_download_transform (GstBaseTransform * trans, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstGLDownload *download;
  GstGLBuffer *gl_inbuf = GST_GL_BUFFER (inbuf);

  download = GST_GL_DOWNLOAD (trans);

  if (download->display == NULL) {
    download->display = g_object_ref (gl_inbuf->display);
  } else {
    g_assert (download->display == gl_inbuf->display);
  }

  GST_DEBUG ("downloading %p size %d",
      GST_BUFFER_DATA (outbuf), GST_BUFFER_SIZE (outbuf));
  gst_gl_buffer_download (gl_inbuf, download->format, GST_BUFFER_DATA (outbuf));

  return GST_FLOW_OK;
}
