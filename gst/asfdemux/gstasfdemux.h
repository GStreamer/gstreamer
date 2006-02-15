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


#ifndef __ASF_DEMUX_H__
#define __ASF_DEMUX_H__

#include <gst/gst.h>
#include <gst/base/gstadapter.h>

G_BEGIN_DECLS
  
#define GST_TYPE_ASF_DEMUX \
  (gst_asf_demux_get_type())
#define GST_ASF_DEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ASF_DEMUX,GstASFDemux))
#define GST_ASF_DEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ASF_DEMUX,GstASFDemux))
#define GST_IS_ASF_DEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_ASF_DEMUX))
#define GST_IS_ASF_DEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_ASF_DEMUX))

typedef struct _GstASFDemux GstASFDemux;
typedef struct _GstASFDemuxClass GstASFDemuxClass;

typedef struct
{
  GstPad     *pad;
  guint16     id;
  guint32     frag_offset;
  guint32     sequence;
  guint64     delay;
  guint64     last_pts;
  GstBuffer  *payload;

  gboolean    need_newsegment;  /* do we need to send a new-segment event? */

  /* video-only */
  guint64     last_buffer_timestamp;  /* timestamp of last buffer sent out */
  gboolean    is_video;
  gboolean    fps_known;
  GstBuffer  *cache;

  GstCaps    *caps;
} asf_stream_context;

typedef enum {
  GST_ASF_DEMUX_STATE_HEADER,
  GST_ASF_DEMUX_STATE_DATA,
  GST_ASF_DEMUX_STATE_EOS
} GstAsfDemuxState;

#define GST_ASF_DEMUX_NUM_VIDEO_PADS   16
#define GST_ASF_DEMUX_NUM_AUDIO_PADS   32
#define GST_ASF_DEMUX_NUM_STREAMS      32
#define GST_ASF_DEMUX_NUM_STREAM_IDS  127

struct _GstASFDemux {
  GstElement 	     element;

  GstPad 	    *sinkpad;

  GstAdapter        *adapter;
  GstTagList        *taglist;
  GstAsfDemuxState   state;
  
  /* The number of bytes needed for the next parsing unit. Set by
   * parsing functions when they return ASF_FLOW_NEED_MORE_DATA.
   * if not set after an ASF_FLOW_NEED_MORE_DATA, this indicates 
   * that we are parsing broken data and want to parse beyond an
   * object or packet boundary. */ 
  guint              bytes_needed;

  guint64            data_offset;  /* byte offset where packets start    */
  guint64            data_size;    /* total size of packet data in bytes */
  guint64            num_packets;  /* total number of data packets       */
  guint64            packet;       /* current packet                     */

  /* bitrates are unused at the moment */
  guint32              bitrate[GST_ASF_DEMUX_NUM_STREAM_IDS];

  guint32              num_audio_streams;
  guint32              num_video_streams;
  guint32              num_streams;
  asf_stream_context   stream[GST_ASF_DEMUX_NUM_STREAMS];

  guint32              packet_size; /* -1 if not fixed or not known */
  guint32              timestamp;   /* in milliseconds              */
  guint64              play_time;

  guint64              preroll;
  guint64              pts;

  /* expected byte offset of next buffer to be received by chain
   * function. Used to calculate the current byte offset into the
   * file from the adapter state and the data parser state */
  gint64               next_byte_offset;

  
  GstSegment           segment; /* configured play segment */

  /* Descrambler settings */
  guint8               span;
  guint16              ds_packet_size;
  guint16              ds_chunk_size;
  guint16              ds_data_size;

};

struct _GstASFDemuxClass {
  GstElementClass parent_class;
};

GType gst_asf_demux_get_type (void);

G_END_DECLS

#endif /* __ASF_DEMUX_H__ */
