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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */


#ifndef __ASF_DEMUX_H__
#define __ASF_DEMUX_H__

#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include <gst/base/gstflowcombiner.h>

#include "asfheaders.h"

G_BEGIN_DECLS
  
#define GST_TYPE_ASF_DEMUX \
  (gst_asf_demux_get_type())
#define GST_ASF_DEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ASF_DEMUX,GstASFDemux))
#define GST_ASF_DEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ASF_DEMUX,GstASFDemuxClass))
#define GST_IS_ASF_DEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_ASF_DEMUX))
#define GST_IS_ASF_DEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_ASF_DEMUX))

GST_DEBUG_CATEGORY_EXTERN (asfdemux_dbg);
#define GST_CAT_DEFAULT asfdemux_dbg

typedef struct _GstASFDemux GstASFDemux;
typedef struct _GstASFDemuxClass GstASFDemuxClass;
typedef enum _GstASF3DMode GstASF3DMode;

typedef struct {
  guint32	packet;
  guint16	count;
} AsfSimpleIndexEntry;

typedef struct {
  AsfPayloadExtensionID   id : 16;  /* extension ID; the :16 makes sure the
                                     * struct gets packed into 4 bytes       */
  guint16                 len;      /* save this so we can skip unknown IDs  */
} AsfPayloadExtension;

/**
 * 3D Types for Media play
 */
enum _GstASF3DMode
{
  GST_ASF_3D_NONE = 0x00,

  //added, interim format - half
  GST_ASF_3D_SIDE_BY_SIDE_HALF_LR = 0x01,
  GST_ASF_3D_SIDE_BY_SIDE_HALF_RL = 0x02,
  GST_ASF_3D_TOP_AND_BOTTOM_HALF_LR = 0x03,
  GST_ASF_3D_TOP_AND_BOTTOM_HALF_RL = 0x04,
  GST_ASF_3D_DUAL_STREAM = 0x0D,                  /**< Full format*/
};

typedef struct
{
  gboolean        valid;               /* TRUE if structure is valid/filled */

  GstClockTime    start_time;
  GstClockTime    end_time;
  GstClockTime    avg_time_per_frame;
  guint32         data_bitrate;
  guint32         buffer_size;
  guint32         intial_buf_fullness;
  guint32         data_bitrate2;
  guint32         buffer_size2;
  guint32         intial_buf_fullness2;
  guint32         max_obj_size;
  guint32         flags;
  guint16         lang_idx;

  /* may be NULL if there are no extensions; otherwise, terminated by
   * an AsfPayloadExtension record with len 0 */
  AsfPayloadExtension  *payload_extensions;

  /* missing: stream names */
} AsfStreamExtProps;

typedef struct
{
  AsfStreamType      type;

  gboolean           active;  /* if the stream has been activated (pad added) */

  GstPad     *pad;
  guint16     id;

  /* video-only */
  gboolean    is_video;
  gboolean    fps_known;

  GstCaps    *caps;

  GstBuffer *streamheader;

  GstTagList *pending_tags;

  gboolean    discont;
  gboolean    first_buffer;

  /* Descrambler settings */
  guint8               span;
  guint16              ds_packet_size;
  guint16              ds_chunk_size;
  guint16              ds_data_size;

  /* for new parsing code */
  GArray         *payloads;  /* pending payloads */

  /* Video stream PAR & interlacing */
  guint8	par_x;
  guint8	par_y;
  gboolean      interlaced;

  /* For reverse playback */
  gboolean	reverse_kf_ready; /* Found complete KF payload*/
  GArray	*payloads_rev; /* Temp queue for storing multiple payloads of packet*/
  gint		kf_pos; /* KF position in payload queue. Payloads from this pos will be pushed */

  /* extended stream properties (optional) */
  AsfStreamExtProps  ext_props;
  
  gboolean     inspect_payload;
} AsfStream;

typedef enum {
  GST_ASF_DEMUX_STATE_HEADER,
  GST_ASF_DEMUX_STATE_DATA,
  GST_ASF_DEMUX_STATE_INDEX
} GstASFDemuxState;

#define GST_ASF_DEMUX_IS_REVERSE_PLAYBACK(seg) (seg.rate < 0.0? TRUE:FALSE)

#define GST_ASF_DEMUX_NUM_VIDEO_PADS   16
#define GST_ASF_DEMUX_NUM_AUDIO_PADS   32
#define GST_ASF_DEMUX_NUM_STREAMS      32
#define GST_ASF_DEMUX_NUM_STREAM_IDS  127

struct _GstASFDemux {
  GstElement 	     element;

  GstPad            *sinkpad;

  gboolean           have_group_id;
  guint              group_id;

  GstAdapter        *adapter;
  GstTagList        *taglist;
  GstASFDemuxState   state;

  /* byte offset where the asf starts, which might not be zero on chained
   * asfs, index_offset and data_offset already are 'offseted' by base_offset */
  guint64            base_offset;

  guint64            index_offset; /* byte offset where index might be, or 0   */
  guint64            data_offset;  /* byte offset where packets start          */
  guint64            data_size;    /* total size of packet data in bytes, or 0 */
  guint64            num_packets;  /* total number of data packets, or 0       */
  gint64             packet;       /* current packet                           */
  guint              speed_packets; /* Known number of packets to get in one go*/

  gchar              **languages;
  guint                num_languages;

  GstCaps             *metadata;         /* metadata, for delayed parsing; one
                                          * structure ('stream-N') per stream */
  GstStructure	      *global_metadata;  /* metadata which isn't specific to one stream */
  GSList              *ext_stream_props; /* for delayed processing (buffers) */
  GSList              *mut_ex_streams;   /* mutually exclusive streams */

  guint32              num_audio_streams;
  guint32              num_video_streams;
  guint32              num_streams;
  AsfStream            stream[GST_ASF_DEMUX_NUM_STREAMS];
  gboolean             activated_streams;
  GstFlowCombiner     *flowcombiner;

  /* for chained asf handling, we need to hold the old asf streams until
   * we detect the new ones */
  AsfStream            old_stream[GST_ASF_DEMUX_NUM_STREAMS];
  gboolean             old_num_streams;

  GstClockTime         first_ts;        /* smallest timestamp found        */

  guint32              packet_size;
  guint64              play_time;

  guint64              preroll;

  gboolean             seekable;
  gboolean             broadcast;

  GstSegment           segment;          /* configured play segment                 */
  gboolean             keyunit_sync;
  gboolean             accurate;

  gboolean             need_newsegment;  /* do we need to send a new-segment event? */
  guint32              segment_seqnum;   /* if the new segment must have this seqnum */
  GstClockTime         segment_ts;       /* streaming; timestamp for segment start */
  GstSegment           in_segment;       /* streaming; upstream segment info */
  GstClockTime         in_gap;           /* streaming; upstream initial segment gap for interpolation */
  gboolean             segment_running;  /* if we've started the current segment    */
  gboolean             streaming;        /* TRUE if we are operating chain-based    */
  GstClockTime         latency;

  /* for debugging only */
  gchar               *objpath;

  /* simple index, if available */
  GstClockTime         sidx_interval;    /* interval between entries in ns */
  guint                sidx_num_entries; /* number of index entries        */
  AsfSimpleIndexEntry *sidx_entries;     /* packet number for each entry   */
  
  GSList              *other_streams;    /* remember streams that are in header but have unknown type */

  /* For reverse playback */
  gboolean             seek_to_cur_pos; /* Search packets till we reach 'seek' time */
  gboolean             multiple_payloads; /* Whether packet has multiple payloads */

  /* parsing 3D */
  GstASF3DMode asf_3D_mode;
};

struct _GstASFDemuxClass {
  GstElementClass parent_class;
};

GType           gst_asf_demux_get_type (void);

AsfStream     * gst_asf_demux_get_stream (GstASFDemux * demux, guint16 id);

gboolean        gst_asf_demux_is_unknown_stream(GstASFDemux *demux, guint stream_num);

G_END_DECLS

#endif /* __ASF_DEMUX_H__ */
