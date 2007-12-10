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

  GstPad *sinkpad;
  GstPad *srcpad;

  int fps_n;
  int fps_d;

  int frame_num;

  GstSegment segment;

  int negotiated;
};

struct _GstVideoParseClass
{
  GstElementClass parent_class;
};


static void gst_video_parse_dispose (GObject * object);

static void gst_video_parse_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_video_parse_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_video_parse_chain (GstPad * pad, GstBuffer * buffer);
static gboolean gst_video_parse_event (GstPad * pad, GstEvent * event);
static gboolean gst_video_parse_set_caps (GstPad * pad, GstCaps * caps);
static gboolean gst_video_parse_src_query (GstPad * pad, GstQuery * query);


static GstStaticPadTemplate gst_video_parse_src_pad_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate gst_video_parse_sink_pad_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (gst_video_parse_debug);
#define GST_CAT_DEFAULT gst_video_parse_debug

static const GstElementDetails gst_video_parse_details =
GST_ELEMENT_DETAILS ("Video Parse",
    "Filter/Video",
    "Converts stream into video frames",
    "David Schleef <ds@schleef.org>");

enum
{
  ARG_0
};


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
}

static void
gst_video_parse_init (GstVideoParse * vp, GstVideoParseClass * g_class)
{
  vp->sinkpad =
      gst_pad_new_from_static_template (&gst_video_parse_sink_pad_template,
      "sink");
  gst_element_add_pad (GST_ELEMENT (vp), vp->sinkpad);

  gst_pad_set_chain_function (vp->sinkpad, gst_video_parse_chain);
  gst_pad_set_event_function (vp->sinkpad, gst_video_parse_event);

  vp->srcpad =
      gst_pad_new_from_static_template (&gst_video_parse_src_pad_template,
      "src");
  gst_element_add_pad (GST_ELEMENT (vp), vp->srcpad);

  gst_pad_set_setcaps_function (vp->srcpad, gst_video_parse_set_caps);
  gst_pad_set_query_function (vp->srcpad, gst_video_parse_src_query);
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
  //GstVideoParse *vp = GST_VIDEO_PARSE (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_video_parse_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  //GstVideoParse *vp = GST_VIDEO_PARSE (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_video_parse_negotiate (GstVideoParse * vp)
{
  GstCaps *caps;
  gboolean ret = FALSE;

  caps = gst_pad_peer_get_caps (vp->srcpad);

  caps = gst_caps_make_writable (caps);
  gst_caps_truncate (caps);

  if (!gst_caps_is_empty (caps)) {
    gst_pad_fixate_caps (vp->srcpad, caps);

    if (gst_caps_is_any (caps)) {
      ret = TRUE;
    } else if (gst_caps_is_fixed (caps)) {
      /* yay, fixed caps, use those then */
      gst_pad_set_caps (vp->srcpad, caps);
      ret = TRUE;
    }
  }

  gst_caps_unref (caps);

  return ret;
}

static GstFlowReturn
gst_video_parse_chain (GstPad * pad, GstBuffer * buffer)
{
  GstVideoParse *vp = GST_VIDEO_PARSE (gst_pad_get_parent (pad));
  GstFlowReturn ret;

  GST_INFO ("here");

  if (!vp->negotiated) {
    gst_video_parse_negotiate (vp);
    vp->negotiated = TRUE;
  }

  if (vp->fps_n) {
    GST_BUFFER_TIMESTAMP (buffer) = vp->segment.start +
        gst_util_uint64_scale (vp->frame_num, GST_SECOND * vp->fps_d,
        vp->fps_n);
    GST_BUFFER_DURATION (buffer) =
        gst_util_uint64_scale (GST_SECOND, vp->fps_d, vp->fps_n);
  } else {
    GST_BUFFER_TIMESTAMP (buffer) = vp->segment.start;
    GST_BUFFER_DURATION (buffer) = GST_CLOCK_TIME_NONE;
  }
  gst_buffer_set_caps (buffer, GST_PAD_CAPS (vp->srcpad));

  vp->frame_num++;

  ret = gst_pad_push (vp->srcpad, buffer);

  gst_object_unref (vp);

  return ret;
}

static gboolean
gst_video_parse_event (GstPad * pad, GstEvent * event)
{
  GstVideoParse *vp = GST_VIDEO_PARSE (gst_pad_get_parent (pad));
  gboolean ret;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:
    {
      GstClockTimeDiff start, stop, time;
      gdouble rate, arate;
      gboolean update;
      GstFormat format;

      gst_event_parse_new_segment_full (event, &update, &rate, &arate, &format,
          &start, &stop, &time);

      if (format == GST_FORMAT_TIME) {
        gst_segment_set_newsegment_full (&vp->segment, update, rate, arate,
            format, start, stop, time);

        GST_DEBUG_OBJECT (vp, "update segment: %" GST_SEGMENT_FORMAT,
            &vp->segment);
      } else {
        GST_ERROR_OBJECT (vp,
            "Segment doesn't have GST_FORMAT_TIME format (%d)", format);

        gst_event_unref (event);
        gst_object_unref (vp);
        return FALSE;
      }
      break;
    }
    default:
      break;
  }

  ret = gst_pad_push_event (vp->srcpad, event);
  gst_object_unref (vp);

  return ret;
}

static gboolean
gst_video_parse_set_caps (GstPad * pad, GstCaps * caps)
{
  GstVideoParse *vp = GST_VIDEO_PARSE (gst_pad_get_parent (pad));
  GstStructure *s;

  s = gst_caps_get_structure (caps, 0);

  vp->fps_n = 0;
  vp->fps_d = 1;
  gst_structure_get_fraction (s, "framerate", &vp->fps_n, &vp->fps_d);

  GST_ERROR_OBJECT (vp, "framerate %d/%d", vp->fps_n, vp->fps_d);

  gst_object_unref (vp);

  return TRUE;
}

static gboolean
gst_video_parse_src_query (GstPad * pad, GstQuery * query)
{
  GstVideoParse *vp = GST_VIDEO_PARSE (gst_pad_get_parent (pad));
  gboolean ret = TRUE;

  if (GST_QUERY_TYPE (query) == GST_QUERY_CONVERT) {
    GstFormat src_fmt, dest_fmt;
    gint64 src_val, dest_val;

    gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
    if (src_fmt == dest_fmt) {
      dest_val = src_val;
    } else if (src_fmt == GST_FORMAT_DEFAULT && dest_fmt == GST_FORMAT_TIME) {
      /* frames to time */

      if (vp->fps_n) {
        dest_val = gst_util_uint64_scale (src_val, vp->fps_d * GST_SECOND,
            vp->fps_d);
      } else {
        dest_val = 0;
      }
    } else if (src_fmt == GST_FORMAT_DEFAULT && dest_fmt == GST_FORMAT_TIME) {
      /* time to frames */

      if (vp->fps_n) {
        dest_val = gst_util_uint64_scale (src_val, vp->fps_n,
            vp->fps_d * GST_SECOND);
      } else {
        dest_val = 0;
      }
    } else {
      GST_DEBUG_OBJECT (vp, "query failed");
      ret = FALSE;
    }

    gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);
  } else {
    /* else forward upstream */
    ret = gst_pad_peer_query (vp->sinkpad, query);
  }

  gst_object_unref (vp);
  return ret;
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
