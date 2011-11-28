/*
 * GStreamer - DTMF Detection
 *
 *  Copyright 2009 Nokia Corporation
 *  Copyright 2009 Collabora Ltd,
 *   @author: Olivier Crete <olivier.crete@collabora.co.uk>
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
 *
 */

/**
 * SECTION:element-dtmfdetect
 * @short_description: Detects DTMF tones
 *
 * This element will detect DTMF tones and emit messages.
 *
 * The message is called <classname>&quot;dtmf-event&quot;</classname> and has
 * the following fields:
 * <itemizedlist>
 * <listitem>
 *   <para>
 *   gint <classname>type</classname> (0-1):
 *   The application uses this field to specify which of the two methods
 *   specified in RFC 2833 to use. The value should be 0 for tones and 1 for
 *   named events. Tones are specified by their frequencies and events are
 *   specfied by their number. This element can only take events as input.
 *   Do not confuse with "method" which specified the output.
 *   </para>
 * </listitem>
 * <listitem>
 *   <para>
 *   gint <classname>number</classname> (0-16):
 *   The event number.
 *   </para>
 * </listitem>
 * <listitem>
 *   <para>
 *   gint <classname>method</classname> (2):
 *   This field will always been 2 (ie sound) from this element.
 *   </para>
 * </listitem>
 * </itemizedlist>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstdtmfdetect.h"

#include <string.h>

GST_DEBUG_CATEGORY (dtmf_detect_debug);
#define GST_CAT_DEFAULT (dtmf_detect_debug)

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "width = (int) 16, "
        "depth = (int) 16, "
        "endianness = (int) " G_STRINGIFY (G_BYTE_ORDER) ", "
        "signed = (bool) true, rate = (int) 8000, channels = (int) 1"));

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "width = (int) 16, "
        "depth = (int) 16, "
        "endianness = (int) " G_STRINGIFY (G_BYTE_ORDER) ", "
        "signed = (bool) true, rate = (int) 8000, channels = (int) 1"));

/* signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
};

static gboolean gst_dtmf_detect_set_caps (GstBaseTransform * trans,
    GstCaps * incaps, GstCaps * outcaps);
static GstFlowReturn gst_dtmf_detect_transform_ip (GstBaseTransform * trans,
    GstBuffer * buf);
static gboolean gst_dtmf_detect_event (GstBaseTransform * trans,
    GstEvent * event);

static void
_do_init (GType type)
{
  GST_DEBUG_CATEGORY_INIT (dtmf_detect_debug, "dtmfdetect", 0, "dtmfdetect");
}

GST_BOILERPLATE_FULL (GstDtmfDetect, gst_dtmf_detect, GstBaseTransform,
    GST_TYPE_BASE_TRANSFORM, _do_init);

static void
gst_dtmf_detect_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class, &srctemplate);
  gst_element_class_add_static_pad_template (element_class, &sinktemplate);

  gst_element_class_set_details_simple (element_class, "DTMF detector element",
      "Filter/Analyzer/Audio",
      "This element detects DTMF tones",
      "Olivier Crete <olivier.crete@collabora.co.uk>");
}

static void
gst_dtmf_detect_class_init (GstDtmfDetectClass * klass)
{
  GstBaseTransformClass *gstbasetransform_class;

  gstbasetransform_class = (GstBaseTransformClass *) klass;

  gstbasetransform_class->set_caps =
      GST_DEBUG_FUNCPTR (gst_dtmf_detect_set_caps);
  gstbasetransform_class->transform_ip =
      GST_DEBUG_FUNCPTR (gst_dtmf_detect_transform_ip);
  gstbasetransform_class->event = GST_DEBUG_FUNCPTR (gst_dtmf_detect_event);
}

static void
gst_dtmf_detect_init (GstDtmfDetect * dtmfdetect, GstDtmfDetectClass * klass)
{
  gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (dtmfdetect), TRUE);
  gst_base_transform_set_gap_aware (GST_BASE_TRANSFORM (dtmfdetect), TRUE);
}

static gboolean
gst_dtmf_detect_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstDtmfDetect *self = GST_DTMF_DETECT (trans);

  zap_dtmf_detect_init (&self->dtmf_state);

  return TRUE;
}


static GstFlowReturn
gst_dtmf_detect_transform_ip (GstBaseTransform * trans, GstBuffer * buf)
{
  GstDtmfDetect *self = GST_DTMF_DETECT (trans);
  gint dtmf_count;
  gchar dtmfbuf[MAX_DTMF_DIGITS] = "";
  gint i;

  if (GST_BUFFER_IS_DISCONT (buf))
    zap_dtmf_detect_init (&self->dtmf_state);
  if (GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_GAP))
    return GST_FLOW_OK;

  zap_dtmf_detect (&self->dtmf_state, (gint16 *) GST_BUFFER_DATA (buf),
      GST_BUFFER_SIZE (buf) / 2, FALSE);

  dtmf_count = zap_dtmf_get (&self->dtmf_state, dtmfbuf, MAX_DTMF_DIGITS);

  if (dtmf_count)
    GST_DEBUG_OBJECT (self, "Got %d DTMF events: %s", dtmf_count, dtmfbuf);
  else
    GST_LOG_OBJECT (self, "Got no DTMF events");

  for (i = 0; i < dtmf_count; i++) {
    GstMessage *dtmf_message = NULL;
    GstStructure *structure;
    gint dtmf_payload_event;

    GST_DEBUG_OBJECT (self, "Got DTMF event %c", dtmfbuf[i]);

    switch (dtmfbuf[i]) {
      case '0':
        dtmf_payload_event = 0;
        break;
      case '1':
        dtmf_payload_event = 1;
        break;
      case '2':
        dtmf_payload_event = 2;
        break;
      case '3':
        dtmf_payload_event = 3;
        break;
      case '4':
        dtmf_payload_event = 4;
        break;
      case '5':
        dtmf_payload_event = 5;
        break;
      case '6':
        dtmf_payload_event = 6;
        break;
      case '7':
        dtmf_payload_event = 7;
        break;
      case '8':
        dtmf_payload_event = 8;
        break;
      case '9':
        dtmf_payload_event = 9;
        break;
      case '*':
        dtmf_payload_event = 10;
        break;
      case '#':
        dtmf_payload_event = 11;
        break;
      case 'A':
        dtmf_payload_event = 12;
        break;
      case 'B':
        dtmf_payload_event = 13;
        break;
      case 'C':
        dtmf_payload_event = 14;
        break;
      case 'D':
        dtmf_payload_event = 15;
        break;
      default:
        continue;
    }

    structure = gst_structure_new ("dtmf-event",
        "type", G_TYPE_INT, 1,
        "number", G_TYPE_INT, dtmf_payload_event,
        "method", G_TYPE_INT, 2, NULL);
    dtmf_message = gst_message_new_element (GST_OBJECT (self), structure);
    gst_element_post_message (GST_ELEMENT (self), dtmf_message);
  }

  return GST_FLOW_OK;
}


static gboolean
gst_dtmf_detect_event (GstBaseTransform * trans, GstEvent * event)
{
  GstDtmfDetect *self = GST_DTMF_DETECT (trans);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
      zap_dtmf_detect_init (&self->dtmf_state);
      break;
    default:
      break;
  }

  return GST_CALL_PARENT_WITH_DEFAULT (GST_BASE_TRANSFORM_CLASS, event,
      (trans, event), TRUE);
}


gboolean
gst_dtmf_detect_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "dtmfdetect",
      GST_RANK_MARGINAL, GST_TYPE_DTMF_DETECT);
}
