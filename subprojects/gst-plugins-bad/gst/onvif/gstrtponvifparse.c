/*
 * gstrtponviftimestamp-parse.c
 *
 * Copyright (C) 2014 Axis Communications AB
 *  Author: Guillaume Desmottes <guillaume.desmottes@collabora.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gst/rtp/gstrtpbuffer.h>

#include "gstrtponvifparse.h"

#define ALLOWED_GAP_IN_SECS 5

static GstFlowReturn gst_rtp_onvif_parse_chain(GstPad *pad,
                                               GstObject *parent, GstBuffer *buf);
static gboolean gst_onvif_parse_sink_event (GstPad * pad,
    GstObject * parent, GstEvent * ev);

static GstStaticPadTemplate sink_template_factory =
    GST_STATIC_PAD_TEMPLATE("sink",
                            GST_PAD_SINK,
                            GST_PAD_ALWAYS,
                            GST_STATIC_CAPS("application/x-rtp"));

static GstStaticPadTemplate src_template_factory =
    GST_STATIC_PAD_TEMPLATE("src",
                            GST_PAD_SRC,
                            GST_PAD_ALWAYS,
                            GST_STATIC_CAPS("application/x-rtp"));

G_DEFINE_TYPE(GstRtpOnvifParse, gst_rtp_onvif_parse, GST_TYPE_ELEMENT);
GST_ELEMENT_REGISTER_DEFINE(rtponvifparse, "rtponvifparse",
                            GST_RANK_NONE, GST_TYPE_RTP_ONVIF_PARSE);

enum
{
  GAP_DETECTED,
  LAST_SIGNAL,
};

static guint rtp_onvif_signals[LAST_SIGNAL] = {0};

static void
gst_rtp_onvif_parse_class_init(GstRtpOnvifParseClass *klass)
{
  GstElementClass *gstelement_class;

  gstelement_class = GST_ELEMENT_CLASS(klass);

  /* register pads */
  gst_element_class_add_static_pad_template(gstelement_class,
                                            &sink_template_factory);
  gst_element_class_add_static_pad_template(gstelement_class,
                                            &src_template_factory);

  rtp_onvif_signals[GAP_DETECTED] =
      g_signal_new("gap-detected", G_TYPE_FROM_CLASS(klass),
                   G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_UINT64);

  gst_element_class_set_static_metadata(gstelement_class,
                                        "ONVIF NTP timestamps RTP extension", "Effect/RTP",
                                        "Add absolute timestamps and flags of recorded data in a playback "
                                        "session",
                                        "Guillaume Desmottes <guillaume.desmottes@collabora.com>");
}

static void
gst_rtp_onvif_parse_init(GstRtpOnvifParse *self)
{
  self->sinkpad =
      gst_pad_new_from_static_template(&sink_template_factory, "sink");
  gst_pad_set_chain_function(self->sinkpad, gst_rtp_onvif_parse_chain);
  gst_pad_set_event_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_onvif_parse_sink_event));
  gst_element_add_pad(GST_ELEMENT(self), self->sinkpad);
  GST_PAD_SET_PROXY_CAPS(self->sinkpad);

  self->srcpad =
      gst_pad_new_from_static_template(&src_template_factory, "src");
  gst_element_add_pad(GST_ELEMENT(self), self->srcpad);
  
  self->gap_detected = FALSE;
  self->is_reverse = FALSE;
  self->previous_key_frame_timestamp = FALSE;
  self->first_buffer = TRUE;
}

#define EXTENSION_ID 0xABAC
#define EXTENSION_SIZE 3

static gboolean
handle_buffer(GstRtpOnvifParse *self, GstBuffer *buf, gboolean *send_eos)
{
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  guint8 *data;
  guint16 bits;
  guint wordlen;
  guint8 flags;
  guint64 timestamp_seconds;
  gint64 gap=0;
  gint64 gap_absolute;
  guint64 timestamp_fraction;
  guint64 timestamp_nseconds;
  /*
     guint8 cseq;
   */


  if (!gst_rtp_buffer_map(buf, GST_MAP_READWRITE, &rtp))
  {
    GST_ELEMENT_ERROR(self, STREAM, FAILED,
                      ("Failed to map RTP buffer"), (NULL));
    return FALSE;
  }

  GST_DEBUG_OBJECT(NULL, "onvif: buffer timestamp %" GST_TIME_FORMAT,
                   GST_TIME_ARGS(GST_BUFFER_TIMESTAMP(buf)));

  /* Check if the ONVIF RTP extension is present in the packet */
  if (!gst_rtp_buffer_get_extension_data(&rtp, &bits, (gpointer)&data,
                                         &wordlen))
    goto out;

  if (bits != EXTENSION_ID || wordlen != EXTENSION_SIZE)
    goto out;

  timestamp_seconds = GST_READ_UINT32_BE(data);
  timestamp_fraction = GST_READ_UINT32_BE(data + 4);
  timestamp_nseconds =
      (timestamp_fraction * G_GINT64_CONSTANT(1000000000)) >> 32;

  if (timestamp_seconds == G_MAXUINT32 && timestamp_fraction == G_MAXUINT32)
  {
    GST_BUFFER_PTS(buf) = GST_CLOCK_TIME_NONE;
  }
  else
  {
    GST_BUFFER_PTS(buf) =
        timestamp_seconds * GST_SECOND + timestamp_nseconds * GST_NSECOND;
  }

  if(self->first_buffer){
    self->previous_key_frame_timestamp = timestamp_seconds;
  }
  flags = GST_READ_UINT8(data + 8);
  /* cseq = GST_READ_UINT8 (data + 9);  TODO */

  /* C */
  if (flags & (1 << 7))
  {
    GST_BUFFER_FLAG_UNSET(buf, GST_BUFFER_FLAG_DELTA_UNIT);
    	    GST_DEBUG("KEY_FRAME: curr_buf_time: %llu\tprevious_buf_time: %llu\n",timestamp_seconds,self->previous_key_frame_timestamp);
  	    /*For reverse direction check the gap between current buffer and previous buffer (ignore first buffer), if gap is more than allowed_gap, then do seek*/
	    if(self->is_reverse && !self->first_buffer){
		    gap = (timestamp_seconds - self->previous_key_frame_timestamp);
		    gap_absolute = llabs(gap);
	 	    if(gap_absolute > ALLOWED_GAP_IN_SECS){
    		    	GST_WARNING("gap_detected: %lld\ttime_before_gap: %llu\ttime_after_gap: %llu\n",gap_absolute,self->previous_key_frame_timestamp,timestamp_seconds);
    			g_signal_emit(self, rtp_onvif_signals[GAP_DETECTED], 0, (timestamp_seconds * GST_SECOND + timestamp_nseconds * GST_NSECOND));
		    }	    
		    self->previous_key_frame_timestamp = timestamp_seconds;
	    }
  }
  else
    GST_BUFFER_FLAG_SET(buf, GST_BUFFER_FLAG_DELTA_UNIT);
  
  /* For forward direction check the gap_detected flag which is true after flag_e comes, if gap_detected is true in forward direction emit signal, ignore for reverse direction */
  if(self->gap_detected){
    self->gap_detected = FALSE;
    if(!self->is_reverse)    
        GST_WARNING("after gap_detected, time: sec: %llu\ttnsec: %llu\n",timestamp_seconds,timestamp_nseconds);	   
    	g_signal_emit(self, rtp_onvif_signals[GAP_DETECTED], 0, (timestamp_seconds * GST_SECOND + timestamp_nseconds * GST_NSECOND));
  }
  /* E */
  if (flags & (1 << 6)) {
    GST_INFO("after receiving E_FLAG, time: sec: %llu\ttnsec: %llu\n",timestamp_seconds,timestamp_nseconds);	   
    self->gap_detected = TRUE;    
  }
  

  /* D */
  if (flags & (1 << 5)) {
    GST_BUFFER_FLAG_SET(buf, GST_BUFFER_FLAG_DISCONT);
  }
  else {
    GST_BUFFER_FLAG_UNSET(buf, GST_BUFFER_FLAG_DISCONT);
  }

  /* T */
  if (flags & (1 << 4))
    *send_eos = TRUE;

  if(self->first_buffer){
    self->first_buffer = FALSE;
  }
out:
  gst_rtp_buffer_unmap(&rtp);
  return TRUE;
}

static GstFlowReturn
gst_rtp_onvif_parse_chain(GstPad *pad, GstObject *parent, GstBuffer *buf)
{
  GstRtpOnvifParse *self = GST_RTP_ONVIF_PARSE(parent);
  GstFlowReturn ret;
  gboolean send_eos = FALSE;

  if (!handle_buffer(self, buf, &send_eos))
  {
    gst_buffer_unref(buf);
    return GST_FLOW_ERROR;
  }

  ret = gst_pad_push(self->srcpad, buf);

  if (ret == GST_FLOW_OK && send_eos)
  {
    GstEvent *event;

    event = gst_event_new_eos();
    gst_pad_push_event(self->srcpad, event);
    ret = GST_FLOW_EOS;
  }

  return ret;
}
static gboolean
gst_onvif_parse_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstRtpOnvifParse *self = GST_RTP_ONVIF_PARSE (parent);
  gboolean ret;
  const GstSegment *new_segment;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEGMENT:{
  	  self->first_buffer = TRUE;
          gst_event_parse_segment (event, &new_segment);
	  if (new_segment->rate < 0)
	  {
      		  GST_ERROR("Got rate as -1, reverse is true\n");
		  self->is_reverse = TRUE;
	  }
	  else 
	  {
      		  GST_ERROR("Got rate as 1, reverse is false\n");
		  self->is_reverse = FALSE;
	  }
    }
    default:
      ret = gst_pad_event_default (pad, parent, event);
      break;
  }
  return ret;
}

