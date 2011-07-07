/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *
 * EffecTV:
 * Copyright (C) 2001 FUKUCHI Kentarou
 *
 * EffecTV is free software. This library is free software;
 * you can redistribute it and/or
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
 * SECTION:element-vertigotv
 *
 * VertigoTV is a loopback alpha blending effector with rotating and scaling.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v videotestsrc ! vertigotv ! videoconvert ! autovideosink
 * ]| This pipeline shows the effect of vertigotv on a test stream.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <math.h>
#include <string.h>

#include "gstvertigo.h"

#include <gst/controller/gstcontroller.h>

#define gst_vertigotv_parent_class parent_class
G_DEFINE_TYPE (GstVertigoTV, gst_vertigotv, GST_TYPE_VIDEO_FILTER);

/* Filter signals and args */
enum
{
  PROP_0,
  PROP_SPEED,
  PROP_ZOOM_SPEED
};

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
#define CAPS_STR GST_VIDEO_CAPS_MAKE ("{ RGBx, BGRx }")
#else
#define CAPS_STR GST_VIDEO_CAPS_MAKE ("{ xRGB, xBGR }")
#endif

static GstStaticPadTemplate gst_vertigotv_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (CAPS_STR)
    );

static GstStaticPadTemplate gst_vertigotv_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (CAPS_STR)
    );

static gboolean
gst_vertigotv_set_caps (GstBaseTransform * btrans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstVertigoTV *filter = GST_VERTIGOTV (btrans);
  GstVideoInfo info;
  gint area, width, height;

  if (!gst_video_info_from_caps (&info, incaps))
    goto invalid_caps;

  filter->info = info;

  width = GST_VIDEO_INFO_WIDTH (&info);
  height = GST_VIDEO_INFO_HEIGHT (&info);

  area = width * height;

  g_free (filter->buffer);
  filter->buffer = (guint32 *) g_malloc0 (area * 2 * sizeof (guint32));

  filter->current_buffer = filter->buffer;
  filter->alt_buffer = filter->buffer + area;
  filter->phase = 0;

  return TRUE;

  /* ERRORS */
invalid_caps:
  {
    GST_DEBUG_OBJECT (filter, "invalid caps received");
    return FALSE;
  }
}

static void
gst_vertigotv_set_parms (GstVertigoTV * filter)
{
  double vx, vy;
  double t;
  double x, y;
  double dizz;
  gint width, height;

  dizz = sin (filter->phase) * 10 + sin (filter->phase * 1.9 + 5) * 5;

  width = GST_VIDEO_INFO_WIDTH (&filter->info);
  height = GST_VIDEO_INFO_HEIGHT (&filter->info);

  x = width / 2;
  y = height / 2;

  t = (x * x + y * y) * filter->zoomrate;

  if (width > height) {
    if (dizz >= 0) {
      if (dizz > x)
        dizz = x;
      vx = (x * (x - dizz) + y * y) / t;
    } else {
      if (dizz < -x)
        dizz = -x;
      vx = (x * (x + dizz) + y * y) / t;
    }
    vy = (dizz * y) / t;
  } else {
    if (dizz >= 0) {
      if (dizz > y)
        dizz = y;
      vx = (x * x + y * (y - dizz)) / t;
    } else {
      if (dizz < -y)
        dizz = -y;
      vx = (x * x + y * (y + dizz)) / t;
    }
    vy = (dizz * x) / t;
  }
  filter->dx = vx * 65536;
  filter->dy = vy * 65536;
  filter->sx = (-vx * x + vy * y + x + cos (filter->phase * 5) * 2) * 65536;
  filter->sy = (-vx * y - vy * x + y + sin (filter->phase * 6) * 2) * 65536;

  filter->phase += filter->phase_increment;
  if (filter->phase > 5700000)
    filter->phase = 0;
}

static GstFlowReturn
gst_vertigotv_transform (GstBaseTransform * trans, GstBuffer * in,
    GstBuffer * out)
{
  GstVertigoTV *filter = GST_VERTIGOTV (trans);
  guint32 *src, *dest, *p;
  guint32 v;
  gint x, y, ox, oy, i, width, height, area;
  GstClockTime timestamp, stream_time;
  GstVideoFrame in_frame, out_frame;

  timestamp = GST_BUFFER_TIMESTAMP (in);
  stream_time =
      gst_segment_to_stream_time (&trans->segment, GST_FORMAT_TIME, timestamp);

  GST_DEBUG_OBJECT (filter, "sync to %" GST_TIME_FORMAT,
      GST_TIME_ARGS (timestamp));

  if (GST_CLOCK_TIME_IS_VALID (stream_time))
    gst_object_sync_values (G_OBJECT (filter), stream_time);

  if (!gst_video_frame_map (&in_frame, &filter->info, in, GST_MAP_READ))
    goto invalid_in;

  if (!gst_video_frame_map (&out_frame, &filter->info, out, GST_MAP_WRITE))
    goto invalid_out;

  src = GST_VIDEO_FRAME_PLANE_DATA (&in_frame, 0);
  dest = GST_VIDEO_FRAME_PLANE_DATA (&in_frame, 0);

  width = GST_VIDEO_FRAME_WIDTH (&in_frame);
  height = GST_VIDEO_FRAME_HEIGHT (&in_frame);

  area = width * height;

  gst_vertigotv_set_parms (filter);
  p = filter->alt_buffer;

  for (y = height; y > 0; y--) {
    ox = filter->sx;
    oy = filter->sy;

    for (x = width; x > 0; x--) {
      i = (oy >> 16) * width + (ox >> 16);
      if (i < 0)
        i = 0;
      if (i >= area)
        i = area;

      v = filter->current_buffer[i] & 0xfcfcff;
      v = (v * 3) + ((*src++) & 0xfcfcff);

      *p++ = (v >> 2);
      ox += filter->dx;
      oy += filter->dy;
    }
    filter->sx -= filter->dy;
    filter->sy += filter->dx;
  }

  memcpy (dest, filter->alt_buffer, area * sizeof (guint32));

  p = filter->current_buffer;
  filter->current_buffer = filter->alt_buffer;
  filter->alt_buffer = p;

  return GST_FLOW_OK;

  /* ERRORS */
invalid_in:
  {
    GST_DEBUG_OBJECT (filter, "invalid input frame");
    return GST_FLOW_ERROR;
  }
invalid_out:
  {
    GST_DEBUG_OBJECT (filter, "invalid output frame");
    gst_video_frame_unmap (&in_frame);
    return GST_FLOW_ERROR;
  }

}

static gboolean
gst_vertigotv_start (GstBaseTransform * trans)
{
  GstVertigoTV *filter = GST_VERTIGOTV (trans);

  filter->phase = 0.0;

  return TRUE;
}

static void
gst_vertigotv_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVertigoTV *filter = GST_VERTIGOTV (object);

  GST_OBJECT_LOCK (filter);
  switch (prop_id) {
    case PROP_SPEED:
      filter->phase_increment = g_value_get_float (value);
      break;
    case PROP_ZOOM_SPEED:
      filter->zoomrate = g_value_get_float (value);
      break;
    default:
      break;
  }
  GST_OBJECT_UNLOCK (filter);
}

static void
gst_vertigotv_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstVertigoTV *filter = GST_VERTIGOTV (object);

  switch (prop_id) {
    case PROP_SPEED:
      g_value_set_float (value, filter->phase_increment);
      break;
    case PROP_ZOOM_SPEED:
      g_value_set_float (value, filter->zoomrate);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vertigotv_finalize (GObject * object)
{
  GstVertigoTV *filter = GST_VERTIGOTV (object);

  g_free (filter->buffer);
  filter->buffer = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_vertigotv_class_init (GstVertigoTVClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;
  GstBaseTransformClass *trans_class = (GstBaseTransformClass *) klass;

  gobject_class->set_property = gst_vertigotv_set_property;
  gobject_class->get_property = gst_vertigotv_get_property;
  gobject_class->finalize = gst_vertigotv_finalize;

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SPEED,
      g_param_spec_float ("speed", "Speed", "Control the speed of movement",
          0.01, 100.0, 0.02, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_ZOOM_SPEED,
      g_param_spec_float ("zoom-speed", "Zoom Speed",
          "Control the rate of zooming", 1.01, 1.1, 1.01,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_details_simple (gstelement_class, "VertigoTV effect",
      "Filter/Effect/Video",
      "A loopback alpha blending effector with rotating and scaling",
      "Wim Taymans <wim.taymans@gmail.be>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_vertigotv_sink_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_vertigotv_src_template));

  trans_class->start = GST_DEBUG_FUNCPTR (gst_vertigotv_start);
  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_vertigotv_set_caps);
  trans_class->transform = GST_DEBUG_FUNCPTR (gst_vertigotv_transform);
}

static void
gst_vertigotv_init (GstVertigoTV * filter)
{
  filter->buffer = NULL;
  filter->phase = 0.0;
  filter->phase_increment = 0.02;
  filter->zoomrate = 1.01;

  gst_pad_use_fixed_caps (GST_BASE_TRANSFORM_SRC_PAD (filter));
  gst_pad_use_fixed_caps (GST_BASE_TRANSFORM_SINK_PAD (filter));
}
