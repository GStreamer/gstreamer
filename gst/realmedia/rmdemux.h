/* GStreamer RealMedia demuxer
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


#ifndef __GST_RMDEMUX_H__
#define __GST_RMDEMUX_H__

#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include <gst/base/gstflowcombiner.h>
#include <gst/pbutils/descriptions.h>

G_BEGIN_DECLS

#define GST_TYPE_RMDEMUX \
  (gst_rmdemux_get_type())
#define GST_RMDEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_RMDEMUX,GstRMDemux))
#define GST_RMDEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_RMDEMUX,GstRMDemuxClass))
#define GST_IS_RMDEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_RMDEMUX))
#define GST_IS_RMDEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_RMDEMUX))

typedef enum
{
  RMDEMUX_STATE_NULL,
  RMDEMUX_STATE_HEADER,
  RMDEMUX_STATE_HEADER_UNKNOWN,
  RMDEMUX_STATE_HEADER_RMF,
  RMDEMUX_STATE_HEADER_PROP,
  RMDEMUX_STATE_HEADER_MDPR,
  RMDEMUX_STATE_HEADER_INDX,
  RMDEMUX_STATE_HEADER_DATA,
  RMDEMUX_STATE_HEADER_CONT,
  RMDEMUX_STATE_HEADER_SEEKING,
  RMDEMUX_STATE_SEEKING,
  RMDEMUX_STATE_DATA_PACKET,
  RMDEMUX_STATE_SEEKING_EOS,
  RMDEMUX_STATE_EOS,
  RMDEMUX_STATE_INDX_DATA
} GstRMDemuxState;

typedef enum
{
  RMDEMUX_LOOP_STATE_HEADER,
  RMDEMUX_LOOP_STATE_INDEX,
  RMDEMUX_LOOP_STATE_DATA
} GstRMDemuxLoopState;

typedef enum
{
  GST_RMDEMUX_STREAM_UNKNOWN,
  GST_RMDEMUX_STREAM_VIDEO,
  GST_RMDEMUX_STREAM_AUDIO,
  GST_RMDEMUX_STREAM_FILEINFO
} GstRMDemuxStreamType;

typedef struct _GstRMDemux GstRMDemux;
typedef struct _GstRMDemuxClass GstRMDemuxClass;
typedef struct _GstRMDemuxStream GstRMDemuxStream;

struct _GstRMDemux {
  GstElement element;

  /* pads */
  GstPad *sinkpad;

  gboolean have_group_id;
  guint group_id;

  GSList *streams;
  guint n_video_streams;
  guint n_audio_streams;
  GstAdapter *adapter;
  gboolean have_pads;

  GstFlowCombiner *flowcombiner;

  guint32 timescale;
  guint64 duration;
  guint32 avg_packet_size;
  guint32 index_offset;
  guint32 data_offset;
  guint32 num_packets;

  guint offset;
  gboolean seekable;

  GstRMDemuxState state;
  GstRMDemuxLoopState loop_state;
  GstRMDemuxStream *index_stream;

  /* playback start/stop positions */
  GstSegment segment;
  gboolean segment_running;
  gboolean running;

  /* Whether we need to send a newsegment event */
  gboolean need_newsegment;

  /* Current timestamp */
  GstClockTime cur_timestamp;

  /* First timestamp */
  GstClockTime base_ts;
  GstClockTime first_ts;

  int n_chunks;
  int chunk_index;

  guint32 object_id;
  guint32 size;
  guint16 object_version;

  /* container tags for all streams */
  GstTagList *pending_tags;
};

struct _GstRMDemuxClass {
  GstElementClass parent_class;
};

/* RealMedia VideoCodec FOURCC codes */
#define GST_RM_VDO_RV10 GST_MAKE_FOURCC('R','V','1','0') // RealVideo 1
#define GST_RM_VDO_RV20 GST_MAKE_FOURCC('R','V','2','0') // RealVideo G2
#define GST_RM_VDO_RV30 GST_MAKE_FOURCC('R','V','3','0') // RealVideo 8
#define GST_RM_VDO_RV40 GST_MAKE_FOURCC('R','V','4','0') // RealVideo 9+10

/* RealMedia AudioCodec FOURCC codes */
#define GST_RM_AUD_14_4 GST_MAKE_FOURCC('1','4','_','4') // 14.4 Audio Codec
#define GST_RM_AUD_28_8 GST_MAKE_FOURCC('2','8','_','8') // 28.8 Audio Codec
#define GST_RM_AUD_COOK GST_MAKE_FOURCC('c','o','o','k') // Cooker G2 Audio Codec
#define GST_RM_AUD_DNET GST_MAKE_FOURCC('d','n','e','t') // DolbyNet Audio Codec (low bitrate Dolby AC3)
#define GST_RM_AUD_SIPR GST_MAKE_FOURCC('s','i','p','r') // Sipro/ACELP.NET Voice Codec
#define GST_RM_AUD_RAAC GST_MAKE_FOURCC('r','a','a','c') // LE-AAC Audio Codec
#define GST_RM_AUD_RACP GST_MAKE_FOURCC('r','a','c','p') // HE-AAC Audio Codec
#define GST_RM_AUD_RALF GST_MAKE_FOURCC('r','a','l','f') // RealAudio Lossless
#define GST_RM_AUD_ATRC GST_MAKE_FOURCC('a','t','r','c') // Sony ATRAC3 Audio Codec

#define GST_RM_AUD_xRA4 GST_MAKE_FOURCC('.','r','a','4') // Not a real audio codec
#define GST_RM_AUD_xRA5 GST_MAKE_FOURCC('.','r','a','5') // Not a real audio codec

gboolean gst_rmdemux_plugin_init (GstPlugin * plugin);

G_END_DECLS

#endif /* __GST_RMDEMUX_H__ */
