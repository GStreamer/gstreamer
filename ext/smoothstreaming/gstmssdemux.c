/* GStreamer
 * Copyright (C) 2012 Smart TV Alliance
 *  Author: Thiago Sousa Santos <thiago.sousa.santos@collabora.com>, Collabora Ltd.
 *
 * gstmssdemux.c:
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
 * SECTION:element-mssdemux
 *
 * Demuxes a Microsoft's Smooth Streaming manifest into its audio and/or video streams.
 *
 * TODO
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gst/gst-i18n-plugin.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gstmssdemux.h"

GST_DEBUG_CATEGORY (mssdemux_debug);

static GstStaticPadTemplate gst_mss_demux_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/vnd.ms-sstr+xml")
    );

static GstStaticPadTemplate gst_mss_demux_videosrc_template =
GST_STATIC_PAD_TEMPLATE ("video_%02u",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate gst_mss_demux_audiosrc_template =
GST_STATIC_PAD_TEMPLATE ("audio_%02u",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

GST_BOILERPLATE (GstMssDemux, gst_mss_demux, GstMssDemux, GST_TYPE_ELEMENT);

static void gst_mss_demux_dispose (GObject * object);
static GstStateChangeReturn
gst_mss_demux_change_state (GstElement * element, GstStateChange transition);
static GstFlowReturn gst_mss_demux_chain (GstPad * pad, GstBuffer * buffer);
static GstFlowReturn gst_mss_demux_event (GstPad * pad, GstEvent * event);

static void gst_mss_demux_process_manifest (GstMssDemux * mssdemux);

static void
gst_mss_demux_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class,
      &gst_mss_demux_sink_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_mss_demux_videosrc_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_mss_demux_audiosrc_template);
  gst_element_class_set_details_simple (element_class, "Smooth Streaming "
      "demuxer", "Demuxer",
      "Parse and demultiplex a Smooth Streaming manifest into audio and video "
      "streams", "Thiago Santos <thiago.sousa.santos@collabora.com>");

  GST_DEBUG_CATEGORY_INIT (mssdemux_debug, "mssdemux", 0, "mssdemux plugin");
}

static void
gst_mss_demux_class_init (GstMssDemuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->dispose = gst_mss_demux_dispose;

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_mss_demux_change_state);
}

static void
gst_mss_demux_init (GstMssDemux * mssdemux, GstMssDemuxClass * klass)
{
  mssdemux->sinkpad =
      gst_pad_new_from_static_template (&gst_mss_demux_sink_template, "sink");
  gst_pad_set_chain_function (mssdemux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_mss_demux_chain));
  gst_pad_set_event_function (mssdemux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_mss_demux_event));
  gst_element_add_pad (GST_ELEMENT_CAST (mssdemux), mssdemux->sinkpad);
}

static void
gst_mss_demux_reset (GstMssDemux * mssdemux)
{
  if (mssdemux->manifest_buffer) {
    gst_buffer_unref (mssdemux->manifest_buffer);
  }
  if (mssdemux->manifest) {
    gst_mss_manifest_free (mssdemux->manifest);
  }
}

static void
gst_mss_demux_dispose (GObject * object)
{
  /* GstMssDemux *mssdemux = GST_MSS_DEMUX_CAST (object); */

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static GstStateChangeReturn
gst_mss_demux_change_state (GstElement * element, GstStateChange transition)
{
  GstMssDemux *mssdemux = GST_MSS_DEMUX_CAST (element);
  GstStateChangeReturn result = GST_STATE_CHANGE_FAILURE;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_mss_demux_reset (mssdemux);
      break;
    default:
      break;
  }

  result = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:{
      break;
    }
    default:
      break;
  }

  return result;
}

static GstFlowReturn
gst_mss_demux_chain (GstPad * pad, GstBuffer * buffer)
{
  GstMssDemux *mssdemux = GST_MSS_DEMUX_CAST (GST_PAD_PARENT (pad));
  if (mssdemux->manifest_buffer == NULL)
    mssdemux->manifest_buffer = buffer;
  else
    mssdemux->manifest_buffer =
        gst_buffer_join (mssdemux->manifest_buffer, buffer);

  return GST_FLOW_OK;
}

static gboolean
gst_mss_demux_event (GstPad * pad, GstEvent * event)
{
  GstMssDemux *mssdemux = GST_MSS_DEMUX_CAST (GST_PAD_PARENT (pad));
  gboolean forward = TRUE;
  gboolean ret = TRUE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      if (mssdemux->manifest_buffer == NULL) {
        GST_WARNING_OBJECT (mssdemux, "Received EOS without a manifest.");
        break;
      }

      gst_mss_demux_process_manifest (mssdemux);
      forward = FALSE;
      break;
    default:
      break;
  }

  if (forward) {
    ret = gst_pad_event_default (pad, event);
  } else {
    gst_event_unref (event);
  }

  return ret;
}

static void
gst_mss_demux_process_manifest (GstMssDemux * mssdemux)
{
  g_return_if_fail (mssdemux->manifest_buffer != NULL);
  g_return_if_fail (mssdemux->manifest == NULL);

  mssdemux->manifest = gst_mss_manifest_new (mssdemux->manifest_buffer);
  /* TODO */
}
