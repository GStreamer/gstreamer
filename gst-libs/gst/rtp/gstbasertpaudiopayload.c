/* GStreamer
 * Copyright (C) <2006> Philippe Khalaf <burger@speedy.org> 
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <gst/rtp/gstrtpbuffer.h>
#include <math.h>

#include "gstbasertpaudiopayload.h"

GST_DEBUG_CATEGORY (basertpaudiopayload_debug);
#define GST_CAT_DEFAULT (basertpaudiopayload_debug)

/* let us define a minimum of 10 ms for sample based codecs */
#define GST_RTP_MIN_PTIME_MS 10

static void gst_basertpaudiopayload_finalize (GObject * object);

static GstFlowReturn
gst_basertpaudiopayload_push (GstBaseRTPPayload * basepayload, guint8 * data,
    guint payload_len, GstClockTime timestamp);

static GstFlowReturn gst_basertpaudiopayload_handle_buffer (GstBaseRTPPayload *
    payload, GstBuffer * buffer);

static GstFlowReturn
gst_basertpaudiopayload_handle_frame_based_buffer (GstBaseRTPPayload *
    basepayload, GstBuffer * buffer);

static GstFlowReturn
gst_basertpaudiopayload_handle_sample_based_buffer (GstBaseRTPPayload *
    basepayload, GstBuffer * buffer);

GST_BOILERPLATE (GstBaseRTPAudioPayload, gst_basertpaudiopayload,
    GstBaseRTPPayload, GST_TYPE_BASE_RTP_PAYLOAD);

static void
gst_basertpaudiopayload_base_init (gpointer klass)
{
}

static void
gst_basertpaudiopayload_class_init (GstBaseRTPAudioPayloadClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseRTPPayloadClass *gstbasertppayload_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasertppayload_class = (GstBaseRTPPayloadClass *) klass;
  gobject_class->finalize = gst_basertpaudiopayload_finalize;

  parent_class = g_type_class_ref (GST_TYPE_BASE_RTP_PAYLOAD);

  gstbasertppayload_class->handle_buffer =
      gst_basertpaudiopayload_handle_buffer;

  GST_DEBUG_CATEGORY_INIT (basertpaudiopayload_debug, "basertpaudiopayload", 0,
      "base audio RTP payloader");
}

static void
gst_basertpaudiopayload_init (GstBaseRTPAudioPayload * basertpaudiopayload,
    GstBaseRTPAudioPayloadClass * klass)
{
  basertpaudiopayload->adapter = gst_adapter_new ();
  basertpaudiopayload->adapter_base_ts = 0;

  basertpaudiopayload->type = AUDIO_CODEC_TYPE_NONE;

  /* these need to be set by child object if frame based */
  basertpaudiopayload->frame_size = 0;
  basertpaudiopayload->frame_duration = 0;

  /* these need to be set by child object if sample based */
  basertpaudiopayload->sample_size = 0;
}

static void
gst_basertpaudiopayload_finalize (GObject * object)
{
  GstBaseRTPAudioPayload *basertpaudiopayload;

  basertpaudiopayload = GST_BASE_RTP_AUDIO_PAYLOAD (object);
  g_object_unref (basertpaudiopayload->adapter);
  basertpaudiopayload->adapter = NULL;

  GST_CALL_PARENT (G_OBJECT_CLASS, finalize, (object));
}

void
gst_basertpaudiopayload_set_frame_based (GstBaseRTPAudioPayload *
    basertpaudiopayload)
{
  g_return_if_fail (basertpaudiopayload != NULL);

  if (basertpaudiopayload->type != AUDIO_CODEC_TYPE_NONE) {
    GST_ERROR_OBJECT (basertpaudiopayload,
        "Codec type already set! You should only set this once!");
  }
  basertpaudiopayload->type = AUDIO_CODEC_TYPE_FRAME_BASED;
}

void
gst_basertpaudiopayload_set_sample_based (GstBaseRTPAudioPayload *
    basertpaudiopayload)
{
  g_return_if_fail (basertpaudiopayload != NULL);

  if (basertpaudiopayload->type != AUDIO_CODEC_TYPE_NONE) {
    GST_ERROR_OBJECT (basertpaudiopayload,
        "Codec type already set! You should only set this once!");
  }
  basertpaudiopayload->type = AUDIO_CODEC_TYPE_SAMPLE_BASED;
}

/* These are options that need to be set for frame based audio codecs */
void
gst_basertpaudiopayload_set_frame_options (GstBaseRTPAudioPayload
    * basertpaudiopayload, gint frame_duration, gint frame_size)
{
  g_return_if_fail (basertpaudiopayload != NULL);

  basertpaudiopayload->frame_size = frame_size;
  basertpaudiopayload->frame_duration = frame_duration;
}

void
gst_basertpaudiopayload_set_sample_options (GstBaseRTPAudioPayload
    * basertpaudiopayload, gint sample_size)
{
  g_return_if_fail (basertpaudiopayload != NULL);

  basertpaudiopayload->sample_size = sample_size;
}

static GstFlowReturn
gst_basertpaudiopayload_handle_buffer (GstBaseRTPPayload * basepayload,
    GstBuffer * buffer)
{
  GstFlowReturn ret;
  GstBaseRTPAudioPayload *basertpaudiopayload;

  basertpaudiopayload = GST_BASE_RTP_AUDIO_PAYLOAD (basepayload);

  ret = GST_FLOW_ERROR;

  if (basertpaudiopayload->type == AUDIO_CODEC_TYPE_FRAME_BASED) {
    ret = gst_basertpaudiopayload_handle_frame_based_buffer (basepayload,
        buffer);
  } else if (basertpaudiopayload->type == AUDIO_CODEC_TYPE_SAMPLE_BASED) {
    ret = gst_basertpaudiopayload_handle_sample_based_buffer (basepayload,
        buffer);
  } else {
    GST_DEBUG_OBJECT (basertpaudiopayload, "Audio codec type not set");
  }

  return ret;
}

/* this assumes all frames have a constant duration and a constant size */
static GstFlowReturn
gst_basertpaudiopayload_handle_frame_based_buffer (GstBaseRTPPayload *
    basepayload, GstBuffer * buffer)
{
  GstBaseRTPAudioPayload *basertpaudiopayload;
  guint payload_len;
  guint8 *data;
  GstFlowReturn ret;
  guint available;
  gint frame_size, frame_duration;

  guint maxptime_octets = G_MAXUINT;

  ret = GST_FLOW_ERROR;

  basertpaudiopayload = GST_BASE_RTP_AUDIO_PAYLOAD (basepayload);

  if (basertpaudiopayload->frame_size == 0 ||
      basertpaudiopayload->frame_duration == 0) {
    GST_DEBUG_OBJECT (basertpaudiopayload, "Required options not set");
    gst_buffer_unref (buffer);
    return GST_FLOW_ERROR;
  }
  frame_size = basertpaudiopayload->frame_size;
  frame_duration = basertpaudiopayload->frame_duration;

  /* If buffer fits on an RTP packet, let's just push it through without using
   * the adapter */
  /* this will check again max_ptime and max_mtu */
  if (!gst_basertppayload_is_filled (basepayload,
          gst_rtp_buffer_calc_packet_len (GST_BUFFER_SIZE (buffer), 0, 0),
          GST_BUFFER_DURATION (buffer))) {
    ret = gst_basertpaudiopayload_push (basepayload, GST_BUFFER_DATA (buffer),
        GST_BUFFER_SIZE (buffer), GST_BUFFER_TIMESTAMP (buffer));
    gst_buffer_unref (buffer);

    return ret;
  }

  /* TODO : would be nice if we had some property that told the payloader to put
   * just 1 frame per RTP packet, for the moment we can set the ptime to 0 or
   * something smaller or equal to a frame duration */

  /* max number of bytes based on given ptime, has to be multiple of
   * frame_duration */
  if (basepayload->max_ptime != -1) {
    guint ptime_ms = basepayload->max_ptime / 1000000;

    maxptime_octets = frame_size * (int) (ptime_ms / frame_duration);
    if (maxptime_octets == 0) {
      GST_WARNING_OBJECT (basertpaudiopayload,
          "Given ptime %d is smaller than minimum %d ms, overwriting to minimum",
          ptime_ms, frame_duration);
      maxptime_octets = frame_size;
    }
  }

  /* if the adapter is empty (should be), let's set the base timestamp */
  if (gst_adapter_available (basertpaudiopayload->adapter) == 0) {
    basertpaudiopayload->adapter_base_ts = GST_BUFFER_TIMESTAMP (buffer);
  } else {
    GST_ERROR_OBJECT (basertpaudiopayload,
        "Adapter should be empty but is not!");
    return GST_FLOW_ERROR;
  }

  gst_adapter_push (basertpaudiopayload->adapter, buffer);

  available = gst_adapter_available (basertpaudiopayload->adapter);

  /* as long as we have full frames */
  /* this loop will always empty the adapter till the last frame */
  /* TODO Make it possible to set a minimum size per packet, this way the
   * algorithm doesn't empty the adapter if there is too little data left and
   * will wait until the next buffers to arrive */
  while (available >= frame_size) {
    /* we need to see how many frames we can get based on maximum MTU, maximum
     * ptime and the number of bytes available in the adapter */
    payload_len = MIN (MIN (
            /* MTU max */
            (int) (gst_rtp_buffer_calc_payload_len (GST_BASE_RTP_PAYLOAD_MTU
                    (basertpaudiopayload), 0, 0) / frame_size) * frame_size,
            /* ptime max */
            maxptime_octets),
        /* currently available */
        floor (available / frame_size) * frame_size);

    data =
        (guint8 *) gst_adapter_peek (basertpaudiopayload->adapter, payload_len);
    ret =
        gst_basertpaudiopayload_push (basepayload, data, payload_len,
        basertpaudiopayload->adapter_base_ts);

    gst_adapter_flush (basertpaudiopayload->adapter, payload_len);
    gfloat ts_inc = (payload_len * frame_duration) / frame_size;

    ts_inc = ts_inc * GST_MSECOND;
    basertpaudiopayload->adapter_base_ts += ts_inc;
    GST_DEBUG_OBJECT (basertpaudiopayload, "%f %f %d", ts_inc,
        ts_inc * GST_MSECOND, (payload_len * frame_duration) / frame_size);
    GST_DEBUG_OBJECT (basertpaudiopayload, "Pushing with ts %" GST_TIME_FORMAT,
        GST_TIME_ARGS (basertpaudiopayload->adapter_base_ts));

    available = gst_adapter_available (basertpaudiopayload->adapter);
  }

  /* adapter should be freed by now */
  if (available != 0) {
    GST_ERROR_OBJECT (basertpaudiopayload,
        "Adapter should be empty but is not!");
    return GST_FLOW_ERROR;
  }

  return ret;
}

static GstFlowReturn
gst_basertpaudiopayload_handle_sample_based_buffer (GstBaseRTPPayload *
    basepayload, GstBuffer * buffer)
{
  GstBaseRTPAudioPayload *basertpaudiopayload;
  guint payload_len;
  guint8 *data;
  GstFlowReturn ret;
  guint available;

  guint maxptime_octets = G_MAXUINT;

  guint minptime_octets = 0;
  guint sample_size;

  ret = GST_FLOW_ERROR;

  basertpaudiopayload = GST_BASE_RTP_AUDIO_PAYLOAD (basepayload);

  if (basertpaudiopayload->sample_size == 0) {
    GST_DEBUG_OBJECT (basertpaudiopayload, "Required options not set");
    gst_buffer_unref (buffer);
    return GST_FLOW_ERROR;
  }
  sample_size = basertpaudiopayload->sample_size;

  /* If buffer fits on an RTP packet, let's just push it through without using
   * the adapter */
  /* this will check again max_ptime and max_mtu */
  if (!gst_basertppayload_is_filled (basepayload,
          gst_rtp_buffer_calc_packet_len (GST_BUFFER_SIZE (buffer), 0, 0),
          GST_BUFFER_DURATION (buffer))) {
    ret = gst_basertpaudiopayload_push (basepayload, GST_BUFFER_DATA (buffer),
        GST_BUFFER_SIZE (buffer), GST_BUFFER_TIMESTAMP (buffer));
    gst_buffer_unref (buffer);

    return ret;
  }

  /* max number of bytes based on given ptime */
  if (basepayload->max_ptime != -1) {
    maxptime_octets = basepayload->max_ptime * basepayload->clock_rate /
        (sample_size * GST_SECOND);
    minptime_octets = GST_RTP_MIN_PTIME_MS * basepayload->clock_rate /
        (sample_size * 1000);
    GST_DEBUG_OBJECT (basertpaudiopayload,
        "Calculated max_octects %u and min_octets %u", maxptime_octets,
        minptime_octets);
    if (maxptime_octets < minptime_octets) {
      GST_WARNING_OBJECT (basertpaudiopayload,
          "Given ptime %d is smaller than minimum %d, replacing by %d",
          maxptime_octets, minptime_octets, minptime_octets);
      maxptime_octets = minptime_octets;
    }
  }

  /* if the adapter is empty (should be), let's set the base timestamp */
  if (gst_adapter_available (basertpaudiopayload->adapter) == 0) {
    basertpaudiopayload->adapter_base_ts = GST_BUFFER_TIMESTAMP (buffer);
    GST_DEBUG_OBJECT (basertpaudiopayload, "Setting to %" GST_TIME_FORMAT,
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)));
  }

  gst_adapter_push (basertpaudiopayload->adapter, buffer);

  available = gst_adapter_available (basertpaudiopayload->adapter);

  /* as long as we have full frames */
  /* this loop will always empty the adapter till the last frame */
  /* TODO Make it possible to set a minimum size per packet, this way the
   * algorithm doesn't empty the adapter if there is too little data left and
   * will wait until the next buffers to arrive */
  while (available >= minptime_octets) {
    /* we need to see how many frames we can get based on maximum MTU, maximum
     * ptime and the number of bytes available in the adapter */
    payload_len = MIN (MIN (
            /* MTU max */
            gst_rtp_buffer_calc_payload_len (GST_BASE_RTP_PAYLOAD_MTU
                (basertpaudiopayload), 0, 0),
            /* ptime max */
            maxptime_octets),
        /* currently available */
        available);

    data =
        (guint8 *) gst_adapter_peek (basertpaudiopayload->adapter, payload_len);
    GST_DEBUG_OBJECT (basertpaudiopayload, "Pushing with ts %" GST_TIME_FORMAT,
        GST_TIME_ARGS (basertpaudiopayload->adapter_base_ts));
    ret =
        gst_basertpaudiopayload_push (basepayload, data, payload_len,
        basertpaudiopayload->adapter_base_ts);

    gst_adapter_flush (basertpaudiopayload->adapter, payload_len);
    gfloat num = payload_len;
    gfloat datarate = (sample_size * basepayload->clock_rate);

    basertpaudiopayload->adapter_base_ts +=
        /* payload_len (bytes) * nsecs/sec / datarate (bytes*sec) */
        num / datarate * GST_SECOND;
    GST_DEBUG_OBJECT (basertpaudiopayload, "Calculating ts inc %f %f %f", num,
        datarate, num / datarate * GST_SECOND);
    GST_DEBUG_OBJECT (basertpaudiopayload, "New ts is %" GST_TIME_FORMAT,
        GST_TIME_ARGS (basertpaudiopayload->adapter_base_ts));

    available = gst_adapter_available (basertpaudiopayload->adapter);
  }

  return ret;
}

static GstFlowReturn
gst_basertpaudiopayload_push (GstBaseRTPPayload * basepayload, guint8 * data,
    guint payload_len, GstClockTime timestamp)
{
  GstBuffer *outbuf;
  guint8 *payload;
  GstFlowReturn ret;

  /* create buffer to hold the payload */
  outbuf = gst_rtp_buffer_new_allocate (payload_len, 0, 0);

  /* copy payload */
  gst_rtp_buffer_set_payload_type (outbuf, basepayload->pt);
  payload = gst_rtp_buffer_get_payload (outbuf);
  memcpy (payload, data, payload_len);

  GST_BUFFER_TIMESTAMP (outbuf) = timestamp;
  ret = gst_basertppayload_push (basepayload, outbuf);

  return ret;
}
