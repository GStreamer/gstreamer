/* GStreamer Matroska muxer/demuxer
 * (c) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * matroska-mux.h: matroska file/stream muxer object types
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

#ifndef __GST_MATROSKA_MUX_H__
#define __GST_MATROSKA_MUX_H__

#include <gst/gst.h>

#include "ebml-write.h"
#include "matroska-ids.h"

G_BEGIN_DECLS

#define GST_TYPE_MATROSKA_MUX \
  (gst_matroska_mux_get_type ())
#define GST_MATROSKA_MUX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_MATROSKA_MUX, GstMatroskaMux))
#define GST_MATROSKA_MUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_MATROSKA_MUX, GstMatroskaMux))
#define GST_IS_MATROSKA_MUX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_MATROSKA_MUX))
#define GST_IS_MATROSKA_MUX_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_MATROSKA_MUX))

#define GST_MATROSKA_MUX_MAX_STREAMS 64

typedef enum {
  GST_MATROSKA_MUX_STATE_START,
  GST_MATROSKA_MUX_STATE_HEADER,
  GST_MATROSKA_MUX_STATE_DATA,
} GstMatroskaMuxState;

typedef struct _GstMatroskaMux {
  GstEbmlWrite   parent;

  /* pads */
  GstPad 	*srcpad;
  struct {
    GstMatroskaTrackContext *track;
    GstBuffer   *buffer;
    gboolean     eos;
  } sink[GST_MATROSKA_MUX_MAX_STREAMS];
  guint          num_streams,
                 num_v_streams, num_a_streams, num_t_streams;

  /* metadata - includes writing_app and creation_time */
  GstCaps      *metadata;

  /* state */
  GstMatroskaMuxState state;

  /* a cue (index) table */
  GstMatroskaIndex *index;
  guint          num_indexes;

  /* timescale in the file */
  guint64        time_scale;

  /* length, position (time, ns) */
  guint64        duration;

  /* byte-positions of master-elements (for replacing contents) */
  guint64        segment_pos,
		 seekhead_pos,
		 cues_pos,
#if 0
		 tags_pos,
#endif
		 info_pos,
		 tracks_pos,
		 duration_pos;
  guint64        segment_master;
} GstMatroskaMux;

typedef struct _GstMatroskaMuxClass {
  GstEbmlWriteClass parent;
} GstMatroskaMuxClass;

GType    gst_matroska_mux_get_type    (void);

gboolean gst_matroska_mux_plugin_init (GstPlugin *plugin);

G_END_DECLS

#endif /* __GST_MATROSKA_MUX_H__ */
