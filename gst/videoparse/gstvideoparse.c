/* GStreamer
 * Copyright (C) 2006 David A. Schleef <ds@schleef.org>
 *
 * gstvideoparse.c:
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
 * SECTION:element-videoparse
 * @short_description: parses a byte stream into video frames
 *
 * Converts a byte stream into video frames.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/base/gstadapter.h>
#include <gst/video/video.h>

#define GST_TYPE_VIDEO_PARSE \
  (gst_video_parse_get_type())
#define GST_VIDEO_PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VIDEO_PARSE,GstVideoParse))
#define GST_VIDEO_PARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VIDEO_PARSE,GstVideoParseClass))
#define GST_IS_VIDEO_PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VIDEO_PARSE))
#define GST_IS_VIDEO_PARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VIDEO_PARSE))

typedef struct _GstVideoParse GstVideoParse;
typedef struct _GstVideoParseClass GstVideoParseClass;

struct _GstVideoParse
{
  GstElement parent;

  /* properties */
  int width;
  int height;
  guint32 format;
  int fps_n;
  int fps_d;
  int par_n;
  int par_d;

  /* private */

  GstPad *sinkpad;
  GstPad *srcpad;

  GstAdapter *adapter;

  int blocksize;

  gboolean discont;
  int n_frames;

  GstSegment segment;

  gboolean negotiated;
  gboolean have_new_segment;
};

struct _GstVideoParseClass
{
  GstElementClass parent_class;
};

typedef enum
{
  GST_VIDEO_PARSE_FORMAT_I420,
  GST_VIDEO_PARSE_FORMAT_YV12,
  GST_VIDEO_PARSE_FORMAT_YUY2,
  GST_VIDEO_PARSE_FORMAT_UYVY
} GstVideoParseFormat;

static void gst_video_parse_dispose (GObject * object);

static void gst_video_parse_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_video_parse_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_video_parse_chain (GstPad * pad, GstBuffer * buffer);
static gboolean gst_video_parse_sink_event (GstPad * pad, GstEvent * event);
static gboolean gst_video_parse_src_query (GstPad * pad, GstQuery * query);
static gboolean gst_video_parse_convert (GstVideoParse * vp,
    GstFormat src_format, gint64 src_value,
    GstFormat dest_format, gint64 * dest_value);
static void gst_video_parse_update_block_size (GstVideoParse * vp);


static GstStaticPadTemplate gst_video_parse_src_pad_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("{ I420, YUY2, UYVY }")));

static GstStaticPadTemplate gst_video_parse_sink_pad_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("{ I420, YUY2, UYVY }")));

GST_DEBUG_CATEGORY_STATIC (gst_video_parse_debug);
#define GST_CAT_DEFAULT gst_video_parse_debug

static const GstElementDetails gst_video_parse_details =
GST_ELEMENT_DETAILS ("Video Parse",
    "Filter/Video",
    "Converts stream into video frames",
    "David Schleef <ds@schleef.org>");

enum
{
  ARG_0,
  ARG_WIDTH,
  ARG_HEIGHT,
  ARG_FORMAT,
  ARG_PAR,
  ARG_FRAMERATE
};


#define GST_VIDEO_PARSE_FORMAT (gst_video_parse_format_get_type ())
static GType
gst_video_parse_format_get_type (void)
{
  static GType video_parse_format_type = 0;
  static const GEnumValue format_types[] = {
    {GST_VIDEO_PARSE_FORMAT_I420, "I420", "I420"},
    {GST_VIDEO_PARSE_FORMAT_YV12, "YV12", "YV12"},
    {GST_VIDEO_PARSE_FORMAT_YUY2, "YUY2", "YUY2"},
    {GST_VIDEO_PARSE_FORMAT_UYVY, "UYVY", "UYVY"},
    {0, NULL, NULL}
  };

  if (!video_parse_format_type) {
    video_parse_format_type =
        g_enum_register_static ("GstVideoParseFormat", format_types);
  }

  return video_parse_format_type;
}

GST_BOILERPLATE (GstVideoParse, gst_video_parse, GstElement, GST_TYPE_ELEMENT);

static void
gst_video_parse_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  GST_DEBUG_CATEGORY_INIT (gst_video_parse_debug, "videoparse", 0,
      "videoparse element");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_video_parse_src_pad_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_video_parse_sink_pad_template));
  gst_element_class_set_details (gstelement_class, &gst_video_parse_details);
}

static void
gst_video_parse_class_init (GstVideoParseClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  //GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_video_parse_set_property;
  gobject_class->get_property = gst_video_parse_get_property;

  gobject_class->dispose = gst_video_parse_dispose;

  g_object_class_install_property (gobject_class, ARG_WIDTH,
      g_param_spec_int ("width", "Width", "Width of images in raw stream",
          0, INT_MAX, 320, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_HEIGHT,
      g_param_spec_int ("height", "Height", "Height of images in raw stream",
          0, INT_MAX, 240, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_FORMAT,
      g_param_spec_enum ("format", "Format", "Format of images in raw stream",
          GST_VIDEO_PARSE_FORMAT, GST_VIDEO_PARSE_FORMAT_I420,
          G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_FRAMERATE,
      gst_param_spec_fraction ("framerate", "Frame Rate",
          "Frame rate of images in raw stream", 0, 1, 100, 1, 25, 1,
          G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_PAR,
      gst_param_spec_fraction ("pixel_aspect_ratio", "Pixel Aspect Ratio",
          "Pixel aspect ratio of images in raw stream", 1, 100, 100, 1, 1, 1,
          G_PARAM_READWRITE));
}

static void
gst_video_parse_init (GstVideoParse * vp, GstVideoParseClass * g_class)
{
  vp->sinkpad =
      gst_pad_new_from_static_template (&gst_video_parse_sink_pad_template,
      "sink");
  gst_element_add_pad (GST_ELEMENT (vp), vp->sinkpad);

  gst_pad_set_chain_function (vp->sinkpad, gst_video_parse_chain);
  gst_pad_set_event_function (vp->sinkpad, gst_video_parse_sink_event);

  vp->srcpad =
      gst_pad_new_from_static_template (&gst_video_parse_src_pad_template,
      "src");
  gst_element_add_pad (GST_ELEMENT (vp), vp->srcpad);

  if (1) {
    gst_pad_set_query_function (vp->srcpad, gst_video_parse_src_query);
  }

  vp->adapter = gst_adapter_new ();

  vp->width = 320;
  vp->height = 240;
  vp->format = GST_VIDEO_PARSE_FORMAT_I420;
  vp->par_n = 1;
  vp->par_d = 1;
  vp->fps_n = 25;
  vp->fps_d = 1;

  gst_video_parse_update_block_size (vp);
}

static void
gst_video_parse_dispose (GObject * object)
{
  //GstVideoParse *vp = GST_VIDEO_PARSE (object);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_video_parse_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVideoParse *vp = GST_VIDEO_PARSE (object);

  switch (prop_id) {
    case ARG_WIDTH:
      vp->width = g_value_get_int (value);
      break;
    case ARG_HEIGHT:
      vp->height = g_value_get_int (value);
      break;
    case ARG_FORMAT:
      vp->format = g_value_get_enum (value);
      break;
    case ARG_FRAMERATE:
      vp->fps_n = gst_value_get_fraction_numerator (value);
      vp->fps_d = gst_value_get_fraction_denominator (value);
      break;
    case ARG_PAR:
      vp->par_n = gst_value_get_fraction_numerator (value);
      vp->par_d = gst_value_get_fraction_denominator (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  gst_video_parse_update_block_size (vp);
}

static void
gst_video_parse_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstVideoParse *vp = GST_VIDEO_PARSE (object);

  switch (prop_id) {
    case ARG_WIDTH:
      g_value_set_int (value, vp->width);
      break;
    case ARG_HEIGHT:
      g_value_set_int (value, vp->height);
      break;
    case ARG_FORMAT:
      g_value_set_enum (value, vp->format);
      break;
    case ARG_FRAMERATE:
      gst_value_set_fraction (value, vp->fps_n, vp->fps_d);
      break;
    case ARG_PAR:
      gst_value_set_fraction (value, vp->par_n, vp->par_d);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static guint32
gst_video_parse_format_to_fourcc (GstVideoParseFormat format)
{
  switch (format) {
    case GST_VIDEO_PARSE_FORMAT_I420:
      return GST_MAKE_FOURCC ('I', '4', '2', '0');
    case GST_VIDEO_PARSE_FORMAT_YV12:
      return GST_MAKE_FOURCC ('Y', 'V', '1', '2');
    case GST_VIDEO_PARSE_FORMAT_YUY2:
      return GST_MAKE_FOURCC ('Y', 'U', 'Y', '2');
    case GST_VIDEO_PARSE_FORMAT_UYVY:
      return GST_MAKE_FOURCC ('U', 'Y', 'V', 'Y');
  }
  return 0;
}

void
gst_video_parse_update_block_size (GstVideoParse * vp)
{
  vp->blocksize = vp->width * vp->height * 3 / 2;
}

static void
gst_video_parse_reset (GstVideoParse * vp)
{
  vp->n_frames = 0;
  vp->discont = TRUE;

  gst_segment_init (&vp->segment, GST_FORMAT_TIME);
  gst_adapter_clear (vp->adapter);
}

static GstFlowReturn
gst_video_parse_chain (GstPad * pad, GstBuffer * buffer)
{
  GstVideoParse *vp = GST_VIDEO_PARSE (gst_pad_get_parent (pad));
  GstFlowReturn ret = GST_FLOW_OK;

  if (G_UNLIKELY (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DISCONT))) {
    GST_DEBUG_OBJECT (vp, "received DISCONT buffer");

    vp->discont = TRUE;
  }

  if (!vp->negotiated) {
    GstCaps *caps;

    caps = gst_caps_new_simple ("video/x-raw-yuv",
        "width", G_TYPE_INT, vp->width,
        "height", G_TYPE_INT, vp->height,
        "format", GST_TYPE_FOURCC,
        gst_video_parse_format_to_fourcc (vp->format), "framerate",
        GST_TYPE_FRACTION, vp->fps_n, vp->fps_d, "pixel_aspect_ratio",
        GST_TYPE_FRACTION, vp->par_n, vp->par_d, NULL);
    gst_pad_set_caps (vp->srcpad, caps);
    vp->negotiated = TRUE;

  }

  gst_adapter_push (vp->adapter, buffer);

  while (gst_adapter_available (vp->adapter) >= vp->blocksize) {
    buffer = gst_adapter_take_buffer (vp->adapter, vp->blocksize);

    if (vp->fps_n) {
      GST_BUFFER_TIMESTAMP (buffer) = vp->segment.start +
          gst_util_uint64_scale (vp->n_frames, GST_SECOND * vp->fps_d,
          vp->fps_n);
      GST_BUFFER_DURATION (buffer) =
          gst_util_uint64_scale (GST_SECOND, vp->fps_d, vp->fps_n);
    } else {
      GST_BUFFER_TIMESTAMP (buffer) = vp->segment.start;
      GST_BUFFER_DURATION (buffer) = GST_CLOCK_TIME_NONE;
    }
    gst_buffer_set_caps (buffer, GST_PAD_CAPS (vp->srcpad));
    if (vp->discont) {
      GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DISCONT);
      vp->discont = FALSE;
    }

    vp->n_frames++;

    ret = gst_pad_push (vp->srcpad, buffer);
    if (ret != GST_FLOW_OK)
      break;
  }

  gst_object_unref (vp);
  return ret;
}

static gboolean
gst_video_parse_convert (GstVideoParse * vp,
    GstFormat src_format, gint64 src_value,
    GstFormat dest_format, gint64 * dest_value)
{
  gboolean ret = FALSE;

  GST_DEBUG ("converting value %" G_GINT64_FORMAT " from %s to %s",
      src_value, gst_format_get_name (src_format),
      gst_format_get_name (dest_format));

  if (src_format == dest_format) {
    *dest_value = src_value;
    ret = TRUE;
  }

  /* bytes to frames */
  if (src_format == GST_FORMAT_BYTES && dest_format == GST_FORMAT_DEFAULT) {
    if (vp->blocksize != 0) {
      *dest_value = gst_util_uint64_scale_int (src_value, 1, vp->blocksize);
    } else {
      GST_ERROR ("blocksize is 0");
      *dest_value = 0;
    }
    ret = TRUE;
  }

  /* frames to bytes */
  if (src_format == GST_FORMAT_DEFAULT && dest_format == GST_FORMAT_BYTES) {
    *dest_value = gst_util_uint64_scale_int (src_value, vp->blocksize, 1);
    ret = TRUE;
  }

  /* time to frames */
  if (src_format == GST_FORMAT_TIME && dest_format == GST_FORMAT_DEFAULT) {
    if (vp->fps_d != 0) {
      *dest_value = gst_util_uint64_scale (src_value,
          vp->fps_n, GST_SECOND * vp->fps_d);
    } else {
      GST_ERROR ("framerate denominator is 0");
      *dest_value = 0;
    }
    ret = TRUE;
  }

  /* frames to time */
  if (src_format == GST_FORMAT_DEFAULT && dest_format == GST_FORMAT_TIME) {
    if (vp->fps_n != 0) {
      *dest_value = gst_util_uint64_scale (src_value,
          GST_SECOND * vp->fps_d, vp->fps_n);
    } else {
      GST_ERROR ("framerate numerator is 0");
      *dest_value = 0;
    }
    ret = TRUE;
  }

  /* time to bytes */
  if (src_format == GST_FORMAT_TIME && dest_format == GST_FORMAT_BYTES) {
    if (vp->fps_d != 0) {
      *dest_value = gst_util_uint64_scale (src_value,
          vp->fps_n * vp->blocksize, GST_SECOND * vp->fps_d);
    } else {
      GST_ERROR ("framerate denominator is 0");
      *dest_value = 0;
    }
    ret = TRUE;
  }

  /* bytes to time */
  if (src_format == GST_FORMAT_BYTES && dest_format == GST_FORMAT_TIME) {
    if (vp->fps_n != 0 && vp->blocksize != 0) {
      *dest_value = gst_util_uint64_scale (src_value,
          GST_SECOND * vp->fps_d, vp->fps_n * vp->blocksize);
    } else {
      GST_ERROR ("framerate denominator and/or blocksize is 0");
      *dest_value = 0;
    }
    ret = TRUE;
  }

  GST_DEBUG ("ret=%d result %" G_GINT64_FORMAT, ret, *dest_value);

  return ret;
}


static gboolean
gst_video_parse_sink_event (GstPad * pad, GstEvent * event)
{
  GstVideoParse *vp = GST_VIDEO_PARSE (gst_pad_get_parent (pad));
  gboolean ret;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
      gst_video_parse_reset (vp);
      ret = gst_pad_push_event (vp->srcpad, event);
      break;
    case GST_EVENT_NEWSEGMENT:
    {
      GstClockTimeDiff start, stop, time;
      gdouble rate, arate;
      gboolean update;
      GstFormat format;

      gst_event_parse_new_segment_full (event, &update, &rate, &arate, &format,
          &start, &stop, &time);

      ret =
          gst_video_parse_convert (vp, format, start, GST_FORMAT_TIME, &start);
      ret &= gst_video_parse_convert (vp, format, stop, GST_FORMAT_TIME, &stop);
      ret &= gst_video_parse_convert (vp, format, time, GST_FORMAT_TIME, &time);
      if (!ret) {
        GST_ERROR_OBJECT (vp,
            "Failed converting to GST_FORMAT_TIME format (%d)", format);
        break;
      }

      gst_segment_set_newsegment_full (&vp->segment, update, rate, arate,
          GST_FORMAT_TIME, start, stop, time);
      event = gst_event_new_new_segment (FALSE, vp->segment.rate,
          GST_FORMAT_TIME, start, stop, time);

      ret = gst_pad_push_event (vp->srcpad, event);
      break;
    }
    default:
      ret = gst_pad_event_default (vp->srcpad, event);
      break;
  }

  gst_object_unref (vp);

  return ret;
}


static gboolean
gst_video_parse_src_query (GstPad * pad, GstQuery * query)
{
  GstVideoParse *vp = GST_VIDEO_PARSE (gst_pad_get_parent (pad));
  gboolean ret = FALSE;

  GST_DEBUG ("src_query %s", gst_query_type_get_name (GST_QUERY_TYPE (query)));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstFormat format;
      gint64 time, value;

      GST_ERROR ("query position");

      gst_query_parse_position (query, &format, NULL);

      time = gst_util_uint64_scale (vp->n_frames,
          GST_SECOND * vp->fps_d, vp->fps_n);
      ret = gst_video_parse_convert (vp, GST_FORMAT_TIME, time, format, &value);

      gst_query_set_position (query, format, value);

      break;
    }
    case GST_QUERY_DURATION:
      GST_ERROR ("query duration");
      ret = gst_pad_query (GST_PAD_PEER (vp->srcpad), query);
      if (!ret)
        goto error;
      break;
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      GST_ERROR ("query convert");

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      ret = gst_video_parse_convert (vp, src_fmt, src_val, dest_fmt, &dest_val);
      if (!ret)
        goto error;
      gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);
      break;
    }
    default:
      /* else forward upstream */
      ret = gst_pad_peer_query (vp->sinkpad, query);
      break;
  }

done:
  gst_object_unref (vp);
  return ret;
error:
  GST_DEBUG_OBJECT (vp, "query failed");
  goto done;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  gst_element_register (plugin, "videoparse", GST_RANK_NONE,
      gst_video_parse_get_type ());

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "videoparse",
    "Parses byte streams into video frames",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
