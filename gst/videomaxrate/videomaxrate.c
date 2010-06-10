/*
 * GStreamer - limit video rate
 *
 *  Copyright 2008 Barracuda Networks, Inc.
 *
 *  Copyright 2009 Nokia Corporation
 *  Copyright 2009 Collabora Ltd,
 *   @contact: Olivier Crete <olivier.crete@collabora.co.uk>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "videomaxrate.h"

static GstStaticPadTemplate gst_video_max_rate_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-yuv; video/x-raw-rgb")
    );

static GstStaticPadTemplate gst_video_max_rate_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-yuv; video/x-raw-rgb")
    );

static gboolean gst_video_max_rate_sink_event (GstPad * pad, GstEvent * event);
static GstCaps *gst_video_max_rate_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps);
static gboolean gst_video_max_rate_set_caps (GstBaseTransform * trans,
    GstCaps * incaps, GstCaps * outcaps);
static GstFlowReturn gst_video_max_rate_transform_ip (GstBaseTransform * trans,
    GstBuffer * buf);

GST_BOILERPLATE (GstVideoMaxRate, gst_video_max_rate, GstBaseTransform,
    GST_TYPE_BASE_TRANSFORM);

static void
gst_video_max_rate_base_init (gpointer gclass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_set_details_simple (element_class,
      "Video maximum rate adjuster",
      "Filter/Effect/Video",
      "Drops extra frames", "Justin Karneges <justin@affinix.com>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_video_max_rate_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_video_max_rate_src_template));
}

static void
gst_video_max_rate_class_init (GstVideoMaxRateClass * klass)
{
  GstBaseTransformClass *base_class = GST_BASE_TRANSFORM_CLASS (klass);

  base_class->transform_caps = gst_video_max_rate_transform_caps;
  base_class->set_caps = gst_video_max_rate_set_caps;
  base_class->transform_ip = gst_video_max_rate_transform_ip;
}

static void
gst_video_max_rate_init (GstVideoMaxRate * videomaxrate,
    GstVideoMaxRateClass * gclass)
{
  videomaxrate->to_rate_numerator = -1;
  videomaxrate->to_rate_denominator = -1;
  videomaxrate->have_last_ts = FALSE;

  gst_pad_set_event_function (GST_BASE_TRANSFORM_SINK_PAD (videomaxrate),
      gst_video_max_rate_sink_event);
}

gboolean
gst_video_max_rate_sink_event (GstPad * pad, GstEvent * event)
{
  GstVideoMaxRate *videomaxrate = GST_VIDEO_MAX_RATE (gst_pad_get_parent (pad));
  gboolean ret;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:
    case GST_EVENT_FLUSH_STOP:
      videomaxrate->have_last_ts = FALSE;
      break;
    default:
      break;
  }

  ret = gst_pad_push_event (GST_BASE_TRANSFORM_SRC_PAD (videomaxrate), event);

  gst_object_unref (videomaxrate);
  return ret;
}

GstCaps *
gst_video_max_rate_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps)
{
  GstCaps *ret;
  GstStructure *structure;

  /* this function is always called with a simple caps */
  g_return_val_if_fail (GST_CAPS_IS_SIMPLE (caps), NULL);

  ret = gst_caps_copy (caps);

  /* set the framerate as a range */
  structure = gst_structure_copy (gst_caps_get_structure (ret, 0));
  gst_structure_set (structure, "framerate", GST_TYPE_FRACTION_RANGE, 0, 1,
      G_MAXINT, 1, NULL);
  gst_caps_merge_structure (ret, gst_structure_copy (structure));
  gst_structure_free (structure);

  return ret;
}

gboolean
gst_video_max_rate_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstVideoMaxRate *videomaxrate = GST_VIDEO_MAX_RATE (trans);
  GstStructure *cs;
  gint numerator, denominator;

  // keep track of the outbound framerate
  cs = gst_caps_get_structure (outcaps, 0);
  if (!gst_structure_get_fraction (cs, "framerate", &numerator, &denominator))
    return FALSE;

  videomaxrate->to_rate_numerator = numerator;
  videomaxrate->to_rate_denominator = denominator;

  return TRUE;
}

GstFlowReturn
gst_video_max_rate_transform_ip (GstBaseTransform * trans, GstBuffer * buf)
{
  GstVideoMaxRate *videomaxrate = GST_VIDEO_MAX_RATE (trans);
  GstClockTime ts = GST_BUFFER_TIMESTAMP (buf);

  /* drop frames if they exceed our output rate */
  if (videomaxrate->have_last_ts) {
    if (ts < videomaxrate->last_ts + gst_util_uint64_scale (1,
            videomaxrate->to_rate_denominator * GST_SECOND,
            videomaxrate->to_rate_numerator)) {
      return GST_BASE_TRANSFORM_FLOW_DROPPED;
    }
  }

  videomaxrate->last_ts = ts;
  videomaxrate->have_last_ts = TRUE;
  return GST_FLOW_OK;
}


static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "videomaxrate", GST_RANK_NONE,
      GST_TYPE_VIDEO_MAX_RATE);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "videomaxrate",
    "Drop extra frames",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
