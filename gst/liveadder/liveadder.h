/*
 * GStreamer
 *
 *  Copyright 2008 Collabora Ltd
 *  Copyright 2008 Nokia Corporation
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */



#ifndef __GST_LIVE_ADDER_H__
#define __GST_LIVE_ADDER_H__

#include <gst/gst.h>
#include <gst/audio/audio.h>

G_BEGIN_DECLS
#define GST_TYPE_LIVE_ADDER            (gst_live_adder_get_type())
#define GST_LIVE_ADDER(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_LIVE_ADDER,GstLiveAdder))
#define GST_IS_LIVE_ADDER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_LIVE_ADDER))
#define GST_LIVE_ADDER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass) ,GST_TYPE_LIVE_ADDER,GstLiveAdderClass))
#define GST_IS_LIVE_ADDER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass) ,GST_TYPE_LIVE_ADDER))
#define GST_LIVE_ADDER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj) ,GST_TYPE_LIVE_ADDER,GstLiveAdderClass))
typedef struct _GstLiveAdder GstLiveAdder;
typedef struct _GstLiveAdderClass GstLiveAdderClass;

typedef void (*GstLiveAdderFunction) (gpointer out, gpointer in, guint size);

/**
 * GstLiveAdder:
 *
 * The adder object structure.
 */
struct _GstLiveAdder
{
  /*< private >*/
  GstElement element;

  GstPad *srcpad;
  /* pad counter, used for creating unique request pads */
  gint padcount;
  GList *sinkpads;

  GstFlowReturn srcresult;
  GstClockID clock_id;

  /* the queue is ordered head to tail */
  GQueue *buffers;
  GCond not_empty_cond;

  GstClockTime next_timestamp;

  /* the next are valid for both int and float */
  GstAudioInfo info;

  /* function to add samples */
  GstLiveAdderFunction func;

  GstClockTime latency_ms;
  GstClockTime peer_latency;

  gboolean segment_pending;

  gboolean playing;
};

struct _GstLiveAdderClass
{
  GstElementClass parent_class;
};

GType gst_live_adder_get_type (void);

G_END_DECLS
#endif /* __GST_LIVE_ADDER_H__ */
