/* Gnome-Streamer
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


#ifndef __GST_AVI_DEMUX_H__
#define __GST_AVI_DEMUX_H__


#include <config.h>
#include <gst/gst.h>
#include <gst/riff/riff.h>
#include <gst/bytestream/bytestream.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define GST_TYPE_AVI_DEMUX \
  (gst_avi_demux_get_type())
#define GST_AVI_DEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AVI_DEMUX,GstAviDemux))
#define GST_AVI_DEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AVI_DEMUX,GstAviDemux))
#define GST_IS_AVI_DEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AVI_DEMUX))
#define GST_IS_AVI_DEMUX_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AVI_DEMUX))


#define GST_AVI_DEMUX_UNKNOWN	  	0	/* initialized state */
#define GST_AVI_DEMUX_REGULAR	  	1	/* regular parsing */
#define GST_AVI_DEMUX_HDRL    		2
#define GST_AVI_DEMUX_STRL    		3
#define GST_AVI_DEMUX_MOVI    		4
#define GST_AVI_DEMUX_AVIH    		5
#define GST_AVI_DEMUX_STRH_VIDS		6
#define GST_AVI_DEMUX_STRH_AUDS		7
#define GST_AVI_DEMUX_STRH_IAVS		8

#define GST_AVI_DEMUX_MAX_AUDIO_PADS	8	
#define GST_AVI_DEMUX_MAX_VIDEO_PADS	8	

typedef struct _GstAviDemux GstAviDemux;
typedef struct _GstAviDemuxClass GstAviDemuxClass;

struct _GstAviDemux {
  GstElement element;

  /* pads */
  GstPad *sinkpad,*srcpad;

  /* AVI decoding state */
  gint state;
  guint32 fcc_type;

  GstByteStream *bs;

  gst_riff_index_entry *index_entries;
  gulong index_size;
  gulong index_offset;
  gulong resync_offset;

  guint64 next_time;
  guint64 time_interval;
  gulong tot_frames;
  gulong current_frame;

  guint32 flags;
  guint32 init_audio;
  guint32 audio_rate;

  guint num_audio_pads;
  guint num_video_pads;
  guint num_iavs_pads;
  GstPad       *audio_pad[GST_AVI_DEMUX_MAX_AUDIO_PADS];
  gboolean 	audio_need_flush[GST_AVI_DEMUX_MAX_AUDIO_PADS];

  GstPad       *video_pad[GST_AVI_DEMUX_MAX_VIDEO_PADS];
  gboolean 	video_need_flush[GST_AVI_DEMUX_MAX_VIDEO_PADS];

  gpointer extra_data;
};

struct _GstAviDemuxClass {
  GstElementClass parent_class;
};

GType 		gst_avi_demux_get_type		(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_AVI_DEMUX_H__ */
