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

#include <gst/gst.h>

#include "gst/riff/riff-ids.h"
#include "gst/riff/riff-read.h"

G_BEGIN_DECLS

#define GST_TYPE_AVI_DEMUX \
  (gst_avi_demux_get_type ())
#define GST_AVI_DEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_AVI_DEMUX, GstAviDemux))
#define GST_AVI_DEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_AVI_DEMUX, GstAviDemux))
#define GST_IS_AVI_DEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_AVI_DEMUX))
#define GST_IS_AVI_DEMUX_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_AVI_DEMUX))

#define GST_AVI_DEMUX_MAX_STREAMS	16	

#define CHUNKID_TO_STREAMNR(chunkid) \
  ((((chunkid) & 0xff) - '0') * 10 + \
   (((chunkid) >> 8) & 0xff) - '0')

typedef struct {
  gint 		 index_nr;
  gint 		 stream_nr;
  guint64 	 ts;
  guint32	 flags;
  guint32	 offset;
  gint 		 size;
  guint64	 bytes_before;
  guint32	 frames_before;
} gst_avi_index_entry;

typedef struct {
  /* index of this streamcontext */
  guint 	 num;

  /* pad, strh */
  GstPad	*pad;
  gst_riff_strh	*strh;

  /* current position (byte, frame, time) */
  guint 	 current_frame;
  guint64 	 current_byte;
  gint		 current_entry;

  /* delay in time (init_frames) */
  guint64 	 delay;

  /* stream length */
  guint64	 total_bytes;
  guint32	 total_frames;

  guint32	 skip;
} avi_stream_context;

typedef enum {
  GST_AVI_DEMUX_START,
  GST_AVI_DEMUX_HEADER,
  GST_AVI_DEMUX_MOVI,
} GstAviDemuxState;

typedef struct _GstAviDemux {
  GstRiffRead	 parent;

  /* pads */
  GstPad 	*sinkpad;

  /* AVI decoding state */
  GstAviDemuxState state;
  guint          level_up;

  /* index */
  gst_avi_index_entry *index_entries;
  guint 	 index_size;
  guint64	 index_offset;

  /* streams */
  guint 	 num_streams;
  guint 	 num_v_streams;
  guint 	 num_a_streams;
  avi_stream_context stream[GST_AVI_DEMUX_MAX_STREAMS];

  /* some stream info for length */
  guint32	 us_per_frame;
  guint32	 num_frames;

  /* seeking */
  guint64 	 seek_offset;
  guint64	 last_seek;

  /* info */
  GstCaps	*streaminfo;
} GstAviDemux;

typedef struct _GstAviDemuxClass {
  GstRiffReadClass parent_class;
} GstAviDemuxClass;

GType 		gst_avi_demux_get_type		(void);

G_END_DECLS

#endif /* __GST_AVI_DEMUX_H__ */
