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


#ifndef __GST_RMDEMUX_H__
#define __GST_RMDEMUX_H__

#include <gst/gst.h>
#include <gst/base/gstadapter.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define GST_TYPE_RMDEMUX \
  (gst_rmdemux_get_type())
#define GST_RMDEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_RMDEMUX,GstRMDemux))
#define GST_RMDEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_RMDEMUX,GstRMDemux))
#define GST_IS_RMDEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_RMDEMUX))
#define GST_IS_RMDEMUX_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_RMDEMUX))

#define GST_RMDEMUX_MAX_STREAMS		8

typedef struct _GstRMDemux GstRMDemux;
typedef struct _GstRMDemuxClass GstRMDemuxClass;
typedef struct _GstRMDemuxStream GstRMDemuxStream;

struct _GstRMDemux {
  GstElement element;

  /* pads */
  GstPad *sinkpad;

  GstRMDemuxStream *streams[GST_RMDEMUX_MAX_STREAMS];
  int n_streams;
  int n_video_streams;
  int n_audio_streams;

  GstAdapter *adapter;

  GNode *moov_node;
  GNode *moov_node_compressed;

  guint32 timescale;
  guint32 duration;

  int state;

  int offset;
  int data_offset;

  int n_chunks;
  int chunk_index;

  guint64 length;

  guint32 object_id;
  guint32 size;
  guint16 object_version;
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

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __GST_RMDEMUX_H__ */
