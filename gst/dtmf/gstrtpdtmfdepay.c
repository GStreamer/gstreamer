/* GstRtpDtmfDepay
 *
 * Copyright (C) 2008 Collabora Limited
 * Copyright (C) 2008 Nokia Corporation
 *   Contact: Youness Alaoui <youness.alaoui@collabora.co.uk>
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
 * SECTION:element-rtpdtmfdepay
 * @see_also: rtpdtmfsrc, rtpdtmfmux
 *
 * This element takes RTP DTMF packets and produces sound. It also emits a
 * message on the #GstBus.
 *
 * The message is called "dtmf-event" and has the following fields
 * <informaltable>
 * <tgroup cols='4'>
 * <colspec colname='Name' />
 * <colspec colname='Type' />
 * <colspec colname='Possible values' />
 * <colspec colname='Purpose' />
 * <thead>
 * <row>
 * <entry>Name</entry>
 * <entry>GType</entry>
 * <entry>Possible values</entry>
 * <entry>Purpose</entry>
 * </row>
 * </thead>
 * <tbody>
 * <row>
 * <entry></entry>
 * <entry>G_TYPE_INT</entry>
 * <entry>0-1</entry>
 * <entry>Which of the two methods
 * specified in RFC 2833 to use. The value should be 0 for tones and 1 for
 * named events. Tones are specified by their frequencies and events are specied
 * by their number. This element currently only recognizes events.
 * Do not confuse with "method" which specified the output.
 * </entry>
 * </row>
 * <row>
 * <entry>number</entry>
 * <entry>G_TYPE_INT</entry>
 * <entry>0-16</entry>
 * <entry>The event number.</entry>
 * </row>
 * <row>
 * <entry>volume</entry>
 * <entry>G_TYPE_INT</entry>
 * <entry>0-36</entry>
 * <entry>This field describes the power level of the tone, expressed in dBm0
 * after dropping the sign. Power levels range from 0 to -63 dBm0. The range of
 * valid DTMF is from 0 to -36 dBm0.
 * </entry>
 * </row>
 * <row>
 * <entry>method</entry>
 * <entry>G_TYPE_INT</entry>
 * <entry>1</entry>
 * <entry>This field will always been 1 (ie RTP event) from this element.
 * </entry>
 * </row>
 * </tbody>
 * </tgroup>
 * </informaltable>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <math.h>

#include <gst/rtp/gstrtpbuffer.h>
#include "gstrtpdtmfdepay.h"

#define DEFAULT_PACKET_INTERVAL  50     /* ms */
#define MIN_PACKET_INTERVAL      10     /* ms */
#define MAX_PACKET_INTERVAL      50     /* ms */
#define SAMPLE_RATE              8000
#define SAMPLE_SIZE              16
#define CHANNELS                 1
#define MIN_DUTY_CYCLE           (MIN_INTER_DIGIT_INTERVAL + MIN_PULSE_DURATION)

#define MIN_UNIT_TIME            0
#define MAX_UNIT_TIME            1000
#define DEFAULT_UNIT_TIME        0

#define DEFAULT_MAX_DURATION     0

typedef struct st_dtmf_key
{
  const char *event_name;
  int event_encoding;
  float low_frequency;
  float high_frequency;
} DTMF_KEY;

static const DTMF_KEY DTMF_KEYS[] = {
  {"DTMF_KEY_EVENT_0", 0, 941, 1336},
  {"DTMF_KEY_EVENT_1", 1, 697, 1209},
  {"DTMF_KEY_EVENT_2", 2, 697, 1336},
  {"DTMF_KEY_EVENT_3", 3, 697, 1477},
  {"DTMF_KEY_EVENT_4", 4, 770, 1209},
  {"DTMF_KEY_EVENT_5", 5, 770, 1336},
  {"DTMF_KEY_EVENT_6", 6, 770, 1477},
  {"DTMF_KEY_EVENT_7", 7, 852, 1209},
  {"DTMF_KEY_EVENT_8", 8, 852, 1336},
  {"DTMF_KEY_EVENT_9", 9, 852, 1477},
  {"DTMF_KEY_EVENT_S", 10, 941, 1209},
  {"DTMF_KEY_EVENT_P", 11, 941, 1477},
  {"DTMF_KEY_EVENT_A", 12, 697, 1633},
  {"DTMF_KEY_EVENT_B", 13, 770, 1633},
  {"DTMF_KEY_EVENT_C", 14, 852, 1633},
  {"DTMF_KEY_EVENT_D", 15, 941, 1633},
};

#define MAX_DTMF_EVENTS 16

enum
{
  DTMF_KEY_EVENT_1 = 1,
  DTMF_KEY_EVENT_2 = 2,
  DTMF_KEY_EVENT_3 = 3,
  DTMF_KEY_EVENT_4 = 4,
  DTMF_KEY_EVENT_5 = 5,
  DTMF_KEY_EVENT_6 = 6,
  DTMF_KEY_EVENT_7 = 7,
  DTMF_KEY_EVENT_8 = 8,
  DTMF_KEY_EVENT_9 = 9,
  DTMF_KEY_EVENT_0 = 0,
  DTMF_KEY_EVENT_STAR = 10,
  DTMF_KEY_EVENT_POUND = 11,
  DTMF_KEY_EVENT_A = 12,
  DTMF_KEY_EVENT_B = 13,
  DTMF_KEY_EVENT_C = 14,
  DTMF_KEY_EVENT_D = 15,
};

GST_DEBUG_CATEGORY_STATIC (gst_rtp_dtmf_depay_debug);
#define GST_CAT_DEFAULT gst_rtp_dtmf_depay_debug

enum
{


  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_UNIT_TIME,
  PROP_MAX_DURATION
};

enum
{
  ARG_0
};

static GstStaticPadTemplate gst_rtp_dtmf_depay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "width = (int) 16, "
        "depth = (int) 16, "
        "endianness = (int) " G_STRINGIFY (G_BYTE_ORDER) ", "
        "signed = (boolean) true, "
        "rate = (int) [0, MAX], " "channels = (int) 1")
    );

static GstStaticPadTemplate gst_rtp_dtmf_depay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate = (int) [ 0, MAX ], "
        "encoding-name = (string) \"TELEPHONE-EVENT\"")
    );

GST_BOILERPLATE (GstRtpDTMFDepay, gst_rtp_dtmf_depay, GstBaseRTPDepayload,
    GST_TYPE_BASE_RTP_DEPAYLOAD);

static void gst_rtp_dtmf_depay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rtp_dtmf_depay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static GstBuffer *gst_rtp_dtmf_depay_process (GstBaseRTPDepayload * depayload,
    GstBuffer * buf);
gboolean gst_rtp_dtmf_depay_setcaps (GstBaseRTPDepayload * filter,
    GstCaps * caps);

static void
gst_rtp_dtmf_depay_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class,
      &gst_rtp_dtmf_depay_src_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_rtp_dtmf_depay_sink_template);


  GST_DEBUG_CATEGORY_INIT (gst_rtp_dtmf_depay_debug,
      "rtpdtmfdepay", 0, "rtpdtmfdepay element");
  gst_element_class_set_details_simple (element_class,
      "RTP DTMF packet depayloader", "Codec/Depayloader/Network",
      "Generates DTMF Sound from telephone-event RTP packets",
      "Youness Alaoui <youness.alaoui@collabora.co.uk>");
}

static void
gst_rtp_dtmf_depay_class_init (GstRtpDTMFDepayClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseRTPDepayloadClass *gstbasertpdepayload_class;

  gobject_class = (GObjectClass *) klass;
  gstbasertpdepayload_class = (GstBaseRTPDepayloadClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_rtp_dtmf_depay_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_rtp_dtmf_depay_get_property);

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_UNIT_TIME,
      g_param_spec_uint ("unit-time", "Duration unittime",
          "The smallest unit (ms) the duration must be a multiple of (0 disables it)",
          MIN_UNIT_TIME, MAX_UNIT_TIME, DEFAULT_UNIT_TIME,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_MAX_DURATION,
      g_param_spec_uint ("max-duration", "Maximum duration",
          "The maxumimum duration (ms) of the outgoing soundpacket. "
          "(0 = no limit)", 0, G_MAXUINT, DEFAULT_MAX_DURATION,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gstbasertpdepayload_class->process =
      GST_DEBUG_FUNCPTR (gst_rtp_dtmf_depay_process);
  gstbasertpdepayload_class->set_caps =
      GST_DEBUG_FUNCPTR (gst_rtp_dtmf_depay_setcaps);

}

static void
gst_rtp_dtmf_depay_init (GstRtpDTMFDepay * rtpdtmfdepay,
    GstRtpDTMFDepayClass * klass)
{
  rtpdtmfdepay->unit_time = DEFAULT_UNIT_TIME;
}

static void
gst_rtp_dtmf_depay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRtpDTMFDepay *rtpdtmfdepay;

  rtpdtmfdepay = GST_RTP_DTMF_DEPAY (object);

  switch (prop_id) {
    case PROP_UNIT_TIME:
      rtpdtmfdepay->unit_time = g_value_get_uint (value);
      break;
    case PROP_MAX_DURATION:
      rtpdtmfdepay->max_duration = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtp_dtmf_depay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstRtpDTMFDepay *rtpdtmfdepay;

  rtpdtmfdepay = GST_RTP_DTMF_DEPAY (object);

  switch (prop_id) {
    case PROP_UNIT_TIME:
      g_value_set_uint (value, rtpdtmfdepay->unit_time);
      break;
    case PROP_MAX_DURATION:
      g_value_set_uint (value, rtpdtmfdepay->max_duration);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

gboolean
gst_rtp_dtmf_depay_setcaps (GstBaseRTPDepayload * filter, GstCaps * caps)
{
  GstCaps *srccaps;
  GstStructure *structure = gst_caps_get_structure (caps, 0);
  gint clock_rate = 8000;       /* default */

  gst_structure_get_int (structure, "clock-rate", &clock_rate);
  filter->clock_rate = clock_rate;

  srccaps = gst_caps_new_simple ("audio/x-raw-int",
      "width", G_TYPE_INT, 16,
      "depth", G_TYPE_INT, 16,
      "endianness", G_TYPE_INT, G_BYTE_ORDER,
      "signed", G_TYPE_BOOLEAN, TRUE,
      "channels", G_TYPE_INT, 1, "rate", G_TYPE_INT, clock_rate, NULL);
  gst_pad_set_caps (GST_BASE_RTP_DEPAYLOAD_SRCPAD (filter), srccaps);
  gst_caps_unref (srccaps);

  return TRUE;
}

static void
gst_dtmf_src_generate_tone (GstRtpDTMFDepay * rtpdtmfdepay,
    GstRTPDTMFPayload payload, GstBuffer * buffer)
{
  gint16 *p;
  gint tone_size;
  double i = 0;
  double amplitude, f1, f2;
  double volume_factor;
  DTMF_KEY key = DTMF_KEYS[payload.event];
  guint32 clock_rate = 8000 /* default */ ;
  GstBaseRTPDepayload *depayload = GST_BASE_RTP_DEPAYLOAD (rtpdtmfdepay);
  gint volume;

  clock_rate = depayload->clock_rate;

  /* Create a buffer for the tone */
  tone_size = (payload.duration * SAMPLE_SIZE * CHANNELS) / 8;
  GST_BUFFER_SIZE (buffer) = tone_size;
  GST_BUFFER_MALLOCDATA (buffer) = g_malloc (tone_size);
  GST_BUFFER_DATA (buffer) = GST_BUFFER_MALLOCDATA (buffer);
  GST_BUFFER_DURATION (buffer) = payload.duration * GST_SECOND / clock_rate;
  volume = payload.volume;

  p = (gint16 *) GST_BUFFER_MALLOCDATA (buffer);

  volume_factor = pow (10, (-volume) / 20);

  /*
   * For each sample point we calculate 'x' as the
   * the amplitude value.
   */
  for (i = 0; i < (tone_size / (SAMPLE_SIZE / 8)); i++) {
    /*
     * We add the fundamental frequencies together.
     */
    f1 = sin (2 * M_PI * key.low_frequency * (rtpdtmfdepay->sample /
            clock_rate));
    f2 = sin (2 * M_PI * key.high_frequency * (rtpdtmfdepay->sample /
            clock_rate));

    amplitude = (f1 + f2) / 2;

    /* Adjust the volume */
    amplitude *= volume_factor;

    /* Make the [-1:1] interval into a [-32767:32767] interval */
    amplitude *= 32767;

    /* Store it in the data buffer */
    *(p++) = (gint16) amplitude;

    (rtpdtmfdepay->sample)++;
  }
}


static GstBuffer *
gst_rtp_dtmf_depay_process (GstBaseRTPDepayload * depayload, GstBuffer * buf)
{

  GstRtpDTMFDepay *rtpdtmfdepay = NULL;
  GstBuffer *outbuf = NULL;
  gint payload_len;
  guint8 *payload = NULL;
  guint32 timestamp;
  GstRTPDTMFPayload dtmf_payload;
  gboolean marker;
  GstStructure *structure = NULL;
  GstMessage *dtmf_message = NULL;

  rtpdtmfdepay = GST_RTP_DTMF_DEPAY (depayload);

  if (!gst_rtp_buffer_validate (buf))
    goto bad_packet;

  payload_len = gst_rtp_buffer_get_payload_len (buf);
  payload = gst_rtp_buffer_get_payload (buf);

  if (payload_len != sizeof (GstRTPDTMFPayload))
    goto bad_packet;

  memcpy (&dtmf_payload, payload, sizeof (GstRTPDTMFPayload));

  if (dtmf_payload.event > MAX_EVENT)
    goto bad_packet;


  marker = gst_rtp_buffer_get_marker (buf);

  timestamp = gst_rtp_buffer_get_timestamp (buf);

  dtmf_payload.duration = g_ntohs (dtmf_payload.duration);

  /* clip to whole units of unit_time */
  if (rtpdtmfdepay->unit_time) {
    guint unit_time_clock =
        (rtpdtmfdepay->unit_time * depayload->clock_rate) / 1000;
    if (dtmf_payload.duration % unit_time_clock) {
      /* Make sure we don't overflow the duration */
      if (dtmf_payload.duration < G_MAXUINT16 - unit_time_clock)
        dtmf_payload.duration += unit_time_clock -
            (dtmf_payload.duration % unit_time_clock);
      else
        dtmf_payload.duration -= dtmf_payload.duration % unit_time_clock;
    }
  }

  /* clip to max duration */
  if (rtpdtmfdepay->max_duration) {
    guint max_duration_clock =
        (rtpdtmfdepay->max_duration * depayload->clock_rate) / 1000;

    if (max_duration_clock < G_MAXUINT16 &&
        dtmf_payload.duration > max_duration_clock)
      dtmf_payload.duration = max_duration_clock;
  }

  GST_DEBUG_OBJECT (depayload, "Received new RTP DTMF packet : "
      "marker=%d - timestamp=%u - event=%d - duration=%d",
      marker, timestamp, dtmf_payload.event, dtmf_payload.duration);

  GST_DEBUG_OBJECT (depayload,
      "Previous information : timestamp=%u - duration=%d",
      rtpdtmfdepay->previous_ts, rtpdtmfdepay->previous_duration);

  /* First packet */
  if (marker || rtpdtmfdepay->previous_ts != timestamp) {
    rtpdtmfdepay->sample = 0;
    rtpdtmfdepay->previous_ts = timestamp;
    rtpdtmfdepay->previous_duration = dtmf_payload.duration;
    rtpdtmfdepay->first_gst_ts = GST_BUFFER_TIMESTAMP (buf);

    structure = gst_structure_new ("dtmf-event",
        "number", G_TYPE_INT, dtmf_payload.event,
        "volume", G_TYPE_INT, dtmf_payload.volume,
        "type", G_TYPE_INT, 1, "method", G_TYPE_INT, 1, NULL);
    if (structure) {
      dtmf_message =
          gst_message_new_element (GST_OBJECT (depayload), structure);
      if (dtmf_message) {
        if (!gst_element_post_message (GST_ELEMENT (depayload), dtmf_message)) {
          GST_ERROR_OBJECT (depayload,
              "Unable to send dtmf-event message to bus");
        }
      } else {
        GST_ERROR_OBJECT (depayload, "Unable to create dtmf-event message");
      }
    } else {
      GST_ERROR_OBJECT (depayload, "Unable to create dtmf-event structure");
    }
  } else {
    guint16 duration = dtmf_payload.duration;
    dtmf_payload.duration -= rtpdtmfdepay->previous_duration;
    /* If late buffer, ignore */
    if (duration > rtpdtmfdepay->previous_duration)
      rtpdtmfdepay->previous_duration = duration;
  }

  GST_DEBUG_OBJECT (depayload, "new previous duration : %d - new duration : %d"
      " - diff  : %d - clock rate : %d - timestamp : %" G_GUINT64_FORMAT,
      rtpdtmfdepay->previous_duration, dtmf_payload.duration,
      (rtpdtmfdepay->previous_duration - dtmf_payload.duration),
      depayload->clock_rate, GST_BUFFER_TIMESTAMP (buf));

  /* If late or duplicate packet (like the redundant end packet). Ignore */
  if (dtmf_payload.duration > 0) {
    outbuf = gst_buffer_new ();
    gst_dtmf_src_generate_tone (rtpdtmfdepay, dtmf_payload, outbuf);


    GST_BUFFER_TIMESTAMP (outbuf) = rtpdtmfdepay->first_gst_ts +
        (rtpdtmfdepay->previous_duration - dtmf_payload.duration) *
        GST_SECOND / depayload->clock_rate;
    GST_BUFFER_OFFSET (outbuf) =
        (rtpdtmfdepay->previous_duration - dtmf_payload.duration) *
        GST_SECOND / depayload->clock_rate;
    GST_BUFFER_OFFSET_END (outbuf) = rtpdtmfdepay->previous_duration *
        GST_SECOND / depayload->clock_rate;

    GST_DEBUG_OBJECT (depayload,
        "timestamp : %" G_GUINT64_FORMAT " - time %" GST_TIME_FORMAT,
        GST_BUFFER_TIMESTAMP (buf), GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)));

  }

  return outbuf;


bad_packet:
  GST_ELEMENT_WARNING (rtpdtmfdepay, STREAM, DECODE,
      ("Packet did not validate"), (NULL));
  return NULL;
}

gboolean
gst_rtp_dtmf_depay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpdtmfdepay",
      GST_RANK_MARGINAL, GST_TYPE_RTP_DTMF_DEPAY);
}
