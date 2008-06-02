/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2006> Nokia Corporation (contact <stefan.kost@nokia.com>)
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

#include "avi-ids.h"
#include "gst/riff/riff-ids.h"
#include "gst/riff/riff-read.h"
#include <gst/base/gstadapter.h>

G_BEGIN_DECLS

#define GST_TYPE_AVI_DEMUX \
  (gst_avi_demux_get_type ())
#define GST_AVI_DEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_AVI_DEMUX, GstAviDemux))
#define GST_AVI_DEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_AVI_DEMUX, GstAviDemuxClass))
#define GST_IS_AVI_DEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_AVI_DEMUX))
#define GST_IS_AVI_DEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_AVI_DEMUX))

#define GST_AVI_DEMUX_MAX_STREAMS       16

#define CHUNKID_TO_STREAMNR(chunkid) \
  ((((chunkid) & 0xff) - '0') * 10 + \
   (((chunkid) >> 8) & 0xff) - '0')

#define GST_AVI_INDEX_ENTRY_FLAG_KEYFRAME 1

/* 48 bytes */
typedef struct {
  guint          index_nr;      /* = (entry-index_entries)/sizeof(gst_avi_index_entry); */
  guchar         stream_nr;
  guchar         flags;
  guint64        ts;
  guint64        dur;           /* =entry[1].ts-entry->ts */
  guint64        offset;
  guint64        bytes_before;  /* calculated */
  guint32        frames_before; /* calculated */
  guint32        size;          /* could be read from the chunk (if we don't split) */
} gst_avi_index_entry;

typedef struct {
  /* index of this streamcontext */
  guint          num;

  /* pad*/
  GstPad        *pad;

  /* stream info and headers */
  gst_riff_strh *strh;
  union {
    gst_riff_strf_vids *vids;
    gst_riff_strf_auds *auds;
    gst_riff_strf_iavs *iavs;
    gpointer     data;
  } strf;
  GstBuffer     *extradata, *initdata;
  gchar         *name;

  /* current position (byte, frame, time) and other status vars */
  guint          current_frame;
  guint64        current_byte;
  GstFlowReturn  last_flow;
  gboolean       discont;

  /* stream length */
  guint64        total_bytes;
  guint32        total_frames;
  /* stream length according to index */
  GstClockTime   idx_duration;
  /* stream length according to header */
  GstClockTime   hdr_duration;
  /* stream length based on header/index */
  GstClockTime   duration;

  /* VBR indicator */
  gboolean       is_vbr;

  /* openDML support (for files >4GB) */
  gboolean       superindex;
  guint64       *indexes;

  GstTagList	*taglist;
} avi_stream_context;

typedef enum {
  GST_AVI_DEMUX_START,
  GST_AVI_DEMUX_HEADER,
  GST_AVI_DEMUX_MOVI,
} GstAviDemuxState;

typedef enum {
  GST_AVI_DEMUX_HEADER_TAG_LIST,
  GST_AVI_DEMUX_HEADER_AVIH,
  GST_AVI_DEMUX_HEADER_ELEMENTS,
  GST_AVI_DEMUX_HEADER_INFO,
  GST_AVI_DEMUX_HEADER_JUNK,
  GST_AVI_DEMUX_HEADER_DATA
} GstAviDemuxHeaderState;

typedef struct _GstAviDemux {
  GstElement     parent;

  /* pads */
  GstPad        *sinkpad;

  /* AVI decoding state */
  GstAviDemuxState state;
  GstAviDemuxHeaderState header_state;
  guint64        offset;

  /* index */
  gst_avi_index_entry *index_entries;
  guint          index_size;
  guint64        index_offset;
  guint          current_entry;
  guint          reverse_start_index;
  guint          reverse_stop_index;

  /* streams */
  guint          num_streams;
  guint          num_v_streams;
  guint          num_a_streams;
  guint          num_t_streams;  /* subtitle text streams */

  avi_stream_context stream[GST_AVI_DEMUX_MAX_STREAMS];

  /* for streaming mode */
  gboolean      streaming;
  gboolean      have_eos;
  GstAdapter    *adapter;

  /* some stream info for length */
  gst_riff_avih *avih;

  /* segment in TIME */
  GstSegment     segment;
  gboolean       segment_running;

  /* pending tags/events */
  GstEvent      *seek_event;
  GstTagList	*globaltags;
  gboolean	got_tags;

} GstAviDemux;

typedef struct _GstAviDemuxClass {
  GstElementClass parent_class;
} GstAviDemuxClass;

GType           gst_avi_demux_get_type          (void);

G_END_DECLS

#endif /* __GST_AVI_DEMUX_H__ */
