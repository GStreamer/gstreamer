/* GStreamer
 *
 * Copyright (C) 2001-2002 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *               2006 Edgard Lima <edgard.lima@indt.org.br>
 *
 * gstv4l2src.c: Video4Linux2 source element
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:element-v4l2src
 *
 * v4l2src can be used to capture video from v4l2 devices, like webcams and tv
 * cards.
 *
 * <refsect2>
 * <title>Example launch lines</title>
 * |[
 * gst-launch-1.0 v4l2src ! xvimagesink
 * ]| This pipeline shows the video captured from /dev/video0 tv card and for
 * webcams.
 * |[
 * gst-launch-1.0 v4l2src ! jpegdec ! xvimagesink
 * ]| This pipeline shows the video captured from a webcam that delivers jpeg
 * images.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include <gst/video/gstvideometa.h>
#include <gst/video/gstvideopool.h>

#include "gstv4l2src.h"

#include "gstv4l2colorbalance.h"
#include "gstv4l2tuner.h"
#include "gstv4l2vidorient.h"

#include "gst/gst-i18n-plugin.h"

GST_DEBUG_CATEGORY (v4l2src_debug);
#define GST_CAT_DEFAULT v4l2src_debug

#define DEFAULT_PROP_DEVICE   "/dev/video0"

enum
{
  PROP_0,
  V4L2_STD_OBJECT_PROPS,
  PROP_LAST
};

/* signals and args */
enum
{
  SIGNAL_PRE_SET_FORMAT,
  LAST_SIGNAL
};

static guint gst_v4l2_signals[LAST_SIGNAL] = { 0 };

GST_IMPLEMENT_V4L2_COLOR_BALANCE_METHODS (GstV4l2Src, gst_v4l2src);
GST_IMPLEMENT_V4L2_TUNER_METHODS (GstV4l2Src, gst_v4l2src);
GST_IMPLEMENT_V4L2_VIDORIENT_METHODS (GstV4l2Src, gst_v4l2src);

static void gst_v4l2src_uri_handler_init (gpointer g_iface,
    gpointer iface_data);

#define gst_v4l2src_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstV4l2Src, gst_v4l2src, GST_TYPE_PUSH_SRC,
    G_IMPLEMENT_INTERFACE (GST_TYPE_URI_HANDLER, gst_v4l2src_uri_handler_init);
    G_IMPLEMENT_INTERFACE (GST_TYPE_TUNER, gst_v4l2src_tuner_interface_init);
    G_IMPLEMENT_INTERFACE (GST_TYPE_COLOR_BALANCE,
        gst_v4l2src_color_balance_interface_init);
    G_IMPLEMENT_INTERFACE (GST_TYPE_VIDEO_ORIENTATION,
        gst_v4l2src_video_orientation_interface_init));

static void gst_v4l2src_finalize (GstV4l2Src * v4l2src);

/* element methods */
static GstStateChangeReturn gst_v4l2src_change_state (GstElement * element,
    GstStateChange transition);

/* basesrc methods */
static gboolean gst_v4l2src_start (GstBaseSrc * src);
static gboolean gst_v4l2src_unlock (GstBaseSrc * src);
static gboolean gst_v4l2src_unlock_stop (GstBaseSrc * src);
static gboolean gst_v4l2src_stop (GstBaseSrc * src);
static gboolean gst_v4l2src_set_caps (GstBaseSrc * src, GstCaps * caps);
static GstCaps *gst_v4l2src_get_caps (GstBaseSrc * src, GstCaps * filter);
static gboolean gst_v4l2src_query (GstBaseSrc * bsrc, GstQuery * query);
static gboolean gst_v4l2src_decide_allocation (GstBaseSrc * src,
    GstQuery * query);
static GstFlowReturn gst_v4l2src_create (GstPushSrc * src, GstBuffer ** out);
static GstCaps *gst_v4l2src_fixate (GstBaseSrc * basesrc, GstCaps * caps);
static gboolean gst_v4l2src_negotiate (GstBaseSrc * basesrc);

static void gst_v4l2src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_v4l2src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void
gst_v4l2src_class_init (GstV4l2SrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstBaseSrcClass *basesrc_class;
  GstPushSrcClass *pushsrc_class;

  gobject_class = G_OBJECT_CLASS (klass);
  element_class = GST_ELEMENT_CLASS (klass);
  basesrc_class = GST_BASE_SRC_CLASS (klass);
  pushsrc_class = GST_PUSH_SRC_CLASS (klass);

  gobject_class->finalize = (GObjectFinalizeFunc) gst_v4l2src_finalize;
  gobject_class->set_property = gst_v4l2src_set_property;
  gobject_class->get_property = gst_v4l2src_get_property;

  element_class->change_state = gst_v4l2src_change_state;

  gst_v4l2_object_install_properties_helper (gobject_class,
      DEFAULT_PROP_DEVICE);

  /**
   * GstV4l2Src::prepare-format:
   * @v4l2src: the v4l2src instance
   * @fd: the file descriptor of the current device
   * @caps: the caps of the format being set
   *
   * This signal gets emitted before calling the v4l2 VIDIOC_S_FMT ioctl
   * (set format). This allows for any custom configuration of the device to
   * happen prior to the format being set.
   * This is mostly useful for UVC H264 encoding cameras which need the H264
   * Probe & Commit to happen prior to the normal Probe & Commit.
   *
   * Since: 0.10.32
   */
  gst_v4l2_signals[SIGNAL_PRE_SET_FORMAT] = g_signal_new ("prepare-format",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      0, NULL, NULL, NULL, G_TYPE_NONE, 2, G_TYPE_INT, GST_TYPE_CAPS);

  gst_element_class_set_static_metadata (element_class,
      "Video (video4linux2) Source", "Source/Video",
      "Reads frames from a Video4Linux2 device",
      "Edgard Lima <edgard.lima@indt.org.br>, "
      "Stefan Kost <ensonic@users.sf.net>");

  gst_element_class_add_pad_template
      (element_class,
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
          gst_v4l2_object_get_all_caps ()));

  basesrc_class->get_caps = GST_DEBUG_FUNCPTR (gst_v4l2src_get_caps);
  basesrc_class->set_caps = GST_DEBUG_FUNCPTR (gst_v4l2src_set_caps);
  basesrc_class->start = GST_DEBUG_FUNCPTR (gst_v4l2src_start);
  basesrc_class->unlock = GST_DEBUG_FUNCPTR (gst_v4l2src_unlock);
  basesrc_class->unlock_stop = GST_DEBUG_FUNCPTR (gst_v4l2src_unlock_stop);
  basesrc_class->stop = GST_DEBUG_FUNCPTR (gst_v4l2src_stop);
  basesrc_class->query = GST_DEBUG_FUNCPTR (gst_v4l2src_query);
  basesrc_class->fixate = GST_DEBUG_FUNCPTR (gst_v4l2src_fixate);
  basesrc_class->negotiate = GST_DEBUG_FUNCPTR (gst_v4l2src_negotiate);
  basesrc_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_v4l2src_decide_allocation);

  pushsrc_class->create = GST_DEBUG_FUNCPTR (gst_v4l2src_create);

  klass->v4l2_class_devices = NULL;

  GST_DEBUG_CATEGORY_INIT (v4l2src_debug, "v4l2src", 0, "V4L2 source element");
}

static void
gst_v4l2src_init (GstV4l2Src * v4l2src)
{
  /* fixme: give an update_fps_function */
  v4l2src->v4l2object = gst_v4l2_object_new (GST_ELEMENT (v4l2src),
      V4L2_BUF_TYPE_VIDEO_CAPTURE, DEFAULT_PROP_DEVICE,
      gst_v4l2_get_input, gst_v4l2_set_input, NULL);

  gst_base_src_set_format (GST_BASE_SRC (v4l2src), GST_FORMAT_TIME);
  gst_base_src_set_live (GST_BASE_SRC (v4l2src), TRUE);
}


static void
gst_v4l2src_finalize (GstV4l2Src * v4l2src)
{
  gst_v4l2_object_destroy (v4l2src->v4l2object);

  G_OBJECT_CLASS (parent_class)->finalize ((GObject *) (v4l2src));
}


static void
gst_v4l2src_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstV4l2Src *v4l2src = GST_V4L2SRC (object);

  if (!gst_v4l2_object_set_property_helper (v4l2src->v4l2object,
          prop_id, value, pspec)) {
    switch (prop_id) {
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
  }
}

static void
gst_v4l2src_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstV4l2Src *v4l2src = GST_V4L2SRC (object);

  if (!gst_v4l2_object_get_property_helper (v4l2src->v4l2object,
          prop_id, value, pspec)) {
    switch (prop_id) {
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
  }
}

/* this function is a bit of a last resort */
static GstCaps *
gst_v4l2src_fixate (GstBaseSrc * basesrc, GstCaps * caps)
{
  GstStructure *structure;
  gint i;

  GST_DEBUG_OBJECT (basesrc, "fixating caps %" GST_PTR_FORMAT, caps);

  caps = gst_caps_make_writable (caps);

  for (i = 0; i < gst_caps_get_size (caps); ++i) {
    structure = gst_caps_get_structure (caps, i);

    /* We are fixating to a reasonable 320x200 resolution
       and the maximum framerate resolution for that size */
    if (gst_structure_has_field (structure, "width"))
      gst_structure_fixate_field_nearest_int (structure, "width", 320);

    if (gst_structure_has_field (structure, "height"))
      gst_structure_fixate_field_nearest_int (structure, "height", 200);

    if (gst_structure_has_field (structure, "framerate"))
      gst_structure_fixate_field_nearest_fraction (structure, "framerate",
          G_MAXINT, 1);

    if (gst_structure_has_field (structure, "format"))
      gst_structure_fixate_field (structure, "format");
  }

  GST_DEBUG_OBJECT (basesrc, "fixated caps %" GST_PTR_FORMAT, caps);

  caps = GST_BASE_SRC_CLASS (parent_class)->fixate (basesrc, caps);

  return caps;
}


static gboolean
gst_v4l2src_negotiate (GstBaseSrc * basesrc)
{
  GstV4l2Src *v4l2src;
  GstV4l2Object *obj;
  GstCaps *thiscaps;
  GstCaps *caps = NULL;
  GstCaps *peercaps = NULL;
  gboolean result = FALSE;

  v4l2src = GST_V4L2SRC (basesrc);
  obj = v4l2src->v4l2object;

  /* We don't allow renegotiation, just return TRUE in that case */
  if (GST_V4L2_IS_ACTIVE (obj))
    return TRUE;

  /* first see what is possible on our source pad */
  thiscaps = gst_pad_query_caps (GST_BASE_SRC_PAD (basesrc), NULL);
  GST_DEBUG_OBJECT (basesrc, "caps of src: %" GST_PTR_FORMAT, thiscaps);
  LOG_CAPS (basesrc, thiscaps);

  /* nothing or anything is allowed, we're done */
  if (thiscaps == NULL || gst_caps_is_any (thiscaps))
    goto no_nego_needed;

  /* get the peer caps without a filter as we'll filter ourselves later on */
  peercaps = gst_pad_peer_query_caps (GST_BASE_SRC_PAD (basesrc), NULL);
  GST_DEBUG_OBJECT (basesrc, "caps of peer: %" GST_PTR_FORMAT, peercaps);
  LOG_CAPS (basesrc, peercaps);
  if (peercaps && !gst_caps_is_any (peercaps)) {
    GstCaps *icaps = NULL;
    int i;

    /* Prefer the first caps we are compatible with that the peer proposed */
    for (i = 0; i < gst_caps_get_size (peercaps); i++) {
      /* get intersection */
      GstCaps *ipcaps = gst_caps_copy_nth (peercaps, i);

      GST_DEBUG_OBJECT (basesrc, "peer: %" GST_PTR_FORMAT, ipcaps);
      LOG_CAPS (basesrc, ipcaps);

      icaps = gst_caps_intersect (thiscaps, ipcaps);
      gst_caps_unref (ipcaps);

      if (!gst_caps_is_empty (icaps))
        break;

      gst_caps_unref (icaps);
      icaps = NULL;
    }

    GST_DEBUG_OBJECT (basesrc, "intersect: %" GST_PTR_FORMAT, icaps);
    LOG_CAPS (basesrc, icaps);
    if (icaps) {
      /* If there are multiple intersections pick the one with the smallest
       * resolution strictly bigger then the first peer caps */
      if (gst_caps_get_size (icaps) > 1) {
        GstStructure *s = gst_caps_get_structure (peercaps, 0);
        int best = 0;
        int twidth, theight;
        int width = G_MAXINT, height = G_MAXINT;

        if (gst_structure_get_int (s, "width", &twidth)
            && gst_structure_get_int (s, "height", &theight)) {

          /* Walk the structure backwards to get the first entry of the
           * smallest resolution bigger (or equal to) the preferred resolution)
           */
          for (i = gst_caps_get_size (icaps) - 1; i >= 0; i--) {
            GstStructure *is = gst_caps_get_structure (icaps, i);
            int w, h;

            if (gst_structure_get_int (is, "width", &w)
                && gst_structure_get_int (is, "height", &h)) {
              if (w >= twidth && w <= width && h >= theight && h <= height) {
                width = w;
                height = h;
                best = i;
              }
            }
          }
        }

        caps = gst_caps_copy_nth (icaps, best);
        gst_caps_unref (icaps);
      } else {
        caps = icaps;
      }
    }
    gst_caps_unref (thiscaps);
  } else {
    /* no peer or peer have ANY caps, work with our own caps then */
    caps = thiscaps;
  }
  if (peercaps)
    gst_caps_unref (peercaps);
  if (caps) {
    caps = gst_caps_truncate (caps);

    /* now fixate */
    if (!gst_caps_is_empty (caps)) {
      caps = gst_v4l2src_fixate (basesrc, caps);
      GST_DEBUG_OBJECT (basesrc, "fixated to: %" GST_PTR_FORMAT, caps);
      LOG_CAPS (basesrc, caps);

      if (gst_caps_is_any (caps)) {
        /* hmm, still anything, so element can do anything and
         * nego is not needed */
        result = TRUE;
      } else if (gst_caps_is_fixed (caps)) {
        /* yay, fixed caps, use those then */
        result = gst_base_src_set_caps (basesrc, caps);
      }
    }
    gst_caps_unref (caps);
  }
  return result;

no_nego_needed:
  {
    GST_DEBUG_OBJECT (basesrc, "no negotiation needed");
    if (thiscaps)
      gst_caps_unref (thiscaps);
    return TRUE;
  }
}

static GstCaps *
gst_v4l2src_get_caps (GstBaseSrc * src, GstCaps * filter)
{
  GstV4l2Src *v4l2src;
  GstV4l2Object *obj;

  v4l2src = GST_V4L2SRC (src);
  obj = v4l2src->v4l2object;

  if (!GST_V4L2_IS_OPEN (obj)) {
    return gst_pad_get_pad_template_caps (GST_BASE_SRC_PAD (v4l2src));
  }

  return gst_v4l2_object_get_caps (obj, filter);
}

static gboolean
gst_v4l2src_set_caps (GstBaseSrc * src, GstCaps * caps)
{
  GstV4l2Src *v4l2src;
  GstV4l2Object *obj;

  v4l2src = GST_V4L2SRC (src);
  obj = v4l2src->v4l2object;

  /* make sure the caps changed before doing anything */
  if (gst_v4l2_object_caps_equal (obj, caps))
    return TRUE;

  /* make sure we stop capturing and dealloc buffers */
  if (!gst_v4l2_object_stop (obj))
    return FALSE;

  g_signal_emit (v4l2src, gst_v4l2_signals[SIGNAL_PRE_SET_FORMAT], 0,
      v4l2src->v4l2object->video_fd, caps);

  if (!gst_v4l2_object_set_format (obj, caps))
    /* error already posted */
    return FALSE;

  return TRUE;
}

static gboolean
gst_v4l2src_decide_allocation (GstBaseSrc * bsrc, GstQuery * query)
{
  GstV4l2Src *src = GST_V4L2SRC (bsrc);
  gboolean ret = FALSE;

  if (gst_v4l2_object_decide_allocation (src->v4l2object, query))
    ret = GST_BASE_SRC_CLASS (parent_class)->decide_allocation (bsrc, query);

  if (ret) {
    if (!gst_buffer_pool_set_active (src->v4l2object->pool, TRUE))
      goto activate_failed;
  }

  return ret;

activate_failed:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, SETTINGS,
        (_("Failed to allocate required memory.")),
        ("Buffer pool activation failed"));
    return FALSE;
  }
}

static gboolean
gst_v4l2src_query (GstBaseSrc * bsrc, GstQuery * query)
{
  GstV4l2Src *src;
  GstV4l2Object *obj;
  gboolean res = FALSE;

  src = GST_V4L2SRC (bsrc);
  obj = src->v4l2object;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:{
      GstClockTime min_latency, max_latency;
      guint32 fps_n, fps_d;
      guint num_buffers = 0;

      /* device must be open */
      if (!GST_V4L2_IS_OPEN (obj)) {
        GST_WARNING_OBJECT (src,
            "Can't give latency since device isn't open !");
        goto done;
      }

      fps_n = GST_V4L2_FPS_N (obj);
      fps_d = GST_V4L2_FPS_D (obj);

      /* we must have a framerate */
      if (fps_n <= 0 || fps_d <= 0) {
        GST_WARNING_OBJECT (src,
            "Can't give latency since framerate isn't fixated !");
        goto done;
      }

      /* min latency is the time to capture one frame */
      min_latency = gst_util_uint64_scale_int (GST_SECOND, fps_d, fps_n);

      /* max latency is total duration of the frame buffer */
      if (obj->pool != NULL)
        num_buffers = GST_V4L2_BUFFER_POOL_CAST (obj->pool)->max_latency;

      if (num_buffers == 0)
        max_latency = -1;
      else
        max_latency = num_buffers * min_latency;

      GST_DEBUG_OBJECT (bsrc,
          "report latency min %" GST_TIME_FORMAT " max %" GST_TIME_FORMAT,
          GST_TIME_ARGS (min_latency), GST_TIME_ARGS (max_latency));

      /* we are always live, the min latency is 1 frame and the max latency is
       * the complete buffer of frames. */
      gst_query_set_latency (query, TRUE, min_latency, max_latency);

      res = TRUE;
      break;
    }
    default:
      res = GST_BASE_SRC_CLASS (parent_class)->query (bsrc, query);
      break;
  }

done:

  return res;
}

/* start and stop are not symmetric -- start will open the device, but not start
 * capture. it's setcaps that will start capture, which is called via basesrc's
 * negotiate method. stop will both stop capture and close the device.
 */
static gboolean
gst_v4l2src_start (GstBaseSrc * src)
{
  GstV4l2Src *v4l2src = GST_V4L2SRC (src);

  v4l2src->offset = 0;

  /* activate settings for first frame */
  v4l2src->ctrl_time = 0;
  gst_object_sync_values (GST_OBJECT (src), v4l2src->ctrl_time);

  return TRUE;
}

static gboolean
gst_v4l2src_unlock (GstBaseSrc * src)
{
  GstV4l2Src *v4l2src = GST_V4L2SRC (src);
  return gst_v4l2_object_unlock (v4l2src->v4l2object);
}

static gboolean
gst_v4l2src_unlock_stop (GstBaseSrc * src)
{
  GstV4l2Src *v4l2src = GST_V4L2SRC (src);
  return gst_v4l2_object_unlock_stop (v4l2src->v4l2object);
}

static gboolean
gst_v4l2src_stop (GstBaseSrc * src)
{
  GstV4l2Src *v4l2src = GST_V4L2SRC (src);
  GstV4l2Object *obj = v4l2src->v4l2object;

  if (GST_V4L2_IS_ACTIVE (obj)) {
    if (!gst_v4l2_object_stop (obj))
      return FALSE;
  }
  return TRUE;
}

static GstStateChangeReturn
gst_v4l2src_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstV4l2Src *v4l2src = GST_V4L2SRC (element);
  GstV4l2Object *obj = v4l2src->v4l2object;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      /* open the device */
      if (!gst_v4l2_object_open (obj))
        return GST_STATE_CHANGE_FAILURE;
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      /* close the device */
      if (!gst_v4l2_object_close (obj))
        return GST_STATE_CHANGE_FAILURE;

      break;
    default:
      break;
  }

  return ret;
}

static GstFlowReturn
gst_v4l2src_create (GstPushSrc * src, GstBuffer ** buf)
{
  GstV4l2Src *v4l2src = GST_V4L2SRC (src);
  GstV4l2Object *obj = v4l2src->v4l2object;
  GstFlowReturn ret;
  GstClock *clock;
  GstClockTime abs_time, base_time, timestamp, duration;
  GstClockTime delay;

  ret = GST_BASE_SRC_CLASS (parent_class)->alloc (GST_BASE_SRC (src), 0,
      obj->info.size, buf);

  if (G_UNLIKELY (ret != GST_FLOW_OK))
    goto alloc_failed;

  ret =
      gst_v4l2_buffer_pool_process (GST_V4L2_BUFFER_POOL_CAST (obj->pool), buf);

  if (G_UNLIKELY (ret != GST_FLOW_OK))
    goto error;

  timestamp = GST_BUFFER_TIMESTAMP (*buf);
  duration = obj->duration;

  /* timestamps, LOCK to get clock and base time. */
  /* FIXME: element clock and base_time is rarely changing */
  GST_OBJECT_LOCK (v4l2src);
  if ((clock = GST_ELEMENT_CLOCK (v4l2src))) {
    /* we have a clock, get base time and ref clock */
    base_time = GST_ELEMENT (v4l2src)->base_time;
    gst_object_ref (clock);
  } else {
    /* no clock, can't set timestamps */
    base_time = GST_CLOCK_TIME_NONE;
  }
  GST_OBJECT_UNLOCK (v4l2src);

  /* sample pipeline clock */
  if (clock) {
    abs_time = gst_clock_get_time (clock);
    gst_object_unref (clock);
  } else {
    abs_time = GST_CLOCK_TIME_NONE;
  }

  if (timestamp != GST_CLOCK_TIME_NONE) {
    struct timespec now;
    GstClockTime gstnow;

    /* v4l2 specs say to use the system time although many drivers switched to
     * the more desirable monotonic time. We first try to use the monotonic time
     * and see how that goes */
    clock_gettime (CLOCK_MONOTONIC, &now);
    gstnow = GST_TIMESPEC_TO_TIME (now);

    if (gstnow < timestamp && (timestamp - gstnow) > (10 * GST_SECOND)) {
      GTimeVal now;

      /* very large diff, fall back to system time */
      g_get_current_time (&now);
      gstnow = GST_TIMEVAL_TO_TIME (now);
    }

    if (gstnow > timestamp) {
      delay = gstnow - timestamp;
    } else {
      delay = 0;
    }

    GST_DEBUG_OBJECT (v4l2src, "ts: %" GST_TIME_FORMAT " now %" GST_TIME_FORMAT
        " delay %" GST_TIME_FORMAT, GST_TIME_ARGS (timestamp),
        GST_TIME_ARGS (gstnow), GST_TIME_ARGS (delay));
  } else {
    /* we assume 1 frame latency otherwise */
    if (GST_CLOCK_TIME_IS_VALID (duration))
      delay = duration;
    else
      delay = 0;
  }

  /* set buffer metadata */
  GST_BUFFER_OFFSET (*buf) = v4l2src->offset++;
  GST_BUFFER_OFFSET_END (*buf) = v4l2src->offset;

  if (G_LIKELY (abs_time != GST_CLOCK_TIME_NONE)) {
    /* the time now is the time of the clock minus the base time */
    timestamp = abs_time - base_time;

    /* adjust for delay in the device */
    if (timestamp > delay)
      timestamp -= delay;
    else
      timestamp = 0;
  } else {
    timestamp = GST_CLOCK_TIME_NONE;
  }

  /* activate settings for next frame */
  if (GST_CLOCK_TIME_IS_VALID (duration)) {
    v4l2src->ctrl_time += duration;
  } else {
    /* this is not very good (as it should be the next timestamp),
     * still good enough for linear fades (as long as it is not -1)
     */
    v4l2src->ctrl_time = timestamp;
  }
  gst_object_sync_values (GST_OBJECT (src), v4l2src->ctrl_time);

  GST_INFO_OBJECT (src, "sync to %" GST_TIME_FORMAT " out ts %" GST_TIME_FORMAT,
      GST_TIME_ARGS (v4l2src->ctrl_time), GST_TIME_ARGS (timestamp));

  GST_BUFFER_TIMESTAMP (*buf) = timestamp;
  GST_BUFFER_DURATION (*buf) = duration;

  return ret;

  /* ERROR */
alloc_failed:
  {
    if (ret != GST_FLOW_FLUSHING)
      GST_ELEMENT_ERROR (src, RESOURCE, NO_SPACE_LEFT,
          ("Failed to allocate a buffer"), (NULL));
    return ret;
  }
error:
  {
    GST_DEBUG_OBJECT (src, "error processing buffer %d (%s)", ret,
        gst_flow_get_name (ret));
    return ret;
  }
}


/* GstURIHandler interface */
static GstURIType
gst_v4l2src_uri_get_type (GType type)
{
  return GST_URI_SRC;
}

static const gchar *const *
gst_v4l2src_uri_get_protocols (GType type)
{
  static const gchar *protocols[] = { "v4l2", NULL };

  return protocols;
}

static gchar *
gst_v4l2src_uri_get_uri (GstURIHandler * handler)
{
  GstV4l2Src *v4l2src = GST_V4L2SRC (handler);

  if (v4l2src->v4l2object->videodev != NULL) {
    return g_strdup_printf ("v4l2://%s", v4l2src->v4l2object->videodev);
  }

  return g_strdup ("v4l2://");
}

static gboolean
gst_v4l2src_uri_set_uri (GstURIHandler * handler, const gchar * uri,
    GError ** error)
{
  GstV4l2Src *v4l2src = GST_V4L2SRC (handler);
  const gchar *device = DEFAULT_PROP_DEVICE;

  if (strcmp (uri, "v4l2://") != 0) {
    device = uri + 7;
  }
  g_object_set (v4l2src, "device", device, NULL);

  return TRUE;
}


static void
gst_v4l2src_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = gst_v4l2src_uri_get_type;
  iface->get_protocols = gst_v4l2src_uri_get_protocols;
  iface->get_uri = gst_v4l2src_uri_get_uri;
  iface->set_uri = gst_v4l2src_uri_set_uri;
}
