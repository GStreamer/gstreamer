/* gstrtpvp9depay.c - Source for GstRtpVP9Depay
 * Copyright (C) 2011 Sjoerd Simons <sjoerd@luon.net>
 * Copyright (C) 2011 Collabora Ltd.
 *   Contact: Youness Alaoui <youness.alaoui@collabora.co.uk>
 * Copyright (C) 2015 Stian Selnes <stian@pexip.com>
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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "gstrtpelements.h"
#include "gstrtpvp9depay.h"
#include "gstrtputils.h"

#include <gst/video/video.h>

#include <stdio.h>

GST_DEBUG_CATEGORY_STATIC (gst_rtp_vp9_depay_debug);
#define GST_CAT_DEFAULT gst_rtp_vp9_depay_debug

static void gst_rtp_vp9_depay_dispose (GObject * object);
static void gst_rtp_vp9_depay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_rtp_vp9_depay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static GstBuffer *gst_rtp_vp9_depay_process (GstRTPBaseDepayload * depayload,
    GstRTPBuffer * rtp);
static GstStateChangeReturn gst_rtp_vp9_depay_change_state (GstElement *
    element, GstStateChange transition);
static gboolean gst_rtp_vp9_depay_handle_event (GstRTPBaseDepayload * depay,
    GstEvent * event);
static gboolean gst_rtp_vp9_depay_packet_lost (GstRTPBaseDepayload * depay,
    GstEvent * event);

G_DEFINE_TYPE (GstRtpVP9Depay, gst_rtp_vp9_depay, GST_TYPE_RTP_BASE_DEPAYLOAD);
GST_ELEMENT_REGISTER_DEFINE_WITH_CODE (rtpvp9depay, "rtpvp9depay",
    GST_RANK_MARGINAL, GST_TYPE_RTP_VP9_DEPAY, rtp_element_init (plugin));

static GstStaticPadTemplate gst_rtp_vp9_depay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-vp9"));

static GstStaticPadTemplate gst_rtp_vp9_depay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "clock-rate = (int) 90000,"
        "media = (string) \"video\","
        "encoding-name = (string) { \"VP9\", \"VP9-DRAFT-IETF-01\" }"));

#define DEFAULT_WAIT_FOR_KEYFRAME FALSE
#define DEFAULT_REQUEST_KEYFRAME FALSE

enum
{
  PROP_0,
  PROP_WAIT_FOR_KEYFRAME,
  PROP_REQUEST_KEYFRAME,
};

#define PICTURE_ID_NONE (UINT_MAX)
#define IS_PICTURE_ID_15BITS(pid) (((guint)(pid) & 0x8000) != 0)

static void
gst_rtp_vp9_depay_init (GstRtpVP9Depay * self)
{
  gst_rtp_base_depayload_set_aggregate_hdrext_enabled (GST_RTP_BASE_DEPAYLOAD
      (self), TRUE);

  self->adapter = gst_adapter_new ();
  self->started = FALSE;
  self->inter_picture = FALSE;
  self->wait_for_keyframe = DEFAULT_WAIT_FOR_KEYFRAME;
  self->request_keyframe = DEFAULT_REQUEST_KEYFRAME;
}

static void
gst_rtp_vp9_depay_class_init (GstRtpVP9DepayClass * gst_rtp_vp9_depay_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gst_rtp_vp9_depay_class);
  GstElementClass *element_class = GST_ELEMENT_CLASS (gst_rtp_vp9_depay_class);
  GstRTPBaseDepayloadClass *depay_class =
      (GstRTPBaseDepayloadClass *) (gst_rtp_vp9_depay_class);


  gst_element_class_add_static_pad_template (element_class,
      &gst_rtp_vp9_depay_sink_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_rtp_vp9_depay_src_template);

  gst_element_class_set_static_metadata (element_class, "RTP VP9 depayloader",
      "Codec/Depayloader/Network/RTP",
      "Extracts VP9 video from RTP packets)", "Stian Selnes <stian@pexip.com>");

  object_class->dispose = gst_rtp_vp9_depay_dispose;
  object_class->set_property = gst_rtp_vp9_depay_set_property;
  object_class->get_property = gst_rtp_vp9_depay_get_property;

  /**
   * GstRtpVP9Depay:wait-for-keyframe:
   *
   * Wait for the next keyframe after packet loss
   *
   * Since: 1.22
   */
  g_object_class_install_property (object_class, PROP_WAIT_FOR_KEYFRAME,
      g_param_spec_boolean ("wait-for-keyframe", "Wait for Keyframe",
          "Wait for the next keyframe after packet loss",
          DEFAULT_WAIT_FOR_KEYFRAME,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstRtpVP9Depay:request-keyframe:
   *
   * Request new keyframe when packet loss is detected
   *
   * Since: 1.22
   */
  g_object_class_install_property (object_class, PROP_REQUEST_KEYFRAME,
      g_param_spec_boolean ("request-keyframe", "Request Keyframe",
          "Request new keyframe when packet loss is detected",
          DEFAULT_REQUEST_KEYFRAME,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  element_class->change_state = gst_rtp_vp9_depay_change_state;

  depay_class->process_rtp_packet = gst_rtp_vp9_depay_process;
  depay_class->handle_event = gst_rtp_vp9_depay_handle_event;
  depay_class->packet_lost = gst_rtp_vp9_depay_packet_lost;

  GST_DEBUG_CATEGORY_INIT (gst_rtp_vp9_depay_debug, "rtpvp9depay", 0,
      "VP9 Video RTP Depayloader");
}

static void
gst_rtp_vp9_depay_dispose (GObject * object)
{
  GstRtpVP9Depay *self = GST_RTP_VP9_DEPAY (object);

  if (self->adapter != NULL)
    g_object_unref (self->adapter);
  self->adapter = NULL;

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (gst_rtp_vp9_depay_parent_class)->dispose)
    G_OBJECT_CLASS (gst_rtp_vp9_depay_parent_class)->dispose (object);
}

static void
gst_rtp_vp9_depay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRtpVP9Depay *self = GST_RTP_VP9_DEPAY (object);

  switch (prop_id) {
    case PROP_WAIT_FOR_KEYFRAME:
      self->wait_for_keyframe = g_value_get_boolean (value);
      break;
    case PROP_REQUEST_KEYFRAME:
      self->request_keyframe = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtp_vp9_depay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstRtpVP9Depay *self = GST_RTP_VP9_DEPAY (object);

  switch (prop_id) {
    case PROP_WAIT_FOR_KEYFRAME:
      g_value_set_boolean (value, self->wait_for_keyframe);
      break;
    case PROP_REQUEST_KEYFRAME:
      g_value_set_boolean (value, self->request_keyframe);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static gint
picture_id_compare (guint16 id0, guint16 id1)
{
  guint shift = 16 - (IS_PICTURE_ID_15BITS (id1) ? 15 : 7);
  id0 = id0 << shift;
  id1 = id1 << shift;
  return ((gint16) (id1 - id0)) >> shift;
}

static void
send_last_lost_event (GstRtpVP9Depay * self)
{
  if (self->last_lost_event) {
    GST_DEBUG_OBJECT (self,
        "Sending the last stopped lost event: %" GST_PTR_FORMAT,
        self->last_lost_event);
    GST_RTP_BASE_DEPAYLOAD_CLASS (gst_rtp_vp9_depay_parent_class)
        ->packet_lost (GST_RTP_BASE_DEPAYLOAD_CAST (self),
        self->last_lost_event);
    gst_event_replace (&self->last_lost_event, NULL);
  }
}

static void
send_last_lost_event_if_needed (GstRtpVP9Depay * self, guint new_picture_id)
{
  if (self->last_picture_id == PICTURE_ID_NONE ||
      self->last_picture_id == new_picture_id)
    return;

  if (self->last_lost_event) {
    gboolean send_lost_event = FALSE;
    if (new_picture_id == PICTURE_ID_NONE) {
      GST_DEBUG_OBJECT (self, "Dropping the last stopped lost event "
          "(picture id does not exist): %" GST_PTR_FORMAT,
          self->last_lost_event);
    } else if (IS_PICTURE_ID_15BITS (self->last_picture_id) &&
        !IS_PICTURE_ID_15BITS (new_picture_id)) {
      GST_DEBUG_OBJECT (self, "Dropping the last stopped lost event "
          "(picture id has less bits than before): %" GST_PTR_FORMAT,
          self->last_lost_event);
    } else if (picture_id_compare (self->last_picture_id, new_picture_id) != 1) {
      GstStructure *s = gst_event_writable_structure (self->last_lost_event);

      GST_DEBUG_OBJECT (self, "Sending the last stopped lost event "
          "(gap in picture id %u %u): %" GST_PTR_FORMAT,
          self->last_picture_id, new_picture_id, self->last_lost_event);
      send_lost_event = TRUE;
      /* Prevent rtpbasedepayload from dropping the event now
       * that we have made sure the lost packet was not FEC */
      gst_structure_remove_field (s, "might-have-been-fec");
    }
    if (send_lost_event)
      GST_RTP_BASE_DEPAYLOAD_CLASS (gst_rtp_vp9_depay_parent_class)
          ->packet_lost (GST_RTP_BASE_DEPAYLOAD_CAST (self),
          self->last_lost_event);

    gst_event_replace (&self->last_lost_event, NULL);
  }
}

static GstBuffer *
gst_rtp_vp9_depay_process (GstRTPBaseDepayload * depay, GstRTPBuffer * rtp)
{
  GstRtpVP9Depay *self = GST_RTP_VP9_DEPAY (depay);
  GstBuffer *payload;
  guint8 *data;
  guint hdrsize = 1;
  guint size;
  gint spatial_layer = 0;
  guint picture_id = PICTURE_ID_NONE;
  gboolean i_bit, p_bit, l_bit, f_bit, b_bit, e_bit, v_bit, d_bit = 0;
  gboolean is_start_of_picture;
  gboolean flushed_adapter = FALSE;

  if (G_UNLIKELY (GST_BUFFER_IS_DISCONT (rtp->buffer))) {
    GST_LOG_OBJECT (self, "Discontinuity, flushing adapter");
    gst_adapter_clear (self->adapter);
    self->started = FALSE;
    flushed_adapter = TRUE;
  }

  size = gst_rtp_buffer_get_payload_len (rtp);

  /* Mandatory with at least one header and one vp9 byte */
  if (G_UNLIKELY (size < hdrsize + 1))
    goto too_small;

  data = gst_rtp_buffer_get_payload (rtp);
  i_bit = (data[0] & 0x80) != 0;
  p_bit = (data[0] & 0x40) != 0;
  l_bit = (data[0] & 0x20) != 0;
  f_bit = (data[0] & 0x10) != 0;
  b_bit = (data[0] & 0x08) != 0;
  e_bit = (data[0] & 0x04) != 0;
  v_bit = (data[0] & 0x02) != 0;

  GST_TRACE_OBJECT (self, "IPLFBEV : %d%d%d%d%d%d%d", i_bit, p_bit, l_bit,
      f_bit, b_bit, e_bit, v_bit);

  /* Check I optional header Picture ID */
  if (i_bit) {
    hdrsize++;
    if (G_UNLIKELY (size < hdrsize + 1))
      goto too_small;
    picture_id = data[1];
    /* Check M for 15 bits PictureID */
    if ((data[1] & 0x80) != 0) {
      hdrsize++;
      if (G_UNLIKELY (size < hdrsize + 1))
        goto too_small;
      picture_id = (picture_id << 8) | data[2];
    }
  }

  /* Check L optional header layer indices */
  if (l_bit) {
    spatial_layer = (data[hdrsize] >> 1) & 0x07;
    d_bit = (data[hdrsize] >> 0) & 0x01;
    GST_TRACE_OBJECT (self, "TID=%d, U=%d, SID=%d, D=%d",
        (data[hdrsize] >> 5) & 0x07, (data[hdrsize] >> 4) & 0x01,
        (data[hdrsize] >> 1) & 0x07, (data[hdrsize] >> 0) & 0x01);

    if (spatial_layer == 0 && d_bit != 0) {
      /* Invalid according to draft-ietf-payload-vp9-06, but firefox 61 and
       * chrome 66 sends enchanment layers with SID=0, so let's not drop the
       * packet. */
      GST_LOG_OBJECT (self, "Invalid inter-layer dependency for base layer");
    }

    hdrsize++;
    /* Check TL0PICIDX temporal layer zero index (non-flexible mode) */
    if (!f_bit)
      hdrsize++;
  }

  if (p_bit && f_bit) {
    gint i;

    /* At least one P_DIFF|N, up to three times */
    for (i = 0; i < 3; i++) {
      guint p_diff, n_bit;

      if (G_UNLIKELY (size < hdrsize + 1))
        goto too_small;

      p_diff = data[hdrsize] >> 1;
      n_bit = data[hdrsize] & 0x1;
      GST_TRACE_OBJECT (self, "P_DIFF[%d]=%d", i, p_diff);
      hdrsize++;

      if (!n_bit)
        break;
    }
  }

  /* Check V optional Scalability Structure */
  if (v_bit) {
    guint n_s, y_bit, g_bit;
    guint8 *ss = &data[hdrsize];
    guint sssize = 1;

    if (G_UNLIKELY (size < hdrsize + sssize + 1))
      goto too_small;

    n_s = (ss[0] & 0xe0) >> 5;
    y_bit = (ss[0] & 0x10) != 0;
    g_bit = (ss[0] & 0x08) != 0;

    GST_TRACE_OBJECT (self, "SS header: N_S=%u, Y=%u, G=%u", n_s, y_bit, g_bit);

    sssize += y_bit ? (n_s + 1) * 4 : 0;
    if (G_UNLIKELY (size < hdrsize + sssize + 1))
      goto too_small;

    if (y_bit) {
      guint i;
      for (i = 0; i <= n_s; i++) {
        /* For now, simply use the last layer specified for width and height */
        self->ss_width = ss[1 + i * 4] * 256 + ss[2 + i * 4];
        self->ss_height = ss[3 + i * 4] * 256 + ss[4 + i * 4];
        GST_TRACE_OBJECT (self, "N_S[%d]: WIDTH=%u, HEIGHT=%u", i,
            self->ss_width, self->ss_height);
      }
    }

    if (g_bit) {
      guint i, j;
      guint n_g = ss[sssize];
      sssize++;
      if (G_UNLIKELY (size < hdrsize + sssize + 1))
        goto too_small;
      for (i = 0; i < n_g; i++) {
        guint t = (ss[sssize] & 0xe0) >> 5;
        guint u = (ss[sssize] & 0x10) >> 4;
        guint r = (ss[sssize] & 0x0c) >> 2;
        GST_TRACE_OBJECT (self, "N_G[%u]: 0x%02x -> T=%u, U=%u, R=%u", i,
            ss[sssize], t, u, r);
        for (j = 0; j < r; j++)
          GST_TRACE_OBJECT (self, "  R[%u]: P_DIFF=%u", j, ss[sssize + 1 + j]);
        sssize += 1 + r;
        if (G_UNLIKELY (size < hdrsize + sssize + 1))
          goto too_small;
      }
    }
    hdrsize += sssize;
  }

  GST_DEBUG_OBJECT (depay, "hdrsize %u, size %u, picture id 0x%x",
      hdrsize, size, picture_id);

  if (G_UNLIKELY (hdrsize >= size))
    goto too_small;

  is_start_of_picture = b_bit && (!l_bit || !d_bit);
  /* If this is a start frame AND we are already processing a frame, we need to flush and wait for next start frame */
  if (is_start_of_picture) {
    if (G_UNLIKELY (self->started)) {
      GST_DEBUG_OBJECT (depay, "Incomplete frame, flushing adapter");
      /* keep the current buffer because it may still be used later */
      gst_rtp_base_depayload_flush (depay, TRUE);
      gst_adapter_clear (self->adapter);
      self->started = FALSE;
      flushed_adapter = TRUE;
    }
  }

  if (G_UNLIKELY (!self->started)) {
    self->inter_picture = FALSE;

    /* We have flushed the adapter and this packet does not
     * start a keyframe, request one if needed */
    if (flushed_adapter && (!b_bit || p_bit)) {
      if (self->wait_for_keyframe) {
        GST_DEBUG_OBJECT (self, "Waiting for keyframe after flushing adapter");
        self->waiting_for_keyframe = TRUE;
      }

      if (self->request_keyframe) {
        GST_DEBUG_OBJECT (self, "Requesting keyframe after flushing adapter");
        gst_pad_push_event (GST_RTP_BASE_DEPAYLOAD_SINKPAD (depay),
            gst_video_event_new_upstream_force_key_unit (GST_CLOCK_TIME_NONE,
                TRUE, 0));
      }
    }

    /* Check if this is the start of a VP9 layer frame, otherwise bail */
    if (!b_bit) {
      GST_DEBUG_OBJECT (depay,
          "The layer is missing the first packets, ignoring the packet");
      if (self->stop_lost_events) {
        send_last_lost_event (self);
        self->stop_lost_events = FALSE;
      }
      goto done;
    }

    GST_DEBUG_OBJECT (depay, "Found the start of the layer");
    if (self->stop_lost_events) {
      send_last_lost_event_if_needed (self, picture_id);
      self->stop_lost_events = FALSE;
    }
    self->started = TRUE;
  }

  payload = gst_rtp_buffer_get_payload_subbuffer (rtp, hdrsize, -1);
  if (GST_LEVEL_MEMDUMP <= gst_debug_category_get_threshold (GST_CAT_DEFAULT)) {
    GstMapInfo map;
    gst_buffer_map (payload, &map, GST_MAP_READ);
    GST_MEMDUMP_OBJECT (self, "vp9 payload", map.data, 16);
    gst_buffer_unmap (payload, &map);
  }
  gst_adapter_push (self->adapter, payload);
  self->last_picture_id = picture_id;
  self->inter_picture |= p_bit;

  /* Marker indicates that it was the last rtp packet for this picture. Note
   * that if spatial scalability is used, e_bit will be set for the last
   * packet of a frame while the marker bit is not set until the last packet
   * of the picture. */
  if (gst_rtp_buffer_get_marker (rtp)) {
    GstBuffer *out;

    GST_DEBUG_OBJECT (depay,
        "Found the end of the frame (%" G_GSIZE_FORMAT " bytes)",
        gst_adapter_available (self->adapter));

    if (gst_adapter_available (self->adapter) < 10)
      goto too_small;

    out = gst_adapter_take_buffer (self->adapter,
        gst_adapter_available (self->adapter));

    self->started = FALSE;

    /* mark keyframes */
    out = gst_buffer_make_writable (out);
    /* Filter away all metas that are not sensible to copy */
    gst_rtp_drop_non_video_meta (self, out);
    if (self->inter_picture) {
      GST_BUFFER_FLAG_SET (out, GST_BUFFER_FLAG_DELTA_UNIT);

      if (self->waiting_for_keyframe) {
        gst_rtp_base_depayload_flush (depay, FALSE);
        gst_buffer_unref (out);
        out = NULL;
        GST_INFO_OBJECT (self, "Dropping inter-frame before intra-frame");
        gst_pad_push_event (GST_RTP_BASE_DEPAYLOAD_SINKPAD (depay),
            gst_video_event_new_upstream_force_key_unit (GST_CLOCK_TIME_NONE,
                TRUE, 0));
      }
    } else {
      GST_BUFFER_FLAG_UNSET (out, GST_BUFFER_FLAG_DELTA_UNIT);

      if (self->last_width != self->ss_width ||
          self->last_height != self->ss_height) {
        GstCaps *srccaps;

        /* Width and height are optional in the RTP header. Consider to parse
         * the frame header in addition if missing from RTP header */
        if (self->ss_width != 0 && self->ss_height != 0) {
          srccaps = gst_caps_new_simple ("video/x-vp9",
              "framerate", GST_TYPE_FRACTION, 0, 1,
              "width", G_TYPE_INT, self->ss_width,
              "height", G_TYPE_INT, self->ss_height, NULL);
        } else {
          srccaps = gst_caps_new_simple ("video/x-vp9",
              "framerate", GST_TYPE_FRACTION, 0, 1, NULL);
        }

        gst_pad_set_caps (GST_RTP_BASE_DEPAYLOAD_SRCPAD (depay), srccaps);
        gst_caps_unref (srccaps);

        self->last_width = self->ss_width;
        self->last_height = self->ss_height;
        self->ss_width = 0;
        self->ss_height = 0;
      }

      self->waiting_for_keyframe = FALSE;
    }

    if (picture_id != PICTURE_ID_NONE)
      self->stop_lost_events = TRUE;
    return out;
  }

done:
  gst_rtp_base_depayload_dropped (depay);
  return NULL;

too_small:
  GST_LOG_OBJECT (self, "Invalid rtp packet (too small), ignoring");
  gst_rtp_base_depayload_flush (depay, FALSE);
  gst_adapter_clear (self->adapter);
  self->started = FALSE;
  goto done;
}

static GstStateChangeReturn
gst_rtp_vp9_depay_change_state (GstElement * element, GstStateChange transition)
{
  GstRtpVP9Depay *self = GST_RTP_VP9_DEPAY (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      self->last_width = -1;
      self->last_height = -1;
      self->last_picture_id = PICTURE_ID_NONE;
      gst_event_replace (&self->last_lost_event, NULL);
      self->stop_lost_events = FALSE;
      self->waiting_for_keyframe = TRUE;
      break;
    default:
      break;
  }

  return
      GST_ELEMENT_CLASS (gst_rtp_vp9_depay_parent_class)->change_state (element,
      transition);
}

static gboolean
gst_rtp_vp9_depay_handle_event (GstRTPBaseDepayload * depay, GstEvent * event)
{
  GstRtpVP9Depay *self = GST_RTP_VP9_DEPAY (depay);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
      self->last_width = -1;
      self->last_height = -1;
      self->last_picture_id = PICTURE_ID_NONE;
      gst_event_replace (&self->last_lost_event, NULL);
      self->stop_lost_events = FALSE;
      break;
    default:
      break;
  }

  return
      GST_RTP_BASE_DEPAYLOAD_CLASS
      (gst_rtp_vp9_depay_parent_class)->handle_event (depay, event);
}

static gboolean
gst_rtp_vp9_depay_packet_lost (GstRTPBaseDepayload * depay, GstEvent * event)
{
  GstRtpVP9Depay *self = GST_RTP_VP9_DEPAY (depay);
  const GstStructure *s;
  gboolean might_have_been_fec;

  s = gst_event_get_structure (event);

  if (self->stop_lost_events) {
    if (gst_structure_get_boolean (s, "might-have-been-fec",
            &might_have_been_fec)
        && might_have_been_fec) {
      GST_DEBUG_OBJECT (depay, "Stopping lost event %" GST_PTR_FORMAT, event);
      gst_event_replace (&self->last_lost_event, event);
      return TRUE;
    }
  } else if (self->last_picture_id != PICTURE_ID_NONE) {
    GstStructure *s = gst_event_writable_structure (self->last_lost_event);

    /* We are currently processing a picture, let's make sure the
     * base depayloader doesn't drop this lost event */
    gst_structure_remove_field (s, "might-have-been-fec");
  }

  return
      GST_RTP_BASE_DEPAYLOAD_CLASS
      (gst_rtp_vp9_depay_parent_class)->packet_lost (depay, event);
}
