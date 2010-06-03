/* RTP DTMF muxer element for GStreamer
 *
 * gstrtpdtmfmux.c:
 *
 * Copyright (C) <2007-2010> Nokia Corporation.
 *   Contact: Zeeshan Ali <zeeshan.ali@nokia.com>
 * Copyright (C) <2007-2010> Collabora Ltd
 *   Contact: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *               2000,2005 Wim Taymans <wim@fluendo.com>
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
 * SECTION:element-rtpdtmfmux
 * @see_also: rtpdtmfsrc, dtmfsrc, rtpmux
 *
 * The RTP "DTMF" Muxer muxes multiple RTP streams into a valid RTP
 * stream. It does exactly what it's parent (#rtpmux) does, except
 * that it prevent buffers coming over a regular sink_%%d pad from going through
 * for the duration of buffers that came in a priority_sink_%%d pad.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <string.h>

#include "gstrtpdtmfmux.h"

GST_DEBUG_CATEGORY_STATIC (gst_rtp_dtmf_mux_debug);
#define GST_CAT_DEFAULT gst_rtp_dtmf_mux_debug

enum
{
  LAST_SIGNAL
};


static GstStaticPadTemplate priority_sink_factory =
GST_STATIC_PAD_TEMPLATE ("priority_sink_%d",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("application/x-rtp"));

// static guint gst_rtpdtmfmux_signals[LAST_SIGNAL] = { 0 };

static GstPad *gst_rtp_dtmf_mux_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name);
static GstStateChangeReturn gst_rtp_dtmf_mux_change_state (GstElement * element,
    GstStateChange transition);

static GstFlowReturn gst_rtp_dtmf_mux_chain (GstPad * pad, GstBuffer * buffer);

GST_BOILERPLATE (GstRTPDTMFMux, gst_rtp_dtmf_mux, GstRTPMux, GST_TYPE_RTP_MUX);

static void
gst_rtp_dtmf_mux_init (GstRTPDTMFMux * object, GstRTPDTMFMuxClass * g_class)
{
}

static void
gst_rtp_dtmf_mux_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&priority_sink_factory));

  gst_element_class_set_details_simple (element_class, "RTP muxer",
      "Codec/Muxer",
      "mixes RTP DTMF streams into other RTP streams",
      "Zeeshan Ali <first.last@nokia.com>");
}

static void
gst_rtp_dtmf_mux_class_init (GstRTPDTMFMuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstRTPMuxClass *gstrtpmux_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstrtpmux_class = (GstRTPMuxClass *) klass;

  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_rtp_dtmf_mux_request_new_pad);
  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_rtp_dtmf_mux_change_state);
  gstrtpmux_class->chain_func = GST_DEBUG_FUNCPTR (gst_rtp_dtmf_mux_chain);
}

static GstFlowReturn
gst_rtp_dtmf_mux_chain (GstPad * pad, GstBuffer * buffer)
{
  GstRTPDTMFMux *mux;
  GstFlowReturn ret = GST_FLOW_ERROR;
  GstRTPMuxPadPrivate *padpriv = NULL;
  GstClockTime running_ts;

  mux = GST_RTP_DTMF_MUX (gst_pad_get_parent (pad));

  running_ts = GST_BUFFER_TIMESTAMP (buffer);

  GST_OBJECT_LOCK (mux);
  if (GST_CLOCK_TIME_IS_VALID (running_ts)) {
    padpriv = gst_pad_get_element_private (pad);
    if (padpriv && padpriv->segment.format == GST_FORMAT_TIME)
      running_ts = gst_segment_to_running_time (&padpriv->segment,
          GST_FORMAT_TIME, GST_BUFFER_TIMESTAMP (buffer));

    if (padpriv && padpriv->priority) {
      if (GST_BUFFER_DURATION_IS_VALID (buffer)) {
        if (GST_CLOCK_TIME_IS_VALID (mux->last_priority_end))
          mux->last_priority_end =
              MAX (running_ts + GST_BUFFER_DURATION (buffer),
              mux->last_priority_end);
        else
          mux->last_priority_end = running_ts + GST_BUFFER_DURATION (buffer);
        GST_LOG_OBJECT (mux, "Got buffer %p on priority pad %s,"
            " blocking regular pads until %" GST_TIME_FORMAT, buffer,
            GST_PAD_NAME (pad), GST_TIME_ARGS (mux->last_priority_end));
      } else {
        GST_WARNING_OBJECT (mux, "Buffer %p on pad %s has an invalid duration,"
            " not blocking other pad", buffer, GST_PAD_NAME (pad));
      }
    } else {
      if (GST_CLOCK_TIME_IS_VALID (mux->last_priority_end) &&
          running_ts < mux->last_priority_end) {
        GST_LOG_OBJECT (mux, "Dropping buffer %p because running time"
            " %" GST_TIME_FORMAT " < %" GST_TIME_FORMAT, buffer,
            GST_TIME_ARGS (running_ts), GST_TIME_ARGS (mux->last_priority_end));
        goto drop_buffer;
      }
    }
  } else {
    GST_LOG_OBJECT (pad, "Buffer %p has an invalid timestamp,"
        " letting through", buffer);
  }
  GST_OBJECT_UNLOCK (mux);

  if (parent_class->chain_func)
    ret = parent_class->chain_func (pad, buffer);
  else
    gst_buffer_unref (buffer);

out:

  gst_object_unref (mux);
  return ret;

drop_buffer:
  gst_buffer_unref (buffer);
  ret = GST_FLOW_OK;
  GST_OBJECT_UNLOCK (mux);
  goto out;
}


static GstPad *
gst_rtp_dtmf_mux_request_new_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * name)
{
  GstPad *pad;

  pad = GST_CALL_PARENT_WITH_DEFAULT (GST_ELEMENT_CLASS, request_new_pad,
      (element, templ, name), NULL);

  if (pad) {
    GstRTPMuxPadPrivate *padpriv;

    GST_OBJECT_LOCK (element);
    padpriv = gst_pad_get_element_private (pad);

    if (gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS (element),
            "priority_sink_%d") == gst_pad_get_pad_template (pad))
      padpriv->priority = TRUE;
    GST_OBJECT_UNLOCK (element);
  }

  return pad;
}


static GstStateChangeReturn
gst_rtp_dtmf_mux_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstRTPDTMFMux *mux = GST_RTP_DTMF_MUX (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
    {
      GST_OBJECT_LOCK (mux);
      mux->last_priority_end = GST_CLOCK_TIME_NONE;
      GST_OBJECT_UNLOCK (mux);
      break;
    }
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  return ret;
}

gboolean
gst_rtp_dtmf_mux_plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_rtp_dtmf_mux_debug, "rtpdtmfmux", 0,
      "rtp dtmf muxer");

  return gst_element_register (plugin, "rtpdtmfmux", GST_RANK_NONE,
      GST_TYPE_RTP_DTMF_MUX);
}
