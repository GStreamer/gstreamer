/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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


#ifndef __MPEG_DEMUX_H__
#define __MPEG_DEMUX_H__


#include <gst/gst.h>
#include "gstmpegparse.h"

G_BEGIN_DECLS

#define GST_TYPE_MPEG_DEMUX \
  (gst_mpeg_demux_get_type())
#define GST_MPEG_DEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MPEG_DEMUX,GstMPEGDemux))
#define GST_MPEG_DEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MPEG_DEMUX,GstMPEGDemuxClass))
#define GST_IS_MPEG_DEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MPEG_DEMUX))
#define GST_IS_MPEG_DEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MPEG_DEMUX))

/* Supported kinds of streams. */
enum {
  GST_MPEG_DEMUX_STREAM_VIDEO = 1,
  GST_MPEG_DEMUX_STREAM_AUDIO,
  GST_MPEG_DEMUX_STREAM_PRIVATE,
  GST_MPEG_DEMUX_STREAM_LAST
};

/* Supported number of streams. */
#define GST_MPEG_DEMUX_NUM_VIDEO_STREAMS        16
#define GST_MPEG_DEMUX_NUM_AUDIO_STREAMS        32
#define GST_MPEG_DEMUX_NUM_PRIVATE_STREAMS      2

/* How to make stream type values. */
#define GST_MPEG_DEMUX_STREAM_TYPE(kind, serial) \
  (((kind) << 16) + (serial))

/* How to retrieve the stream kind back from a type. */
#define GST_MPEG_DEMUX_STREAM_KIND(type) ((type) >> 16)

/* The recognized video types. */
enum {
  GST_MPEG_DEMUX_VIDEO_UNKNOWN =
    GST_MPEG_DEMUX_STREAM_TYPE (GST_MPEG_DEMUX_STREAM_VIDEO, 1),
  GST_MPEG_DEMUX_VIDEO_MPEG,
  GST_MPEG_DEMUX_VIDEO_LAST
};

/* The recognized audio types. */
enum {
  GST_MPEG_DEMUX_AUDIO_UNKNOWN =
    GST_MPEG_DEMUX_STREAM_TYPE (GST_MPEG_DEMUX_STREAM_AUDIO, 1),
  GST_MPEG_DEMUX_AUDIO_MPEG,
  GST_MPEG_DEMUX_AUDIO_LAST
};

/* The recognized private stream types. */
enum {
  GST_MPEG_DEMUX_PRIVATE_UNKNOWN =
    GST_MPEG_DEMUX_STREAM_TYPE (GST_MPEG_DEMUX_STREAM_PRIVATE, 1),
  GST_MPEG_DEMUX_PRIVATE_LAST
};

typedef struct _GstMPEGStream GstMPEGStream;
typedef struct _GstMPEGVideoStream GstMPEGVideoStream;
typedef struct _GstMPEGDemux GstMPEGDemux;
typedef struct _GstMPEGDemuxClass GstMPEGDemuxClass;

/* Information associated to a single MPEG stream. */
struct _GstMPEGStream {
  gint              type;
  gint              number;
  GstPad           *pad;
  GstCaps          *caps;
  gint              index_id;
  gint              size_bound;
  GstClockTime      cur_ts;
  GstClockTimeDiff  scr_offs;
  GstFlowReturn     last_flow;
  guint             buffers_sent;
  GstTagList       *tags;
};

/* Extended structure to hold additional information for video
   streams. */
struct _GstMPEGVideoStream {
  GstMPEGStream parent;
  gint          mpeg_version;
};

struct _GstMPEGDemux {
  GstMPEGParse   parent;

  /* previous partial chunk and bytes remaining in it */
  gboolean       in_flush;

  /* program stream header values */
  guint16        header_length;
  guint32        rate_bound;
  guint8         audio_bound;
  gboolean       fixed;
  gboolean       constrained;
  gboolean       audio_lock;
  gboolean       video_lock;
  guint8         video_bound;
  gboolean       packet_rate_restriction;
  gint64         total_size_bound;

  gint64         last_pts;
  gboolean       pending_tags;
  GstIndex      *index;

  /* stream output */
  GstMPEGStream *video_stream[GST_MPEG_DEMUX_NUM_VIDEO_STREAMS];
  GstMPEGStream *audio_stream[GST_MPEG_DEMUX_NUM_AUDIO_STREAMS];
  GstMPEGStream *private_stream[GST_MPEG_DEMUX_NUM_PRIVATE_STREAMS];

  GstClockTime max_gap;         /* Maximum timestamp difference to
                                   allow between pads before using a
                                   filler to catch up. */
  GstClockTime max_gap_tolerance;
                                /* When catching a pad up, how far
                                   behind to make it. */

  GstClockTime max_ts;          /* Highest timestamp of all pads. */
  GstPad      *max_pad;         /* Pad with highest timestamp. */
};

struct _GstMPEGDemuxClass {
  GstMPEGParseClass parent_class;

  GstPadTemplate *video_template;
  GstPadTemplate *audio_template;
  GstPadTemplate *private_template;

  GstPad *      (*new_output_pad)       (GstMPEGDemux *mpeg_demux,
                                         const gchar *name,
                                         GstPadTemplate *temp);
  void          (*init_stream)          (GstMPEGDemux *mpeg_demux,
                                         gint type,
                                         GstMPEGStream *str,
                                         gint number,
                                         const gchar *name,
                                         GstPadTemplate *temp);

  GstMPEGStream *
                (*get_video_stream)     (GstMPEGDemux *mpeg_demux,
                                         guint8 stream_nr,
                                         gint type,
                                         const gpointer info);
  GstMPEGStream *
                (*get_audio_stream)     (GstMPEGDemux *mpeg_demux,
                                         guint8 stream_nr,
                                         gint type,
                                         const gpointer info);
  GstMPEGStream *
                (*get_private_stream)   (GstMPEGDemux *mpeg_demux,
                                         guint8 stream_nr,
                                         gint type,
                                         const gpointer info);

  GstFlowReturn (*send_subbuffer)        (GstMPEGDemux *mpeg_demux,
                                          GstMPEGStream *outstream,
                                          GstBuffer *buffer,
                                          GstClockTime timestamp,
                                          guint offset,
                                          guint size);


  GstFlowReturn (*combine_flows)        (GstMPEGDemux  *mpeg_demux,
                                         GstMPEGStream *stream,
                                         GstFlowReturn  flow);

  GstFlowReturn (*process_private)      (GstMPEGDemux *mpeg_demux,
                                         GstBuffer *buffer,
                                         guint stream_nr,
                                         GstClockTime timestamp,
                                         guint headerlen, guint datalen);

  void          (*synchronise_pads)    (GstMPEGDemux *mpeg_demux,
                                        GstClockTime threshold,
                                        GstClockTime new_ts);

  void          (*sync_stream_to_time) (GstMPEGDemux *mpeg_demux,
                                        GstMPEGStream *stream,
                                        GstClockTime last_ts);
};

void            gst_mpeg_streams_reset_last_flow (GstMPEGStream *streams[],
                                                  guint          num);
void            gst_mpeg_streams_reset_cur_ts    (GstMPEGStream *streams[],
                                                  guint          num,
                                                  GstClockTime   cur_ts);

GType           gst_mpeg_demux_get_type          (void);

gboolean        gst_mpeg_demux_plugin_init       (GstPlugin *plugin);

G_END_DECLS

#endif /* __MPEG_DEMUX_H__ */
