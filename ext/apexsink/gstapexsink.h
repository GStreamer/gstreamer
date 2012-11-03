/* GStreamer - AirPort Express (ApEx) Audio Sink -
 *
 * Remote Audio Access Protocol (RAOP) as used in Apple iTunes to stream music to the Airport Express (ApEx) -
 * RAOP is based on the Real Time Streaming Protocol (RTSP) but with an extra challenge-response RSA based authentication step.
 *
 * RAW PCM input only as defined by the following GST_STATIC_PAD_TEMPLATE regarding the expected gstapexraop input format.
 *
 * Copyright (C) 2008 Jérémie Bernard [GRemi] <gremimail@gmail.com>
 *
 * gstapexsink.h
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#ifndef __GST_APEXSINK_H__
#define __GST_APEXSINK_H__

#include "gstapexraop.h"

#include <gst/audio/gstaudiosink.h>
#include <gst/interfaces/mixer.h>

G_BEGIN_DECLS

/* standard gstreamer macros */
#define GST_TYPE_APEX_SINK           	 	(gst_apexsink_get_type())
#define GST_APEX_SINK(obj)            		(G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_APEX_SINK,GstApExSink))
#define GST_APEX_SINK_CLASS(klass)    		(G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_APEX_SINK,GstApExSinkClass))
#define GST_IS_APEX_SINK(obj)         		(G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_APEX_SINK))
#define GST_IS_APEX_SINK_CLASS(klass) 		(G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_APEX_SINK))
#define GST_APEX_SINK_CAST(obj)       		((GstApExSink*)(obj))
#define GST_APEX_SINK_NAME			"apexsink"
#define GST_APEX_SINK_JACKTYPE_TYPE		(gst_apexsink_jacktype_get_type())
#define GST_APEX_SINK_JACKSTATUS_TYPE		(gst_apexsink_jackstatus_get_type())
#define GST_APEX_SINK_GENERATION_TYPE		(gst_apexsink_generation_get_type())
#define GST_APEX_SINK_TRANSPORT_PROTOCOL_TYPE	(gst_apexsink_transport_protocol_get_type())
/* ApEx classes declaration */
typedef struct _GstApExSink GstApExSink;
typedef struct _GstApExSinkClass GstApExSinkClass;

struct _GstApExSink
{
  /* base definition */
  GstAudioSink sink;

  /* public read/write sink properties */
  gchar *host;
  guint port;
  guint volume;
  GstApExGeneration generation;
  GstApExTransportProtocol transport_protocol;

  /* private attributes : 
   * latency time local copy 
   * tracks list of the mixer interface
   * clock for sleeping
   * clock ID for sleeping / canceling sleep
   */
  guint64 latency_time;
  GList *tracks;
  GstClock *clock;
  GstClockID clock_id;

  /* private apex client */
  GstApExRAOP *gst_apexraop;
};

struct _GstApExSinkClass
{
  GstAudioSinkClass parent_class;
};

/* genums */
GType gst_apexsink_jackstatus_get_type (void);
GType gst_apexsink_jacktype_get_type (void);
GType gst_apexsink_generation_get_type (void);
GType gst_apexsink_transport_protocol_get_type (void);

/* audio sink standard api */
GType gst_apexsink_get_type (void);

G_END_DECLS

#endif
