/* GStreamer
 * Copyright (C) 2004 Wim Taymans <wim@fluendo.com>
 *
 * gstoggdemux.c: ogg stream demuxer
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

#ifndef __GST_OGG_DEMUX_H__
#define __GST_OGG_DEMUX_H__

#include <ogg/ogg.h>

#include <gst/gst.h>

#include "gstoggstream.h"

G_BEGIN_DECLS

#define GST_TYPE_OGG_PAD (gst_ogg_pad_get_type())
#define GST_OGG_PAD(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_OGG_PAD, GstOggPad))
#define GST_OGG_PAD_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_OGG_PAD, GstOggPad))
#define GST_IS_OGG_PAD(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_OGG_PAD))
#define GST_IS_OGG_PAD_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_OGG_PAD))

typedef struct _GstOggPad GstOggPad;
typedef struct _GstOggPadClass GstOggPadClass;

#define GST_TYPE_OGG_DEMUX (gst_ogg_demux_get_type())
#define GST_OGG_DEMUX(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_OGG_DEMUX, GstOggDemux))
#define GST_OGG_DEMUX_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_OGG_DEMUX, GstOggDemux))
#define GST_IS_OGG_DEMUX(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_OGG_DEMUX))
#define GST_IS_OGG_DEMUX_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_OGG_DEMUX))

GType gst_ogg_demux_get_type (void);

typedef struct _GstOggDemux GstOggDemux;
typedef struct _GstOggDemuxClass GstOggDemuxClass;
typedef struct _GstOggChain GstOggChain;

/* all information needed for one ogg chain (relevant for chained bitstreams) */
struct _GstOggChain
{
  GstOggDemux *ogg;

  gint64 offset;                /* starting offset of chain */
  gint64 end_offset;            /* end offset of chain */
  gint64 bytes;                 /* number of bytes */

  gboolean have_bos;

  GArray *streams;

  GstClockTime total_time;      /* the total time of this chain, this is the MAX of
                                   the totals of all streams */
  GstClockTime begin_time;      /* when this chain starts in the stream */

  GstClockTime segment_start;   /* the timestamp of the first sample, this is the MIN of
                                   the start times of all streams. */
  GstClockTime segment_stop;    /* the timestamp of the last page, this is the MAX of the
                                   streams. */
};

/* different modes for the pad */
typedef enum
{
  GST_OGG_PAD_MODE_INIT,        /* we are feeding our internal decoder to get info */
  GST_OGG_PAD_MODE_STREAMING,   /* we are streaming buffers to the outside */
} GstOggPadMode;

/* all information needed for one ogg stream */
struct _GstOggPad
{
  GstPad pad;                   /* subclass GstPad */

  gboolean have_type;
  GstOggPadMode mode;

  GstOggChain *chain;           /* the chain we are part of */
  GstOggDemux *ogg;             /* the ogg demuxer we are part of */

  GstOggStream map;

  gint64 packetno;
  gint64 current_granule;
  gint64 keyframe_granule;

  GstClockTime start_time;      /* the timestamp of the first sample */

  gint64 first_granule;         /* the granulepos of first page == first sample in next page */
  GstClockTime first_time;      /* the timestamp of the second page or granuletime of first page */

  gboolean     is_sparse;       /* TRUE if this is a subtitle pad or some other sparse stream */
  GstClockTime last_stop;       /* last_stop when last push occured; used to detect when we
                                 * need to send a newsegment update event for sparse streams */

  GList *continued;

  gboolean discont;
  GstFlowReturn last_ret;       /* last return of _pad_push() */
  gboolean is_eos;

  gboolean added;
};

struct _GstOggPadClass
{
  GstPadClass parent_class;
};

/**
 * GstOggDemux:
 *
 * The ogg demuxer object structure.
 */
struct _GstOggDemux
{
  GstElement element;

  GstPad *sinkpad;

  gint64 length;
  gint64 read_offset;
  gint64 offset;

  gboolean pullmode;
  gboolean running;

  gboolean need_chains;
  gboolean resync;

  /* state */
  GMutex *chain_lock;           /* we need the lock to protect the chains */
  GArray *chains;               /* list of chains we know */
  GstClockTime total_time;
  gint bitrate;                 /* bitrate of the current chain */

  GstOggChain *current_chain;
  GstOggChain *building_chain;

  /* playback start/stop positions */
  GstSegment segment;
  gboolean segment_running;
  guint32  seqnum;

  GstEvent *event;
  GstEvent *newsegment;         /* pending newsegment to be sent from _loop */

  /* annodex stuff */
  gint64 basetime;
  gint64 prestime;

  /* ogg stuff */
  ogg_sync_state sync;
};

struct _GstOggDemuxClass
{
  GstElementClass parent_class;
};

gboolean gst_ogg_demux_plugin_init (GstPlugin * plugin);

G_END_DECLS

#endif /* __GST_OGG_DEMUX_H__ */
