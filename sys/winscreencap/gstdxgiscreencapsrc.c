/* GStreamer
 * Copyright (C) 2019 OKADA Jun-ichi <okada@abt.jp>
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
 * SECTION:element-dxgiscreencapsrc
 * @title: dxgiscreencapsrc
 *
 * This element uses DXGI Desktop Duplication API.
 * The default is capturing the whole desktop, but #GstDXGIScreenCapSrc:x,
 * #GstDXGIScreenCapSrc:y, #GstDXGIScreenCapSrc:width and
 * #GstDXGIScreenCapSrc:height can be used to select a particular region.
 * Use #GstDXGIScreenCapSrc:monitor for changing which monitor to capture
 * from.
 *
 * ## Example pipelines
 * |[
 * gst-launch-1.0 dxgiscreencapsrc ! videoconvert ! dshowvideosink
 * ]| Capture the desktop and display it.
 * |[
 * gst-launch-1.0 dxgiscreencapsrc x=100 y=100 width=320 height=240 !
 * videoconvert ! dshowvideosink
 * ]| Capture a portion of the desktop and display it.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <windows.h>
#include <versionhelpers.h>
#include <gst/video/video.h>
#include "gstdxgiscreencapsrc.h"
#include "dxgicapture.h"

GST_DEBUG_CATEGORY_EXTERN (gst_dxgi_screen_cap_src_debug);
#define GST_CAT_DEFAULT gst_dxgi_screen_cap_src_debug

struct _GstDXGIScreenCapSrc
{
  /* Parent */
  GstPushSrc src;

  /* Properties */
  gint capture_x;
  gint capture_y;
  gint capture_w;
  gint capture_h;
  guint monitor;
  gchar *device_name;
  gboolean show_cursor;

  /* Source pad frame rate */
  gint rate_numerator;
  gint rate_denominator;

  /* Runtime variables */
  RECT screen_rect;
  RECT src_rect;
  guint64 frame_number;
  GstClockID clock_id;
  GstVideoInfo video_info;

  /*DXGI capture */
  DxgiCapture *dxgi_capture;
};

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("BGRA")));

#define gst_dxgi_screen_cap_src_parent_class parent_class
G_DEFINE_TYPE (GstDXGIScreenCapSrc, gst_dxgi_screen_cap_src, GST_TYPE_PUSH_SRC);

#define DEFAULT_MONITOR (-1)
#define DEFAULT_DEVICE_NAME (NULL)
#define DEFAULT_SHOW_CURSOR (FALSE)
#define DEFAULT_X_POS (0)
#define DEFAULT_Y_POS (0)
#define DEFAULT_WIDTH (0)
#define DEFAULT_HEIGHT (0)

enum
{
  PROP_0,
  PROP_MONITOR,
  PROP_DEVICE_NAME,
  PROP_SHOW_CURSOR,
  PROP_X_POS,
  PROP_Y_POS,
  PROP_WIDTH,
  PROP_HEIGHT
};

static void gst_dxgi_screen_cap_src_dispose (GObject * object);
static void gst_dxgi_screen_cap_src_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_dxgi_screen_cap_src_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static GstCaps *gst_dxgi_screen_cap_src_fixate (GstBaseSrc * bsrc,
    GstCaps * caps);
static gboolean gst_dxgi_screen_cap_src_set_caps (GstBaseSrc * bsrc,
    GstCaps * caps);
static GstCaps *gst_dxgi_screen_cap_src_get_caps (GstBaseSrc * bsrc,
    GstCaps * filter);
static gboolean gst_dxgi_screen_cap_src_start (GstBaseSrc * bsrc);
static gboolean gst_dxgi_screen_cap_src_stop (GstBaseSrc * bsrc);

static gboolean gst_dxgi_screen_cap_src_unlock (GstBaseSrc * bsrc);

static GstFlowReturn gst_dxgi_screen_cap_src_fill (GstPushSrc * pushsrc,
    GstBuffer * buffer);


static HMONITOR _get_hmonitor (GstDXGIScreenCapSrc * src);

/* Implementation. */
static void
gst_dxgi_screen_cap_src_class_init (GstDXGIScreenCapSrcClass * klass)
{
  GObjectClass *go_class;
  GstElementClass *e_class;
  GstBaseSrcClass *bs_class;
  GstPushSrcClass *ps_class;

  go_class = G_OBJECT_CLASS (klass);
  e_class = GST_ELEMENT_CLASS (klass);
  bs_class = GST_BASE_SRC_CLASS (klass);
  ps_class = GST_PUSH_SRC_CLASS (klass);

  go_class->set_property = gst_dxgi_screen_cap_src_set_property;
  go_class->get_property = gst_dxgi_screen_cap_src_get_property;

  go_class->dispose = GST_DEBUG_FUNCPTR (gst_dxgi_screen_cap_src_dispose);
  bs_class->get_caps = GST_DEBUG_FUNCPTR (gst_dxgi_screen_cap_src_get_caps);
  bs_class->set_caps = GST_DEBUG_FUNCPTR (gst_dxgi_screen_cap_src_set_caps);
  bs_class->start = GST_DEBUG_FUNCPTR (gst_dxgi_screen_cap_src_start);
  bs_class->stop = GST_DEBUG_FUNCPTR (gst_dxgi_screen_cap_src_stop);
  bs_class->unlock = GST_DEBUG_FUNCPTR (gst_dxgi_screen_cap_src_unlock);
  bs_class->fixate = GST_DEBUG_FUNCPTR (gst_dxgi_screen_cap_src_fixate);
  ps_class->fill = GST_DEBUG_FUNCPTR (gst_dxgi_screen_cap_src_fill);

  g_object_class_install_property (go_class, PROP_MONITOR,
      g_param_spec_int ("monitor", "Monitor",
          "Which monitor to use (-1 = primary monitor and default)",
          DEFAULT_MONITOR, G_MAXINT, DEFAULT_MONITOR, G_PARAM_READWRITE));
  g_object_class_install_property (go_class, PROP_DEVICE_NAME,
      g_param_spec_string ("device-name", "Monitor device name",
          "Which monitor to use by device name (e.g. \"\\\\\\\\.\\\\DISPLAY1\")",
          DEFAULT_DEVICE_NAME, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (go_class, PROP_SHOW_CURSOR,
      g_param_spec_boolean ("cursor",
          "Show mouse cursor",
          "Whether to show mouse cursor (default off)",
          DEFAULT_SHOW_CURSOR, G_PARAM_READWRITE));
  g_object_class_install_property (go_class, PROP_X_POS,
      g_param_spec_int ("x", "X",
          "Horizontal coordinate of top left corner for the screen capture "
          "area", 0, G_MAXINT, DEFAULT_X_POS, G_PARAM_READWRITE));
  g_object_class_install_property (go_class, PROP_Y_POS,
      g_param_spec_int ("y", "Y",
          "Vertical coordinate of top left corner for the screen capture "
          "area", 0, G_MAXINT, DEFAULT_Y_POS, G_PARAM_READWRITE));
  g_object_class_install_property (go_class, PROP_WIDTH,
      g_param_spec_int ("width", "Width",
          "Width of screen capture area (0 = maximum)",
          0, G_MAXINT, DEFAULT_WIDTH, G_PARAM_READWRITE));
  g_object_class_install_property (go_class, PROP_HEIGHT,
      g_param_spec_int ("height", "Height",
          "Height of screen capture area (0 = maximum)",
          0, G_MAXINT, DEFAULT_HEIGHT, G_PARAM_READWRITE));

  gst_element_class_add_static_pad_template (e_class, &src_template);

  gst_element_class_set_static_metadata (e_class,
      "DirectX DXGI screen capture source",
      "Source/Video", "Captures screen", "OKADA Jun-ichi <okada@abt.jp>");
}

static void
gst_dxgi_screen_cap_src_init (GstDXGIScreenCapSrc * src)
{
  /* Set src element inital values... */
  src->capture_x = DEFAULT_X_POS;
  src->capture_y = DEFAULT_Y_POS;
  src->capture_w = DEFAULT_WIDTH;
  src->capture_h = DEFAULT_HEIGHT;

  src->monitor = DEFAULT_MONITOR;
  src->device_name = DEFAULT_DEVICE_NAME;
  src->show_cursor = DEFAULT_SHOW_CURSOR;

  src->dxgi_capture = NULL;

  gst_base_src_set_live (GST_BASE_SRC (src), TRUE);
  gst_base_src_set_format (GST_BASE_SRC (src), GST_FORMAT_TIME);
}

static void
gst_dxgi_screen_cap_src_dispose (GObject * object)
{
  GstDXGIScreenCapSrc *src = GST_DXGI_SCREEN_CAP_SRC (object);

  g_free (src->device_name);
  src->device_name = NULL;

  dxgicap_destory (src->dxgi_capture);
  src->dxgi_capture = NULL;

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_dxgi_screen_cap_src_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstDXGIScreenCapSrc *src = GST_DXGI_SCREEN_CAP_SRC (object);

  switch (prop_id) {
    case PROP_MONITOR:
      src->monitor = g_value_get_int (value);
      break;
    case PROP_DEVICE_NAME:
      g_free (src->device_name);
      src->device_name = g_value_dup_string (value);
      break;
    case PROP_SHOW_CURSOR:
      src->show_cursor = g_value_get_boolean (value);
      break;
    case PROP_X_POS:
      src->capture_x = g_value_get_int (value);
      break;
    case PROP_Y_POS:
      src->capture_y = g_value_get_int (value);
      break;
    case PROP_WIDTH:
      src->capture_w = g_value_get_int (value);
      break;
    case PROP_HEIGHT:
      src->capture_h = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  };
}

static void
gst_dxgi_screen_cap_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstDXGIScreenCapSrc *src = GST_DXGI_SCREEN_CAP_SRC (object);

  switch (prop_id) {
    case PROP_MONITOR:
      g_value_set_int (value, src->monitor);
      break;
    case PROP_DEVICE_NAME:
      g_value_set_string (value, src->device_name);
      break;
    case PROP_SHOW_CURSOR:
      g_value_set_boolean (value, src->show_cursor);
      break;
    case PROP_X_POS:
      g_value_set_int (value, src->capture_x);
      break;
    case PROP_Y_POS:
      g_value_set_int (value, src->capture_y);
      break;
    case PROP_WIDTH:
      g_value_set_int (value, src->capture_w);
      break;
    case PROP_HEIGHT:
      g_value_set_int (value, src->capture_h);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  };
}

static GstCaps *
gst_dxgi_screen_cap_src_fixate (GstBaseSrc * bsrc, GstCaps * caps)
{
  GstStructure *structure;

  caps = gst_caps_make_writable (caps);

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_fixate_field_nearest_int (structure, "width", 640);
  gst_structure_fixate_field_nearest_int (structure, "height", 480);
  gst_structure_fixate_field_nearest_fraction (structure, "framerate", 30, 1);

  caps = GST_BASE_SRC_CLASS (parent_class)->fixate (bsrc, caps);

  return caps;
}

static gboolean
gst_dxgi_screen_cap_src_set_caps (GstBaseSrc * bsrc, GstCaps * caps)
{
  GstDXGIScreenCapSrc *src = GST_DXGI_SCREEN_CAP_SRC (bsrc);
  GstStructure *structure;

  structure = gst_caps_get_structure (caps, 0);

  src->src_rect = src->screen_rect;
  if (src->capture_w && src->capture_h) {
    src->src_rect.left += src->capture_x;
    src->src_rect.top += src->capture_y;
    src->src_rect.right = src->src_rect.left + src->capture_w;
    src->src_rect.bottom = src->src_rect.top + src->capture_h;
  }

  gst_structure_get_fraction (structure, "framerate",
      &src->rate_numerator, &src->rate_denominator);

  GST_DEBUG_OBJECT (src, "set_caps size %dx%d, %d/%d fps",
      (gint) RECT_WIDTH (src->src_rect),
      (gint) RECT_HEIGHT (src->src_rect),
      src->rate_numerator, src->rate_denominator);

  gst_video_info_from_caps (&src->video_info, caps);
  gst_base_src_set_blocksize (bsrc, GST_VIDEO_INFO_SIZE (&src->video_info));
  return TRUE;
}

static GstCaps *
gst_dxgi_screen_cap_src_get_caps (GstBaseSrc * bsrc, GstCaps * filter)
{
  GstDXGIScreenCapSrc *src = GST_DXGI_SCREEN_CAP_SRC (bsrc);
  RECT rect_dst;
  GstCaps *caps = NULL;

  HMONITOR hmonitor = _get_hmonitor (src);
  if (!get_monitor_physical_size (hmonitor, &rect_dst)) {
    GST_ELEMENT_ERROR (src, RESOURCE, NOT_FOUND,
        ("Specified monitor with index %d not found", src->monitor), (NULL));
    return NULL;
  }

  src->screen_rect = rect_dst;
  if (src->capture_w && src->capture_h &&
      src->capture_x + src->capture_w <= RECT_WIDTH (rect_dst) &&
      src->capture_y + src->capture_h <= RECT_HEIGHT (rect_dst)) {
    rect_dst.left = src->capture_x;
    rect_dst.top = src->capture_y;
    rect_dst.right = src->capture_x + src->capture_w;
    rect_dst.bottom = src->capture_y + src->capture_h;
  } else {
    /* Default values */
    src->capture_x = src->capture_y = 0;
    src->capture_w = src->capture_h = 0;
  }

  /* The desktop image is always in the DXGI_FORMAT_B8G8R8A8_UNORM format. */
  GST_DEBUG_OBJECT (src, "get_cap rect: %ld, %ld, %ld, %ld", rect_dst.left,
      rect_dst.top, rect_dst.right, rect_dst.bottom);

  caps =
      gst_caps_new_simple ("video/x-raw", "format", G_TYPE_STRING, "BGRA",
      "width", G_TYPE_INT, RECT_WIDTH (rect_dst),
      "height", G_TYPE_INT, RECT_HEIGHT (rect_dst),
      "framerate", GST_TYPE_FRACTION_RANGE, 1, 1, G_MAXINT,
      1, "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1, NULL);

  if (filter) {
    GstCaps *tmp =
        gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (caps);
    caps = tmp;
  }
  return caps;
}

static gboolean
gst_dxgi_screen_cap_src_start (GstBaseSrc * bsrc)
{
  GstDXGIScreenCapSrc *src = GST_DXGI_SCREEN_CAP_SRC (bsrc);
  HMONITOR hmonitor = _get_hmonitor (src);
  if (NULL == hmonitor) {
    GST_ELEMENT_ERROR (src, RESOURCE, NOT_FOUND,
        ("Specified monitor with index %d not found", src->monitor), (NULL));
    return FALSE;
  }
  src->dxgi_capture = dxgicap_new (hmonitor, src);

  if (NULL == src->dxgi_capture) {
    GST_ELEMENT_ERROR (src, RESOURCE, NOT_FOUND,
        ("Specified monitor with index %d not found", src->monitor), (NULL));
    return FALSE;
  }
  dxgicap_start (src->dxgi_capture);

  src->frame_number = -1;
  return TRUE;
}

static gboolean
gst_dxgi_screen_cap_src_stop (GstBaseSrc * bsrc)
{
  GstDXGIScreenCapSrc *src = GST_DXGI_SCREEN_CAP_SRC (bsrc);
  dxgicap_stop (src->dxgi_capture);
  dxgicap_destory (src->dxgi_capture);
  src->dxgi_capture = NULL;

  return TRUE;
}

static gboolean
gst_dxgi_screen_cap_src_unlock (GstBaseSrc * bsrc)
{
  GstDXGIScreenCapSrc *src = GST_DXGI_SCREEN_CAP_SRC (bsrc);

  GST_OBJECT_LOCK (src);
  if (src->clock_id) {
    GST_DEBUG_OBJECT (src, "Waking up waiting clock");
    gst_clock_id_unschedule (src->clock_id);
  }
  GST_OBJECT_UNLOCK (src);

  return TRUE;
}

static GstFlowReturn
gst_dxgi_screen_cap_src_fill (GstPushSrc * push_src, GstBuffer * buf)
{
  GstDXGIScreenCapSrc *src = GST_DXGI_SCREEN_CAP_SRC (push_src);
  GstClock *clock;
  GstClockTime buf_time, buf_dur;
  guint64 frame_number;

  if (G_UNLIKELY (!src->dxgi_capture)) {
    GST_DEBUG_OBJECT (src, "format wasn't negotiated before create function");
    return GST_FLOW_NOT_NEGOTIATED;
  }

  clock = gst_element_get_clock (GST_ELEMENT (src));
  if (clock != NULL) {
    GstClockTime time, base_time;

    /* Calculate sync time. */

    time = gst_clock_get_time (clock);
    base_time = gst_element_get_base_time (GST_ELEMENT (src));
    buf_time = time - base_time;

    if (src->rate_numerator) {
      frame_number = gst_util_uint64_scale (buf_time,
          src->rate_numerator, GST_SECOND * src->rate_denominator);
    } else {
      frame_number = -1;
    }
  } else {
    buf_time = GST_CLOCK_TIME_NONE;
    frame_number = -1;
  }

  if (frame_number != -1 && frame_number == src->frame_number) {
    GstClockID id;
    GstClockReturn ret;

    /* Need to wait for the next frame */
    frame_number += 1;

    /* Figure out what the next frame time is */
    buf_time = gst_util_uint64_scale (frame_number,
        src->rate_denominator * GST_SECOND, src->rate_numerator);

    id = gst_clock_new_single_shot_id (clock,
        buf_time + gst_element_get_base_time (GST_ELEMENT (src)));
    GST_OBJECT_LOCK (src);
    src->clock_id = id;
    GST_OBJECT_UNLOCK (src);

    GST_DEBUG_OBJECT (src, "Waiting for next frame time %" G_GUINT64_FORMAT,
        buf_time);
    ret = gst_clock_id_wait (id, NULL);

    GST_OBJECT_LOCK (src);
    gst_clock_id_unref (id);
    src->clock_id = NULL;
    GST_OBJECT_UNLOCK (src);

    if (ret == GST_CLOCK_UNSCHEDULED) {
      /* Got woken up by the unlock function */
      if (clock) {
        gst_object_unref (clock);
      }
      return GST_FLOW_FLUSHING;
    }

    /* Duration is a complete 1/fps frame duration */
    buf_dur =
        gst_util_uint64_scale_int (GST_SECOND, src->rate_denominator,
        src->rate_numerator);
  } else if (frame_number != -1) {
    GstClockTime next_buf_time;

    GST_DEBUG_OBJECT (src, "No need to wait for next frame time %"
        G_GUINT64_FORMAT " next frame = %" G_GINT64_FORMAT
        " prev = %" G_GINT64_FORMAT, buf_time, frame_number, src->frame_number);
    next_buf_time =
        gst_util_uint64_scale (frame_number + 1,
        src->rate_denominator * GST_SECOND, src->rate_numerator);
    /* Frame duration is from now until the next expected capture time */
    buf_dur = next_buf_time - buf_time;
  } else {
    buf_dur = GST_CLOCK_TIME_NONE;
  }
  src->frame_number = frame_number;

  if (clock) {
    gst_object_unref (clock);
  }

  /* Get the latest desktop frame. */
  if (dxgicap_acquire_next_frame (src->dxgi_capture, src->show_cursor, 0)) {
    /* Copy the latest desktop frame to the video frame. */
    if (dxgicap_copy_buffer (src->dxgi_capture, src->show_cursor,
            &src->src_rect, &src->video_info, buf)) {
      GST_BUFFER_TIMESTAMP (buf) = buf_time;
      GST_BUFFER_DURATION (buf) = buf_dur;
      return GST_FLOW_OK;
    }
  }
  return GST_FLOW_ERROR;
}

static HMONITOR
_get_hmonitor (GstDXGIScreenCapSrc * src)
{
  HMONITOR hmonitor = NULL;
  GST_DEBUG_OBJECT (src, "device_name:%s", GST_STR_NULL (src->device_name));
  if (NULL != src->device_name) {
    hmonitor = get_hmonitor_by_device_name (src->device_name);
  }
  if (NULL == hmonitor && DEFAULT_MONITOR != src->monitor) {
    hmonitor = get_hmonitor_by_index (src->monitor);
  }
  if (NULL == hmonitor) {
    hmonitor = get_hmonitor_primary ();
  }
  return hmonitor;
}

void
gst_dxgi_screen_cap_src_register (GstPlugin * plugin, GstRank rank)
{
  if (!IsWindows8OrGreater ()) {
    GST_WARNING ("OS version is too old");
    return;
  }

  if (!gst_dxgicap_shader_init ()) {
    GST_WARNING ("Couldn't load HLS compiler");
    return;
  }

  /**
   * element-dxgiscreencapsrc:
   *
   * Since: 1.18
   */
  gst_element_register (plugin, "dxgiscreencapsrc",
      rank, GST_TYPE_DXGI_SCREEN_CAP_SRC);
}
