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


#include <config.h>
#include <gst/gst.h>
#include "gstmpegparse.h"


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define GST_TYPE_MPEG_DEMUX \
  (mpeg_demux_get_type())
#define GST_MPEG_DEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MPEG_DEMUX,GstMPEGDemux))
#define GST_MPEG_DEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MPEG_DEMUX,GstMPEGDemux))
#define GST_IS_MPEG_DEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MPEG_DEMUX))
#define GST_IS_MPEG_DEMUX_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MPEG_DEMUX))

typedef struct _GstMPEGDemux GstMPEGDemux;
typedef struct _GstMPEGDemuxClass GstMPEGDemuxClass;

typedef struct _GstMPEGStream GstMPEGStream;

struct _GstMPEGStream {
  gint8 	 STD_buffer_bound_scale;
  gint16 	 STD_buffer_size_bound;
  GstPad 	*pad;
  guint64	 pts;
  gint	 	 index_id;
  gint		 size_bound;
};

struct _GstMPEGDemux {
  GstMPEGParse	 parent;

  /* previous partial chunk and bytes remaining in it */
  gboolean 	 in_flush;

  /* program stream header values */
  guint16	 header_length;
  guint32	 rate_bound;
  guint8 	 audio_bound;
  gboolean 	 fixed;
  gboolean 	 constrained;
  gboolean 	 audio_lock;
  gboolean 	 video_lock;
  guint8 	 video_bound;
  gboolean 	 packet_rate_restriction;
  gint64	 total_size_bound;

#define NUM_PRIVATE_1_STREAMS 	 8
#define NUM_SUBTITLE_STREAMS 	16
#define NUM_VIDEO_STREAMS 	16
#define NUM_AUDIO_STREAMS 	32

  /* stream output */
  GstMPEGStream *private_1_stream[NUM_PRIVATE_1_STREAMS];	/* up to 8 ac3 audio tracks */
  GstMPEGStream *subtitle_stream[NUM_SUBTITLE_STREAMS];
  GstMPEGStream *private_2_stream;
  GstMPEGStream *video_stream[NUM_VIDEO_STREAMS];
  GstMPEGStream *audio_stream[NUM_AUDIO_STREAMS];

  GstIndex	*index;
};

struct _GstMPEGDemuxClass {
  GstMPEGParseClass parent_class;
};

GType gst_mpeg_demux_get_type(void);

gboolean 	gst_mpeg_demux_plugin_init 	(GModule *module, GstPlugin *plugin);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __MPEG_DEMUX_H__ */
