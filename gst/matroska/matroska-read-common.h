/* GStreamer Matroska muxer/demuxer
 * (c) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 * (c) 2011 Debarshi Ray <rishi@gnu.org>
 *
 * matroska-read-common.h: shared by matroska file/stream demuxer and parser
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

#ifndef __GST_MATROSKA_READ_COMMON_H__
#define __GST_MATROSKA_READ_COMMON_H__

#include <glib.h>
#include <gst/gst.h>
#include <gst/base/gstadapter.h>

#include "matroska-ids.h"

G_BEGIN_DECLS

typedef enum {
  GST_MATROSKA_READ_STATE_START,
  GST_MATROSKA_READ_STATE_SEGMENT,
  GST_MATROSKA_READ_STATE_HEADER,
  GST_MATROSKA_READ_STATE_DATA,
  GST_MATROSKA_READ_STATE_SEEK,
  GST_MATROSKA_READ_STATE_SCANNING
} GstMatroskaReadState;

typedef struct _GstMatroskaReadCommon {
  GstIndex                *element_index;
  gint                     element_index_writer_id;

  /* pads */
  GstPad                  *sinkpad;
  GPtrArray               *src;
  guint                    num_streams;

  /* state */
  GstMatroskaReadState     state;

  /* did we parse cues/tracks/segmentinfo already? */
  gboolean                 index_parsed;

  /* start-of-segment */
  guint64                  ebml_segment_start;

  /* a cue (index) table */
  GArray                  *index;

  /* timescale in the file */
  guint64                  time_scale;

  /* pull mode caching */
  GstBuffer *cached_buffer;

  /* push and pull mode */
  guint64                  offset;

  /* push based mode usual suspects */
  GstAdapter              *adapter;
} GstMatroskaReadCommon;

GstFlowReturn gst_matroska_decode_content_encodings (GArray * encodings);
gboolean gst_matroska_decompress_data (GstMatroskaTrackEncoding * enc,
    guint8 ** data_out, guint * size_out,
    GstMatroskaTrackCompressionAlgorithm algo);
GstFlowReturn gst_matroska_read_common_parse_index (GstMatroskaReadCommon *
    common, GstEbmlRead * ebml);
GstFlowReturn gst_matroska_read_common_parse_skip (GstMatroskaReadCommon *
    common, GstEbmlRead * ebml, const gchar * parent_name, guint id);
const guint8 * gst_matroska_read_common_peek_adapter (GstMatroskaReadCommon *
    common, guint peek);
GstFlowReturn gst_matroska_read_common_peek_bytes (GstMatroskaReadCommon *
    common, guint64 offset, guint size, GstBuffer ** p_buf, guint8 ** bytes);
const guint8 * gst_matroska_read_common_peek_pull (GstMatroskaReadCommon *
    common, guint peek);
gint gst_matroska_read_common_stream_from_num (GstMatroskaReadCommon * common,
    guint track_num);
GstFlowReturn gst_matroska_read_common_read_track_encoding (
    GstMatroskaReadCommon * common, GstEbmlRead * ebml,
    GstMatroskaTrackContext * context);

G_END_DECLS

#endif /* __GST_MATROSKA_READ_COMMON_H__ */
