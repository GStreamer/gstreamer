/* GStreamer Matroska muxer/demuxer
 * (c) 2011 Debarshi Ray <rishi@gnu.org>
 *
 * matroska-read-common.c: shared by matroska file/stream demuxer and parser
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ebml-read.h"
#include "matroska-ids.h"
#include "matroska-read-common.h"

GST_DEBUG_CATEGORY_STATIC (matroskareadcommon_debug);
#define GST_CAT_DEFAULT matroskareadcommon_debug

#define DEBUG_ELEMENT_START(common, ebml, element) \
    GST_DEBUG_OBJECT (common, "Parsing " element " element at offset %" \
        G_GUINT64_FORMAT, gst_ebml_read_get_pos (ebml))

#define DEBUG_ELEMENT_STOP(common, ebml, element, ret) \
    GST_DEBUG_OBJECT (common, "Parsing " element " element " \
        " finished with '%s'", gst_flow_get_name (ret))

static gint
gst_matroska_index_compare (GstMatroskaIndex * i1, GstMatroskaIndex * i2)
{
  if (i1->time < i2->time)
    return -1;
  else if (i1->time > i2->time)
    return 1;
  else if (i1->block < i2->block)
    return -1;
  else if (i1->block > i2->block)
    return 1;
  else
    return 0;
}

/* skip unknown or alike element */
GstFlowReturn
gst_matroska_read_common_parse_skip (GstMatroskaReadCommon * common,
    GstEbmlRead * ebml, const gchar * parent_name, guint id)
{
  if (id == GST_EBML_ID_VOID) {
    GST_DEBUG_OBJECT (common, "Skipping EBML Void element");
  } else if (id == GST_EBML_ID_CRC32) {
    GST_DEBUG_OBJECT (common, "Skipping EBML CRC32 element");
  } else {
    GST_WARNING_OBJECT (common,
        "Unknown %s subelement 0x%x - ignoring", parent_name, id);
  }

  return gst_ebml_read_skip (ebml);
}

static GstFlowReturn
gst_matroska_read_common_parse_index_cuetrack (GstMatroskaReadCommon * common,
    GstEbmlRead * ebml, guint * nentries)
{
  guint32 id;
  GstFlowReturn ret;
  GstMatroskaIndex idx;

  idx.pos = (guint64) - 1;
  idx.track = 0;
  idx.time = GST_CLOCK_TIME_NONE;
  idx.block = 1;

  DEBUG_ELEMENT_START (common, ebml, "CueTrackPositions");

  if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK) {
    DEBUG_ELEMENT_STOP (common, ebml, "CueTrackPositions", ret);
    return ret;
  }

  while (ret == GST_FLOW_OK && gst_ebml_read_has_remaining (ebml, 1, TRUE)) {
    if ((ret = gst_ebml_peek_id (ebml, &id)) != GST_FLOW_OK)
      break;

    switch (id) {
        /* track number */
      case GST_MATROSKA_ID_CUETRACK:
      {
        guint64 num;

        if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK)
          break;

        if (num == 0) {
          idx.track = 0;
          GST_WARNING_OBJECT (common, "Invalid CueTrack 0");
          break;
        }

        GST_DEBUG_OBJECT (common, "CueTrack: %" G_GUINT64_FORMAT, num);
        idx.track = num;
        break;
      }

        /* position in file */
      case GST_MATROSKA_ID_CUECLUSTERPOSITION:
      {
        guint64 num;

        if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK)
          break;

        if (num > G_MAXINT64) {
          GST_WARNING_OBJECT (common, "CueClusterPosition %" G_GUINT64_FORMAT
              " too large", num);
          break;
        }

        idx.pos = num;
        break;
      }

        /* number of block in the cluster */
      case GST_MATROSKA_ID_CUEBLOCKNUMBER:
      {
        guint64 num;

        if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK)
          break;

        if (num == 0) {
          GST_WARNING_OBJECT (common, "Invalid CueBlockNumber 0");
          break;
        }

        GST_DEBUG_OBJECT (common, "CueBlockNumber: %" G_GUINT64_FORMAT, num);
        idx.block = num;

        /* mild sanity check, disregard strange cases ... */
        if (idx.block > G_MAXUINT16) {
          GST_DEBUG_OBJECT (common, "... looks suspicious, ignoring");
          idx.block = 1;
        }
        break;
      }

      default:
        ret = gst_matroska_read_common_parse_skip (common, ebml,
            "CueTrackPositions", id);
        break;

      case GST_MATROSKA_ID_CUECODECSTATE:
      case GST_MATROSKA_ID_CUEREFERENCE:
        ret = gst_ebml_read_skip (ebml);
        break;
    }
  }

  DEBUG_ELEMENT_STOP (common, ebml, "CueTrackPositions", ret);

  if ((ret == GST_FLOW_OK || ret == GST_FLOW_UNEXPECTED)
      && idx.pos != (guint64) - 1 && idx.track > 0) {
    g_array_append_val (common->index, idx);
    (*nentries)++;
  } else if (ret == GST_FLOW_OK || ret == GST_FLOW_UNEXPECTED) {
    GST_DEBUG_OBJECT (common, "CueTrackPositions without valid content");
  }

  return ret;
}

static GstFlowReturn
gst_matroska_read_common_parse_index_pointentry (GstMatroskaReadCommon *
    common, GstEbmlRead * ebml)
{
  guint32 id;
  GstFlowReturn ret;
  GstClockTime time = GST_CLOCK_TIME_NONE;
  guint nentries = 0;

  DEBUG_ELEMENT_START (common, ebml, "CuePoint");

  if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK) {
    DEBUG_ELEMENT_STOP (common, ebml, "CuePoint", ret);
    return ret;
  }

  while (ret == GST_FLOW_OK && gst_ebml_read_has_remaining (ebml, 1, TRUE)) {
    if ((ret = gst_ebml_peek_id (ebml, &id)) != GST_FLOW_OK)
      break;

    switch (id) {
        /* one single index entry ('point') */
      case GST_MATROSKA_ID_CUETIME:
      {
        if ((ret = gst_ebml_read_uint (ebml, &id, &time)) != GST_FLOW_OK)
          break;

        GST_DEBUG_OBJECT (common, "CueTime: %" G_GUINT64_FORMAT, time);
        time = time * common->time_scale;
        break;
      }

        /* position in the file + track to which it belongs */
      case GST_MATROSKA_ID_CUETRACKPOSITIONS:
      {
        if ((ret =
                gst_matroska_read_common_parse_index_cuetrack (common, ebml,
                    &nentries)) != GST_FLOW_OK)
          break;
        break;
      }

      default:
        ret = gst_matroska_read_common_parse_skip (common, ebml, "CuePoint",
            id);
        break;
    }
  }

  DEBUG_ELEMENT_STOP (common, ebml, "CuePoint", ret);

  if (nentries > 0) {
    if (time == GST_CLOCK_TIME_NONE) {
      GST_WARNING_OBJECT (common, "CuePoint without valid time");
      g_array_remove_range (common->index, common->index->len - nentries,
          nentries);
    } else {
      gint i;

      for (i = common->index->len - nentries; i < common->index->len; i++) {
        GstMatroskaIndex *idx =
            &g_array_index (common->index, GstMatroskaIndex, i);

        idx->time = time;
        GST_DEBUG_OBJECT (common, "Index entry: pos=%" G_GUINT64_FORMAT
            ", time=%" GST_TIME_FORMAT ", track=%u, block=%u", idx->pos,
            GST_TIME_ARGS (idx->time), (guint) idx->track, (guint) idx->block);
      }
    }
  } else {
    GST_DEBUG_OBJECT (common, "Empty CuePoint");
  }

  return ret;
}

gint
gst_matroska_read_common_stream_from_num (GstMatroskaReadCommon * common,
    guint track_num)
{
  guint n;

  g_assert (common->src->len == common->num_streams);
  for (n = 0; n < common->src->len; n++) {
    GstMatroskaTrackContext *context = g_ptr_array_index (common->src, n);

    if (context->num == track_num) {
      return n;
    }
  }

  if (n == common->num_streams)
    GST_WARNING_OBJECT (common,
        "Failed to find corresponding pad for tracknum %d", track_num);

  return -1;
}

GstFlowReturn
gst_matroska_read_common_parse_index (GstMatroskaReadCommon * common,
    GstEbmlRead * ebml)
{
  guint32 id;
  GstFlowReturn ret = GST_FLOW_OK;
  guint i;

  if (common->index)
    g_array_free (common->index, TRUE);
  common->index =
      g_array_sized_new (FALSE, FALSE, sizeof (GstMatroskaIndex), 128);

  DEBUG_ELEMENT_START (common, ebml, "Cues");

  if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK) {
    DEBUG_ELEMENT_STOP (common, ebml, "Cues", ret);
    return ret;
  }

  while (ret == GST_FLOW_OK && gst_ebml_read_has_remaining (ebml, 1, TRUE)) {
    if ((ret = gst_ebml_peek_id (ebml, &id)) != GST_FLOW_OK)
      break;

    switch (id) {
        /* one single index entry ('point') */
      case GST_MATROSKA_ID_POINTENTRY:
        ret = gst_matroska_read_common_parse_index_pointentry (common, ebml);
        break;

      default:
        ret = gst_matroska_read_common_parse_skip (common, ebml, "Cues", id);
        break;
    }
  }
  DEBUG_ELEMENT_STOP (common, ebml, "Cues", ret);

  /* Sort index by time, smallest time first, for easier searching */
  g_array_sort (common->index, (GCompareFunc) gst_matroska_index_compare);

  /* Now sort the track specific index entries into their own arrays */
  for (i = 0; i < common->index->len; i++) {
    GstMatroskaIndex *idx = &g_array_index (common->index, GstMatroskaIndex,
        i);
    gint track_num;
    GstMatroskaTrackContext *ctx;

    if (common->element_index) {
      gint writer_id;

      if (idx->track != 0 &&
          (track_num =
              gst_matroska_read_common_stream_from_num (common,
                  idx->track)) != -1) {
        ctx = g_ptr_array_index (common->src, track_num);

        if (ctx->index_writer_id == -1)
          gst_index_get_writer_id (common->element_index,
              GST_OBJECT (ctx->pad), &ctx->index_writer_id);
        writer_id = ctx->index_writer_id;
      } else {
        if (common->element_index_writer_id == -1)
          gst_index_get_writer_id (common->element_index, GST_OBJECT (common),
              &common->element_index_writer_id);
        writer_id = common->element_index_writer_id;
      }

      GST_LOG_OBJECT (common, "adding association %" GST_TIME_FORMAT "-> %"
          G_GUINT64_FORMAT " for writer id %d", GST_TIME_ARGS (idx->time),
          idx->pos, writer_id);
      gst_index_add_association (common->element_index, writer_id,
          GST_ASSOCIATION_FLAG_KEY_UNIT, GST_FORMAT_TIME, idx->time,
          GST_FORMAT_BYTES, idx->pos + common->ebml_segment_start, NULL);
    }

    if (idx->track == 0)
      continue;

    track_num = gst_matroska_read_common_stream_from_num (common, idx->track);
    if (track_num == -1)
      continue;

    ctx = g_ptr_array_index (common->src, track_num);

    if (ctx->index_table == NULL)
      ctx->index_table =
          g_array_sized_new (FALSE, FALSE, sizeof (GstMatroskaIndex), 128);

    g_array_append_vals (ctx->index_table, idx, 1);
  }

  common->index_parsed = TRUE;

  /* sanity check; empty index normalizes to no index */
  if (common->index->len == 0) {
    g_array_free (common->index, TRUE);
    common->index = NULL;
  }

  return ret;
}
