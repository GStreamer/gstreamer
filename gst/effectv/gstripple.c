/* GStreamer
 * Copyright (C) <2009> Sebastian Dröge <sebastian.droege@collabora.co.uk>
 *
 * EffecTV - Realtime Digital Video Effector
 * Copyright (C) 2001-2006 FUKUCHI Kentaro
 *
 * RippleTV - Water ripple effect.
 * Copyright (C) 2001-2002 FUKUCHI Kentaro
 *
 * This combines the RippleTV and BaltanTV effects, which are
 * very similar. BaltanTV is used if the feedback property is set
 * to TRUE, otherwise RippleTV is used.
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
 * SECTION:element-rippletv
 *
 * RippleTV does ripple mark effect on the video input. The ripple is caused
 * by motion or random rain drops.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v videotestsrc ! rippletv ! videoconvert ! autovideosink
 * ]| This pipeline shows the effect of rippletv on a test stream.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <math.h>
#include <string.h>

#include "gstripple.h"
#include "gsteffectv.h"

#include <gst/controller/gstcontroller.h>

#define DEFAULT_MODE 0

enum
{
  PROP_0,
  PROP_RESET,
  PROP_MODE
};

static gint sqrtable[256];

#define GST_TYPE_RIPPLETV_MODE (gst_rippletv_mode_get_type())
static GType
gst_rippletv_mode_get_type (void)
{
  static GType type = 0;

  static const GEnumValue enumvalue[] = {
    {0, "Motion Detection", "motion-detection"},
    {1, "Rain", "rain"},
    {0, NULL, NULL},
  };

  if (!type) {
    type = g_enum_register_static ("GstRippleTVMode", enumvalue);
  }
  return type;
}

#define gst_rippletv_parent_class parent_class
G_DEFINE_TYPE (GstRippleTV, gst_rippletv, GST_TYPE_VIDEO_FILTER);

static GstStaticPadTemplate gst_rippletv_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{ BGRx, RGBx, xBGR, xRGB }"))
    );

static GstStaticPadTemplate gst_rippletv_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{ BGRx, RGBx, xBGR, xRGB }"))
    );

static const gint point = 16;
static const gint impact = 2;
static const gint decay = 8;
static const gint loopnum = 2;

static void
setTable (void)
{
  gint i;

  for (i = 0; i < 128; i++) {
    sqrtable[i] = i * i;
  }
  for (i = 1; i <= 128; i++) {
    sqrtable[256 - i] = -i * i;
  }
}

static void
image_bgset_y (guint32 * src, gint16 * background, gint video_area)
{
  gint i;
  gint R, G, B;
  guint32 *p;
  gint16 *q;

  p = src;
  q = background;
  for (i = 0; i < video_area; i++) {
    R = ((*p) & 0xff0000) >> (16 - 1);
    G = ((*p) & 0xff00) >> (8 - 2);
    B = (*p) & 0xff;
    *q = (gint16) (R + G + B);
    p++;
    q++;
  }
}

static gint
setBackground (GstRippleTV * filter, guint32 * src)
{
  image_bgset_y (src, filter->background,
      GST_VIDEO_INFO_WIDTH (&filter->info) *
      GST_VIDEO_INFO_HEIGHT (&filter->info));
  filter->bg_is_set = TRUE;

  return 0;
}

static void
image_bgsubtract_update_y (guint32 * src, gint16 * background, guint8 * diff,
    gint video_area)
{
  gint i;
  gint R, G, B;
  guint32 *p;
  gint16 *q;
  guint8 *r;
  gint v;

  p = src;
  q = background;
  r = diff;
  for (i = 0; i < video_area; i++) {
    R = ((*p) & 0xff0000) >> (16 - 1);
    G = ((*p) & 0xff00) >> (8 - 2);
    B = (*p) & 0xff;
    v = (R + G + B) - (gint) (*q);
    *q = (gint16) (R + G + B);
    *r = ((v + 70 * 7) >> 24) | ((70 * 7 - v) >> 24);

    p++;
    q++;
    r++;
  }
}

static void
motiondetect (GstRippleTV * filter, guint32 * src)
{
  guint8 *diff = filter->diff;
  gint width, height;
  gint *p, *q;
  gint x, y, h;

  width = GST_VIDEO_INFO_WIDTH (&filter->info);
  height = GST_VIDEO_INFO_HEIGHT (&filter->info);

  if (!filter->bg_is_set)
    setBackground (filter, src);

  image_bgsubtract_update_y (src, filter->background, filter->diff,
      width * height);
  p = filter->map1 + filter->map_w + 1;
  q = filter->map2 + filter->map_w + 1;
  diff += width + 2;

  for (y = filter->map_h - 2; y > 0; y--) {
    for (x = filter->map_w - 2; x > 0; x--) {
      h = (gint) * diff + (gint) * (diff + 1) + (gint) * (diff + width) +
          (gint) * (diff + width + 1);
      if (h > 0) {
        *p = h << (point + impact - 8);
        *q = *p;
      }
      p++;
      q++;
      diff += 2;
    }
    diff += width + 2;
    p += 2;
    q += 2;
  }
}

static inline void
drop (gint power, gint * map1, gint * map2, gint map_w, gint map_h)
{
  gint x, y;
  gint *p, *q;

  x = fastrand () % (map_w - 4) + 2;
  y = fastrand () % (map_h - 4) + 2;
  p = map1 + y * map_w + x;
  q = map2 + y * map_w + x;
  *p = power;
  *q = power;
  *(p - map_w) = *(p - 1) = *(p + 1) = *(p + map_w) = power / 2;
  *(p - map_w - 1) = *(p - map_w + 1) = *(p + map_w - 1) = *(p + map_w + 1) =
      power / 4;
  *(q - map_w) = *(q - 1) = *(q + 1) = *(q + map_w) = power / 2;
  *(q - map_w - 1) = *(q - map_w + 1) = *(q + map_w - 1) = *(p + map_w + 1) =
      power / 4;
}

static void
raindrop (GstRippleTV * filter)
{
  gint i;

  if (filter->period == 0) {
    switch (filter->rain_stat) {
      case 0:
        filter->period = (fastrand () >> 23) + 100;
        filter->drop_prob = 0;
        filter->drop_prob_increment = 0x00ffffff / filter->period;
        filter->drop_power = (-(fastrand () >> 28) - 2) << point;
        filter->drops_per_frame_max = 2 << (fastrand () >> 30); // 2,4,8 or 16
        filter->rain_stat = 1;
        break;
      case 1:
        filter->drop_prob = 0x00ffffff;
        filter->drops_per_frame = 1;
        filter->drop_prob_increment = 1;
        filter->period = (filter->drops_per_frame_max - 1) * 16;
        filter->rain_stat = 2;
        break;
      case 2:
        filter->period = (fastrand () >> 22) + 1000;
        filter->drop_prob_increment = 0;
        filter->rain_stat = 3;
        break;
      case 3:
        filter->period = (filter->drops_per_frame_max - 1) * 16;
        filter->drop_prob_increment = -1;
        filter->rain_stat = 4;
        break;
      case 4:
        filter->period = (fastrand () >> 24) + 60;
        filter->drop_prob_increment = -(filter->drop_prob / filter->period);
        filter->rain_stat = 5;
        break;
      case 5:
      default:
        filter->period = (fastrand () >> 23) + 500;
        filter->drop_prob = 0;
        filter->rain_stat = 0;
        break;
    }
  }
  switch (filter->rain_stat) {
    default:
    case 0:
      break;
    case 1:
    case 5:
      if ((fastrand () >> 8) < filter->drop_prob) {
        drop (filter->drop_power, filter->map1, filter->map2, filter->map_w,
            filter->map_h);
      }
      filter->drop_prob += filter->drop_prob_increment;
      break;
    case 2:
    case 3:
    case 4:
      for (i = filter->drops_per_frame / 16; i > 0; i--) {
        drop (filter->drop_power, filter->map1, filter->map2, filter->map_w,
            filter->map_h);
      }
      filter->drops_per_frame += filter->drop_prob_increment;
      break;
  }
  filter->period--;
}

static GstFlowReturn
gst_rippletv_transform (GstBaseTransform * trans, GstBuffer * in,
    GstBuffer * out)
{
  GstRippleTV *filter = GST_RIPPLETV (trans);
  guint32 *src, *dest;
  GstVideoFrame in_frame, out_frame;
  gint x, y, i;
  gint dx, dy;
  gint h, v;
  gint width, height;
  gint *p, *q, *r;
  gint8 *vp;
  GstClockTime timestamp, stream_time;

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
  dest = GST_VIDEO_FRAME_PLANE_DATA (&out_frame, 0);

  GST_OBJECT_LOCK (filter);
  /* impact from the motion or rain drop */
  if (filter->mode)
    raindrop (filter);
  else
    motiondetect (filter, src);

  /* simulate surface wave */
  width = filter->map_w;
  height = filter->map_h;

  /* This function is called only 30 times per second. To increase a speed
   * of wave, iterates this loop several times. */
  for (i = loopnum; i > 0; i--) {
    /* wave simulation */
    p = filter->map1 + width + 1;
    q = filter->map2 + width + 1;
    r = filter->map3 + width + 1;
    for (y = height - 2; y > 0; y--) {
      for (x = width - 2; x > 0; x--) {
        h = *(p - width - 1) + *(p - width + 1) + *(p + width - 1) + *(p +
            width + 1)
            + *(p - width) + *(p - 1) + *(p + 1) + *(p + width) - (*p) * 9;
        h = h >> 3;
        v = *p - *q;
        v += h - (v >> decay);
        *r = v + *p;
        p++;
        q++;
        r++;
      }
      p += 2;
      q += 2;
      r += 2;
    }

    /* low pass filter */
    p = filter->map3 + width + 1;
    q = filter->map2 + width + 1;
    for (y = height - 2; y > 0; y--) {
      for (x = width - 2; x > 0; x--) {
        h = *(p - width) + *(p - 1) + *(p + 1) + *(p + width) + (*p) * 60;
        *q = h >> 6;
        p++;
        q++;
      }
      p += 2;
      q += 2;
    }

    p = filter->map1;
    filter->map1 = filter->map2;
    filter->map2 = p;
  }

  vp = filter->vtable;
  p = filter->map1;
  for (y = height - 1; y > 0; y--) {
    for (x = width - 1; x > 0; x--) {
      /* difference of the height between two voxel. They are twiced to
       * emphasise the wave. */
      vp[0] = sqrtable[((p[0] - p[1]) >> (point - 1)) & 0xff];
      vp[1] = sqrtable[((p[0] - p[width]) >> (point - 1)) & 0xff];
      p++;
      vp += 2;
    }
    p++;
    vp += 2;
  }

  width = GST_VIDEO_FRAME_WIDTH (&in_frame);
  height = GST_VIDEO_FRAME_HEIGHT (&in_frame);
  vp = filter->vtable;

  /* draw refracted image. The vector table is stretched. */
  for (y = 0; y < height; y += 2) {
    for (x = 0; x < width; x += 2) {
      h = (gint) vp[0];
      v = (gint) vp[1];
      dx = x + h;
      dy = y + v;
      if (dx < 0)
        dx = 0;
      if (dy < 0)
        dy = 0;
      if (dx >= width)
        dx = width - 1;
      if (dy >= height)
        dy = height - 1;
      dest[0] = src[dy * width + dx];

      i = dx;

      dx = x + 1 + (h + (gint) vp[2]) / 2;
      if (dx < 0)
        dx = 0;
      if (dx >= width)
        dx = width - 1;
      dest[1] = src[dy * width + dx];

      dy = y + 1 + (v + (gint) vp[filter->map_w * 2 + 1]) / 2;
      if (dy < 0)
        dy = 0;
      if (dy >= height)
        dy = height - 1;
      dest[width] = src[dy * width + i];

      dest[width + 1] = src[dy * width + dx];
      dest += 2;
      vp += 2;
    }
    dest += width;
    vp += 2;
  }
  GST_OBJECT_UNLOCK (filter);

  gst_video_frame_unmap (&in_frame);
  gst_video_frame_unmap (&out_frame);

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
gst_rippletv_set_caps (GstBaseTransform * btrans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstRippleTV *filter = GST_RIPPLETV (btrans);
  GstVideoInfo info;
  gint width, height;

  if (!gst_video_info_from_caps (&info, incaps))
    goto invalid_caps;

  filter->info = info;

  width = GST_VIDEO_INFO_WIDTH (&info);
  height = GST_VIDEO_INFO_HEIGHT (&info);

  GST_OBJECT_LOCK (filter);
  filter->map_h = height / 2 + 1;
  filter->map_w = width / 2 + 1;

  if (filter->map)
    g_free (filter->map);
  filter->map = g_new0 (gint, filter->map_h * filter->map_w * 3);

  filter->map1 = filter->map;
  filter->map2 = filter->map + filter->map_w * filter->map_h;
  filter->map3 = filter->map + filter->map_w * filter->map_h * 2;

  if (filter->vtable)
    g_free (filter->vtable);
  filter->vtable = g_new0 (gint8, filter->map_h * filter->map_w * 2);

  if (filter->background)
    g_free (filter->background);
  filter->background = g_new0 (gint16, width * height);

  if (filter->diff)
    g_free (filter->diff);
  filter->diff = g_new0 (guint8, width * height);
  GST_OBJECT_UNLOCK (filter);

  return TRUE;

  /* ERRORS */
invalid_caps:
  {
    GST_DEBUG_OBJECT (filter, "invalid caps received");
    return FALSE;
  }
}

static gboolean
gst_rippletv_start (GstBaseTransform * trans)
{
  GstRippleTV *filter = GST_RIPPLETV (trans);

  filter->bg_is_set = FALSE;

  filter->period = 0;
  filter->rain_stat = 0;
  filter->drop_prob = 0;
  filter->drop_prob_increment = 0;
  filter->drops_per_frame_max = 0;
  filter->drops_per_frame = 0;
  filter->drop_power = 0;

  return TRUE;
}

static void
gst_rippletv_finalize (GObject * object)
{
  GstRippleTV *filter = GST_RIPPLETV (object);

  if (filter->map)
    g_free (filter->map);
  filter->map = NULL;

  if (filter->vtable)
    g_free (filter->vtable);
  filter->vtable = NULL;

  if (filter->background)
    g_free (filter->background);
  filter->background = NULL;

  if (filter->diff)
    g_free (filter->diff);
  filter->diff = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_rippletv_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRippleTV *filter = GST_RIPPLETV (object);

  GST_OBJECT_LOCK (filter);
  switch (prop_id) {
    case PROP_RESET:{
      memset (filter->map, 0,
          filter->map_h * filter->map_w * 2 * sizeof (gint));
      break;
    }
    case PROP_MODE:
      filter->mode = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (filter);
}

static void
gst_rippletv_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstRippleTV *filter = GST_RIPPLETV (object);

  switch (prop_id) {
    case PROP_MODE:
      g_value_set_enum (value, filter->mode);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rippletv_class_init (GstRippleTVClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;
  GstBaseTransformClass *trans_class = (GstBaseTransformClass *) klass;

  gobject_class->set_property = gst_rippletv_set_property;
  gobject_class->get_property = gst_rippletv_get_property;

  gobject_class->finalize = gst_rippletv_finalize;

  g_object_class_install_property (gobject_class, PROP_RESET,
      g_param_spec_boolean ("reset", "Reset",
          "Reset all current ripples", FALSE,
          G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS | GST_PARAM_CONTROLLABLE));

  g_object_class_install_property (gobject_class, PROP_MODE,
      g_param_spec_enum ("mode", "Mode",
          "Mode", GST_TYPE_RIPPLETV_MODE, DEFAULT_MODE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_CONTROLLABLE));

  gst_element_class_set_details_simple (gstelement_class, "RippleTV effect",
      "Filter/Effect/Video",
      "RippleTV does ripple mark effect on the video input",
      "FUKUCHI, Kentarou <fukuchi@users.sourceforge.net>, "
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_rippletv_sink_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_rippletv_src_template));

  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_rippletv_set_caps);
  trans_class->transform = GST_DEBUG_FUNCPTR (gst_rippletv_transform);
  trans_class->start = GST_DEBUG_FUNCPTR (gst_rippletv_start);

  setTable ();
}

static void
gst_rippletv_init (GstRippleTV * filter)
{
  filter->mode = DEFAULT_MODE;

  gst_pad_use_fixed_caps (GST_BASE_TRANSFORM_SRC_PAD (filter));
  gst_pad_use_fixed_caps (GST_BASE_TRANSFORM_SINK_PAD (filter));
}
