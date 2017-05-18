/* GStreamer
 *
 * uvch264_mjpg_demux: a demuxer for muxed stream in UVC H264 compliant MJPG
 *
 * Copyright (C) 2012 Cisco Systems, Inc.
 *   Author: Youness Alaoui <youness.alaoui@collabora.co.uk>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:element-uvch264mjpgdemux
 * @title: uvch264mjpgdemux
 * @short_description: UVC H264 compliant MJPG demuxer
 *
 * Parses a MJPG stream from a UVC H264 compliant encoding camera and extracts
 * each muxed stream into separate pads.
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <linux/uvcvideo.h>
#include <linux/usb/video.h>
#include <sys/ioctl.h>

#ifndef UVCIOC_GET_LAST_SCR
#include <time.h>

struct uvc_last_scr_sample
{
  __u32 dev_frequency;
  __u32 dev_stc;
  __u16 dev_sof;
  struct timespec host_ts;
  __u16 host_sof;
};

#define UVCIOC_GET_LAST_SCR	_IOR('u', 0x23, struct uvc_last_scr_sample)
#endif

#include "gstuvch264_mjpgdemux.h"

enum
{
  PROP_0,
  PROP_DEVICE_FD,
  PROP_NUM_CLOCK_SAMPLES
};

#define DEFAULT_NUM_CLOCK_SAMPLES 32

static GstStaticPadTemplate mjpgsink_pad_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("image/jpeg, "
        "width = (int) [ 0, MAX ],"
        "height = (int) [ 0, MAX ], " "framerate = (fraction) [ 0/1, MAX ] ")
    );

static GstStaticPadTemplate jpegsrc_pad_template =
GST_STATIC_PAD_TEMPLATE ("jpeg",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("image/jpeg, "
        "width = (int) [ 0, MAX ],"
        "height = (int) [ 0, MAX ], " "framerate = (fraction) [ 0/1, MAX ] ")
    );

static GstStaticPadTemplate h264src_pad_template =
GST_STATIC_PAD_TEMPLATE ("h264",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264, "
        "width = (int) [ 0, MAX ], "
        "height = (int) [ 0, MAX ], " "framerate = (fraction) [ 0/1, MAX ] ")
    );

static GstStaticPadTemplate yuy2src_pad_template =
GST_STATIC_PAD_TEMPLATE ("yuy2",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw, "
        "format = (string) YUY2, "
        "width = (int) [ 0, MAX ], "
        "height = (int) [ 0, MAX ], " "framerate = (fraction) [ 0/1, MAX ] ")
    );
static GstStaticPadTemplate nv12src_pad_template =
GST_STATIC_PAD_TEMPLATE ("nv12",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw, "
        "format = (string) NV12, "
        "width = (int) [ 0, MAX ], "
        "height = (int) [ 0, MAX ], " "framerate = (fraction) [ 0/1, MAX ] ")
    );


GST_DEBUG_CATEGORY_STATIC (uvc_h264_mjpg_demux_debug);
#define GST_CAT_DEFAULT uvc_h264_mjpg_demux_debug

typedef struct
{
  guint32 dev_stc;
  guint32 dev_sof;
  GstClockTime host_ts;
  guint32 host_sof;
} GstUvcH264ClockSample;

struct _GstUvcH264MjpgDemuxPrivate
{
  int device_fd;
  int num_clock_samples;
  GstUvcH264ClockSample *clock_samples;
  int last_sample;
  int num_samples;
  GstPad *sink_pad;
  GstPad *jpeg_pad;
  GstPad *h264_pad;
  GstPad *yuy2_pad;
  GstPad *nv12_pad;
  GstCaps *h264_caps;
  GstCaps *yuy2_caps;
  GstCaps *nv12_caps;
  guint16 h264_width;
  guint16 h264_height;
  guint16 yuy2_width;
  guint16 yuy2_height;
  guint16 nv12_width;
  guint16 nv12_height;

  /* input segment */
  GstSegment segment;
  GstClockTime last_pts;
  gboolean pts_reordered_warning;
};

typedef struct
{
  guint16 version;
  guint16 header_len;
  guint32 type;
  guint16 width;
  guint16 height;
  guint32 frame_interval;
  guint16 delay;
  guint32 pts;
} __attribute__ ((packed)) AuxiliaryStreamHeader;

static void gst_uvc_h264_mjpg_demux_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_uvc_h264_mjpg_demux_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_uvc_h264_mjpg_demux_dispose (GObject * object);
static GstFlowReturn gst_uvc_h264_mjpg_demux_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buffer);
static gboolean gst_uvc_h264_mjpg_demux_sink_event (GstPad * pad,
    GstObject * parent, GstEvent * event);
static gboolean gst_uvc_h264_mjpg_demux_query (GstPad * pad,
    GstObject * parent, GstQuery * query);

#define gst_uvc_h264_mjpg_demux_parent_class parent_class
G_DEFINE_TYPE (GstUvcH264MjpgDemux, gst_uvc_h264_mjpg_demux, GST_TYPE_ELEMENT);

static void
gst_uvc_h264_mjpg_demux_class_init (GstUvcH264MjpgDemuxClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *element_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  g_type_class_add_private (gobject_class, sizeof (GstUvcH264MjpgDemuxPrivate));

  gobject_class->set_property = gst_uvc_h264_mjpg_demux_set_property;
  gobject_class->get_property = gst_uvc_h264_mjpg_demux_get_property;
  gobject_class->dispose = gst_uvc_h264_mjpg_demux_dispose;

  gst_element_class_add_static_pad_template (element_class,
      &mjpgsink_pad_template);
  gst_element_class_add_static_pad_template (element_class,
      &jpegsrc_pad_template);
  gst_element_class_add_static_pad_template (element_class,
      &h264src_pad_template);
  gst_element_class_add_static_pad_template (element_class,
      &yuy2src_pad_template);
  gst_element_class_add_static_pad_template (element_class,
      &nv12src_pad_template);

  gst_element_class_set_static_metadata (element_class,
      "UVC H264 MJPG Demuxer",
      "Video/Demuxer",
      "Demux UVC H264 auxiliary streams from MJPG images",
      "Youness Alaoui <youness.alaoui@collabora.co.uk>");

  g_object_class_install_property (gobject_class, PROP_DEVICE_FD,
      g_param_spec_int ("device-fd", "device-fd",
          "File descriptor of the v4l2 device",
          -1, G_MAXINT, -1, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_NUM_CLOCK_SAMPLES,
      g_param_spec_int ("num-clock-samples", "num-clock-samples",
          "Number of clock samples to gather for the PTS synchronization"
          " (-1 = unlimited)",
          0, G_MAXINT, DEFAULT_NUM_CLOCK_SAMPLES,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  GST_DEBUG_CATEGORY_INIT (uvc_h264_mjpg_demux_debug,
      "uvch264mjpgdemux", 0, "UVC H264 MJPG Demuxer");
}

static void
gst_uvc_h264_mjpg_demux_init (GstUvcH264MjpgDemux * self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, GST_TYPE_UVC_H264_MJPG_DEMUX,
      GstUvcH264MjpgDemuxPrivate);


  self->priv->last_pts = GST_CLOCK_TIME_NONE;
  self->priv->pts_reordered_warning = FALSE;
  self->priv->device_fd = -1;

  /* create the sink and src pads */
  self->priv->sink_pad =
      gst_pad_new_from_static_template (&mjpgsink_pad_template, "sink");
  gst_pad_set_chain_function (self->priv->sink_pad,
      GST_DEBUG_FUNCPTR (gst_uvc_h264_mjpg_demux_chain));
  gst_pad_set_event_function (self->priv->sink_pad,
      GST_DEBUG_FUNCPTR (gst_uvc_h264_mjpg_demux_sink_event));
  gst_pad_set_query_function (self->priv->sink_pad,
      GST_DEBUG_FUNCPTR (gst_uvc_h264_mjpg_demux_query));
  gst_element_add_pad (GST_ELEMENT (self), self->priv->sink_pad);

  /* JPEG */
  self->priv->jpeg_pad =
      gst_pad_new_from_static_template (&jpegsrc_pad_template, "jpeg");
  gst_pad_set_query_function (self->priv->jpeg_pad,
      GST_DEBUG_FUNCPTR (gst_uvc_h264_mjpg_demux_query));
  gst_element_add_pad (GST_ELEMENT (self), self->priv->jpeg_pad);

  /* H264 */
  self->priv->h264_pad =
      gst_pad_new_from_static_template (&h264src_pad_template, "h264");
  gst_pad_use_fixed_caps (self->priv->h264_pad);
  gst_element_add_pad (GST_ELEMENT (self), self->priv->h264_pad);

  /* YUY2 */
  self->priv->yuy2_pad =
      gst_pad_new_from_static_template (&yuy2src_pad_template, "yuy2");
  gst_pad_use_fixed_caps (self->priv->yuy2_pad);
  gst_element_add_pad (GST_ELEMENT (self), self->priv->yuy2_pad);

  /* NV12 */
  self->priv->nv12_pad =
      gst_pad_new_from_static_template (&nv12src_pad_template, "nv12");
  gst_pad_use_fixed_caps (self->priv->nv12_pad);
  gst_element_add_pad (GST_ELEMENT (self), self->priv->nv12_pad);

  self->priv->h264_caps = gst_caps_new_empty_simple ("video/x-h264");
  self->priv->yuy2_caps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "YUY2", NULL);
  self->priv->nv12_caps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12", NULL);
  self->priv->h264_width = self->priv->h264_height = 0;
  self->priv->yuy2_width = self->priv->yuy2_height = 0;
  self->priv->nv12_width = self->priv->nv12_height = 0;
}

static void
gst_uvc_h264_mjpg_demux_dispose (GObject * object)
{
  GstUvcH264MjpgDemux *self = GST_UVC_H264_MJPG_DEMUX (object);

  if (self->priv->h264_caps)
    gst_caps_unref (self->priv->h264_caps);
  self->priv->h264_caps = NULL;
  if (self->priv->yuy2_caps)
    gst_caps_unref (self->priv->yuy2_caps);
  self->priv->yuy2_caps = NULL;
  if (self->priv->nv12_caps)
    gst_caps_unref (self->priv->nv12_caps);
  self->priv->nv12_caps = NULL;
  g_free (self->priv->clock_samples);
  self->priv->clock_samples = NULL;

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_uvc_h264_mjpg_demux_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstUvcH264MjpgDemux *self = GST_UVC_H264_MJPG_DEMUX (object);

  switch (prop_id) {
    case PROP_DEVICE_FD:
      self->priv->device_fd = g_value_get_int (value);
      break;
    case PROP_NUM_CLOCK_SAMPLES:
      self->priv->num_clock_samples = g_value_get_int (value);
      if (self->priv->clock_samples) {
        if (self->priv->num_clock_samples) {
          self->priv->clock_samples = g_realloc_n (self->priv->clock_samples,
              self->priv->num_clock_samples, sizeof (GstUvcH264ClockSample));
          if (self->priv->num_samples > self->priv->num_clock_samples) {
            self->priv->num_samples = self->priv->num_clock_samples;
            if (self->priv->last_sample >= self->priv->num_samples)
              self->priv->last_sample = self->priv->num_samples - 1;
          }
        } else {
          g_free (self->priv->clock_samples);
          self->priv->clock_samples = NULL;
          self->priv->last_sample = -1;
          self->priv->num_samples = 0;
        }
      }
      if (self->priv->num_clock_samples > 0) {
        self->priv->clock_samples = g_malloc0_n (self->priv->num_clock_samples,
            sizeof (GstUvcH264ClockSample));
        self->priv->last_sample = -1;
        self->priv->num_samples = 0;
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (self, prop_id, pspec);
      break;
  }
}

static void
gst_uvc_h264_mjpg_demux_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstUvcH264MjpgDemux *self = GST_UVC_H264_MJPG_DEMUX (object);

  switch (prop_id) {
    case PROP_DEVICE_FD:
      g_value_set_int (value, self->priv->device_fd);
      break;
    case PROP_NUM_CLOCK_SAMPLES:
      g_value_set_int (value, self->priv->num_clock_samples);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (self, prop_id, pspec);
      break;
  }
}

static gboolean
gst_uvc_h264_mjpg_demux_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstUvcH264MjpgDemux *self = GST_UVC_H264_MJPG_DEMUX (parent);
  gboolean res;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEGMENT:
      gst_event_copy_segment (event, &self->priv->segment);
      self->priv->last_pts = GST_CLOCK_TIME_NONE;
      res = gst_pad_push_event (self->priv->jpeg_pad, event);
      break;
    case GST_EVENT_CAPS:
      res = gst_pad_push_event (self->priv->jpeg_pad, event);
      break;
    default:
      res = gst_pad_event_default (pad, parent, event);
      break;
  }
  return res;
}

static gboolean
gst_uvc_h264_mjpg_demux_query (GstPad * pad, GstObject * parent,
    GstQuery * query)
{
  GstUvcH264MjpgDemux *self = GST_UVC_H264_MJPG_DEMUX (parent);
  gboolean ret = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
      if (pad == self->priv->sink_pad)
        ret = gst_pad_peer_query (self->priv->jpeg_pad, query);
      else
        ret = gst_pad_peer_query (self->priv->sink_pad, query);
      break;
    default:
      ret = gst_pad_query_default (pad, parent, query);
  }

  return ret;
}

static gboolean
_pts_to_timestamp (GstUvcH264MjpgDemux * self, GstBuffer * buf, guint32 pts)
{
  GstUvcH264MjpgDemuxPrivate *priv = self->priv;
  GstUvcH264ClockSample *current_sample = NULL;
  GstUvcH264ClockSample *oldest_sample = NULL;
  guint32 next_sample;
  struct uvc_last_scr_sample sample;
  guint32 dev_sof;

  if (self->priv->device_fd == -1 || priv->clock_samples == NULL)
    return FALSE;

  if (-1 == ioctl (priv->device_fd, UVCIOC_GET_LAST_SCR, &sample)) {
    //GST_WARNING_OBJECT (self, " GET_LAST_SCR error");
    return FALSE;
  }

  dev_sof = (guint32) (sample.dev_sof + 2048) << 16;
  if (priv->num_samples > 0 &&
      priv->clock_samples[priv->last_sample].dev_sof == dev_sof) {
    current_sample = &priv->clock_samples[priv->last_sample];
  } else {
    next_sample = (priv->last_sample + 1) % priv->num_clock_samples;
    current_sample = &priv->clock_samples[next_sample];
    current_sample->dev_stc = sample.dev_stc;
    current_sample->dev_sof = dev_sof;
    current_sample->host_ts = sample.host_ts.tv_sec * GST_SECOND +
        sample.host_ts.tv_nsec * GST_NSECOND;
    current_sample->host_sof = (guint32) (sample.host_sof + 2048) << 16;

    priv->num_samples++;
    priv->last_sample = next_sample;

    /* Debug printing */
    GST_DEBUG_OBJECT (self, "device frequency: %u", sample.dev_frequency);
    GST_DEBUG_OBJECT (self, "dev_sof: %u", sample.dev_sof);
    GST_DEBUG_OBJECT (self, "dev_stc: %u", sample.dev_stc);
    GST_DEBUG_OBJECT (self,
        "host_ts: %" G_GUINT64_FORMAT " -- %" GST_TIME_FORMAT,
        current_sample->host_ts, GST_TIME_ARGS (current_sample->host_ts));
    GST_DEBUG_OBJECT (self, "host_sof: %u", sample.host_sof);
    GST_DEBUG_OBJECT (self, "PTS: %u", pts);
    GST_DEBUG_OBJECT (self, "Diff: %u - %f\n", sample.dev_stc - pts,
        (gdouble) (sample.dev_stc - pts) / sample.dev_frequency);
  }

  if (priv->num_samples < priv->num_clock_samples)
    return FALSE;

  next_sample = (priv->last_sample + 1) % priv->num_clock_samples;
  oldest_sample = &priv->clock_samples[next_sample];

  /* TODO: Use current_sample and oldest_sample to do the
   * double linear regression and calculate a new PTS */
  (void) oldest_sample;

  return TRUE;
}

static GstFlowReturn
gst_uvc_h264_mjpg_demux_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buf)
{
  GstUvcH264MjpgDemux *self;
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer *jpeg_buf = NULL;
  GstBuffer *aux_buf = NULL;
  AuxiliaryStreamHeader aux_header = { 0 };
  guint32 aux_size = 0;
  GstPad *aux_pad = NULL;
  GstCaps **aux_caps = NULL;
  guint last_offset;
  guint i;
  GstMapInfo info;
  guint16 segment_size;

  self = GST_UVC_H264_MJPG_DEMUX (GST_PAD_PARENT (pad));

  if (gst_buffer_get_size (buf) == 0) {
    return gst_pad_push (self->priv->jpeg_pad, buf);
  }

  last_offset = 0;
  gst_buffer_map (buf, &info, GST_MAP_READ);

  jpeg_buf = gst_buffer_copy_region (buf, GST_BUFFER_COPY_METADATA, 0, 0);

  for (i = 0; i < info.size - 1; i++) {
    /* Check for APP4 (0xe4) marker in the jpeg */
    if (info.data[i] == 0xff && info.data[i + 1] == 0xe4) {

      /* Sanity check sizes and get segment size */
      if (i + 4 >= info.size) {
        GST_ELEMENT_ERROR (self, STREAM, DEMUX,
            ("Not enough data to read marker size"), (NULL));
        ret = GST_FLOW_ERROR;
        goto done;
      }
      segment_size = GUINT16_FROM_BE (*((guint16 *) (info.data + i + 2)));

      if (i + segment_size + 2 >= info.size) {
        GST_ELEMENT_ERROR (self, STREAM, DEMUX,
            ("Not enough data to read marker content"), (NULL));
        ret = GST_FLOW_ERROR;
        goto done;
      }
      GST_DEBUG_OBJECT (self,
          "Found APP4 marker (%d). JPG: %d-%d - APP4: %d - %d", segment_size,
          last_offset, i, i, i + 2 + segment_size);

      /* Add JPEG data between the last offset and this market */
      if (i - last_offset > 0) {
        GstMemory *m = gst_memory_copy (info.memory, last_offset,
            i - last_offset);
        gst_buffer_append_memory (jpeg_buf, m);
      }
      last_offset = i + 2 + segment_size;

      /* Reset i/segment size to the app4 data (ignore marker header/size) */
      i += 4;
      segment_size -= 2;

      /* If this is a new auxiliary stream, initialize everything properly */
      if (aux_buf == NULL) {
        if (segment_size < sizeof (aux_header) + sizeof (aux_size)) {
          GST_ELEMENT_ERROR (self, STREAM, DEMUX,
              ("Not enough data to read aux header"), (NULL));
          ret = GST_FLOW_ERROR;
          goto done;
        }

        aux_header = *((AuxiliaryStreamHeader *) (info.data + i));
        /* version should be little endian but it looks more like BE */
        aux_header.version = GUINT16_FROM_BE (aux_header.version);
        aux_header.header_len = GUINT16_FROM_LE (aux_header.header_len);
        aux_header.width = GUINT16_FROM_LE (aux_header.width);
        aux_header.height = GUINT16_FROM_LE (aux_header.height);
        aux_header.frame_interval = GUINT32_FROM_LE (aux_header.frame_interval);
        aux_header.delay = GUINT16_FROM_LE (aux_header.delay);
        aux_header.pts = GUINT32_FROM_LE (aux_header.pts);
        GST_DEBUG_OBJECT (self, "New auxiliary stream : v%d - %d bytes - %"
            GST_FOURCC_FORMAT " %dx%d -- %d *100ns -- %d ms -- %d",
            aux_header.version, aux_header.header_len,
            GST_FOURCC_ARGS (aux_header.type),
            aux_header.width, aux_header.height,
            aux_header.frame_interval, aux_header.delay, aux_header.pts);
        aux_size = *((guint32 *) (info.data + i + aux_header.header_len));
        GST_DEBUG_OBJECT (self, "Auxiliary stream size : %d bytes", aux_size);

        if (aux_size > 0) {
          guint16 *width = NULL;
          guint16 *height = NULL;

          /* Find the auxiliary stream's pad and caps */
          switch (aux_header.type) {
            case GST_MAKE_FOURCC ('H', '2', '6', '4'):
              aux_pad = self->priv->h264_pad;
              aux_caps = &self->priv->h264_caps;
              width = &self->priv->h264_width;
              height = &self->priv->h264_height;
              break;
            case GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'):
              aux_pad = self->priv->yuy2_pad;
              aux_caps = &self->priv->yuy2_caps;
              width = &self->priv->yuy2_width;
              height = &self->priv->yuy2_height;
              break;
            case GST_MAKE_FOURCC ('N', 'V', '1', '2'):
              aux_pad = self->priv->nv12_pad;
              aux_caps = &self->priv->nv12_caps;
              width = &self->priv->nv12_width;
              height = &self->priv->nv12_height;
              break;
            default:
              GST_ELEMENT_ERROR (self, STREAM, DEMUX,
                  ("Unknown auxiliary stream format : %" GST_FOURCC_FORMAT,
                      GST_FOURCC_ARGS (aux_header.type)), (NULL));
              ret = GST_FLOW_ERROR;
              break;
          }

          if (ret != GST_FLOW_OK)
            goto done;

          if (*width != aux_header.width || *height != aux_header.height) {
            GstCaps *peercaps = gst_pad_peer_query_caps (aux_pad, NULL);
            GstStructure *s = NULL;
            gint fps_num = 1000000000 / aux_header.frame_interval;
            gint fps_den = 100;

            /* TODO: intersect with pad template */
            GST_DEBUG ("peercaps : %" GST_PTR_FORMAT, peercaps);
            if (peercaps && !gst_caps_is_any (peercaps)) {
              peercaps = gst_caps_make_writable (peercaps);
              s = gst_caps_get_structure (peercaps, 0);
            }
            if (s && gst_structure_has_field (s, "framerate")) {
              /* TODO: make sure it contains the right format/width/height */
              gst_structure_fixate_field_nearest_fraction (s, "framerate",
                  fps_num, fps_den);
              GST_DEBUG ("Fixated struct : %" GST_PTR_FORMAT, s);
              gst_structure_get_fraction (s, "framerate", &fps_num, &fps_den);
            }
            if (peercaps)
              gst_caps_unref (peercaps);

            *width = aux_header.width;
            *height = aux_header.height;
            *aux_caps = gst_caps_make_writable (*aux_caps);
            /* FIXME: fps must match the caps and be allowed and represent
               our first buffer */
            gst_caps_set_simple (*aux_caps,
                "width", G_TYPE_INT, aux_header.width,
                "height", G_TYPE_INT, aux_header.height,
                "framerate", GST_TYPE_FRACTION, fps_num, fps_den, NULL);
            gst_pad_push_event (aux_pad, gst_event_new_caps (*aux_caps));
            gst_pad_push_event (aux_pad,
                gst_event_new_segment (&self->priv->segment));
          }

          /* Create new auxiliary buffer list and adjust i/segment size */
          aux_buf = gst_buffer_new ();
        }

        i += sizeof (aux_header) + sizeof (aux_size);
        segment_size -= sizeof (aux_header) + sizeof (aux_size);
      }

      if (segment_size > aux_size) {
        GST_ELEMENT_ERROR (self, STREAM, DEMUX,
            ("Expected %d auxiliary data, got %d bytes", aux_size,
                segment_size), (NULL));
        ret = GST_FLOW_ERROR;
        goto done;
      }

      if (segment_size > 0) {
        GstMemory *m;
        m = gst_memory_copy (info.memory, i, segment_size);

        GST_BUFFER_DURATION (aux_buf) =
            aux_header.frame_interval * 100 * GST_NSECOND;

        _pts_to_timestamp (self, aux_buf, aux_header.pts);

        gst_buffer_append_memory (aux_buf, m);

        aux_size -= segment_size;

        /* Push completed aux data */
        if (aux_size == 0) {
          /* Last attempt to apply timestamp. FIXME: This
           * is broken for H.264 with B-frames */
          if (GST_BUFFER_PTS (aux_buf) == GST_CLOCK_TIME_NONE) {
            if (!self->priv->pts_reordered_warning &&
                self->priv->last_pts != GST_CLOCK_TIME_NONE &&
                self->priv->last_pts > GST_BUFFER_PTS (buf)) {
              GST_WARNING_OBJECT (self, "PTS went backward, timestamping "
                  "might be broken");
              self->priv->pts_reordered_warning = TRUE;
            }
            self->priv->last_pts = GST_BUFFER_PTS (buf);

            GST_BUFFER_PTS (aux_buf) = GST_BUFFER_PTS (buf);
          }
          if (GST_BUFFER_DTS (aux_buf) == GST_CLOCK_TIME_NONE) {
            GstClockTime dts = GST_BUFFER_PTS (aux_buf);
            GstClockTime delay = aux_header.delay * GST_MSECOND;
            if (dts > delay)
              dts -= delay;
            else
              dts = 0;
            GST_BUFFER_DTS (aux_buf) = dts;
            GST_LOG_OBJECT (self, "Applied DTS %" GST_TIME_FORMAT
                " to aux_buf", GST_TIME_ARGS (dts));
          }

          GST_DEBUG_OBJECT (self, "Pushing %" GST_FOURCC_FORMAT
              " auxiliary buffer %" GST_PTR_FORMAT,
              GST_FOURCC_ARGS (aux_header.type), *aux_caps);
          ret = gst_pad_push (aux_pad, aux_buf);
          aux_buf = NULL;
          if (ret != GST_FLOW_OK) {
            GST_WARNING_OBJECT (self, "Error pushing %" GST_FOURCC_FORMAT
                " auxiliary data", GST_FOURCC_ARGS (aux_header.type));
            goto done;
          }
        }
      }

      i += segment_size - 1;
    } else if (info.data[i] == 0xff && info.data[i + 1] == 0xda) {
      GstMemory *m;

      /* The APP4 markers must be before the SOS marker, so this is the end */
      GST_DEBUG_OBJECT (self, "Found SOS marker.");

      m = gst_memory_copy (info.memory, last_offset, info.size - last_offset);
      gst_buffer_append_memory (jpeg_buf, m);
      last_offset = info.size;
      break;
    }
  }

  if (aux_buf != NULL) {
    GST_DEBUG_OBJECT (self, "Incomplete auxiliary stream: %d bytes missing, "
        "%d segment size remaining -- missing segment, C920 bug?",
        aux_size, segment_size);
    ret = GST_FLOW_OK;
    goto done;
  }

  if (last_offset != info.size) {
    /* this means there was no SOS marker in the jpg, so we assume the JPG was
       just a container */
    GST_DEBUG_OBJECT (self, "SOS marker wasn't found. MJPG is container only");
    gst_buffer_unref (jpeg_buf);
    jpeg_buf = NULL;
  } else {
    ret = gst_pad_push (self->priv->jpeg_pad, jpeg_buf);
    jpeg_buf = NULL;
  }

  if (ret != GST_FLOW_OK) {
    GST_WARNING_OBJECT (self, "Error pushing jpeg data");
    goto done;
  }

done:
  /* In case of error, unref whatever was left */
  if (aux_buf)
    gst_buffer_unref (aux_buf);
  if (jpeg_buf)
    gst_buffer_unref (jpeg_buf);

  gst_buffer_unmap (buf, &info);

  /* We must always unref the input buffer since we never push it out */
  gst_buffer_unref (buf);

  return ret;
}
