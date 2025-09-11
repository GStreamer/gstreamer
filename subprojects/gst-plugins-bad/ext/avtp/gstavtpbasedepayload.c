/*
 * GStreamer AVTP Plugin
 * Copyright (C) 2019 Intel Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later
 * version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA
 */

#include "gstavtpbasedepayload.h"

GST_DEBUG_CATEGORY_STATIC (avtpbasedepayload_debug);
#define GST_CAT_DEFAULT (avtpbasedepayload_debug)

#define DEFAULT_STREAMID 0xAABBCCDDEEFF0000

enum
{
  PROP_0,
  PROP_STREAMID,
};

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-avtp")
    );

static void gst_avtp_base_depayload_class_init (GstAvtpBaseDepayloadClass *
    klass);
static void gst_avtp_base_depayload_init (GstAvtpBaseDepayload *
    avtpbasedepayload, gpointer g_class);

static void gst_avtp_base_depayload_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_avtp_base_depayload_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static gboolean gst_avtp_base_depayload_sink_event (GstAvtpBaseDepayload * self,
    GstEvent * event);

static GstFlowReturn avtp_base_depayload_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buffer);
static gboolean avtp_base_depayload_sink_event (GstPad * pad,
    GstObject * parent, GstEvent * event);

static gboolean gst_avtp_base_depayload_push_segment_event (GstAvtpBaseDepayload
    * avtpbasedepayload);

GType
gst_avtp_base_depayload_get_type (void)
{
  static GType avtpbasedepayload_type = 0;

  if (g_once_init_enter ((gsize *) & avtpbasedepayload_type)) {
    static const GTypeInfo avtpbasedepayload_info = {
      sizeof (GstAvtpBaseDepayloadClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_avtp_base_depayload_class_init,
      NULL,
      NULL,
      sizeof (GstAvtpBaseDepayload),
      0,
      (GInstanceInitFunc) gst_avtp_base_depayload_init,
    };
    GType _type;

    _type = g_type_register_static (GST_TYPE_ELEMENT, "GstAvtpBaseDepayload",
        &avtpbasedepayload_info, G_TYPE_FLAG_ABSTRACT);

    g_once_init_leave ((gsize *) & avtpbasedepayload_type, _type);
  }
  return avtpbasedepayload_type;
}

static void
gst_avtp_base_depayload_class_init (GstAvtpBaseDepayloadClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = gst_avtp_base_depayload_set_property;
  object_class->get_property = gst_avtp_base_depayload_get_property;

  g_object_class_install_property (object_class, PROP_STREAMID,
      g_param_spec_uint64 ("streamid", "Stream ID",
          "Stream ID associated with the AVTPDU", 0, G_MAXUINT64,
          DEFAULT_STREAMID, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PAUSED));

  klass->sink_event = gst_avtp_base_depayload_sink_event;

  GST_DEBUG_CATEGORY_INIT (avtpbasedepayload_debug, "avtpbasedepayload", 0,
      "Base class for AVTP depayloaders");

  gst_type_mark_as_plugin_api (GST_TYPE_AVTP_BASE_DEPAYLOAD, 0);
}

static void
gst_avtp_base_depayload_init (GstAvtpBaseDepayload * avtpbasedepayload,
    gpointer g_class)
{
  GstPadTemplate *templ;
  GstElement *element = GST_ELEMENT (avtpbasedepayload);
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstAvtpBaseDepayloadClass *avtpbasedepayload_class GST_UNUSED_ASSERT =
      GST_AVTP_BASE_DEPAYLOAD_CLASS (g_class);

  g_assert (avtpbasedepayload_class->process != NULL);

  templ = gst_element_class_get_pad_template (element_class, "src");
  g_assert (templ != NULL);
  avtpbasedepayload->srcpad = gst_pad_new_from_template (templ, "src");
  gst_pad_use_fixed_caps (avtpbasedepayload->srcpad);
  gst_element_add_pad (element, avtpbasedepayload->srcpad);

  avtpbasedepayload->sinkpad =
      gst_pad_new_from_static_template (&sink_template, "sink");
  gst_pad_set_chain_function (avtpbasedepayload->sinkpad,
      avtp_base_depayload_chain);
  gst_pad_set_event_function (avtpbasedepayload->sinkpad,
      avtp_base_depayload_sink_event);
  gst_element_add_pad (element, avtpbasedepayload->sinkpad);

  avtpbasedepayload->streamid = DEFAULT_STREAMID;

  avtpbasedepayload->seqnum = 0;
}

static void
gst_avtp_base_depayload_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAvtpBaseDepayload *avtpbasedepayload = GST_AVTP_BASE_DEPAYLOAD (object);

  GST_DEBUG_OBJECT (avtpbasedepayload, "prop_id %u", prop_id);

  switch (prop_id) {
    case PROP_STREAMID:
      avtpbasedepayload->streamid = g_value_get_uint64 (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_avtp_base_depayload_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstAvtpBaseDepayload *avtpbasedepayload = GST_AVTP_BASE_DEPAYLOAD (object);

  GST_DEBUG_OBJECT (avtpbasedepayload, "prop_id %u", prop_id);

  switch (prop_id) {
    case PROP_STREAMID:
      g_value_set_uint64 (value, avtpbasedepayload->streamid);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstFlowReturn
avtp_base_depayload_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  GstAvtpBaseDepayload *avtpbasedepayload = GST_AVTP_BASE_DEPAYLOAD (parent);
  GstAvtpBaseDepayloadClass *klass =
      GST_AVTP_BASE_DEPAYLOAD_GET_CLASS (avtpbasedepayload);

  avtpbasedepayload->last_dts = GST_BUFFER_DTS (buffer);

  return klass->process (avtpbasedepayload, buffer);
}


static gboolean
gst_avtp_base_depayload_sink_event (GstAvtpBaseDepayload * avtpbasedepayload,
    GstEvent * event)
{
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEGMENT:
      /* Once the first AVTPDU is received, proper CAPS and SEGMENT events are
       * pushed downstream. These events are expected to be pushed in that
       * order by GStreamer. Since the default handling implemented by
       * gst_pad_event_default() pushes the SEGMENT event downstream right
       * away, it doesn't work for us and we have to handle it ourselves.
       *
       * Our handling is very straightforward: we discard this event and send
       * a proper segment event once the first AVTPDU is received. See
       * gst_avtp_base_depayload_push_segment_event() for more information.
       */
      gst_event_unref (event);
      avtpbasedepayload->segment_sent = FALSE;
      return TRUE;
    default:
      return gst_pad_event_default (avtpbasedepayload->sinkpad,
          GST_OBJECT (avtpbasedepayload), event);
  }
}

static gboolean
avtp_base_depayload_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstAvtpBaseDepayload *avtpbasedepayload = GST_AVTP_BASE_DEPAYLOAD (parent);
  GstAvtpBaseDepayloadClass *klass =
      GST_AVTP_BASE_DEPAYLOAD_GET_CLASS (avtpbasedepayload);

  GST_DEBUG_OBJECT (avtpbasedepayload, "event %s", GST_EVENT_TYPE_NAME (event));

  return klass->sink_event (avtpbasedepayload, event);
}

/* Helper function to convert AVTP timestamp to AVTP presentation time. Since
 * AVTP timestamp represents the lower 32-bit part from AVTP presentation time,
 * the helper requires a reference time ('ref' argument) to convert it properly.
 * The reference time must be in gstreamer clock-time coordinate.
 */
GstClockTime
gst_avtp_base_depayload_tstamp_to_ptime (GstAvtpBaseDepayload *
    avtpbasedepayload, guint32 tstamp, GstClockTime ref)
{
  GstClockTime ptime;
  guint32 ref_low;

  ref += gst_element_get_base_time (GST_ELEMENT (avtpbasedepayload));

  GST_LOG_OBJECT (avtpbasedepayload, "dts: %" GST_TIME_FORMAT " tstamp: %u",
      GST_TIME_ARGS (ref), tstamp);

  ref_low = ref & 0xFFFFFFFFULL;
  ptime = (ref & 0xFFFFFFFF00000000ULL) | tstamp;

  /* If 'ptime' is less than the our reference time, it means the higher part
   * from 'ptime' needs to be incremented by 1 in order reflect the correct
   * presentation time.
   */
  if (tstamp < G_MAXINT32 && ref_low > G_MAXINT32)
    ptime += G_MAXUINT32 + 1;

  if (tstamp < G_MAXINT32 && ref_low > G_MAXINT32 && ptime > G_MAXUINT32)
    ptime -= G_MAXUINT32 + 1;

  GST_LOG_OBJECT (avtpbasedepayload, "AVTP presentation time %" GST_TIME_FORMAT,
      GST_TIME_ARGS (ptime));
  return ptime;
}

static gboolean
gst_avtp_base_depayload_push_segment_event (GstAvtpBaseDepayload *
    avtpbasedepayload)
{
  GstEvent *event;
  GstSegment segment;
  GstClockTime base_time;

  base_time = gst_element_get_base_time (GST_ELEMENT (avtpbasedepayload));

  gst_segment_init (&segment, GST_FORMAT_TIME);
  segment.base = 0;
  segment.start = base_time;
  segment.stop = -1;

  event = gst_event_new_segment (&segment);
  if (!event) {
    GST_ERROR_OBJECT (avtpbasedepayload, "Failed to create SEGMENT event");
    return FALSE;
  }

  if (!gst_pad_push_event (avtpbasedepayload->srcpad, event)) {
    GST_ERROR_OBJECT (avtpbasedepayload, "Failed to push SEGMENT event");
    return FALSE;
  }

  GST_DEBUG_OBJECT (avtpbasedepayload, "SEGMENT event pushed: %"
      GST_SEGMENT_FORMAT, &segment);

  avtpbasedepayload->segment_sent = TRUE;
  return TRUE;
}

GstFlowReturn
gst_avtp_base_depayload_push (GstAvtpBaseDepayload *
    avtpbasedepayload, GstBuffer * buffer)
{
  if (!avtpbasedepayload->segment_sent)
    gst_avtp_base_depayload_push_segment_event (avtpbasedepayload);

  return gst_pad_push (avtpbasedepayload->srcpad, buffer);
}
