/* GStreamer
 * Copyright (C) <2008> Thijs Vermeir <thijsvermeir@gmail.com>
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

#include "gstsrtenc.h"
#include <gst/controller/gstcontroller.h>

GST_DEBUG_CATEGORY_STATIC (srtenc_debug);
#define GST_CAT_DEFAULT srtenc_debug

enum
{
  ARG_0,
  ARG_TIMESTAMP,
  ARG_DURATION
};

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-subtitle"));

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("text/plain; text/x-pango-markup"));

static GstFlowReturn gst_srt_enc_chain (GstPad * pad, GstBuffer * buf);
static gchar *gst_srt_enc_timeconvertion (GstSrtEnc * srtenc, GstBuffer * buf);
static gchar *gst_srt_enc_timestamp_to_string (GstClockTime timestamp);
static void gst_srt_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_srt_enc_reset (GstSrtEnc * srtenc);
static void gst_srt_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);

GST_BOILERPLATE (GstSrtEnc, gst_srt_enc, GstElement, GST_TYPE_ELEMENT);

static gchar *
gst_srt_enc_timestamp_to_string (GstClockTime timestamp)
{
  guint h, m, s, ms;

  h = timestamp / (3600 * GST_SECOND);

  timestamp -= h * 3600 * GST_SECOND;
  m = timestamp / (60 * GST_SECOND);

  timestamp -= m * 60 * GST_SECOND;
  s = timestamp / GST_SECOND;

  timestamp -= s * GST_SECOND;
  ms = timestamp / GST_MSECOND;

  return g_strdup_printf ("%.2d:%.2d:%.2d,%.3d", h, m, s, ms);
}

static gchar *
gst_srt_enc_timeconvertion (GstSrtEnc * srtenc, GstBuffer * buf)
{
  gchar *start_time =
      gst_srt_enc_timestamp_to_string (GST_BUFFER_TIMESTAMP (buf) +
      srtenc->timestamp);
  gchar *stop_time =
      gst_srt_enc_timestamp_to_string (GST_BUFFER_TIMESTAMP (buf) +
      srtenc->timestamp + GST_BUFFER_DURATION (buf) + srtenc->duration);
  gchar *string = g_strdup_printf ("%s --> %s\n", start_time, stop_time);

  g_free (start_time);
  g_free (stop_time);
  return string;
}

static GstFlowReturn
gst_srt_enc_chain (GstPad * pad, GstBuffer * buf)
{
  GstSrtEnc *srtenc;
  GstBuffer *new_buffer;
  gchar *timing;
  gchar *string;

  srtenc = GST_SRT_ENC (gst_pad_get_parent_element (pad));
  gst_object_sync_values (G_OBJECT (srtenc), GST_BUFFER_TIMESTAMP (buf));
  timing = gst_srt_enc_timeconvertion (srtenc, buf);
  string = g_strdup_printf ("%d\n%s", srtenc->counter++, timing);
  g_free (timing);
  new_buffer =
      gst_buffer_new_and_alloc (strlen (string) + GST_BUFFER_SIZE (buf) + 2);
  memcpy (GST_BUFFER_DATA (new_buffer), string, strlen (string));
  memcpy (GST_BUFFER_DATA (new_buffer) + strlen (string), GST_BUFFER_DATA (buf),
      GST_BUFFER_SIZE (buf));
  memcpy (GST_BUFFER_DATA (new_buffer) + GST_BUFFER_SIZE (new_buffer) - 2,
      "\n\n", 2);
  g_free (string);

  gst_buffer_unref (buf);

  return gst_pad_push (srtenc->srcpad, new_buffer);
}

static void
gst_srt_enc_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class,
      &sink_template);
  gst_element_class_add_static_pad_template (element_class, &src_template);

  gst_element_class_set_details_simple (element_class,
      "Srt encoder", "Codec/Encoder/Subtitle",
      "Srt subtitle encoder", "Thijs Vermeir <thijsvermeir@gmail.com>");
}

static void
gst_srt_enc_reset (GstSrtEnc * srtenc)
{
  srtenc->counter = 1;
}

static GstStateChangeReturn
gst_srt_enc_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstSrtEnc *srtenc = GST_SRT_ENC (element);

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_srt_enc_reset (srtenc);
      break;
    default:
      break;
  }

  return ret;
}

static void
gst_srt_enc_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstSrtEnc *srtenc;

  srtenc = GST_SRT_ENC (object);

  switch (prop_id) {
    case ARG_TIMESTAMP:
      g_value_set_int64 (value, srtenc->timestamp);
      break;
    case ARG_DURATION:
      g_value_set_int64 (value, srtenc->duration);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_srt_enc_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{

  GstSrtEnc *srtenc;

  srtenc = GST_SRT_ENC (object);

  switch (prop_id) {
    case ARG_TIMESTAMP:
      srtenc->timestamp = g_value_get_int64 (value);
      break;
    case ARG_DURATION:
      srtenc->duration = g_value_get_int64 (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_srt_enc_class_init (GstSrtEncClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_srt_enc_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_srt_enc_get_property);

  element_class->change_state = GST_DEBUG_FUNCPTR (gst_srt_enc_change_state);

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

  GST_DEBUG_CATEGORY_INIT (srtenc_debug, "srtenc", 0,
      "SubRip subtitle encoder");
}

static void
gst_srt_enc_init (GstSrtEnc * srtenc, GstSrtEncClass * klass)
{
  gst_srt_enc_reset (srtenc);

  srtenc->srcpad = gst_pad_new_from_static_template (&src_template, "src");
  gst_element_add_pad (GST_ELEMENT (srtenc), srtenc->srcpad);
  srtenc->sinkpad = gst_pad_new_from_static_template (&sink_template, "sink");
  gst_element_add_pad (GST_ELEMENT (srtenc), srtenc->sinkpad);
  gst_pad_set_chain_function (srtenc->sinkpad, gst_srt_enc_chain);
}
