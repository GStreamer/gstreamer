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
#include <gst/bytestream/bytestream.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */
  
  
#define GST_TYPE_ASF_DEMUX \
  (asf_demux_get_type())
#define GST_ASF_DEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ASF_DEMUX,GstASFDemux))
#define GST_ASF_DEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ASF_DEMUX,GstASFDemux))
#define GST_IS_ASF_DEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_ASF_DEMUX))
#define GST_IS_ASF_DEMUX_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_ASF_DEMUX))

typedef struct _GstASFDemux GstASFDemux;
typedef struct _GstASFDemuxClass GstASFDemuxClass;

typedef struct {
  guint32 v1;
  guint32 v2;
  guint32 v3;
  guint32 v4;
} GstASFGuid;
  


typedef struct {
  guint8     obj_id;
  GstASFGuid guid; 
} GstASFGuidHash;

struct _GstASFStreamContext {
  GstPad *pad;
  guint64 pts;
};


typedef struct
{
  GstPad  *pad;
  guint32 num;
  guint32 frag_offset;
  guint32 sequence;
} asf_stream_context;

struct _GstASFDemux {
  GstElement 	 element;

  /* pads */
  GstPad 	*sinkpad;

  GstByteStream *bs;

#define GST_ASF_DEMUX_NUM_VIDEO_PADS 16
#define GST_ASF_DEMUX_NUM_AUDIO_PADS 32
#define GST_ASF_DEMUX_NUM_STREAMS 48

  /* stream output pads */
  GstPad *video_pad[GST_ASF_DEMUX_NUM_VIDEO_PADS];
  gint64 video_PTS[GST_ASF_DEMUX_NUM_VIDEO_PADS];

  GstPad *audio_pad[GST_ASF_DEMUX_NUM_AUDIO_PADS];
  gint64 audio_PTS[GST_ASF_DEMUX_NUM_AUDIO_PADS];

  gboolean restart;

  /* Demuxing state */
  guint32 num_audio_streams;
  guint32 num_video_streams;
  guint32 num_streams;
  asf_stream_context stream[GST_ASF_DEMUX_NUM_STREAMS];

  guint32 packet_size;
  guint32 timestamp;
  guint32 play_time;
};

struct _GstASFDemuxClass {
  GstElementClass parent_class;
};

GType gst_asf_demux_get_type(void);

gboolean 	gst_asf_demux_plugin_init 	(GModule *module, GstPlugin *plugin);




#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __ASF_DEMUX_H__ */
