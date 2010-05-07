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
 * @see_also: rtpdtmfsrc, dtmfsrc
 *
 * The RTPDTMFMuxer mixes/muxes RTP DTMF stream(s) into other RTP
 * streams. It does exactly what it's parent (rtpmux) does, except
 * that it allows upstream peer elements to request exclusive access
 * to the stream, which is required by the RTP DTMF standards (see RFC
 * 2833, section 3.2, para 1 for details). The peer upstream element
 * requests the acquisition and release of a stream lock beginning
 * using custom downstream gstreamer events. To request the acquisition
 * of the lock, the peer element must send an event of type
 * GST_EVENT_CUSTOM_DOWNSTREAM_OOB, having a
 * structure of name "stream-lock" with only one boolean field:
 * "lock". If this field is set to TRUE, the request is for the
 * acquisition of the lock, otherwise it is for release of the lock.
 *
 * For example, the following code in an upstream peer element
 * requests the acquisition of the stream lock:
 *
 * <programlisting>
 * GstEvent *event;
 * GstStructure *structure;
 * GstPad *srcpad;
 *
 * ... /\* srcpad initialization goes here \*\/
 *
 * structure = gst_structure_new ("stream-lock",
 *                    "lock", G_TYPE_BOOLEAN, TRUE, NULL);
 *
 * event = gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM_OOB, structure);
 * gst_pad_push_event (dtmfsrc->srcpad, event);
 * </programlisting>
 *
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
  SIGNAL_LOCKING_STREAM,
  SIGNAL_UNLOCKED_STREAM,
  LAST_SIGNAL
};

static guint gst_rtpdtmfmux_signals[LAST_SIGNAL] = { 0 };

static void gst_rtp_dtmf_mux_dispose (GObject * object);

static void gst_rtp_dtmf_mux_release_pad (GstElement * element, GstPad * pad);

static gboolean gst_rtp_dtmf_mux_sink_event (GstPad * pad, GstEvent * event);
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

  gst_rtpdtmfmux_signals[SIGNAL_LOCKING_STREAM] =
      g_signal_new ("locking", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstRTPDTMFMuxClass, locking), NULL, NULL,
      gst_marshal_VOID__OBJECT, G_TYPE_NONE, 1, GST_TYPE_PAD);

  gst_rtpdtmfmux_signals[SIGNAL_UNLOCKED_STREAM] =
      g_signal_new ("unlocked", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstRTPDTMFMuxClass, unlocked), NULL, NULL,
      gst_marshal_VOID__OBJECT, G_TYPE_NONE, 1, GST_TYPE_PAD);

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_rtp_dtmf_mux_dispose);
  gstelement_class->release_pad =
      GST_DEBUG_FUNCPTR (gst_rtp_dtmf_mux_release_pad);
  gstrtpmux_class->chain_func = GST_DEBUG_FUNCPTR (gst_rtp_dtmf_mux_chain);
  gstrtpmux_class->sink_event_func =
      GST_DEBUG_FUNCPTR (gst_rtp_dtmf_mux_sink_event);
}

static void
gst_rtp_dtmf_mux_dispose (GObject * object)
{
  GstRTPDTMFMux *mux;

  mux = GST_RTP_DTMF_MUX (object);

  GST_OBJECT_LOCK (mux);
  if (mux->special_pad != NULL) {
    gst_object_unref (mux->special_pad);
    mux->special_pad = NULL;
  }
  GST_OBJECT_UNLOCK (mux);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static GstFlowReturn
gst_rtp_dtmf_mux_chain (GstPad * pad, GstBuffer * buffer)
{
  GstRTPDTMFMux *mux;
  GstFlowReturn ret;

  mux = GST_RTP_DTMF_MUX (gst_pad_get_parent (pad));

  GST_OBJECT_LOCK (mux);
  if (mux->special_pad != NULL && mux->special_pad != pad) {
    /* Drop the buffer */
    gst_buffer_unref (buffer);
    ret = GST_FLOW_OK;
    GST_OBJECT_UNLOCK (mux);
  }

  else {
    GST_OBJECT_UNLOCK (mux);
    if (parent_class->chain_func)
      ret = parent_class->chain_func (pad, buffer);
    else {
      gst_buffer_unref (buffer);
      ret = GST_FLOW_ERROR;
    }
  }

  gst_object_unref (mux);
  return ret;
}

static void
gst_rtp_dtmf_mux_lock_stream (GstRTPDTMFMux * mux, GstPad * pad)
{
  if (mux->special_pad != NULL)
    GST_WARNING_OBJECT (mux,
        "Stream lock already acquired by pad %s",
        GST_ELEMENT_NAME (mux->special_pad));

  else {
    GST_DEBUG_OBJECT (mux,
        "Stream lock acquired by pad %s", GST_ELEMENT_NAME (pad));
    mux->special_pad = gst_object_ref (pad);
  }
}

static void
gst_rtp_dtmf_mux_unlock_stream (GstRTPDTMFMux * mux, GstPad * pad)
{
  if (mux->special_pad == NULL)
    GST_WARNING_OBJECT (mux, "Stream lock not acquired, can't release it");

  else if (pad != mux->special_pad)
    GST_WARNING_OBJECT (mux,
        "pad %s attempted to release Stream lock"
        " which was acquired by pad %s", GST_ELEMENT_NAME (pad),
        GST_ELEMENT_NAME (mux->special_pad));
  else {
    GST_DEBUG_OBJECT (mux,
        "Stream lock released by pad %s", GST_ELEMENT_NAME (mux->special_pad));
    gst_object_unref (mux->special_pad);
    mux->special_pad = NULL;
  }
}

static gboolean
gst_rtp_dtmf_mux_handle_stream_lock_event (GstRTPDTMFMux * mux, GstPad * pad,
    const GstStructure * event_structure)
{
  gboolean lock;

  if (!gst_structure_get_boolean (event_structure, "lock", &lock))
    return FALSE;

  if (lock)
    g_signal_emit (G_OBJECT (mux),
        gst_rtpdtmfmux_signals[SIGNAL_LOCKING_STREAM], 0, pad);

  GST_OBJECT_LOCK (mux);
  if (lock)
    gst_rtp_dtmf_mux_lock_stream (mux, pad);
  else
    gst_rtp_dtmf_mux_unlock_stream (mux, pad);
  GST_OBJECT_UNLOCK (mux);

  if (!lock)
    g_signal_emit (G_OBJECT (mux),
        gst_rtpdtmfmux_signals[SIGNAL_UNLOCKED_STREAM], 0, pad);

  return TRUE;
}

static gboolean
gst_rtp_dtmf_mux_handle_downstream_event (GstRTPDTMFMux * mux,
    GstPad * pad, GstEvent * event)
{
  const GstStructure *structure;
  gboolean ret = FALSE;

  structure = gst_event_get_structure (event);
  /* FIXME: is this event generic enough to be given a generic name? */
  if (structure && gst_structure_has_name (structure, "stream-lock"))
    ret = gst_rtp_dtmf_mux_handle_stream_lock_event (mux, pad, structure);

  return ret;
}

static gboolean
gst_rtp_dtmf_mux_ignore_event (GstPad * pad, GstEvent * event)
{
  gboolean ret;

  if (parent_class->sink_event_func)
    /* Give the parent a chance to handle the event first */
    ret = parent_class->sink_event_func (pad, event);

  else
    ret = gst_pad_event_default (pad, event);

  return ret;
}

static gboolean
gst_rtp_dtmf_mux_sink_event (GstPad * pad, GstEvent * event)
{
  GstRTPDTMFMux *mux;
  gboolean ret = FALSE;

  mux = (GstRTPDTMFMux *) gst_pad_get_parent (pad);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CUSTOM_DOWNSTREAM_OOB:
      ret = gst_rtp_dtmf_mux_handle_downstream_event (mux, pad, event);
      gst_event_unref (event);
      break;
    default:
      ret = gst_rtp_dtmf_mux_ignore_event (pad, event);
      break;
  }

  gst_object_unref (mux);
  return ret;
}

static void
gst_rtp_dtmf_mux_release_pad (GstElement * element, GstPad * pad)
{
  GstRTPDTMFMux *mux = GST_RTP_DTMF_MUX (element);

  GST_OBJECT_LOCK (mux);
  if (mux->special_pad == pad) {
    gst_object_unref (mux->special_pad);
    mux->special_pad = NULL;
  }
  GST_OBJECT_UNLOCK (mux);

  GST_CALL_PARENT (GST_ELEMENT_CLASS, release_pad, (element, pad));
}

gboolean
gst_rtp_dtmf_mux_plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_rtp_dtmf_mux_debug, "rtpdtmfmux", 0,
      "rtp dtmf muxer");

  return gst_element_register (plugin, "rtpdtmfmux", GST_RANK_NONE,
      GST_TYPE_RTP_DTMF_MUX);
}
