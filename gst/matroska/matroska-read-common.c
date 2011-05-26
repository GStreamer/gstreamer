/* GStreamer Matroska muxer/demuxer
 * (c) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 * (c) 2006 Tim-Philipp Müller <tim centricular net>
 * (c) 2008 Sebastian Dröge <slomo@circular-chaos.org>
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

#include <string.h>

#ifdef HAVE_ZLIB
#include <zlib.h>
#endif

#ifdef HAVE_BZ2
#include <bzlib.h>
#endif

#include "lzo.h"

#include "ebml-read.h"
#include "matroska-read-common.h"

GST_DEBUG_CATEGORY_STATIC (matroskareadcommon_debug);
#define GST_CAT_DEFAULT matroskareadcommon_debug

#define DEBUG_ELEMENT_START(common, ebml, element) \
    GST_DEBUG_OBJECT (common, "Parsing " element " element at offset %" \
        G_GUINT64_FORMAT, gst_ebml_read_get_pos (ebml))

#define DEBUG_ELEMENT_STOP(common, ebml, element, ret) \
    GST_DEBUG_OBJECT (common, "Parsing " element " element " \
        " finished with '%s'", gst_flow_get_name (ret))

GstFlowReturn
gst_matroska_decode_content_encodings (GArray * encodings)
{
  gint i;

  if (encodings == NULL)
    return GST_FLOW_OK;

  for (i = 0; i < encodings->len; i++) {
    GstMatroskaTrackEncoding *enc =
        &g_array_index (encodings, GstMatroskaTrackEncoding, i);
    guint8 *data = NULL;
    guint size;

    if ((enc->scope & GST_MATROSKA_TRACK_ENCODING_SCOPE_NEXT_CONTENT_ENCODING)
        == 0)
      continue;

    /* Encryption not supported yet */
    if (enc->type != 0)
      return GST_FLOW_ERROR;

    if (i + 1 >= encodings->len)
      return GST_FLOW_ERROR;

    if (enc->comp_settings_length == 0)
      continue;

    data = enc->comp_settings;
    size = enc->comp_settings_length;

    if (!gst_matroska_decompress_data (enc, &data, &size, enc->comp_algo))
      return GST_FLOW_ERROR;

    g_free (enc->comp_settings);

    enc->comp_settings = data;
    enc->comp_settings_length = size;
  }

  return GST_FLOW_OK;
}

gboolean
gst_matroska_decompress_data (GstMatroskaTrackEncoding * enc,
    guint8 ** data_out, guint * size_out,
    GstMatroskaTrackCompressionAlgorithm algo)
{
  guint8 *new_data = NULL;
  guint new_size = 0;
  guint8 *data = *data_out;
  guint size = *size_out;
  gboolean ret = TRUE;

  if (algo == GST_MATROSKA_TRACK_COMPRESSION_ALGORITHM_ZLIB) {
#ifdef HAVE_ZLIB
    /* zlib encoded data */
    z_stream zstream;
    guint orig_size;
    int result;

    orig_size = size;
    zstream.zalloc = (alloc_func) 0;
    zstream.zfree = (free_func) 0;
    zstream.opaque = (voidpf) 0;
    if (inflateInit (&zstream) != Z_OK) {
      GST_WARNING ("zlib initialization failed.");
      ret = FALSE;
      goto out;
    }
    zstream.next_in = (Bytef *) data;
    zstream.avail_in = orig_size;
    new_size = orig_size;
    new_data = g_malloc (new_size);
    zstream.avail_out = new_size;
    zstream.next_out = (Bytef *) new_data;

    do {
      result = inflate (&zstream, Z_NO_FLUSH);
      if (result != Z_OK && result != Z_STREAM_END) {
        GST_WARNING ("zlib decompression failed.");
        g_free (new_data);
        inflateEnd (&zstream);
        break;
      }
      new_size += 4000;
      new_data = g_realloc (new_data, new_size);
      zstream.next_out = (Bytef *) (new_data + zstream.total_out);
      zstream.avail_out += 4000;
    } while (zstream.avail_in != 0 && result != Z_STREAM_END);

    if (result != Z_STREAM_END) {
      ret = FALSE;
      goto out;
    } else {
      new_size = zstream.total_out;
      inflateEnd (&zstream);
    }
#else
    GST_WARNING ("zlib encoded tracks not supported.");
    ret = FALSE;
    goto out;
#endif
  } else if (algo == GST_MATROSKA_TRACK_COMPRESSION_ALGORITHM_BZLIB) {
#ifdef HAVE_BZ2
    /* bzip2 encoded data */
    bz_stream bzstream;
    guint orig_size;
    int result;

    bzstream.bzalloc = NULL;
    bzstream.bzfree = NULL;
    bzstream.opaque = NULL;
    orig_size = size;

    if (BZ2_bzDecompressInit (&bzstream, 0, 0) != BZ_OK) {
      GST_WARNING ("bzip2 initialization failed.");
      ret = FALSE;
      goto out;
    }

    bzstream.next_in = (char *) data;
    bzstream.avail_in = orig_size;
    new_size = orig_size;
    new_data = g_malloc (new_size);
    bzstream.avail_out = new_size;
    bzstream.next_out = (char *) new_data;

    do {
      result = BZ2_bzDecompress (&bzstream);
      if (result != BZ_OK && result != BZ_STREAM_END) {
        GST_WARNING ("bzip2 decompression failed.");
        g_free (new_data);
        BZ2_bzDecompressEnd (&bzstream);
        break;
      }
      new_size += 4000;
      new_data = g_realloc (new_data, new_size);
      bzstream.next_out = (char *) (new_data + bzstream.total_out_lo32);
      bzstream.avail_out += 4000;
    } while (bzstream.avail_in != 0 && result != BZ_STREAM_END);

    if (result != BZ_STREAM_END) {
      ret = FALSE;
      goto out;
    } else {
      new_size = bzstream.total_out_lo32;
      BZ2_bzDecompressEnd (&bzstream);
    }
#else
    GST_WARNING ("bzip2 encoded tracks not supported.");
    ret = FALSE;
    goto out;
#endif
  } else if (algo == GST_MATROSKA_TRACK_COMPRESSION_ALGORITHM_LZO1X) {
    /* lzo encoded data */
    int result;
    int orig_size, out_size;

    orig_size = size;
    out_size = size;
    new_size = size;
    new_data = g_malloc (new_size);

    do {
      orig_size = size;
      out_size = new_size;

      result = lzo1x_decode (new_data, &out_size, data, &orig_size);

      if (orig_size > 0) {
        new_size += 4000;
        new_data = g_realloc (new_data, new_size);
      }
    } while (orig_size > 0 && result == LZO_OUTPUT_FULL);

    new_size -= out_size;

    if (result != LZO_OUTPUT_FULL) {
      GST_WARNING ("lzo decompression failed");
      g_free (new_data);

      ret = FALSE;
      goto out;
    }

  } else if (algo == GST_MATROSKA_TRACK_COMPRESSION_ALGORITHM_HEADERSTRIP) {
    /* header stripped encoded data */
    if (enc->comp_settings_length > 0) {
      new_data = g_malloc (size + enc->comp_settings_length);
      new_size = size + enc->comp_settings_length;

      memcpy (new_data, enc->comp_settings, enc->comp_settings_length);
      memcpy (new_data + enc->comp_settings_length, data, size);
    }
  } else {
    GST_ERROR ("invalid compression algorithm %d", algo);
    ret = FALSE;
  }

out:

  if (!ret) {
    *data_out = NULL;
    *size_out = 0;
  } else {
    *data_out = new_data;
    *size_out = new_size;
  }

  return ret;
}

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

static gint
gst_matroska_read_common_encoding_cmp (GstMatroskaTrackEncoding * a,
    GstMatroskaTrackEncoding * b)
{
  if (b->order > a->order)
    return 1;
  else if (b->order < a->order)
    return -1;
  else
    return 0;
}

static gboolean
gst_matroska_read_common_encoding_order_unique (GArray * encodings, guint64
    order)
{
  gint i;

  if (encodings == NULL || encodings->len == 0)
    return TRUE;

  for (i = 0; i < encodings->len; i++)
    if (g_array_index (encodings, GstMatroskaTrackEncoding, i).order == order)
      return FALSE;

  return TRUE;
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

static const guint8 *
gst_matroska_read_common_peek_adapter (GstMatroskaReadCommon * common, guint
    peek)
{
  return gst_adapter_peek (common->adapter, peek);
}

/*
 * Calls pull_range for (offset,size) without advancing our offset
 */
GstFlowReturn
gst_matroska_read_common_peek_bytes (GstMatroskaReadCommon * common, guint64
    offset, guint size, GstBuffer ** p_buf, guint8 ** bytes)
{
  GstFlowReturn ret;

  /* Caching here actually makes much less difference than one would expect.
   * We do it mainly to avoid pulling buffers of 1 byte all the time */
  if (common->cached_buffer) {
    guint64 cache_offset = GST_BUFFER_OFFSET (common->cached_buffer);
    guint cache_size = GST_BUFFER_SIZE (common->cached_buffer);

    if (cache_offset <= common->offset &&
        (common->offset + size) <= (cache_offset + cache_size)) {
      if (p_buf)
        *p_buf = gst_buffer_create_sub (common->cached_buffer,
            common->offset - cache_offset, size);
      if (bytes)
        *bytes = GST_BUFFER_DATA (common->cached_buffer) + common->offset -
            cache_offset;
      return GST_FLOW_OK;
    }
    /* not enough data in the cache, free cache and get a new one */
    gst_buffer_unref (common->cached_buffer);
    common->cached_buffer = NULL;
  }

  /* refill the cache */
  ret = gst_pad_pull_range (common->sinkpad, common->offset,
      MAX (size, 64 * 1024), &common->cached_buffer);
  if (ret != GST_FLOW_OK) {
    common->cached_buffer = NULL;
    return ret;
  }

  if (GST_BUFFER_SIZE (common->cached_buffer) >= size) {
    if (p_buf)
      *p_buf = gst_buffer_create_sub (common->cached_buffer, 0, size);
    if (bytes)
      *bytes = GST_BUFFER_DATA (common->cached_buffer);
    return GST_FLOW_OK;
  }

  /* Not possible to get enough data, try a last time with
   * requesting exactly the size we need */
  gst_buffer_unref (common->cached_buffer);
  common->cached_buffer = NULL;

  ret =
      gst_pad_pull_range (common->sinkpad, common->offset, size,
      &common->cached_buffer);
  if (ret != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (common, "pull_range returned %d", ret);
    if (p_buf)
      *p_buf = NULL;
    if (bytes)
      *bytes = NULL;
    return ret;
  }

  if (GST_BUFFER_SIZE (common->cached_buffer) < size) {
    GST_WARNING_OBJECT (common, "Dropping short buffer at offset %"
        G_GUINT64_FORMAT ": wanted %u bytes, got %u bytes", common->offset,
        size, GST_BUFFER_SIZE (common->cached_buffer));

    gst_buffer_unref (common->cached_buffer);
    common->cached_buffer = NULL;
    if (p_buf)
      *p_buf = NULL;
    if (bytes)
      *bytes = NULL;
    return GST_FLOW_UNEXPECTED;
  }

  if (p_buf)
    *p_buf = gst_buffer_create_sub (common->cached_buffer, 0, size);
  if (bytes)
    *bytes = GST_BUFFER_DATA (common->cached_buffer);

  return GST_FLOW_OK;
}

static const guint8 *
gst_matroska_read_common_peek_pull (GstMatroskaReadCommon * common, guint peek)
{
  guint8 *data = NULL;

  gst_matroska_read_common_peek_bytes (common, common->offset, peek, NULL,
      &data);
  return data;
}

GstFlowReturn
gst_matroska_read_common_peek_id_length_pull (GstMatroskaReadCommon * common,
    GstElement * el, guint32 * _id, guint64 * _length, guint * _needed)
{
  return gst_ebml_peek_id_length (_id, _length, _needed,
      (GstPeekData) gst_matroska_read_common_peek_pull, (gpointer) common, el,
      common->offset);
}

GstFlowReturn
gst_matroska_read_common_peek_id_length_push (GstMatroskaReadCommon * common,
    GstElement * el, guint32 * _id, guint64 * _length, guint * _needed)
{
  return gst_ebml_peek_id_length (_id, _length, _needed,
      (GstPeekData) gst_matroska_read_common_peek_adapter, (gpointer) common,
      el, common->offset);
}

static GstFlowReturn
gst_matroska_read_common_read_track_encoding (GstMatroskaReadCommon * common,
    GstEbmlRead * ebml, GstMatroskaTrackContext * context)
{
  GstMatroskaTrackEncoding enc = { 0, };
  GstFlowReturn ret;
  guint32 id;

  DEBUG_ELEMENT_START (common, ebml, "ContentEncoding");
  /* Set default values */
  enc.scope = 1;
  /* All other default values are 0 */

  if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK) {
    DEBUG_ELEMENT_STOP (common, ebml, "ContentEncoding", ret);
    return ret;
  }

  while (ret == GST_FLOW_OK && gst_ebml_read_has_remaining (ebml, 1, TRUE)) {
    if ((ret = gst_ebml_peek_id (ebml, &id)) != GST_FLOW_OK)
      break;

    switch (id) {
      case GST_MATROSKA_ID_CONTENTENCODINGORDER:{
        guint64 num;

        if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK)
          break;

        if (!gst_matroska_read_common_encoding_order_unique (context->encodings,
                num)) {
          GST_ERROR_OBJECT (common, "ContentEncodingOrder %" G_GUINT64_FORMAT
              "is not unique for track %d", num, context->num);
          ret = GST_FLOW_ERROR;
          break;
        }

        GST_DEBUG_OBJECT (common, "ContentEncodingOrder: %" G_GUINT64_FORMAT,
            num);
        enc.order = num;
        break;
      }
      case GST_MATROSKA_ID_CONTENTENCODINGSCOPE:{
        guint64 num;

        if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK)
          break;

        if (num > 7 && num == 0) {
          GST_ERROR_OBJECT (common, "Invalid ContentEncodingScope %"
              G_GUINT64_FORMAT, num);
          ret = GST_FLOW_ERROR;
          break;
        }

        GST_DEBUG_OBJECT (common, "ContentEncodingScope: %" G_GUINT64_FORMAT,
            num);
        enc.scope = num;

        break;
      }
      case GST_MATROSKA_ID_CONTENTENCODINGTYPE:{
        guint64 num;

        if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK)
          break;

        if (num > 1) {
          GST_ERROR_OBJECT (common, "Invalid ContentEncodingType %"
              G_GUINT64_FORMAT, num);
          ret = GST_FLOW_ERROR;
          break;
        } else if (num != 0) {
          GST_ERROR_OBJECT (common, "Encrypted tracks are not supported yet");
          ret = GST_FLOW_ERROR;
          break;
        }
        GST_DEBUG_OBJECT (common, "ContentEncodingType: %" G_GUINT64_FORMAT,
            num);
        enc.type = num;
        break;
      }
      case GST_MATROSKA_ID_CONTENTCOMPRESSION:{

        DEBUG_ELEMENT_START (common, ebml, "ContentCompression");

        if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK)
          break;

        while (ret == GST_FLOW_OK &&
            gst_ebml_read_has_remaining (ebml, 1, TRUE)) {
          if ((ret = gst_ebml_peek_id (ebml, &id)) != GST_FLOW_OK)
            break;

          switch (id) {
            case GST_MATROSKA_ID_CONTENTCOMPALGO:{
              guint64 num;

              if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK) {
                break;
              }
              if (num > 3) {
                GST_ERROR_OBJECT (common, "Invalid ContentCompAlgo %"
                    G_GUINT64_FORMAT, num);
                ret = GST_FLOW_ERROR;
                break;
              }
              GST_DEBUG_OBJECT (common, "ContentCompAlgo: %" G_GUINT64_FORMAT,
                  num);
              enc.comp_algo = num;

              break;
            }
            case GST_MATROSKA_ID_CONTENTCOMPSETTINGS:{
              guint8 *data;
              guint64 size;

              if ((ret =
                      gst_ebml_read_binary (ebml, &id, &data,
                          &size)) != GST_FLOW_OK) {
                break;
              }
              enc.comp_settings = data;
              enc.comp_settings_length = size;
              GST_DEBUG_OBJECT (common,
                  "ContentCompSettings of size %" G_GUINT64_FORMAT, size);
              break;
            }
            default:
              GST_WARNING_OBJECT (common,
                  "Unknown ContentCompression subelement 0x%x - ignoring", id);
              ret = gst_ebml_read_skip (ebml);
              break;
          }
        }
        DEBUG_ELEMENT_STOP (common, ebml, "ContentCompression", ret);
        break;
      }

      case GST_MATROSKA_ID_CONTENTENCRYPTION:
        GST_ERROR_OBJECT (common, "Encrypted tracks not yet supported");
        gst_ebml_read_skip (ebml);
        ret = GST_FLOW_ERROR;
        break;
      default:
        GST_WARNING_OBJECT (common,
            "Unknown ContentEncoding subelement 0x%x - ignoring", id);
        ret = gst_ebml_read_skip (ebml);
        break;
    }
  }

  DEBUG_ELEMENT_STOP (common, ebml, "ContentEncoding", ret);
  if (ret != GST_FLOW_OK && ret != GST_FLOW_UNEXPECTED)
    return ret;

  /* TODO: Check if the combination of values is valid */

  g_array_append_val (context->encodings, enc);

  return ret;
}

GstFlowReturn
gst_matroska_read_common_read_track_encodings (GstMatroskaReadCommon * common,
    GstEbmlRead * ebml, GstMatroskaTrackContext * context)
{
  GstFlowReturn ret;
  guint32 id;

  DEBUG_ELEMENT_START (common, ebml, "ContentEncodings");

  if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK) {
    DEBUG_ELEMENT_STOP (common, ebml, "ContentEncodings", ret);
    return ret;
  }

  context->encodings =
      g_array_sized_new (FALSE, FALSE, sizeof (GstMatroskaTrackEncoding), 1);

  while (ret == GST_FLOW_OK && gst_ebml_read_has_remaining (ebml, 1, TRUE)) {
    if ((ret = gst_ebml_peek_id (ebml, &id)) != GST_FLOW_OK)
      break;

    switch (id) {
      case GST_MATROSKA_ID_CONTENTENCODING:
        ret = gst_matroska_read_common_read_track_encoding (common, ebml,
            context);
        break;
      default:
        GST_WARNING_OBJECT (common,
            "Unknown ContentEncodings subelement 0x%x - ignoring", id);
        ret = gst_ebml_read_skip (ebml);
        break;
    }
  }

  DEBUG_ELEMENT_STOP (common, ebml, "ContentEncodings", ret);
  if (ret != GST_FLOW_OK && ret != GST_FLOW_UNEXPECTED)
    return ret;

  /* Sort encodings according to their order */
  g_array_sort (context->encodings,
      (GCompareFunc) gst_matroska_read_common_encoding_cmp);

  return gst_matroska_decode_content_encodings (context->encodings);
}
