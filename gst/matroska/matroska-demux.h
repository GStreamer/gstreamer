/* GStreamer Matroska muxer/demuxer
 * (c) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * matroska-demux.h: matroska file/stream demuxer definition
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

#ifndef __GST_MATROSKA_DEMUX_H__
#define __GST_MATROSKA_DEMUX_H__

#include <gst/gst.h>
#include <gst/bytestream/bytestream.h>

#include "ebml-read.h"
#include "matroska-ids.h"

G_BEGIN_DECLS

#define GST_TYPE_MATROSKA_DEMUX \
  (gst_matroska_demux_get_type ())
#define GST_MATROSKA_DEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_MATROSKA_DEMUX, GstMatroskaDemux))
#define GST_MATROSKA_DEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_MATROSKA_DEMUX, GstMatroskaDemux))
#define GST_IS_MATROSKA_DEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_MATROSKA_DEMUX))
#define GST_IS_MATROSKA_DEMUX_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_MATROSKA_DEMUX))

#define GST_MATROSKA_DEMUX_MAX_STREAMS 64	

typedef enum {
  GST_MATROSKA_DEMUX_STATE_START,
  GST_MATROSKA_DEMUX_STATE_HEADER,
  GST_MATROSKA_DEMUX_STATE_DATA
} GstMatroskaDemuxState;

typedef struct _GstMatroskaDemuxIndex {
  guint64        pos;   /* of the corresponding *cluster*! */
  guint16        track; /* reference to 'num' */
  guint64        time;  /* in nanoseconds */
} GstMatroskaDemuxIndex;

typedef struct _GstMatroskaDemux {
  GstEbmlRead    parent;

  /* pads */
  GstPad 	*sinkpad;
  GstMatroskaTrackContext *src[GST_MATROSKA_DEMUX_MAX_STREAMS];
  guint          num_streams,
                 num_v_streams, num_a_streams, num_t_streams;
  GstClock	*clock;

  /* metadata */
  GstCaps       *metadata,
		*streaminfo;
  gchar         *muxing_app, *writing_app;
  gint64         created;

  /* state */
  GstMatroskaDemuxState state;
  guint          level_up;

  /* did we parse metadata/cues already? */
  gboolean       metadata_parsed,
		 index_parsed;

  /* start-of-segment */
  guint64        segment_start;

  /* a cue (index) table */
  GstMatroskaIndex *index;
  guint          num_indexes;

  /* timescale in the file */
  guint64        time_scale;

  /* length, position (time, ns) */
  guint64        duration,
		 pos;

  /* a possible pending seek */
  guint64        seek_pending;
} GstMatroskaDemux;

typedef struct _GstMatroskaDemuxClass {
  GstEbmlReadClass parent;
} GstMatroskaDemuxClass;

GType    gst_matroska_demux_get_type    (void);

gboolean gst_matroska_demux_plugin_init (GstPlugin *plugin);

G_END_DECLS

#endif /* __GST_MATROSKA_DEMUX_H__ */
