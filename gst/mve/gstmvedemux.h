/*
 * GStreamer demultiplexer plugin for Interplay MVE movie files
 *
 * Copyright (C) 2006 Jens Granseuer <jensgr@gmx.net>
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

#ifndef __GST_MVE_DEMUX_H__
#define __GST_MVE_DEMUX_H__

#include <gst/gst.h>
#include <gst/base/gstadapter.h>

G_BEGIN_DECLS

#define GST_TYPE_MVE_DEMUX \
  (gst_mve_demux_get_type())
#define GST_MVE_DEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MVE_DEMUX,GstMveDemux))
#define GST_MVE_DEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MVE_DEMUX,GstMveDemuxClass))
#define GST_IS_MVE_DEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MVE_DEMUX))
#define GST_IS_MVE_DEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MVE_DEMUX))

typedef struct _GstMveDemux       GstMveDemux;
typedef struct _GstMveDemuxClass  GstMveDemuxClass;
typedef struct _GstMveDemuxStream GstMveDemuxStream;

struct _GstMveDemux
{
  GstElement element;

  GstPad *sinkpad;

  GstMveDemuxStream *video_stream;
  GstMveDemuxStream *audio_stream;

  gint state;

  /* time per frame (1/framerate) */
  GstClockTime frame_duration;

  /* push based variables */
  guint16 needed_bytes;
  GstAdapter *adapter;
  
  /* size of current chunk */
  guint32 chunk_size;
  /* offset in current chunk */
  guint32 chunk_offset;
};

struct _GstMveDemuxClass 
{
  GstElementClass parent_class;
};

struct _GstMveDemuxStream {
  /* shared properties */
  GstCaps *caps;
  GstPad *pad;
  GstClockTime last_ts;
  gint64 offset;

  GstFlowReturn last_flow;

  /* video properties */
  guint16 width;
  guint16 height;
  guint8 bpp;   /* bytes per pixel */
  guint8 *code_map;
  gboolean code_map_avail;
  guint8 *back_buf1;
  guint8 *back_buf2;
  guint32 max_block_offset;
  GstBuffer *palette;
  GstBuffer *buffer;

  /* audio properties */
  guint16 sample_rate;
  guint16 n_channels;
  guint16 sample_size;
  gboolean compression;
};

GType gst_mve_demux_get_type (void);

int ipvideo_decode_frame8 (const GstMveDemuxStream * s,
    const unsigned char *data, unsigned short len);
int ipvideo_decode_frame16 (const GstMveDemuxStream * s,
    const unsigned char *data, unsigned short len);

void ipaudio_uncompress (short *buffer,
    unsigned short buf_len, const unsigned char *data, unsigned char channels);

G_END_DECLS

#endif /* __GST_MVE_DEMUX_H__ */
