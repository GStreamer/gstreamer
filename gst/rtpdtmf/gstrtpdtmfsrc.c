/* GStreamer RTP DTMF source
 *
 * gstrtpdtmfsrc.c:
 *
 * Copyright (C) <2007> Nokia Corporation.
 *   Contact: Zeeshan Ali <zeeshan.ali@nokia.com>
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
#  include "config.h"
#endif

#include <stdlib.h>
#include <string.h>

#include "gstrtpdtmfsrc.h"

#define GST_RTP_DTMF_TYPE_EVENT 1
#define DEFAULT_PACKET_INTERVAL ((guint16) 50) /* ms */
#define DEFAULT_SSRC            -1
#define DEFAULT_PT              96
#define DEFAULT_CLOCK_RATE      8000
#define MIN_EVENT               0
#define MAX_EVENT               16
#define MIN_EVENT_STRING        "0"
#define MAX_EVENT_STRING        "16"
#define MIN_VOLUME              0
#define MAX_VOLUME              36

/* elementfactory information */
static const GstElementDetails gst_rtp_dtmf_src_details =
GST_ELEMENT_DETAILS ("RTP DTMF packet generator",
    "Source/Network",
    "Generates RTP DTMF packets",
    "Zeeshan Ali <zeeshan.ali@nokia.com>");

GST_DEBUG_CATEGORY_STATIC (gst_rtp_dtmf_src_debug);
#define GST_CAT_DEFAULT gst_rtp_dtmf_src_debug

/* signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_SSRC,
  PROP_PT,
  PROP_CLOCK_RATE,
};

static GstStaticPadTemplate gst_rtp_dtmf_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) [ 96, 127 ], "
        "clock-rate = (int) [ 0, MAX ], " 
        "ssrc = (int) [ 0, MAX ], " 
        "event = (int) [ " MIN_EVENT_STRING ", " MAX_EVENT_STRING " ], "
        "encoding-name = (string) \"telephone-event\"")
    );

static GstElementClass *parent_class = NULL;

static void gst_rtp_dtmf_src_base_init (gpointer g_class);
static void gst_rtp_dtmf_src_class_init (GstRTPDTMFSrcClass * klass);
static void gst_rtp_dtmf_src_init (GstRTPDTMFSrc * dtmfsrc, gpointer g_class);
static void gst_rtp_dtmf_src_finalize (GObject * object);

GType
gst_rtp_dtmf_src_get_type (void)
{
  static GType base_src_type = 0;

  if (G_UNLIKELY (base_src_type == 0)) {
    static const GTypeInfo base_src_info = {
      sizeof (GstRTPDTMFSrcClass),
      (GBaseInitFunc) gst_rtp_dtmf_src_base_init,
      NULL,
      (GClassInitFunc) gst_rtp_dtmf_src_class_init,
      NULL,
      NULL,
      sizeof (GstRTPDTMFSrc),
      0,
      (GInstanceInitFunc) gst_rtp_dtmf_src_init,
    };

    base_src_type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstRTPDTMFSrc", &base_src_info, 0);
  }
  return base_src_type;
}

static void gst_rtp_dtmf_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rtp_dtmf_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static gboolean gst_rtp_dtmf_src_handle_event (GstPad * pad, GstEvent * event);
static GstStateChangeReturn gst_rtp_dtmf_src_change_state (GstElement * element,
    GstStateChange transition);
static void gst_rtp_dtmf_src_push_next_rtp_packet (GstRTPDTMFSrc *dtmfsrc);
static void gst_rtp_dtmf_src_start (GstRTPDTMFSrc *dtmfsrc, gint event_number,
    gint event_volume);
static void gst_rtp_dtmf_src_stop (GstRTPDTMFSrc *dtmfsrc);
static void gst_rtp_dtmf_src_set_caps (GstRTPDTMFSrc *dtmfsrc);

static void
gst_rtp_dtmf_src_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  
  GST_DEBUG_CATEGORY_INIT (gst_rtp_dtmf_src_debug,
          "dtmfsrc", 0, "dtmfsrc element");
  
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_dtmf_src_template));
  
  gst_element_class_set_details (element_class, &gst_rtp_dtmf_src_details);
}

static void
gst_rtp_dtmf_src_class_init (GstRTPDTMFSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_rtp_dtmf_src_finalize);
  gobject_class->set_property = 
      GST_DEBUG_FUNCPTR (gst_rtp_dtmf_src_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_rtp_dtmf_src_get_property);

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_CLOCK_RATE,
      g_param_spec_uint ("clock-rate", "clockrate",
          "The clock-rate at which to generate the dtmf packets",
          0, G_MAXUINT, DEFAULT_CLOCK_RATE, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SSRC,
      g_param_spec_uint ("ssrc", "SSRC",
          "The SSRC of the packets (-1 == random)",
          0, G_MAXUINT, DEFAULT_SSRC, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_PT,
      g_param_spec_uint ("pt", "payload type",
          "The payload type of the packets",
          0, 0x80, DEFAULT_PT, G_PARAM_READWRITE));

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_rtp_dtmf_src_change_state);
}

static void
gst_rtp_dtmf_src_init (GstRTPDTMFSrc * dtmfsrc, gpointer g_class)
{
  dtmfsrc->srcpad =
      gst_pad_new_from_static_template (&gst_rtp_dtmf_src_template, "src");
  GST_DEBUG_OBJECT (dtmfsrc, "adding src pad");
  gst_element_add_pad (GST_ELEMENT (dtmfsrc), dtmfsrc->srcpad);

  gst_pad_set_event_function (dtmfsrc->srcpad, gst_rtp_dtmf_src_handle_event);
  
  dtmfsrc->ssrc = DEFAULT_SSRC;
  dtmfsrc->pt = DEFAULT_PT;
  dtmfsrc->clock_rate = DEFAULT_CLOCK_RATE;
  
  gst_rtp_dtmf_src_set_caps (dtmfsrc);
  
  GST_DEBUG_OBJECT (dtmfsrc, "init done");
}

static void
gst_rtp_dtmf_src_finalize (GObject * object)
{
  GstRTPDTMFSrc *dtmfsrc;

  dtmfsrc = GST_RTP_DTMF_SRC (object);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_rtp_dtmf_src_handle_event (GstPad * pad, GstEvent * event)
{
  GstRTPDTMFSrc *dtmfsrc;
  gboolean result = FALSE;

  dtmfsrc = GST_RTP_DTMF_SRC (GST_PAD_PARENT (pad));

  GST_DEBUG_OBJECT (dtmfsrc, "Received an event on the src pad");
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CUSTOM_UPSTREAM:
    {
      const GstStructure *structure;

      if (GST_STATE (dtmfsrc) != GST_STATE_PLAYING) {
        GST_DEBUG_OBJECT (dtmfsrc, "Received event while not in PLAYING state");
        break;
      }

      GST_DEBUG_OBJECT (dtmfsrc, "Received event is of our interest");
      structure = gst_event_get_structure (event);
      if (structure && gst_structure_has_name (structure, "dtmf-event")) {
        gint event_type;
        gboolean start;

        if (!gst_structure_get_int (structure, "type", &event_type) ||
            !gst_structure_get_boolean (structure, "start", &start) ||
            event_type != GST_RTP_DTMF_TYPE_EVENT)
          break;

        if (start) {
          gint event_number;
          gint event_volume;

          if (!gst_structure_get_int (structure, "number", &event_number) ||
              !gst_structure_get_int (structure, "volume", &event_volume)) 
            break;

          gst_rtp_dtmf_src_start (dtmfsrc, event_number, event_volume);
        }

        else {
          gst_rtp_dtmf_src_stop (dtmfsrc);
        }
      }
      break;
    }
    default:
      break;
  }
  gst_event_unref (event);
  return result;
}

static void
gst_rtp_dtmf_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRTPDTMFSrc *dtmfsrc;

  dtmfsrc = GST_RTP_DTMF_SRC (object);

  switch (prop_id) {
    case PROP_CLOCK_RATE:
      dtmfsrc->clock_rate = g_value_get_uint (value);
      gst_rtp_dtmf_src_set_caps (dtmfsrc);
      break;
    case PROP_SSRC:
      dtmfsrc->ssrc = g_value_get_uint (value);
      break;
    case PROP_PT:
      dtmfsrc->pt = g_value_get_uint (value);
      gst_rtp_dtmf_src_set_caps (dtmfsrc);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtp_dtmf_src_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstRTPDTMFSrc *dtmfsrc;

  dtmfsrc = GST_RTP_DTMF_SRC (object);

  switch (prop_id) {
    case PROP_CLOCK_RATE:
      g_value_set_uint (value, dtmfsrc->clock_rate);
      break;
    case PROP_SSRC:
      g_value_set_uint (value, dtmfsrc->ssrc);
      break;
    case PROP_PT:
      g_value_set_uint (value, dtmfsrc->pt);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtp_dtmf_src_start (GstRTPDTMFSrc *dtmfsrc,
        gint event_number, gint event_volume)
{
  g_return_if_fail (dtmfsrc->payload == NULL);

  dtmfsrc->payload = g_new0 (GstRTPDTMFPayload, 1);
  dtmfsrc->payload->event = CLAMP (event_number, MIN_EVENT, MAX_EVENT);
  dtmfsrc->payload->volume = CLAMP (event_volume, MIN_VOLUME, MAX_VOLUME);

  dtmfsrc->next_ts = gst_clock_get_time (GST_ELEMENT_CLOCK (dtmfsrc));
  dtmfsrc->first_packet = TRUE;

  if (dtmfsrc->ssrc == -1)
    dtmfsrc->current_ssrc = g_random_int ();
  else
    dtmfsrc->current_ssrc = dtmfsrc->ssrc;

  if (!gst_pad_start_task (dtmfsrc->srcpad,
      (GstTaskFunction) gst_rtp_dtmf_src_push_next_rtp_packet, dtmfsrc)) {
    GST_ERROR_OBJECT (dtmfsrc, "Failed to start task on src pad");
  }
}

static void
gst_rtp_dtmf_src_stop (GstRTPDTMFSrc *dtmfsrc)
{
  g_return_if_fail (dtmfsrc->payload != NULL);

  if (!gst_pad_stop_task (dtmfsrc->srcpad)) {
    GST_ERROR_OBJECT (dtmfsrc, "Failed to stop task on src pad");
    return;
  }

  /* Push the last packet with e-bit set */
  dtmfsrc->payload->e = 1;
  gst_rtp_dtmf_src_push_next_rtp_packet (dtmfsrc);

  g_free (dtmfsrc->payload);
  dtmfsrc->payload = NULL;
}

static void
gst_rtp_dtmf_src_push_next_rtp_packet (GstRTPDTMFSrc *dtmfsrc)
{
  GstBuffer *buf = NULL;
  GstFlowReturn ret;
  GstRTPDTMFPayload *payload;
  GstClock * clock;
  guint32 rtp_ts;

  /* create buffer to hold the payload */
  buf = gst_rtp_buffer_new_allocate (sizeof (GstRTPDTMFPayload), 0, 0);

  gst_rtp_buffer_set_ssrc (buf, dtmfsrc->current_ssrc);
  gst_rtp_buffer_set_payload_type (buf, dtmfsrc->pt);
  if (dtmfsrc->first_packet) {
    gst_rtp_buffer_set_marker (buf, TRUE);
    dtmfsrc->first_packet = FALSE;
  }

  /* timestamp and duration of GstBuffer */ 
  GST_BUFFER_TIMESTAMP (buf) = dtmfsrc->next_ts;
  GST_BUFFER_DURATION (buf) = DEFAULT_PACKET_INTERVAL * GST_MSECOND;
  
  /* duration of DTMF payload */
  dtmfsrc->payload->duration +=
      DEFAULT_PACKET_INTERVAL * dtmfsrc->clock_rate / 1000;

  payload = (GstRTPDTMFPayload *) gst_rtp_buffer_get_payload (buf);
  /* timestamp of RTP header */
  rtp_ts = dtmfsrc->next_ts * dtmfsrc->clock_rate / GST_SECOND;
  gst_rtp_buffer_set_timestamp (buf, rtp_ts);
  
  /* copy payload and convert to network-byte order */
  g_memmove (payload, dtmfsrc->payload, sizeof (GstRTPDTMFPayload));
  payload->duration = g_htons (payload->duration);

  /* FIXME: Should we sync to clock ourselves or leave it to sink */
  clock = GST_ELEMENT_CLOCK (dtmfsrc);
  if (clock != NULL) {
    GstClockID clock_id;
    GstClockReturn clock_ret;

    clock_id = gst_clock_new_single_shot_id (clock, dtmfsrc->next_ts);
    clock_ret = gst_clock_id_wait (clock_id, NULL);
    if (clock_ret != GST_CLOCK_OK && clock_ret != GST_CLOCK_EARLY) {
      GST_ERROR_OBJECT (dtmfsrc, "Failed to wait on clock %s",
              GST_ELEMENT_NAME (clock));
    }
    gst_clock_id_unref (clock_id);
  }

  else {
    GST_ERROR_OBJECT (dtmfsrc, "No clock set for element %s", GST_ELEMENT_NAME (dtmfsrc));
  }

  dtmfsrc->next_ts += GST_BUFFER_DURATION (buf);

  GST_DEBUG_OBJECT (dtmfsrc,
          "pushing buffer on src pad of size %d", GST_BUFFER_SIZE (buf));
  ret = gst_pad_push (dtmfsrc->srcpad, buf);
  if (ret != GST_FLOW_OK)
    GST_ERROR_OBJECT (dtmfsrc,
            "Failed to push buffer on src pad", GST_BUFFER_SIZE (buf));
    
  GST_DEBUG_OBJECT (dtmfsrc,
          "pushed DTMF event '%d' on src pad", dtmfsrc->payload->event);
}

static void
gst_rtp_dtmf_src_set_caps (GstRTPDTMFSrc *dtmfsrc)
{
  GstCaps *caps;

  caps = gst_caps_new_simple ("application/x-rtp",
      "media", G_TYPE_STRING, "audio",
      "payload", G_TYPE_INT, dtmfsrc->pt,
      "clock-rate", G_TYPE_INT, dtmfsrc->clock_rate,
      "encoding-name", G_TYPE_STRING, "telephone-event",
      "ssrc", G_TYPE_UINT, dtmfsrc->current_ssrc, NULL);

  if (!gst_pad_set_caps (dtmfsrc->srcpad, caps)) {
    GST_ERROR_OBJECT (dtmfsrc, "Failed to set caps % on src pad",
            GST_PTR_FORMAT, caps);
  }

  gst_caps_unref (caps);
}

static GstStateChangeReturn
gst_rtp_dtmf_src_change_state (GstElement * element, GstStateChange transition)
{
  GstRTPDTMFSrc *dtmfsrc;
  GstStateChangeReturn result;
  gboolean no_preroll = TRUE;

  dtmfsrc = GST_RTP_DTMF_SRC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      /* Indicate that we don't do PRE_ROLL */
      no_preroll = TRUE;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  if ((result =
          GST_ELEMENT_CLASS (parent_class)->change_state (element,
              transition)) == GST_STATE_CHANGE_FAILURE)
    goto failure;

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      /* Indicate that we don't do PRE_ROLL */
      no_preroll = TRUE;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  if (no_preroll && result == GST_STATE_CHANGE_SUCCESS)
    result = GST_STATE_CHANGE_NO_PREROLL;

  return result;

  /* ERRORS */
failure:
  {
    GST_ERROR_OBJECT (dtmfsrc, "parent failed state change");
    return result;
  }
}

static gboolean
gst_rtp_dtmf_src_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpdtmfsrc",
      GST_RANK_NONE, GST_TYPE_RTP_DTMF_SRC);
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_rtp_dtmf_src_plugin_init (plugin))
    return FALSE;

  return TRUE;
}

#define PACKAGE "gstreamer"

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "dtmf",
    "DTMF plugins",
    plugin_init, "0.1" , "LGPL", "DTMF", "");
