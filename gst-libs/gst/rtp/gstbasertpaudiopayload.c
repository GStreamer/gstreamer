/* GStreamer
 * Copyright (C) <2006> Philippe Khalaf <philippe.kalaf@collabora.co.uk>
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
 * max-ptime (if set) or available data. The RTP packet size is always larger or
 * equal to min-ptime (if set). If min-ptime is not set, any residual data is
 * sent in a last RTP packet. In the case of frame based codecs, the resulting
 * RTP packets always contain full frames.
 * </para>
 * <title>Usage</title>
 * <para>
 * To use this base class, your child element needs to call either
 * gst_base_rtp_audio_payload_set_frame_based() or
 * gst_base_rtp_audio_payload_set_sample_based(). This is usually done in the
 * element's _init() function. Then, the child element must call either
 * gst_base_rtp_audio_payload_set_frame_options(),
 * gst_base_rtp_audio_payload_set_sample_options() or
 * gst_base_rtp_audio_payload_set_samplebits_options. Since
 * GstBaseRTPAudioPayload derives from GstBaseRTPPayload, the child element
 * must set any variables or call/override any functions required by that base
 * class. The child element does not need to override any other functions
 * specific to GstBaseRTPAudioPayload.
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <gst/rtp/gstrtpbuffer.h>
#include <gst/base/gstadapter.h>

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
  GstAdapter *adapter;
  guint64 min_ptime;
};


#define GST_BASE_RTP_AUDIO_PAYLOAD_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), GST_TYPE_BASE_RTP_AUDIO_PAYLOAD, \
                                GstBaseRTPAudioPayloadPrivate))

static void gst_base_rtp_audio_payload_finalize (GObject * object);

static GstFlowReturn gst_base_rtp_audio_payload_handle_buffer (GstBaseRTPPayload
    * payload, GstBuffer * buffer);

static GstFlowReturn
gst_base_rtp_audio_payload_handle_frame_based_buffer (GstBaseRTPPayload *
    basepayload, GstBuffer * buffer);

static GstFlowReturn
gst_base_rtp_audio_payload_handle_sample_based_buffer (GstBaseRTPPayload *
    basepayload, GstBuffer * buffer);

static GstStateChangeReturn
gst_base_rtp_payload_audio_change_state (GstElement * element,
    GstStateChange transition);
static gboolean
gst_base_rtp_payload_audio_handle_event (GstPad * pad, GstEvent * event);

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

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_base_rtp_payload_audio_change_state);

  gstbasertppayload_class->handle_buffer =
      GST_DEBUG_FUNCPTR (gst_base_rtp_audio_payload_handle_buffer);
  gstbasertppayload_class->handle_event =
      GST_DEBUG_FUNCPTR (gst_base_rtp_payload_audio_handle_event);

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

  basertpaudiopayload->priv->adapter = gst_adapter_new ();
}

static void
gst_base_rtp_audio_payload_finalize (GObject * object)
{
  GstBaseRTPAudioPayload *basertpaudiopayload;

  basertpaudiopayload = GST_BASE_RTP_AUDIO_PAYLOAD (object);

  g_object_unref (basertpaudiopayload->priv->adapter);

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

  if (basertpaudiopayload->priv->adapter) {
    gst_adapter_clear (basertpaudiopayload->priv->adapter);
  }
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

  /* sample_size is in bits internally */
  basertpaudiopayload->sample_size = sample_size * 8;

  if (basertpaudiopayload->priv->adapter) {
    gst_adapter_clear (basertpaudiopayload->priv->adapter);
  }
}

/**
 * gst_base_rtp_audio_payload_set_samplebits_options:
 * @basertpaudiopayload: a pointer to the element.
 * @sample_size: Size per sample in bits.
 *
 * Sets the options for sample based audio codecs.
 *
 * Since: 0.10.18
 */
void
gst_base_rtp_audio_payload_set_samplebits_options (GstBaseRTPAudioPayload
    * basertpaudiopayload, gint sample_size)
{
  g_return_if_fail (basertpaudiopayload != NULL);

  basertpaudiopayload->sample_size = sample_size;

  if (basertpaudiopayload->priv->adapter) {
    gst_adapter_clear (basertpaudiopayload->priv->adapter);
  }
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
  const guint8 *data = NULL;
  GstFlowReturn ret;
  guint available;
  gint frame_size, frame_duration;

  guint maxptime_octets = G_MAXUINT;
  guint minptime_octets = 0;
  guint min_payload_len;
  guint max_payload_len;
  gboolean use_adapter = FALSE;
  guint minptime_ms;

  ret = GST_FLOW_OK;

  basertpaudiopayload = GST_BASE_RTP_AUDIO_PAYLOAD (basepayload);

  if (basertpaudiopayload->frame_size == 0 ||
      basertpaudiopayload->frame_duration == 0) {
    GST_DEBUG_OBJECT (basertpaudiopayload, "Required options not set");
    gst_buffer_unref (buffer);
    return GST_FLOW_ERROR;
  }
  frame_size = basertpaudiopayload->frame_size;
  frame_duration = basertpaudiopayload->frame_duration;

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

  max_payload_len = MIN (
      /* MTU max */
      (int) (gst_rtp_buffer_calc_payload_len (GST_BASE_RTP_PAYLOAD_MTU
              (basertpaudiopayload), 0, 0) / frame_size) * frame_size,
      /* ptime max */
      maxptime_octets);

  /* min number of bytes based on a given ptime, has to be a multiple
     of frame duration */
  minptime_ms = basepayload->min_ptime / 1000000;

  minptime_octets = frame_size * (int) (minptime_ms / frame_duration);

  min_payload_len = MAX (minptime_octets, frame_size);

  if (min_payload_len > max_payload_len) {
    min_payload_len = max_payload_len;
  }

  GST_DEBUG_OBJECT (basertpaudiopayload,
      "Calculated min_payload_len %u and max_payload_len %u",
      min_payload_len, max_payload_len);

  if (basertpaudiopayload->priv->adapter &&
      gst_adapter_available (basertpaudiopayload->priv->adapter)) {
    /* If there is always data in the adapter, we have to use it */
    gst_adapter_push (basertpaudiopayload->priv->adapter, buffer);
    available = gst_adapter_available (basertpaudiopayload->priv->adapter);
    use_adapter = TRUE;
  } else {
    /* let's set the base timestamp */
    basertpaudiopayload->base_ts = GST_BUFFER_TIMESTAMP (buffer);

    /* If buffer fits on an RTP packet, let's just push it through */
    /* this will check against max_ptime and max_mtu */
    if (GST_BUFFER_SIZE (buffer) >= min_payload_len &&
        GST_BUFFER_SIZE (buffer) <= max_payload_len) {
      ret = gst_base_rtp_audio_payload_push (basertpaudiopayload,
          GST_BUFFER_DATA (buffer), GST_BUFFER_SIZE (buffer),
          GST_BUFFER_TIMESTAMP (buffer));
      gst_buffer_unref (buffer);

      return ret;
    }

    available = GST_BUFFER_SIZE (buffer);
    data = (guint8 *) GST_BUFFER_DATA (buffer);
  }

  /* as long as we have full frames */
  while (available >= min_payload_len) {
    gfloat ts_inc;

    /* We send as much as we can */
    payload_len = MIN (max_payload_len, (available / frame_size) * frame_size);

    if (use_adapter) {
      data = gst_adapter_peek (basertpaudiopayload->priv->adapter, payload_len);
    }

    ret =
        gst_base_rtp_audio_payload_push (basertpaudiopayload, data, payload_len,
        basertpaudiopayload->base_ts);

    ts_inc = (payload_len * frame_duration) / frame_size;

    ts_inc = ts_inc * GST_MSECOND;
    basertpaudiopayload->base_ts += gst_gdouble_to_guint64 (ts_inc);

    if (use_adapter) {
      gst_adapter_flush (basertpaudiopayload->priv->adapter, payload_len);
      available = gst_adapter_available (basertpaudiopayload->priv->adapter);
    } else {
      available -= payload_len;
      data += payload_len;
    }
  }

  if (!use_adapter) {
    if (available != 0 && basertpaudiopayload->priv->adapter) {
      GstBuffer *buf;

      buf = gst_buffer_create_sub (buffer,
          GST_BUFFER_SIZE (buffer) - available, available);
      gst_adapter_push (basertpaudiopayload->priv->adapter, buf);
    }
    gst_buffer_unref (buffer);
  }

  return ret;
}

static GstFlowReturn
gst_base_rtp_audio_payload_handle_sample_based_buffer (GstBaseRTPPayload *
    basepayload, GstBuffer * buffer)
{
  GstBaseRTPAudioPayload *basertpaudiopayload;
  guint payload_len;
  const guint8 *data = NULL;
  GstFlowReturn ret;
  guint available;

  guint maxptime_octets = G_MAXUINT;
  guint minptime_octets = 0;
  guint min_payload_len;
  guint max_payload_len;
  gboolean use_adapter = FALSE;

  guint fragment_size;

  ret = GST_FLOW_OK;

  basertpaudiopayload = GST_BASE_RTP_AUDIO_PAYLOAD (basepayload);

  if (basertpaudiopayload->sample_size == 0) {
    GST_DEBUG_OBJECT (basertpaudiopayload, "Required options not set");
    gst_buffer_unref (buffer);
    return GST_FLOW_ERROR;
  }

  /* sample_size is in bits and is converted into multiple bytes */
  fragment_size = basertpaudiopayload->sample_size;
  while ((fragment_size % 8) != 0)
    fragment_size += fragment_size;
  fragment_size /= 8;

  /* max number of bytes based on given ptime */
  if (basepayload->max_ptime != -1) {
    maxptime_octets = 8 * basepayload->max_ptime * basepayload->clock_rate /
        (basertpaudiopayload->sample_size * GST_SECOND);
  }

  max_payload_len = MIN (
      /* MTU max */
      gst_rtp_buffer_calc_payload_len (GST_BASE_RTP_PAYLOAD_MTU
          (basertpaudiopayload), 0, 0),
      /* ptime max */
      maxptime_octets);

  /* min number of bytes based on a given ptime, has to be a multiple
     of sample rate */
  minptime_octets = 8 * basepayload->min_ptime * basepayload->clock_rate /
      (basertpaudiopayload->sample_size * GST_SECOND);

  min_payload_len = MAX (minptime_octets, fragment_size);

  if (min_payload_len > max_payload_len) {
    min_payload_len = max_payload_len;
  }

  GST_DEBUG_OBJECT (basertpaudiopayload,
      "Calculated min_payload_len %u and max_payload_len %u",
      min_payload_len, max_payload_len);

  if (basertpaudiopayload->priv->adapter &&
      gst_adapter_available (basertpaudiopayload->priv->adapter)) {
    /* If there is always data in the adapter, we have to use it */
    gst_adapter_push (basertpaudiopayload->priv->adapter, buffer);
    available = gst_adapter_available (basertpaudiopayload->priv->adapter);
    use_adapter = TRUE;
  } else {
    /* let's set the base timestamp */
    basertpaudiopayload->base_ts = GST_BUFFER_TIMESTAMP (buffer);

    /* If buffer fits on an RTP packet, let's just push it through */
    /* this will check against max_ptime and max_mtu */
    if (GST_BUFFER_SIZE (buffer) >= min_payload_len &&
        GST_BUFFER_SIZE (buffer) <= max_payload_len) {
      ret = gst_base_rtp_audio_payload_push (basertpaudiopayload,
          GST_BUFFER_DATA (buffer), GST_BUFFER_SIZE (buffer),
          GST_BUFFER_TIMESTAMP (buffer));
      gst_buffer_unref (buffer);

      return ret;
    }

    available = GST_BUFFER_SIZE (buffer);
    data = (guint8 *) GST_BUFFER_DATA (buffer);
  }

  while (available >= min_payload_len) {
    gfloat num, datarate;

    payload_len =
        MIN (max_payload_len, (available / fragment_size) * fragment_size);

    if (use_adapter) {
      data = gst_adapter_peek (basertpaudiopayload->priv->adapter, payload_len);
    }

    ret =
        gst_base_rtp_audio_payload_push (basertpaudiopayload, data, payload_len,
        basertpaudiopayload->base_ts);

    num = payload_len * 8;
    datarate = (basertpaudiopayload->sample_size * basepayload->clock_rate);

    basertpaudiopayload->base_ts +=
        /* payload_len (bits) * nsecs/sec / datarate (bits*sec) */
        gst_gdouble_to_guint64 (num / datarate * GST_SECOND);
    GST_DEBUG_OBJECT (basertpaudiopayload, "New ts is %" GST_TIME_FORMAT,
        GST_TIME_ARGS (basertpaudiopayload->base_ts));

    if (use_adapter) {
      gst_adapter_flush (basertpaudiopayload->priv->adapter, payload_len);
      available = gst_adapter_available (basertpaudiopayload->priv->adapter);
    } else {
      available -= payload_len;
      data += payload_len;
    }
  }

  if (!use_adapter) {
    if (available != 0 && basertpaudiopayload->priv->adapter) {
      GstBuffer *buf;

      buf = gst_buffer_create_sub (buffer,
          GST_BUFFER_SIZE (buffer) - available, available);
      gst_adapter_push (basertpaudiopayload->priv->adapter, buf);
    }
    gst_buffer_unref (buffer);
  }

  return ret;
}

/**
 * gst_base_rtp_audio_payload_push:
 * @baseaudiopayload: a #GstBaseRTPPayload
 * @data: data to set as payload
 * @payload_len: length of payload
 * @timestamp: a #GstClockTime
 *
 * Create an RTP buffer and store @payload_len bytes of @data as the
 * payload. Set the timestamp on the new buffer to @timestamp before pushing
 * the buffer downstream.
 *
 * Returns: a #GstFlowReturn
 *
 * Since: 0.10.13
 */
GstFlowReturn
gst_base_rtp_audio_payload_push (GstBaseRTPAudioPayload * baseaudiopayload,
    const guint8 * data, guint payload_len, GstClockTime timestamp)
{
  GstBaseRTPPayload *basepayload;
  GstBuffer *outbuf;
  guint8 *payload;
  GstFlowReturn ret;

  basepayload = GST_BASE_RTP_PAYLOAD (baseaudiopayload);

  GST_DEBUG_OBJECT (baseaudiopayload, "Pushing %d bytes ts %" GST_TIME_FORMAT,
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

static GstStateChangeReturn
gst_base_rtp_payload_audio_change_state (GstElement * element,
    GstStateChange transition)
{
  GstBaseRTPAudioPayload *basertppayload;
  GstStateChangeReturn ret;

  basertppayload = GST_BASE_RTP_AUDIO_PAYLOAD (element);

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (basertppayload->priv->adapter) {
        gst_adapter_clear (basertppayload->priv->adapter);
      }
      break;
    default:
      break;
  }

  return ret;
}

static gboolean
gst_base_rtp_payload_audio_handle_event (GstPad * pad, GstEvent * event)
{
  GstBaseRTPAudioPayload *basertpaudiopayload;
  gboolean res = FALSE;

  basertpaudiopayload = GST_BASE_RTP_AUDIO_PAYLOAD (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      if (basertpaudiopayload->priv->adapter) {
        gst_adapter_clear (basertpaudiopayload->priv->adapter);
      }
      break;
    case GST_EVENT_FLUSH_STOP:
      if (basertpaudiopayload->priv->adapter) {
        gst_adapter_clear (basertpaudiopayload->priv->adapter);
      }
      break;
    default:
      break;
  }

  gst_object_unref (basertpaudiopayload);

  /* return FALSE to let parent handle the remainder of the event */
  return res;
}

/**
 * gst_base_rtp_audio_payload_get_adapter:
 * @basertpaudiopayload: a #GstBaseRTPAudioPayload
 *
 * Gets the internal adapter used by the depayloader.
 *
 * Returns: a #GstAdapter.
 *
 * Since: 0.10.13
 */
GstAdapter *
gst_base_rtp_audio_payload_get_adapter (GstBaseRTPAudioPayload
    * basertpaudiopayload)
{
  GstAdapter *adapter;

  if ((adapter = basertpaudiopayload->priv->adapter))
    g_object_ref (adapter);

  return adapter;
}
