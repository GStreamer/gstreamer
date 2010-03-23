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
#include <gst/base/gstadapter.h>

#include "ebml-read.h"
#include "matroska-ids.h"

G_BEGIN_DECLS

#define GST_TYPE_MATROSKA_DEMUX \
  (gst_matroska_demux_get_type ())
#define GST_MATROSKA_DEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_MATROSKA_DEMUX, GstMatroskaDemux))
#define GST_MATROSKA_DEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_MATROSKA_DEMUX, GstMatroskaDemuxClass))
#define GST_IS_MATROSKA_DEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_MATROSKA_DEMUX))
#define GST_IS_MATROSKA_DEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_MATROSKA_DEMUX))

typedef enum {
  GST_MATROSKA_DEMUX_STATE_START,
  GST_MATROSKA_DEMUX_STATE_HEADER,
  GST_MATROSKA_DEMUX_STATE_DATA
} GstMatroskaDemuxState;

typedef struct _GstMatroskaDemux {
  GstEbmlRead              parent;

  /* < private > */

  GstIndex                *element_index;
  gint                     element_index_writer_id;

  /* pads */
  GstPad                  *sinkpad;
  GPtrArray               *src;
  GstClock                *clock;
  guint                    num_streams;
  guint                    num_v_streams;
  guint                    num_a_streams;
  guint                    num_t_streams;

  /* metadata */
  gchar                   *muxing_app;
  gchar                   *writing_app;
  gint64                   created;

  /* state */
  GstMatroskaDemuxState    state;
  guint                    level_up;
  guint64                  seek_block;

  /* did we parse cues/tracks/segmentinfo already? */
  gboolean                 index_parsed;
  gboolean                 tracks_parsed;
  gboolean                 segmentinfo_parsed;
  gboolean                 attachments_parsed;
  GList                   *tags_parsed;

  /* start-of-segment */
  guint64                  ebml_segment_start;

  /* a cue (index) table */
  GArray                  *index;

  /* timescale in the file */
  guint64                  time_scale;

  /* keeping track of playback position */
  GstSegment               segment;
  gboolean                 segment_running;
  GstClockTime             last_stop_end;

  GstEvent                *close_segment;
  GstEvent                *new_segment;
  GstTagList              *global_tags;

  /* push based mode usual suspects */
  guint64                  offset;
  GstAdapter              *adapter;
  /* some state saving */
  GstClockTime             cluster_time;
  guint64                  cluster_offset;

  /* reverse playback */
  GArray                  *seek_index;
  gint                     seek_entry;
  gint64                   from_offset;
  gint64                   to_offset;
} GstMatroskaDemux;

typedef struct _GstMatroskaDemuxClass {
  GstEbmlReadClass parent;
} GstMatroskaDemuxClass;

gboolean gst_matroska_demux_plugin_init (GstPlugin *plugin);

G_END_DECLS

#endif /* __GST_MATROSKA_DEMUX_H__ */
