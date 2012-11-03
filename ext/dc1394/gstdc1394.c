/* GStreamer
 * Copyright (C) <2006> Eric Jonas <jonas@mit.edu>
 * Copyright (C) <2006> Antoine Tremblay <hexa00@gmail.com>
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
 * SECTION:element-dc1394
 *
 * Source for IIDC (Instrumentation & Industrial Digital Camera) firewire
 * cameras.
 * 
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v dc1394 camera-number=0 ! xvimagesink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "gstdc1394.h"
#include <sys/time.h>
#include <time.h>
#include <string.h>

GST_DEBUG_CATEGORY (dc1394_debug);
#define GST_CAT_DEFAULT dc1394_debug

enum
{
  PROP_0,
  PROP_TIMESTAMP_OFFSET,
  PROP_CAMNUM,
  PROP_BUFSIZE,
  PROP_ISO_SPEED
      /* FILL ME */
};


GST_BOILERPLATE (GstDc1394, gst_dc1394, GstPushSrc, GST_TYPE_PUSH_SRC);

static void gst_dc1394_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_dc1394_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstCaps *gst_dc1394_getcaps (GstBaseSrc * bsrc);
static gboolean gst_dc1394_setcaps (GstBaseSrc * bsrc, GstCaps * caps);
static void gst_dc1394_src_fixate (GstPad * pad, GstCaps * caps);

static void gst_dc1394_get_times (GstBaseSrc * basesrc,
    GstBuffer * buffer, GstClockTime * start, GstClockTime * end);

static GstFlowReturn gst_dc1394_create (GstPushSrc * psrc, GstBuffer ** buffer);

static GstStateChangeReturn
gst_dc1394_change_state (GstElement * element, GstStateChange transition);

static gboolean gst_dc1394_parse_caps (const GstCaps * caps,
    gint * width,
    gint * height,
    gint * rate_numerator, gint * rate_denominator, gint * vmode, gint * bpp);

static gint gst_dc1394_caps_set_format_vmode_caps (GstStructure * st,
    gint mode);
static gboolean gst_dc1394_set_caps_color (GstStructure * gs, gint mc);
static void gst_dc1394_set_caps_framesize (GstStructure * gs, gint width,
    gint height);
static void gst_dc1394_set_caps_framesize_range (GstStructure * gs,
    gint minwidth, gint maxwidth, gint incwidth,
    gint minheight, gint maxheight, gint incheight);

static gint gst_dc1394_caps_set_framerate_list (GstStructure * gs,
    dc1394framerates_t * framerates);

static GstCaps *gst_dc1394_get_all_dc1394_caps (void);
static GstCaps *gst_dc1394_get_cam_caps (GstDc1394 * src);
static gboolean gst_dc1394_open_cam_with_best_caps (GstDc1394 * src);
static gint gst_dc1394_framerate_frac_to_const (gint num, gint denom);
static void gst_dc1394_framerate_const_to_frac (gint framerateconst,
    GValue * framefrac);
static gboolean
gst_dc1394_change_camera_transmission (GstDc1394 * src, gboolean on);
static gboolean gst_dc1394_query (GstBaseSrc * bsrc, GstQuery * query);

static void
gst_dc1394_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_static_metadata (element_class,
      "1394 IIDC Video Source", "Source/Video",
      "libdc1394 based source, supports 1394 IIDC cameras",
      "Antoine Tremblay <hexa00@gmail.com>");

  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
          gst_dc1394_get_all_dc1394_caps ()));

}

static void
gst_dc1394_class_init (GstDc1394Class * klass)
{
  GObjectClass *gobject_class;
  GstBaseSrcClass *gstbasesrc_class;
  GstPushSrcClass *gstpushsrc_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstbasesrc_class = (GstBaseSrcClass *) klass;
  gstpushsrc_class = (GstPushSrcClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_dc1394_set_property;
  gobject_class->get_property = gst_dc1394_get_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_TIMESTAMP_OFFSET, g_param_spec_int64 ("timestamp-offset",
          "Timestamp offset",
          "An offset added to timestamps set on buffers (in ns)", G_MININT64,
          G_MAXINT64, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_CAMNUM, g_param_spec_int ("camera-number",
          "The number of the camera on the firewire bus",
          "The number of the camera on the firewire bus", 0,
          G_MAXINT, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_BUFSIZE, g_param_spec_int ("buffer-size",
          "The number of frames in the dma ringbuffer",
          "The number of frames in the dma ringbuffer", 1,
          G_MAXINT, 10, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_ISO_SPEED, g_param_spec_int ("iso-speed",
          "The iso bandwidth in Mbps (100, 200, 400, 800, 1600, 3200)",
          "The iso bandwidth in Mbps (100, 200, 400, 800, 1600, 3200)", 100,
          3200, 400, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gstbasesrc_class->get_caps = gst_dc1394_getcaps;
  gstbasesrc_class->set_caps = gst_dc1394_setcaps;
  gstbasesrc_class->query = gst_dc1394_query;

  gstbasesrc_class->get_times = gst_dc1394_get_times;
  gstpushsrc_class->create = gst_dc1394_create;

  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_dc1394_change_state);
}

static void
gst_dc1394_init (GstDc1394 * src, GstDc1394Class * g_class)
{

  src->segment_start_frame = -1;
  src->segment_end_frame = -1;
  src->timestamp_offset = 0;
  src->caps = gst_dc1394_get_all_dc1394_caps ();
  src->bufsize = 10;
  src->iso_speed = 400;
  src->camnum = 0;
  src->n_frames = 0;

  gst_pad_set_fixatecaps_function (GST_BASE_SRC_PAD (src),
      gst_dc1394_src_fixate);

  gst_base_src_set_live (GST_BASE_SRC (src), TRUE);
}

static void
gst_dc1394_src_fixate (GstPad * pad, GstCaps * caps)
{

  GstDc1394 *src = GST_DC1394 (gst_pad_get_parent (pad));
  GstStructure *structure;
  int i;

  GST_LOG_OBJECT (src, " fixating caps to closest to 320x240 , 30 fps");

  for (i = 0; i < gst_caps_get_size (caps); ++i) {
    structure = gst_caps_get_structure (caps, i);

    gst_structure_fixate_field_nearest_int (structure, "width", 320);
    gst_structure_fixate_field_nearest_int (structure, "height", 240);
    gst_structure_fixate_field_nearest_fraction (structure, "framerate", 30, 1);
  }
  gst_object_unref (GST_OBJECT (src));
}

static gboolean
gst_dc1394_query (GstBaseSrc * bsrc, GstQuery * query)
{
  gboolean res = TRUE;
  GstDc1394 *src = GST_DC1394 (bsrc);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:
    {
      GstClockTime min_latency, max_latency;

      if (!src->camera) {
        GST_WARNING_OBJECT (src,
            "Can't give latency since device isn't open !");
        res = FALSE;
        goto done;
      }

      if (src->rate_denominator <= 0 || src->rate_numerator <= 0) {
        GST_WARNING_OBJECT (bsrc,
            "Can't give latency since framerate isn't fixated !");
        res = FALSE;
        goto done;
      }

      /* min latency is the time to capture one frame */
      min_latency = gst_util_uint64_scale (GST_SECOND,
          src->rate_denominator, src->rate_numerator);

      /* max latency is total duration of the frame buffer */
      max_latency = gst_util_uint64_scale (src->bufsize,
          GST_SECOND * src->rate_denominator, src->rate_numerator);

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

static void
gst_dc1394_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDc1394 *src = GST_DC1394 (object);

  switch (prop_id) {
    case PROP_TIMESTAMP_OFFSET:
      src->timestamp_offset = g_value_get_int64 (value);
      break;
    case PROP_CAMNUM:
      src->camnum = g_value_get_int (value);
      break;
    case PROP_BUFSIZE:
      src->bufsize = g_value_get_int (value);
      break;
    case PROP_ISO_SPEED:
      switch (g_value_get_int (value)) {
        case 100:
        case 200:
        case 300:
        case 400:
        case 800:
        case 1600:
        case 3200:
          // fallthrough
          src->iso_speed = g_value_get_int (value);
          break;
        default:
          g_warning ("%s: Invalid iso speed %d, ignoring",
              GST_ELEMENT_NAME (src), g_value_get_int (value));
          break;
      }
    default:
      break;
  }
}

static void
gst_dc1394_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstDc1394 *src = GST_DC1394 (object);

  switch (prop_id) {
    case PROP_TIMESTAMP_OFFSET:
      g_value_set_int64 (value, src->timestamp_offset);
      break;
    case PROP_CAMNUM:
      g_value_set_int (value, src->camnum);
      break;
    case PROP_BUFSIZE:
      g_value_set_int (value, src->bufsize);
      break;
    case PROP_ISO_SPEED:
      g_value_set_int (value, src->iso_speed);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstCaps *
gst_dc1394_getcaps (GstBaseSrc * bsrc)
{
  GstDc1394 *gsrc;

  gsrc = GST_DC1394 (bsrc);

  g_return_val_if_fail (gsrc->caps, NULL);

  return gst_caps_copy (gsrc->caps);
}

static gboolean
gst_dc1394_setcaps (GstBaseSrc * bsrc, GstCaps * caps)
{
  gboolean res = TRUE;
  GstDc1394 *dc1394;
  gint width, height, rate_denominator, rate_numerator;
  gint bpp, vmode;

  dc1394 = GST_DC1394 (bsrc);

  if (dc1394->caps) {
    gst_caps_unref (dc1394->caps);
  }

  dc1394->caps = gst_caps_copy (caps);

  res = gst_dc1394_parse_caps (caps, &width, &height,
      &rate_numerator, &rate_denominator, &vmode, &bpp);

  if (res) {
    /* looks ok here */
    dc1394->width = width;
    dc1394->height = height;
    dc1394->vmode = vmode;
    dc1394->rate_numerator = rate_numerator;
    dc1394->rate_denominator = rate_denominator;
    dc1394->bpp = bpp;
  }

  return res;
}

static void
gst_dc1394_get_times (GstBaseSrc * basesrc, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end)
{
  /* for live sources, sync on the timestamp of the buffer */
  if (gst_base_src_is_live (basesrc)) {
    GstClockTime timestamp = GST_BUFFER_TIMESTAMP (buffer);

    if (GST_CLOCK_TIME_IS_VALID (timestamp)) {
      /* get duration to calculate end time */
      GstClockTime duration = GST_BUFFER_DURATION (buffer);

      if (GST_CLOCK_TIME_IS_VALID (duration)) {
        *end = timestamp + duration;
      }
      *start = timestamp;
    }
  } else {
    *start = -1;
    *end = -1;
  }
}

static GstFlowReturn
gst_dc1394_create (GstPushSrc * psrc, GstBuffer ** buffer)
{
  GstDc1394 *src;
  GstBuffer *outbuf;
  GstCaps *caps;
  dc1394video_frame_t *frame[1];
  GstFlowReturn res = GST_FLOW_OK;
  dc1394error_t err;

  src = GST_DC1394 (psrc);

  err = dc1394_capture_dequeue (src->camera, DC1394_CAPTURE_POLICY_WAIT, frame);

  if (err != DC1394_SUCCESS) {
    GST_ELEMENT_ERROR (src, RESOURCE, FAILED,
        ("failed to dequeue frame"), ("failed to dequeue frame"));
    goto error;
  }

  outbuf = gst_buffer_new_and_alloc (frame[0]->image_bytes);

  memcpy (GST_BUFFER_MALLOCDATA (outbuf), (guchar *) frame[0]->image,
      frame[0]->image_bytes * sizeof (guchar));

  GST_BUFFER_DATA (outbuf) = GST_BUFFER_MALLOCDATA (outbuf);

  caps = gst_pad_get_caps (GST_BASE_SRC_PAD (psrc));
  gst_buffer_set_caps (outbuf, caps);
  gst_caps_unref (caps);

  GST_BUFFER_TIMESTAMP (outbuf) = src->timestamp_offset + src->running_time;
  if (src->rate_numerator != 0) {
    GST_BUFFER_DURATION (outbuf) = gst_util_uint64_scale_int (GST_SECOND,
        src->rate_denominator, src->rate_numerator);
  }

  src->n_frames++;
  if (src->rate_numerator != 0) {
    src->running_time = gst_util_uint64_scale_int (src->n_frames * GST_SECOND,
        src->rate_denominator, src->rate_numerator);
  }

  if (dc1394_capture_enqueue (src->camera, frame[0]) != DC1394_SUCCESS) {
    GST_ELEMENT_ERROR (src, RESOURCE, FAILED, ("failed to enqueue frame"),
        ("failed to enqueue frame"));
    goto error;
  }

  *buffer = outbuf;

  return res;

error:
  {
    return GST_FLOW_ERROR;
  }
}


static gboolean
gst_dc1394_parse_caps (const GstCaps * caps,
    gint * width,
    gint * height,
    gint * rate_numerator, gint * rate_denominator, gint * vmode, gint * bpp)
{
  const GstStructure *structure;
  GstPadLinkReturn ret;
  const GValue *framerate;

  if (gst_caps_get_size (caps) < 1)
    return FALSE;

  structure = gst_caps_get_structure (caps, 0);

  ret = gst_structure_get_int (structure, "width", width);
  ret &= gst_structure_get_int (structure, "height", height);

  framerate = gst_structure_get_value (structure, "framerate");

  ret &= gst_structure_get_int (structure, "vmode", vmode);

  ret &= gst_structure_get_int (structure, "bpp", bpp);


  if (framerate) {
    *rate_numerator = gst_value_get_fraction_numerator (framerate);
    *rate_denominator = gst_value_get_fraction_denominator (framerate);
  } else {
    ret = FALSE;
  }

  return ret;
}

static GstStateChangeReturn
gst_dc1394_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstDc1394 *src = GST_DC1394 (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      GST_LOG_OBJECT (src, "State change null to ready");
      src->dc1394 = dc1394_new ();
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      GST_LOG_OBJECT (src, "State ready to paused");

      if (src->caps) {
        gst_caps_unref (src->caps);
        src->caps = NULL;
      }
      src->caps = gst_dc1394_get_cam_caps (src);
      if (src->caps == NULL) {
        GST_LOG_OBJECT (src,
            "Error : Set property  could not get cam caps ! , reverting to default");
        src->caps = gst_dc1394_get_all_dc1394_caps ();
        ret = GST_STATE_CHANGE_FAILURE;
      }

      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      GST_LOG_OBJECT (src, "State change paused to playing");

      if (!gst_dc1394_open_cam_with_best_caps (src)) {
        ret = GST_STATE_CHANGE_FAILURE;
      }

      if (src->camera && !gst_dc1394_change_camera_transmission (src, TRUE)) {
        ret = GST_STATE_CHANGE_FAILURE;
      }

      break;
    default:
      break;
  }
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      GST_LOG_OBJECT (src, "State change playing to paused");
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      GST_LOG_OBJECT (src, "State change paused to ready");

      if (src->camera && !gst_dc1394_change_camera_transmission (src, FALSE)) {

        if (src->camera) {
          dc1394_camera_free (src->camera);
        }
        src->camera = NULL;

        if (src->caps) {
          gst_caps_unref (src->caps);
          src->caps = NULL;
        }

        ret = GST_STATE_CHANGE_FAILURE;
      }

      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      GST_LOG_OBJECT (src, "State change ready to null");
      if (src->camera) {
        dc1394_camera_free (src->camera);
      }
      src->camera = NULL;

      if (src->dc1394) {
        dc1394_free (src->dc1394);
      }
      src->dc1394 = NULL;

      if (src->caps) {
        gst_caps_unref (src->caps);
        src->caps = NULL;
      }
      break;
    default:
      break;
  }

  return ret;
}


static gint
gst_dc1394_caps_set_format_vmode_caps (GstStructure * gs, gint mode)
{
  gint retval = 0;

  switch (mode) {
    case DC1394_VIDEO_MODE_160x120_YUV444:
      gst_dc1394_set_caps_color (gs, DC1394_COLOR_CODING_YUV444);
      gst_dc1394_set_caps_framesize (gs, 160, 120);
      break;
    case DC1394_VIDEO_MODE_320x240_YUV422:
      gst_dc1394_set_caps_color (gs, DC1394_COLOR_CODING_YUV422);
      gst_dc1394_set_caps_framesize (gs, 320, 240);
      break;
    case DC1394_VIDEO_MODE_640x480_YUV411:
      gst_dc1394_set_caps_color (gs, DC1394_COLOR_CODING_YUV411);
      gst_dc1394_set_caps_framesize (gs, 640, 480);
      break;
    case DC1394_VIDEO_MODE_640x480_YUV422:
      gst_dc1394_set_caps_color (gs, DC1394_COLOR_CODING_YUV422);
      gst_dc1394_set_caps_framesize (gs, 640, 480);
      break;
    case DC1394_VIDEO_MODE_640x480_RGB8:
      gst_dc1394_set_caps_color (gs, DC1394_COLOR_CODING_RGB8);
      gst_dc1394_set_caps_framesize (gs, 640, 480);
      break;
    case DC1394_VIDEO_MODE_640x480_MONO8:
      gst_dc1394_set_caps_color (gs, DC1394_COLOR_CODING_MONO8);
      gst_dc1394_set_caps_framesize (gs, 640, 480);
      break;
    case DC1394_VIDEO_MODE_640x480_MONO16:
      gst_dc1394_set_caps_color (gs, DC1394_COLOR_CODING_MONO16);
      gst_dc1394_set_caps_framesize (gs, 640, 480);
      break;
    case DC1394_VIDEO_MODE_800x600_YUV422:
      gst_dc1394_set_caps_color (gs, DC1394_COLOR_CODING_YUV422);
      gst_dc1394_set_caps_framesize (gs, 800, 600);
      break;
    case DC1394_VIDEO_MODE_800x600_RGB8:
      gst_dc1394_set_caps_color (gs, DC1394_COLOR_CODING_RGB8);
      gst_dc1394_set_caps_framesize (gs, 800, 600);
      break;
    case DC1394_VIDEO_MODE_800x600_MONO8:
      gst_dc1394_set_caps_color (gs, DC1394_COLOR_CODING_MONO8);
      gst_dc1394_set_caps_framesize (gs, 800, 600);
      break;
    case DC1394_VIDEO_MODE_1024x768_YUV422:
      gst_dc1394_set_caps_color (gs, DC1394_COLOR_CODING_YUV422);
      gst_dc1394_set_caps_framesize (gs, 1024, 768);
      break;
    case DC1394_VIDEO_MODE_1024x768_RGB8:
      gst_dc1394_set_caps_color (gs, DC1394_COLOR_CODING_RGB8);
      gst_dc1394_set_caps_framesize (gs, 1024, 768);
      break;
    case DC1394_VIDEO_MODE_1024x768_MONO8:
      gst_dc1394_set_caps_color (gs, DC1394_COLOR_CODING_MONO8);
      gst_dc1394_set_caps_framesize (gs, 1024, 768);
      break;
    case DC1394_VIDEO_MODE_800x600_MONO16:
      gst_dc1394_set_caps_color (gs, DC1394_COLOR_CODING_MONO16);
      gst_dc1394_set_caps_framesize (gs, 800, 600);
      break;
    case DC1394_VIDEO_MODE_1024x768_MONO16:
      gst_dc1394_set_caps_color (gs, DC1394_COLOR_CODING_MONO16);
      gst_dc1394_set_caps_framesize (gs, 1024, 768);
      break;
    case DC1394_VIDEO_MODE_1280x960_YUV422:
      gst_dc1394_set_caps_color (gs, DC1394_COLOR_CODING_YUV422);
      gst_dc1394_set_caps_framesize (gs, 1280, 960);
      break;
    case DC1394_VIDEO_MODE_1280x960_RGB8:
      gst_dc1394_set_caps_color (gs, DC1394_COLOR_CODING_RGB8);
      gst_dc1394_set_caps_framesize (gs, 1280, 960);
      break;
    case DC1394_VIDEO_MODE_1280x960_MONO8:
      gst_dc1394_set_caps_color (gs, DC1394_COLOR_CODING_MONO8);
      gst_dc1394_set_caps_framesize (gs, 1280, 960);
      break;
    case DC1394_VIDEO_MODE_1600x1200_YUV422:
      gst_dc1394_set_caps_color (gs, DC1394_COLOR_CODING_YUV422);
      gst_dc1394_set_caps_framesize (gs, 1600, 1200);
      break;
    case DC1394_VIDEO_MODE_1600x1200_RGB8:
      gst_dc1394_set_caps_color (gs, DC1394_COLOR_CODING_RGB8);
      gst_dc1394_set_caps_framesize (gs, 1600, 1200);
      break;
    case DC1394_VIDEO_MODE_1600x1200_MONO8:
      gst_dc1394_set_caps_color (gs, DC1394_COLOR_CODING_MONO8);
      gst_dc1394_set_caps_framesize (gs, 1600, 1200);
      break;
    case DC1394_VIDEO_MODE_1280x960_MONO16:
      gst_dc1394_set_caps_color (gs, DC1394_COLOR_CODING_MONO16);
      gst_dc1394_set_caps_framesize (gs, 1280, 960);
      break;
    case DC1394_VIDEO_MODE_1600x1200_MONO16:
      gst_dc1394_set_caps_color (gs, DC1394_COLOR_CODING_MONO8);
      gst_dc1394_set_caps_framesize (gs, 1600, 1200);
      break;

    default:
      retval = -1;
  }

  return retval;

}


static gboolean
gst_dc1394_set_caps_color (GstStructure * gs, gint mc)
{
  gboolean ret = TRUE;
  gint fourcc;

  switch (mc) {
    case DC1394_COLOR_CODING_YUV444:
      gst_structure_set_name (gs, "video/x-raw-yuv");

      fourcc = GST_MAKE_FOURCC ('I', 'Y', 'U', '2');
      gst_structure_set (gs,
          "format", GST_TYPE_FOURCC, fourcc, "bpp", G_TYPE_INT, 16, NULL);
      break;

    case DC1394_COLOR_CODING_YUV422:
      gst_structure_set_name (gs, "video/x-raw-yuv");
      fourcc = GST_MAKE_FOURCC ('U', 'Y', 'V', 'Y');
      gst_structure_set (gs,
          "format", GST_TYPE_FOURCC, fourcc, "bpp", G_TYPE_INT, 16, NULL);
      break;

    case DC1394_COLOR_CODING_YUV411:
      gst_structure_set_name (gs, "video/x-raw-yuv");
      fourcc = GST_MAKE_FOURCC ('I', 'Y', 'U', '1');
      gst_structure_set (gs,
          "format", GST_TYPE_FOURCC, fourcc, "bpp", G_TYPE_INT, 12, NULL);
      break;
    case DC1394_COLOR_CODING_RGB8:
      gst_structure_set_name (gs, "video/x-raw-rgb");
      gst_structure_set (gs,
          "bpp", G_TYPE_INT, 24,
          "depth", G_TYPE_INT, 24,
          "endianness", G_TYPE_INT, G_BIG_ENDIAN,
          "red_mask", G_TYPE_INT, 0xFF0000,
          "green_mask", G_TYPE_INT, 0x00FF00,
          "blue_mask", G_TYPE_INT, 0x0000FF, NULL);
      break;
    case DC1394_COLOR_CODING_MONO8:
      gst_structure_set_name (gs, "video/x-raw-gray");
      gst_structure_set (gs,
          "bpp", G_TYPE_INT, 8, "depth", G_TYPE_INT, 8, NULL);

      break;
    case DC1394_COLOR_CODING_MONO16:
      gst_structure_set_name (gs, "video/x-raw-gray");
      gst_structure_set (gs,
          "bpp", G_TYPE_INT, 16, "depth", G_TYPE_INT, 16, NULL);
      // there is no fourcc for this format
      break;
    default:
      GST_DEBUG ("Ignoring unsupported color format %d", mc);
      ret = FALSE;
      break;
  }
  return ret;
}


static void
gst_dc1394_set_caps_framesize (GstStructure * gs, gint width, gint height)
{
  gst_structure_set (gs,
      "width", G_TYPE_INT, width, "height", G_TYPE_INT, height, NULL);
}

static void
gst_dc1394_set_caps_framesize_range (GstStructure * gs,
    gint minwidth,
    gint maxwidth,
    gint incwidth, gint minheight, gint maxheight, gint incheight)
{
  /* 
     Format 7 cameras allow you to change the camera width/height in multiples
     of incwidth/incheight up to some max. This sets the necessary
     list structure in the gst caps structure
   */

  GValue widthlist = { 0 };
  GValue widthval = { 0 };
  GValue heightlist = { 0 };
  GValue heightval = { 0 };
  gint x = 0;

  g_value_init (&widthlist, GST_TYPE_LIST);
  g_value_init (&widthval, G_TYPE_INT);
  for (x = minwidth; x <= maxwidth; x += incwidth) {
    g_value_set_int (&widthval, x);
    gst_value_list_append_value (&widthlist, &widthval);
  }
  gst_structure_set_value (gs, "width", &widthlist);

  g_value_unset (&widthlist);
  g_value_unset (&widthval);

  g_value_init (&heightlist, GST_TYPE_LIST);
  g_value_init (&heightval, G_TYPE_INT);
  for (x = minheight; x <= maxheight; x += incheight) {
    g_value_set_int (&heightval, x);
    gst_value_list_append_value (&heightlist, &heightval);
  }

  gst_structure_set_value (gs, "height", &heightlist);

  g_value_unset (&heightlist);
  g_value_unset (&heightval);
}


static gint
gst_dc1394_caps_set_framerate_list (GstStructure * gs,
    dc1394framerates_t * framerates)
{
  GValue framefrac = { 0 };
  GValue frameratelist = { 0 };
  gint f;

  g_value_init (&frameratelist, GST_TYPE_LIST);
  g_value_init (&framefrac, GST_TYPE_FRACTION);

  // figure out the frame rate
  for (f = framerates->num - 1; f >= 0; f--) {
    /* reverse order so we place the faster frame rates higher in 
       the sequence */
    if (framerates->framerates[f]) {
      gst_dc1394_framerate_const_to_frac (framerates->framerates[f],
          &framefrac);

      gst_value_list_append_value (&frameratelist, &framefrac);
    }
  }
  gst_structure_set_value (gs, "framerate", &frameratelist);

  g_value_unset (&framefrac);
  g_value_unset (&frameratelist);
  return 0;
}



static void
gst_dc1394_framerate_const_to_frac (gint framerateconst, GValue * framefrac)
{

  // frac must have been already initialized

  switch (framerateconst) {
    case DC1394_FRAMERATE_1_875:
      gst_value_set_fraction (framefrac, 15, 8);
      break;
    case DC1394_FRAMERATE_3_75:
      gst_value_set_fraction (framefrac, 15, 4);
      break;
    case DC1394_FRAMERATE_7_5:
      gst_value_set_fraction (framefrac, 15, 2);
      break;
    case DC1394_FRAMERATE_15:
      gst_value_set_fraction (framefrac, 15, 1);
      break;
    case DC1394_FRAMERATE_30:
      gst_value_set_fraction (framefrac, 30, 1);
      break;
    case DC1394_FRAMERATE_60:
      gst_value_set_fraction (framefrac, 60, 1);
      break;
    case DC1394_FRAMERATE_120:
      gst_value_set_fraction (framefrac, 120, 1);
      break;
    case DC1394_FRAMERATE_240:
      gst_value_set_fraction (framefrac, 240, 1);
      break;
  }
}

static GstCaps *
gst_dc1394_get_all_dc1394_caps (void)
{
  /* 
     generate all possible caps

   */

  GstCaps *gcaps;
  gint i = 0;

  gcaps = gst_caps_new_empty ();
  // first, the fixed mode caps
  for (i = DC1394_VIDEO_MODE_MIN; i < DC1394_VIDEO_MODE_EXIF; i++) {
    GstStructure *gs = gst_structure_empty_new ("video");
    gint ret = gst_dc1394_caps_set_format_vmode_caps (gs, i);

    gst_structure_set (gs,
        "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);
    gst_structure_set (gs, "vmode", G_TYPE_INT, i, NULL);
    if (ret >= 0) {
      gst_caps_append_structure (gcaps, gs);
    }
  }

  // then Format 7 options

  for (i = DC1394_COLOR_CODING_MIN; i <= DC1394_COLOR_CODING_MAX; i++) {
    GstStructure *gs = gst_structure_empty_new ("video");

    //int ret =  gst_dc1394_caps_set_format_vmode_caps(gs, i); 

    gst_structure_set (gs, "vmode", GST_TYPE_INT_RANGE, 1, G_MAXINT, NULL);

    gst_structure_set (gs,
        "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);
    gst_structure_set (gs,
        "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
        "height", GST_TYPE_INT_RANGE, 1, G_MAXINT, NULL);

    if (gst_dc1394_set_caps_color (gs, i)) {
      gst_caps_append_structure (gcaps, gs);
    }
  }
  return gcaps;

}

GstCaps *
gst_dc1394_get_cam_caps (GstDc1394 * src)
{

  dc1394camera_t *camera = NULL;
  dc1394camera_list_t *cameras = NULL;
  dc1394error_t camerr;
  gint i, j;
  dc1394video_modes_t modes;
  dc1394framerates_t framerates;
  GstCaps *gcaps = NULL;

  gcaps = gst_caps_new_empty ();

  camerr = dc1394_camera_enumerate (src->dc1394, &cameras);

  if (camerr != DC1394_SUCCESS || cameras == NULL) {
    GST_ELEMENT_ERROR (src, RESOURCE, NOT_FOUND,
        ("Can't find cameras error : %d", camerr),
        ("Can't find cameras error : %d", camerr));
    goto error;
  }

  if (cameras->num == 0) {
    GST_ELEMENT_ERROR (src, RESOURCE, NOT_FOUND, ("There were no cameras"),
        ("There were no cameras"));
    goto error;
  }

  if (src->camnum > (cameras->num - 1)) {
    GST_ELEMENT_ERROR (src, RESOURCE, FAILED, ("Invalid camera number"),
        ("Invalid camera number %d", src->camnum));
    goto error;
  }

  camera =
      dc1394_camera_new_unit (src->dc1394, cameras->ids[src->camnum].guid,
      cameras->ids[src->camnum].unit);

  dc1394_camera_free_list (cameras);
  cameras = NULL;

  camerr = dc1394_video_get_supported_modes (camera, &modes);
  if (camerr != DC1394_SUCCESS) {
    GST_ELEMENT_ERROR (src, RESOURCE, FAILED, ("Error getting supported modes"),
        ("Error getting supported modes"));
    goto error;
  }

  for (i = modes.num - 1; i >= 0; i--) {
    int m = modes.modes[i];

    if (m < DC1394_VIDEO_MODE_EXIF) {

      GstStructure *gs = gst_structure_empty_new ("video");

      gst_structure_set (gs, "vmode", G_TYPE_INT, m, NULL);

      if (gst_dc1394_caps_set_format_vmode_caps (gs, m) < 0) {
        GST_ELEMENT_ERROR (src, STREAM, FAILED,
            ("attempt to set mode to %d failed", m),
            ("attempt to set mode to %d failed", m));
        goto error;
      } else {

        camerr = dc1394_video_get_supported_framerates (camera, m, &framerates);
        gst_dc1394_caps_set_framerate_list (gs, &framerates);
        gst_caps_append_structure (gcaps, gs);

      }
    } else {
      // FORMAT 7
      guint maxx, maxy;
      GstStructure *gs = gst_structure_empty_new ("video");
      dc1394color_codings_t colormodes;
      guint xunit, yunit;

      gst_structure_set (gs, "vmode", G_TYPE_INT, m, NULL);

      // Get the maximum frame size
      camerr = dc1394_format7_get_max_image_size (camera, m, &maxx, &maxy);
      if (camerr != DC1394_SUCCESS) {
        GST_ELEMENT_ERROR (src, RESOURCE, FAILED,
            ("Error getting format 7 max image size"),
            ("Error getting format 7 max image size"));
        goto error;
      }
      GST_LOG_OBJECT (src, "Format 7 maxx=%d maxy=%d", maxx, maxy);

      camerr = dc1394_format7_get_unit_size (camera, m, &xunit, &yunit);
      if (camerr != DC1394_SUCCESS) {
        GST_ELEMENT_ERROR (src, RESOURCE, FAILED,
            ("Error getting format 7 image unit size"),
            ("Error getting format 7 image unit size"));
        goto error;
      }
      GST_LOG_OBJECT (src, "Format 7 unitx=%d unity=%d", xunit, yunit);

      gst_dc1394_set_caps_framesize_range (gs, xunit, maxx, xunit,
          yunit, maxy, yunit);

      // note that format 7 has no concept of a framerate, so we pass the 
      // full range
      gst_structure_set (gs,
          "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);

      // get the available color codings
      camerr = dc1394_format7_get_color_codings (camera, m, &colormodes);
      if (camerr != DC1394_SUCCESS) {
        GST_ELEMENT_ERROR (src, RESOURCE, FAILED,
            ("Error getting format 7 color modes"),
            ("Error getting format 7 color modes"));
        goto error;
      }

      for (j = 0; j < colormodes.num; j++) {
        GstStructure *newgs = gst_structure_copy (gs);

        gst_dc1394_set_caps_color (newgs, colormodes.codings[j]);
        GST_LOG_OBJECT (src, "Format 7 colormode set : %d",
            colormodes.codings[j]);
        // note that since there are multiple color modes, we append
        // multiple structures.
        gst_caps_append_structure (gcaps, newgs);
      }
    }
  }

  if (camera) {
    dc1394_camera_free (camera);
  }

  return gcaps;

error:

  if (gcaps) {
    gst_caps_unref (gcaps);
  }

  if (cameras) {
    dc1394_camera_free_list (cameras);
    cameras = NULL;
  }

  if (camera) {
    dc1394_camera_free (camera);
    camera = NULL;
  }

  return NULL;
}

static gint
gst_dc1394_framerate_frac_to_const (gint num, gint denom)
{
  // frac must have been already initialized
  int retvalue = -1;

  if (num == 15 && denom == 8)
    retvalue = DC1394_FRAMERATE_1_875;

  if (num == 15 && denom == 4)
    retvalue = DC1394_FRAMERATE_3_75;

  if (num == 15 && denom == 2)
    retvalue = DC1394_FRAMERATE_7_5;

  if (num == 15 && denom == 1)
    retvalue = DC1394_FRAMERATE_15;


  if (num == 30 && denom == 1)
    retvalue = DC1394_FRAMERATE_30;

  if (num == 60 && denom == 1)
    retvalue = DC1394_FRAMERATE_60;

  return retvalue;
}


static gboolean
gst_dc1394_open_cam_with_best_caps (GstDc1394 * src)
{
  dc1394camera_list_t *cameras = NULL;
  gint err = 0;
  int framerateconst;

  GST_LOG_OBJECT (src, "Opening the camera!!!");


  if (dc1394_camera_enumerate (src->dc1394, &cameras) != DC1394_SUCCESS) {
    GST_ELEMENT_ERROR (src, RESOURCE, FAILED, ("Can't find cameras"),
        ("Can't find cameras"));
    goto error;
  }

  GST_LOG_OBJECT (src, "Found  %d  cameras", cameras->num);

  if (src->camnum > (cameras->num - 1)) {
    GST_ELEMENT_ERROR (src, RESOURCE, FAILED, ("Invalid camera number"),
        ("Invalid camera number"));
    goto error;
  }

  GST_LOG_OBJECT (src, "Opening camera : %d", src->camnum);

  src->camera =
      dc1394_camera_new_unit (src->dc1394, cameras->ids[src->camnum].guid,
      cameras->ids[src->camnum].unit);

  dc1394_camera_free_list (cameras);
  cameras = NULL;

  // figure out mode
  framerateconst = gst_dc1394_framerate_frac_to_const (src->rate_numerator,
      src->rate_denominator);

  GST_LOG_OBJECT (src, "The dma buffer queue size is %d  buffers",
      src->bufsize);

  switch (src->iso_speed) {
    case 100:
      err = dc1394_video_set_iso_speed (src->camera, DC1394_ISO_SPEED_100);
      break;
    case 200:
      err = dc1394_video_set_iso_speed (src->camera, DC1394_ISO_SPEED_200);
      break;
    case 400:
      err = dc1394_video_set_iso_speed (src->camera, DC1394_ISO_SPEED_400);
      break;
    case 800:
      if (src->camera->bmode_capable > 0) {
        dc1394_video_set_operation_mode (src->camera,
            DC1394_OPERATION_MODE_1394B);
        err = dc1394_video_set_iso_speed (src->camera, DC1394_ISO_SPEED_800);
      }
      break;
    case 1600:
      if (src->camera->bmode_capable > 0) {
        dc1394_video_set_operation_mode (src->camera,
            DC1394_OPERATION_MODE_1394B);
        err = dc1394_video_set_iso_speed (src->camera, DC1394_ISO_SPEED_1600);
      }
      break;
    case 3200:
      if (src->camera->bmode_capable > 0) {
        dc1394_video_set_operation_mode (src->camera,
            DC1394_OPERATION_MODE_1394B);
        err = dc1394_video_set_iso_speed (src->camera, DC1394_ISO_SPEED_3200);
      }
      break;
    default:
      GST_ELEMENT_ERROR (src, RESOURCE, FAILED, ("Invalid ISO speed"),
          ("Invalid ISO speed"));
      goto error;
      break;
  }

  if (err != DC1394_SUCCESS) {
    GST_ELEMENT_ERROR (src, RESOURCE, FAILED, ("Could not set ISO speed"),
        ("Could not set ISO speed"));
    goto error;
  }

  GST_LOG_OBJECT (src, "Setting mode :  %d", src->vmode);
  err = dc1394_video_set_mode (src->camera, src->vmode);

  if (err != DC1394_SUCCESS) {
    GST_ELEMENT_ERROR (src, RESOURCE, FAILED, ("Could not set video mode %d",
            src->vmode), ("Could not set video mode %d", src->vmode));
    goto error;
  }

  GST_LOG_OBJECT (src, "Setting framerate :  %d", framerateconst);
  dc1394_video_set_framerate (src->camera, framerateconst);

  if (err != DC1394_SUCCESS) {
    GST_ELEMENT_ERROR (src, RESOURCE, FAILED, ("Could not set framerate to %d",
            framerateconst), ("Could not set framerate to %d", framerateconst));
    goto error;
  }
  // set any format-7 parameters if this is a format-7 mode
  if (src->vmode >= DC1394_VIDEO_MODE_FORMAT7_MIN &&
      src->vmode <= DC1394_VIDEO_MODE_FORMAT7_MAX) {
    // the big thing we care about right now is frame size
    err = dc1394_format7_set_image_size (src->camera, src->vmode,
        src->width, src->height);
    if (err != DC1394_SUCCESS) {
      GST_ELEMENT_ERROR (src, RESOURCE, FAILED,
          ("Could not set format 7 image size to %d x %d", src->width,
              src->height), ("Could not set format 7 image size to %d x %d",
              src->width, src->height));

      goto error;
    }

  }
  err = dc1394_capture_setup (src->camera, src->bufsize,
      DC1394_CAPTURE_FLAGS_DEFAULT);
  if (err != DC1394_SUCCESS) {
    GST_ELEMENT_ERROR (src, RESOURCE, FAILED, ("Error setting capture mode"),
        ("Error setting capture mode"));
  }
  if (err != DC1394_SUCCESS) {
    if (err == DC1394_NO_BANDWIDTH) {
      GST_LOG_OBJECT (src, "Capture setup_dma failed."
          "Trying to cleanup the iso_channels_and_bandwidth and retrying");

      // try to cleanup the bandwidth and retry 
      err = dc1394_iso_release_all (src->camera);
      if (err != DC1394_SUCCESS) {
        GST_ELEMENT_ERROR (src, RESOURCE, FAILED,
            ("Could not cleanup bandwidth"), ("Could not cleanup bandwidth"));
        goto error;
      } else {
        err =
            dc1394_capture_setup (src->camera, src->bufsize,
            DC1394_CAPTURE_FLAGS_DEFAULT);
        if (err != DC1394_SUCCESS) {
          GST_ELEMENT_ERROR (src, RESOURCE, FAILED,
              ("unable to setup camera error %d", err),
              ("unable to setup camera error %d", err));
          goto error;
        }
      }
    } else {
      GST_ELEMENT_ERROR (src, RESOURCE, FAILED,
          ("unable to setup camera error %d", err),
          ("unable to setup camera error %d", err));
      goto error;

    }
  }


  return TRUE;

error:

  if (src->camera) {
    dc1394_camera_free (src->camera);
    src->camera = NULL;
  }

  return FALSE;;

}


gboolean
gst_dc1394_change_camera_transmission (GstDc1394 * src, gboolean on)
{
  dc1394switch_t status = DC1394_OFF;
  dc1394error_t err = DC1394_FAILURE;
  gint i = 0;

  g_return_val_if_fail (src->camera, FALSE);

  if (on) {

    status = dc1394_video_set_transmission (src->camera, DC1394_ON);

    i = 0;
    while (status == DC1394_OFF && i++ < 5) {
      g_usleep (50000);
      if (dc1394_video_get_transmission (src->camera,
              &status) != DC1394_SUCCESS) {
        if (status == DC1394_OFF) {
          GST_LOG_OBJECT (src, "camera is still off , retrying");
        }
      }
    }

    if (i == 5) {
      GST_ELEMENT_ERROR (src, RESOURCE, FAILED,
          ("Camera doesn't seem to want to turn on!"),
          ("Camera doesn't seem to want to turn on!"));
      return FALSE;
    }

    GST_LOG_OBJECT (src, "got transmision status ON");

  } else {

    if (dc1394_video_set_transmission (src->camera,
            DC1394_OFF) != DC1394_SUCCESS) {
      GST_ELEMENT_ERROR (src, RESOURCE, FAILED, ("Unable to stop transmision"),
          ("Unable to stop transmision"));
      return FALSE;
    }

    GST_LOG_OBJECT (src, "Stopping capture");

    err = dc1394_capture_stop (src->camera);
    if (err > 0) {
      GST_ELEMENT_ERROR (src, RESOURCE, FAILED, ("Capture stop error : %d ",
              err), ("Capture stop error : %d ", err));
      return FALSE;
    } else {
      GST_LOG_OBJECT (src, "Capture stoped successfully");
    }
  }

  return TRUE;
}


static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (dc1394_debug, "dc1394", 0, "DC1394 interface");

  return gst_element_register (plugin, "dc1394src", GST_RANK_NONE,
      GST_TYPE_DC1394);

}


GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    dc1394,
    "1394 IIDC Video Source",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
