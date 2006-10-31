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

/**
 * SECTION:gstbasertpaudiopayload
 * @short_description: Base class for audio RTP payloader
 *
 * <refsect2>
 * <para>
 * Provides a base class for audio RTP payloaders for frame or sample based
 * audio codecs (constant bitrate)
 * </para>
 * <para>
 * This class derives from GstBaseRTPPayload. It can be used for payloading
 * audio codecs. It will only work with constant bitrate codecs. It supports
 * both frame based and sample based codecs. It takes care of packing up the
 * audio data into RTP packets and filling up the headers accordingly. The
 * payloading is done based on the maximum MTU (mtu) and the maximum time per
 * packet (max-ptime). The general idea is to divide large data buffers into
 * smaller RTP packets. The RTP packet size is the minimum of either the MTU,
 * max-ptime (if set) or available data. Any residual data is always sent in a
 * last RTP packet (no minimum RTP packet size). A minimum packet size might be
 * added in future versions if the need arises. In the case of frame
 * based codecs, the resulting RTP packets always contain full frames.
 * </para>
 * <title>Usage</title>
 * <para>
 * To use this base class, your child element needs to call either
 * gst_basertpaudiopayload_set_frame_based() or
 * gst_basertpaudiopayload_set_sample_based(). This is usually done in the
 * element's _init() function. Then, the child element must call either
 * gst_basertpaudiopayload_set_frame_options() or
 * gst_basertpaudiopayload_set_sample_options(). Since GstBaseRTPAudioPayload
 * derives from GstBaseRTPPayload, the child element must set any variables or
 * call/override any functions required by that base class. The child element
 * does not need to override any other functions specific to
 * GstBaseRTPAudioPayload.
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <gst/rtp/gstrtpbuffer.h>

#include "gstbasertpaudiopayload.h"

GST_DEBUG_CATEGORY_STATIC (basertpaudiopayload_debug);
#define GST_CAT_DEFAULT (basertpaudiopayload_debug)

typedef enum
{
  AUDIO_CODEC_TYPE_NONE,
  AUDIO_CODEC_TYPE_FRAME_BASED,
  AUDIO_CODEC_TYPE_SAMPLE_BASED
} AudioCodecType;

struct _GstBaseRTPAudioPayloadPrivate
{
  AudioCodecType type;
};

#define GST_BASE_RTP_AUDIO_PAYLOAD_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), GST_TYPE_BASE_RTP_AUDIO_PAYLOAD, \
                                GstBaseRTPAudioPayloadPrivate))

static void gst_base_rtp_audio_payload_finalize (GObject * object);

static GstFlowReturn
gst_base_rtp_audio_payload_push (GstBaseRTPPayload * basepayload, guint8 * data,
    guint payload_len, GstClockTime timestamp);

static GstFlowReturn gst_base_rtp_audio_payload_handle_buffer (GstBaseRTPPayload
    * payload, GstBuffer * buffer);

static GstFlowReturn
gst_base_rtp_audio_payload_handle_frame_based_buffer (GstBaseRTPPayload *
    basepayload, GstBuffer * buffer);

static GstFlowReturn
gst_base_rtp_audio_payload_handle_sample_based_buffer (GstBaseRTPPayload *
    basepayload, GstBuffer * buffer);

GST_BOILERPLATE (GstBaseRTPAudioPayload, gst_base_rtp_audio_payload,
    GstBaseRTPPayload, GST_TYPE_BASE_RTP_PAYLOAD);

static void
gst_base_rtp_audio_payload_base_init (gpointer klass)
{
}

static void
gst_base_rtp_audio_payload_class_init (GstBaseRTPAudioPayloadClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseRTPPayloadClass *gstbasertppayload_class;

  g_type_class_add_private (klass, sizeof (GstBaseRTPAudioPayloadPrivate));

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasertppayload_class = (GstBaseRTPPayloadClass *) klass;
  gobject_class->finalize =
      GST_DEBUG_FUNCPTR (gst_base_rtp_audio_payload_finalize);

  parent_class = g_type_class_ref (GST_TYPE_BASE_RTP_PAYLOAD);

  gstbasertppayload_class->handle_buffer =
      GST_DEBUG_FUNCPTR (gst_base_rtp_audio_payload_handle_buffer);

  GST_DEBUG_CATEGORY_INIT (basertpaudiopayload_debug, "basertpaudiopayload", 0,
      "base audio RTP payloader");
}

static void
gst_base_rtp_audio_payload_init (GstBaseRTPAudioPayload * basertpaudiopayload,
    GstBaseRTPAudioPayloadClass * klass)
{
  basertpaudiopayload->priv =
      GST_BASE_RTP_AUDIO_PAYLOAD_GET_PRIVATE (basertpaudiopayload);

  basertpaudiopayload->base_ts = 0;

  basertpaudiopayload->priv->type = AUDIO_CODEC_TYPE_NONE;

  /* these need to be set by child object if frame based */
  basertpaudiopayload->frame_size = 0;
  basertpaudiopayload->frame_duration = 0;

  /* these need to be set by child object if sample based */
  basertpaudiopayload->sample_size = 0;
}

static void
gst_base_rtp_audio_payload_finalize (GObject * object)
{
  GstBaseRTPAudioPayload *basertpaudiopayload;

  basertpaudiopayload = GST_BASE_RTP_AUDIO_PAYLOAD (object);

  GST_CALL_PARENT (G_OBJECT_CLASS, finalize, (object));
}

/**
 * gst_base_rtp_audio_payload_set_frame_based:
 * @basertpaudiopayload: a pointer to the element.
 *
 * Tells #GstBaseRTPAudioPayload that the child element is for a frame based
 * audio codec
 *
 */
void
gst_base_rtp_audio_payload_set_frame_based (GstBaseRTPAudioPayload *
    basertpaudiopayload)
{
  g_return_if_fail (basertpaudiopayload != NULL);

  g_return_if_fail (basertpaudiopayload->priv->type == AUDIO_CODEC_TYPE_NONE);

  basertpaudiopayload->priv->type = AUDIO_CODEC_TYPE_FRAME_BASED;
}

/**
 * gst_base_rtp_audio_payload_set_sample_based:
 * @basertpaudiopayload: a pointer to the element.
 *
 * Tells #GstBaseRTPAudioPayload that the child element is for a sample based
 * audio codec
 *
 */
void
gst_base_rtp_audio_payload_set_sample_based (GstBaseRTPAudioPayload *
    basertpaudiopayload)
{
  g_return_if_fail (basertpaudiopayload != NULL);

  g_return_if_fail (basertpaudiopayload->priv->type == AUDIO_CODEC_TYPE_NONE);

  basertpaudiopayload->priv->type = AUDIO_CODEC_TYPE_SAMPLE_BASED;
}

/**
 * gst_base_rtp_audio_payload_set_frame_options:
 * @basertpaudiopayload: a pointer to the element.
 * @frame_duration: The duraction of an audio frame in milliseconds. 
 * @frame_size: The size of an audio frame in bytes.
 *
 * Sets the options for frame based audio codecs.
 *
 */
void
gst_base_rtp_audio_payload_set_frame_options (GstBaseRTPAudioPayload
    * basertpaudiopayload, gint frame_duration, gint frame_size)
{
  g_return_if_fail (basertpaudiopayload != NULL);

  basertpaudiopayload->frame_size = frame_size;
  basertpaudiopayload->frame_duration = frame_duration;
}

/**
 * gst_base_rtp_audio_payload_set_sample_options:
 * @basertpaudiopayload: a pointer to the element.
 * @sample_size: Size per sample in bytes.
 *
 * Sets the options for sample based audio codecs.
 *
 */
void
gst_base_rtp_audio_payload_set_sample_options (GstBaseRTPAudioPayload
    * basertpaudiopayload, gint sample_size)
{
  g_return_if_fail (basertpaudiopayload != NULL);

  basertpaudiopayload->sample_size = sample_size;
}

static GstFlowReturn
gst_base_rtp_audio_payload_handle_buffer (GstBaseRTPPayload * basepayload,
    GstBuffer * buffer)
{
  GstFlowReturn ret;
  GstBaseRTPAudioPayload *basertpaudiopayload;

  basertpaudiopayload = GST_BASE_RTP_AUDIO_PAYLOAD (basepayload);

  ret = GST_FLOW_ERROR;

  if (basertpaudiopayload->priv->type == AUDIO_CODEC_TYPE_FRAME_BASED) {
    ret = gst_base_rtp_audio_payload_handle_frame_based_buffer (basepayload,
        buffer);
  } else if (basertpaudiopayload->priv->type == AUDIO_CODEC_TYPE_SAMPLE_BASED) {
    ret = gst_base_rtp_audio_payload_handle_sample_based_buffer (basepayload,
        buffer);
  } else {
    GST_DEBUG_OBJECT (basertpaudiopayload, "Audio codec type not set");
  }

  return ret;
}

/* this assumes all frames have a constant duration and a constant size */
static GstFlowReturn
gst_base_rtp_audio_payload_handle_frame_based_buffer (GstBaseRTPPayload *
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

  /* If buffer fits on an RTP packet, let's just push it through */
  /* this will check against max_ptime and max_mtu */
  if (!gst_basertppayload_is_filled (basepayload,
          gst_rtp_buffer_calc_packet_len (GST_BUFFER_SIZE (buffer), 0, 0),
          GST_BUFFER_DURATION (buffer))) {
    ret = gst_base_rtp_audio_payload_push (basepayload,
        GST_BUFFER_DATA (buffer), GST_BUFFER_SIZE (buffer),
        GST_BUFFER_TIMESTAMP (buffer));
    gst_buffer_unref (buffer);

    return ret;
  }

  /* max number of bytes based on given ptime, has to be multiple of
   * frame_duration */
  if (basepayload->max_ptime != -1) {
    guint ptime_ms = basepayload->max_ptime / 1000000;

    maxptime_octets = frame_size * (int) (ptime_ms / frame_duration);
    if (maxptime_octets == 0) {
      GST_WARNING_OBJECT (basertpaudiopayload, "Given ptime %d is smaller than"
          " minimum %d ms, overwriting to minimum", ptime_ms, frame_duration);
      maxptime_octets = frame_size;
    }
  }

  /* let's set the base timestamp */
  basertpaudiopayload->base_ts = GST_BUFFER_TIMESTAMP (buffer);

  available = GST_BUFFER_SIZE (buffer);
  data = (guint8 *) GST_BUFFER_DATA (buffer);

  /* as long as we have full frames */
  /* this loop will push all available buffers till the last frame */
  while (available >= frame_size) {
    /* we need to see how many frames we can get based on maximum MTU, maximum
     * ptime and the number of bytes available */
    payload_len = MIN (MIN (
            /* MTU max */
            (int) (gst_rtp_buffer_calc_payload_len (GST_BASE_RTP_PAYLOAD_MTU
                    (basertpaudiopayload), 0, 0) / frame_size) * frame_size,
            /* ptime max */
            maxptime_octets),
        /* currently available */
        (available / frame_size) * frame_size);

    ret = gst_base_rtp_audio_payload_push (basepayload, data, payload_len,
        basertpaudiopayload->base_ts);

    gfloat ts_inc = (payload_len * frame_duration) / frame_size;

    ts_inc = ts_inc * GST_MSECOND;
    basertpaudiopayload->base_ts += ts_inc;

    available -= payload_len;
    data += payload_len;
  }

  gst_buffer_unref (buffer);

  /* none should be available by now */
  if (available != 0) {
    GST_ERROR_OBJECT (basertpaudiopayload, "The buffer size is not a multiple"
        " of the frame_size");
    return GST_FLOW_ERROR;
  }

  return ret;
}

static GstFlowReturn
gst_base_rtp_audio_payload_handle_sample_based_buffer (GstBaseRTPPayload *
    basepayload, GstBuffer * buffer)
{
  GstBaseRTPAudioPayload *basertpaudiopayload;
  guint payload_len;
  guint8 *data;
  GstFlowReturn ret;
  guint available;

  guint maxptime_octets = G_MAXUINT;

  guint sample_size;

  ret = GST_FLOW_ERROR;

  basertpaudiopayload = GST_BASE_RTP_AUDIO_PAYLOAD (basepayload);

  if (basertpaudiopayload->sample_size == 0) {
    GST_DEBUG_OBJECT (basertpaudiopayload, "Required options not set");
    gst_buffer_unref (buffer);
    return GST_FLOW_ERROR;
  }
  sample_size = basertpaudiopayload->sample_size;

  /* If buffer fits on an RTP packet, let's just push it through */
  /* this will check against max_ptime and max_mtu */
  if (!gst_basertppayload_is_filled (basepayload,
          gst_rtp_buffer_calc_packet_len (GST_BUFFER_SIZE (buffer), 0, 0),
          GST_BUFFER_DURATION (buffer))) {
    ret = gst_base_rtp_audio_payload_push (basepayload,
        GST_BUFFER_DATA (buffer), GST_BUFFER_SIZE (buffer),
        GST_BUFFER_TIMESTAMP (buffer));
    gst_buffer_unref (buffer);

    return ret;
  }

  /* max number of bytes based on given ptime */
  if (basepayload->max_ptime != -1) {
    maxptime_octets = basepayload->max_ptime * basepayload->clock_rate /
        (sample_size * GST_SECOND);
    GST_DEBUG_OBJECT (basertpaudiopayload, "Calculated max_octects %u",
        maxptime_octets);
  }

  /* let's set the base timestamp */
  basertpaudiopayload->base_ts = GST_BUFFER_TIMESTAMP (buffer);
  GST_DEBUG_OBJECT (basertpaudiopayload, "Setting to %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)));

  available = GST_BUFFER_SIZE (buffer);
  data = (guint8 *) GST_BUFFER_DATA (buffer);

  /* as long as we have full frames */
  /* this loop will use all available data until the last byte */
  while (available) {
    /* we need to see how many frames we can get based on maximum MTU, maximum
     * ptime and the number of bytes available */
    payload_len = MIN (MIN (
            /* MTU max */
            gst_rtp_buffer_calc_payload_len (GST_BASE_RTP_PAYLOAD_MTU
                (basertpaudiopayload), 0, 0),
            /* ptime max */
            maxptime_octets),
        /* currently available */
        available);

    ret = gst_base_rtp_audio_payload_push (basepayload, data, payload_len,
        basertpaudiopayload->base_ts);

    gfloat num = payload_len;
    gfloat datarate = (sample_size * basepayload->clock_rate);

    basertpaudiopayload->base_ts +=
        /* payload_len (bytes) * nsecs/sec / datarate (bytes*sec) */
        num / datarate * GST_SECOND;
    GST_DEBUG_OBJECT (basertpaudiopayload, "New ts is %" GST_TIME_FORMAT,
        GST_TIME_ARGS (basertpaudiopayload->base_ts));

    available -= payload_len;
    data += payload_len;
  }

  gst_buffer_unref (buffer);

  return ret;
}

static GstFlowReturn
gst_base_rtp_audio_payload_push (GstBaseRTPPayload * basepayload, guint8 * data,
    guint payload_len, GstClockTime timestamp)
{
  GstBuffer *outbuf;
  guint8 *payload;
  GstFlowReturn ret;

  GST_DEBUG_OBJECT (basepayload, "Pushing %d bytes ts %" GST_TIME_FORMAT,
      payload_len, GST_TIME_ARGS (timestamp));

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
