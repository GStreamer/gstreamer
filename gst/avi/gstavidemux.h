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


#define GST_AVI_DEMUX_MAX_STREAMS	16	

#define CHUNKID_TO_STREAMNR(chunkid) \
  (((GUINT32_FROM_BE (chunkid) >> 24) - '0') * 10 + \
   ((GUINT32_FROM_BE (chunkid) >> 16) & 0xff) - '0')

typedef struct _GstAviDemux GstAviDemux;
typedef struct _GstAviDemuxClass GstAviDemuxClass;

typedef struct
{
  gint 		index_nr;
  gint 		stream_nr;
  guint64 	ts;
  guint32	flags;
  guint32	offset;
  gint 		size;
  guint64	bytes_before;
  guint32	frames_before;
} gst_avi_index_entry;

typedef struct
{
  GstPad       *pad;
  gint 		num;
  gst_riff_strh strh;
  guint64 	next_ts;
  guint32 	current_frame;
  guint32 	current_byte;
  guint64 	delay;
  gboolean 	need_flush;

  guint64	total_bytes;
  gint32	total_frames;

  guint32	skip;

} avi_stream_context;

struct _GstAviDemux {
  GstElement 	 element;

  /* pads */
  GstPad 	*sinkpad, *srcpad;

  /* AVI decoding state */
  guint32 	 fcc_type;

  GstByteStream *bs;

  gst_avi_index_entry *index_entries;
  gulong 	 index_size;
  gulong 	 index_offset;

  gst_riff_avih  avih;

  guint 	 num_streams;
  guint 	 num_v_streams;
  guint 	 num_a_streams;

  avi_stream_context stream[GST_AVI_DEMUX_MAX_STREAMS];

  gboolean 	 seek_pending;
  gint64 	 seek_offset;
  guint64 	 last_seek;
};

struct _GstAviDemuxClass {
  GstElementClass parent_class;
};

GType 		gst_avi_demux_get_type		(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_AVI_DEMUX_H__ */
