/* GStreamer
 * Copyright (C) <2008> Thijs Vermeir <thijsvermeir@gmail.com>
 * Copyright (C) 2011 David Schleef <ds@schleef.org>
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
#include "string.h"

#include "gstwebvttenc.h"
#include <gst/controller/gstcontroller.h>

GST_DEBUG_CATEGORY_STATIC (webvttenc_debug);
#define GST_CAT_DEFAULT webvttenc_debug

enum
{
  ARG_0,
  ARG_TIMESTAMP,
  ARG_DURATION
};

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("text/webvtt"));

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("text/plain; text/x-pango-markup"));

static GstFlowReturn gst_webvtt_enc_chain (GstPad * pad, GstBuffer * buf);
static gchar *gst_webvtt_enc_timeconvertion (GstWebvttEnc * webvttenc,
    GstBuffer * buf);
static gchar *gst_webvtt_enc_timestamp_to_string (GstClockTime timestamp);
static void gst_webvtt_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_webvtt_enc_reset (GstWebvttEnc * webvttenc);
static void gst_webvtt_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);

GST_BOILERPLATE (GstWebvttEnc, gst_webvtt_enc, GstElement, GST_TYPE_ELEMENT);

static gchar *
gst_webvtt_enc_timestamp_to_string (GstClockTime timestamp)
{
  guint h, m, s, ms;

  h = timestamp / (3600 * GST_SECOND);

  timestamp -= h * 3600 * GST_SECOND;
  m = timestamp / (60 * GST_SECOND);

  timestamp -= m * 60 * GST_SECOND;
  s = timestamp / GST_SECOND;

  timestamp -= s * GST_SECOND;
  ms = timestamp / GST_MSECOND;

  return g_strdup_printf ("%02d:%02d:%02d.%03d", h, m, s, ms);
}

static gchar *
gst_webvtt_enc_timeconvertion (GstWebvttEnc * webvttenc, GstBuffer * buf)
{
  gchar *start_time;
  gchar *stop_time;
  gchar *string;

  start_time = gst_webvtt_enc_timestamp_to_string (GST_BUFFER_TIMESTAMP (buf) +
      webvttenc->timestamp);
  if (GST_CLOCK_TIME_IS_VALID (GST_BUFFER_DURATION (buf))) {
    stop_time = gst_webvtt_enc_timestamp_to_string (GST_BUFFER_TIMESTAMP (buf) +
        webvttenc->timestamp + GST_BUFFER_DURATION (buf) + webvttenc->duration);
  } else {
    stop_time = gst_webvtt_enc_timestamp_to_string (GST_BUFFER_TIMESTAMP (buf) +
        webvttenc->timestamp + webvttenc->duration);
  }
  string = g_strdup_printf ("%s --> %s\n", start_time, stop_time);

  g_free (start_time);
  g_free (stop_time);
  return string;
}

static GstFlowReturn
gst_webvtt_enc_chain (GstPad * pad, GstBuffer * buf)
{
  GstWebvttEnc *webvttenc;
  GstBuffer *new_buffer;
  gchar *timing;
  GstFlowReturn ret;

  webvttenc = GST_WEBVTT_ENC (gst_pad_get_parent_element (pad));

  if (!webvttenc->pushed_header) {
    const char *header = "WEBVTT\n\n";

    new_buffer = gst_buffer_new_and_alloc (strlen (header));
    memcpy (GST_BUFFER_DATA (new_buffer), header, strlen (header));

    GST_BUFFER_TIMESTAMP (new_buffer) = GST_CLOCK_TIME_NONE;
    GST_BUFFER_DURATION (new_buffer) = GST_CLOCK_TIME_NONE;

    ret = gst_pad_push (webvttenc->srcpad, new_buffer);
    if (ret != GST_FLOW_OK) {
      goto out;
    }

    webvttenc->pushed_header = TRUE;
  }

  gst_object_sync_values (G_OBJECT (webvttenc), GST_BUFFER_TIMESTAMP (buf));

  timing = gst_webvtt_enc_timeconvertion (webvttenc, buf);
  new_buffer =
      gst_buffer_new_and_alloc (strlen (timing) + GST_BUFFER_SIZE (buf) + 1);
  memcpy (GST_BUFFER_DATA (new_buffer), timing, strlen (timing));
  memcpy (GST_BUFFER_DATA (new_buffer) + strlen (timing), GST_BUFFER_DATA (buf),
      GST_BUFFER_SIZE (buf));
  memcpy (GST_BUFFER_DATA (new_buffer) + GST_BUFFER_SIZE (new_buffer) - 1,
      "\n", 1);
  g_free (timing);

  GST_BUFFER_TIMESTAMP (new_buffer) = GST_BUFFER_TIMESTAMP (buf);
  GST_BUFFER_DURATION (new_buffer) = GST_BUFFER_DURATION (buf);


  ret = gst_pad_push (webvttenc->srcpad, new_buffer);

out:
  gst_buffer_unref (buf);
  gst_object_unref (webvttenc);

  return ret;
}

static void
gst_webvtt_enc_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class,
      &sink_template);
  gst_element_class_add_static_pad_template (element_class, &src_template);

  gst_element_class_set_details_simple (element_class,
      "WebVTT encoder", "Codec/Encoder/Subtitle",
      "WebVTT subtitle encoder", "David Schleef <ds@schleef.org>");
}

static void
gst_webvtt_enc_reset (GstWebvttEnc * webvttenc)
{
  webvttenc->counter = 1;
}

static GstStateChangeReturn
gst_webvtt_enc_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstWebvttEnc *webvttenc = GST_WEBVTT_ENC (element);

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_webvtt_enc_reset (webvttenc);
      break;
    default:
      break;
  }

  return ret;
}

static void
gst_webvtt_enc_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstWebvttEnc *webvttenc;

  webvttenc = GST_WEBVTT_ENC (object);

  switch (prop_id) {
    case ARG_TIMESTAMP:
      g_value_set_int64 (value, webvttenc->timestamp);
      break;
    case ARG_DURATION:
      g_value_set_int64 (value, webvttenc->duration);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_webvtt_enc_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{

  GstWebvttEnc *webvttenc;

  webvttenc = GST_WEBVTT_ENC (object);

  switch (prop_id) {
    case ARG_TIMESTAMP:
      webvttenc->timestamp = g_value_get_int64 (value);
      break;
    case ARG_DURATION:
      webvttenc->duration = g_value_get_int64 (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_webvtt_enc_class_init (GstWebvttEncClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_webvtt_enc_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_webvtt_enc_get_property);

  element_class->change_state = GST_DEBUG_FUNCPTR (gst_webvtt_enc_change_state);

  g_object_class_install_property (gobject_class, ARG_TIMESTAMP,
      g_param_spec_int64 ("timestamp", "Offset for the starttime",
          "Offset for the starttime for the subtitles", G_MININT64, G_MAXINT64,
          0,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_DURATION,
      g_param_spec_int64 ("duration", "Offset for the duration",
          "Offset for the duration of the subtitles", G_MININT64, G_MAXINT64,
          0,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));

  GST_DEBUG_CATEGORY_INIT (webvttenc_debug, "webvttenc", 0,
      "SubRip subtitle encoder");
}

static void
gst_webvtt_enc_init (GstWebvttEnc * webvttenc, GstWebvttEncClass * klass)
{
  gst_webvtt_enc_reset (webvttenc);

  webvttenc->srcpad = gst_pad_new_from_static_template (&src_template, "src");
  gst_element_add_pad (GST_ELEMENT (webvttenc), webvttenc->srcpad);
  webvttenc->sinkpad =
      gst_pad_new_from_static_template (&sink_template, "sink");
  gst_element_add_pad (GST_ELEMENT (webvttenc), webvttenc->sinkpad);
  gst_pad_set_chain_function (webvttenc->sinkpad, gst_webvtt_enc_chain);
}
