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

#include <stdio.h>
#include <string.h>

#ifdef HAVE_ZLIB
#include <zlib.h>
#endif

#ifdef HAVE_BZ2
#include <bzlib.h>
#endif

#include <gst/tag/tag.h>
#include <gst/base/gsttypefindhelper.h>

#include "lzo.h"

#include "ebml-read.h"
#include "matroska-read-common.h"

GST_DEBUG_CATEGORY (matroskareadcommon_debug);
#define GST_CAT_DEFAULT matroskareadcommon_debug

#define DEBUG_ELEMENT_START(common, ebml, element) \
    GST_DEBUG_OBJECT (common, "Parsing " element " element at offset %" \
        G_GUINT64_FORMAT, gst_ebml_read_get_pos (ebml))

#define DEBUG_ELEMENT_STOP(common, ebml, element, ret) \
    GST_DEBUG_OBJECT (common, "Parsing " element " element " \
        " finished with '%s'", gst_flow_get_name (ret))

#define GST_MATROSKA_TOC_UID_CHAPTER "chapter"
#define GST_MATROSKA_TOC_UID_EDITION "edition"
#define GST_MATROSKA_TOC_UID_EMPTY "empty"

static gboolean
gst_matroska_decompress_data (GstMatroskaTrackEncoding * enc,
    gpointer * data_out, gsize * size_out,
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

GstFlowReturn
gst_matroska_decode_content_encodings (GArray * encodings)
{
  gint i;

  if (encodings == NULL)
    return GST_FLOW_OK;

  for (i = 0; i < encodings->len; i++) {
    GstMatroskaTrackEncoding *enc =
        &g_array_index (encodings, GstMatroskaTrackEncoding, i);
    gpointer data = NULL;
    gsize size;

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
gst_matroska_decode_data (GArray * encodings, gpointer * data_out,
    gsize * size_out, GstMatroskaTrackEncodingScope scope, gboolean free)
{
  gpointer data;
  gsize size;
  gboolean ret = TRUE;
  gint i;

  g_return_val_if_fail (encodings != NULL, FALSE);
  g_return_val_if_fail (data_out != NULL && *data_out != NULL, FALSE);
  g_return_val_if_fail (size_out != NULL, FALSE);

  data = *data_out;
  size = *size_out;

  for (i = 0; i < encodings->len; i++) {
    GstMatroskaTrackEncoding *enc =
        &g_array_index (encodings, GstMatroskaTrackEncoding, i);
    gpointer new_data = NULL;
    gsize new_size = 0;

    if ((enc->scope & scope) == 0)
      continue;

    /* Encryption not supported yet */
    if (enc->type != 0) {
      ret = FALSE;
      break;
    }

    new_data = data;
    new_size = size;

    ret =
        gst_matroska_decompress_data (enc, &new_data, &new_size,
        enc->comp_algo);

    if (!ret)
      break;

    if ((data == *data_out && free) || (data != *data_out))
      g_free (data);

    data = new_data;
    size = new_size;
  }

  if (!ret) {
    if ((data == *data_out && free) || (data != *data_out))
      g_free (data);

    *data_out = NULL;
    *size_out = 0;
  } else {
    *data_out = data;
    *size_out = size;
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

gint
gst_matroska_index_seek_find (GstMatroskaIndex * i1, GstClockTime * time,
    gpointer user_data)
{
  if (i1->time < *time)
    return -1;
  else if (i1->time > *time)
    return 1;
  else
    return 0;
}

GstMatroskaIndex *
gst_matroska_read_common_do_index_seek (GstMatroskaReadCommon * common,
    GstMatroskaTrackContext * track, gint64 seek_pos, GArray ** _index,
    gint * _entry_index, gboolean next)
{
  GstMatroskaIndex *entry = NULL;
  GArray *index;

  if (!common->index || !common->index->len)
    return NULL;

  /* find entry just before or at the requested position */
  if (track && track->index_table)
    index = track->index_table;
  else
    index = common->index;

  entry =
      gst_util_array_binary_search (index->data, index->len,
      sizeof (GstMatroskaIndex),
      (GCompareDataFunc) gst_matroska_index_seek_find,
      next ? GST_SEARCH_MODE_AFTER : GST_SEARCH_MODE_BEFORE, &seek_pos, NULL);

  if (entry == NULL) {
    if (next) {
      return NULL;
    } else {
      entry = &g_array_index (index, GstMatroskaIndex, 0);
    }
  }

  if (_index)
    *_index = index;
  if (_entry_index)
    *_entry_index = entry - (GstMatroskaIndex *) index->data;

  return entry;
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

/* takes ownership of taglist */
void
gst_matroska_read_common_found_global_tag (GstMatroskaReadCommon * common,
    GstElement * el, GstTagList * taglist)
{
  if (common->global_tags) {
    /* nothing sent yet, add to cache */
    gst_tag_list_insert (common->global_tags, taglist, GST_TAG_MERGE_APPEND);
    gst_tag_list_unref (taglist);
  } else {
    GstEvent *tag_event = gst_event_new_tag (taglist);
    gint i;

    /* hm, already sent, no need to cache and wait anymore */
    GST_DEBUG_OBJECT (common, "Sending late global tags %" GST_PTR_FORMAT,
        taglist);

    for (i = 0; i < common->src->len; i++) {
      GstMatroskaTrackContext *stream;

      stream = g_ptr_array_index (common->src, i);
      gst_pad_push_event (stream->pad, gst_event_ref (tag_event));
    }

    gst_event_unref (tag_event);
  }
}

gint64
gst_matroska_read_common_get_length (GstMatroskaReadCommon * common)
{
  gint64 end = -1;

  if (!gst_pad_peer_query_duration (common->sinkpad, GST_FORMAT_BYTES,
          &end) || end < 0)
    GST_DEBUG_OBJECT (common, "no upstream length");

  return end;
}

/* determine track to seek in */
GstMatroskaTrackContext *
gst_matroska_read_common_get_seek_track (GstMatroskaReadCommon * common,
    GstMatroskaTrackContext * track)
{
  gint i;

  if (track && track->type == GST_MATROSKA_TRACK_TYPE_VIDEO)
    return track;

  for (i = 0; i < common->src->len; i++) {
    GstMatroskaTrackContext *stream;

    stream = g_ptr_array_index (common->src, i);
    if (stream->type == GST_MATROSKA_TRACK_TYPE_VIDEO && stream->index_table)
      track = stream;
  }

  return track;
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
gst_matroska_read_common_parse_attached_file (GstMatroskaReadCommon * common,
    GstEbmlRead * ebml, GstTagList * taglist)
{
  guint32 id;
  GstFlowReturn ret;
  gchar *description = NULL;
  gchar *filename = NULL;
  gchar *mimetype = NULL;
  guint8 *data = NULL;
  guint64 datalen = 0;

  DEBUG_ELEMENT_START (common, ebml, "AttachedFile");

  if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK) {
    DEBUG_ELEMENT_STOP (common, ebml, "AttachedFile", ret);
    return ret;
  }

  while (ret == GST_FLOW_OK && gst_ebml_read_has_remaining (ebml, 1, TRUE)) {
    /* read all sub-entries */

    if ((ret = gst_ebml_peek_id (ebml, &id)) != GST_FLOW_OK)
      break;

    switch (id) {
      case GST_MATROSKA_ID_FILEDESCRIPTION:
        if (description) {
          GST_WARNING_OBJECT (common, "FileDescription can only appear once");
          break;
        }

        ret = gst_ebml_read_utf8 (ebml, &id, &description);
        GST_DEBUG_OBJECT (common, "FileDescription: %s",
            GST_STR_NULL (description));
        break;
      case GST_MATROSKA_ID_FILENAME:
        if (filename) {
          GST_WARNING_OBJECT (common, "FileName can only appear once");
          break;
        }

        ret = gst_ebml_read_utf8 (ebml, &id, &filename);

        GST_DEBUG_OBJECT (common, "FileName: %s", GST_STR_NULL (filename));
        break;
      case GST_MATROSKA_ID_FILEMIMETYPE:
        if (mimetype) {
          GST_WARNING_OBJECT (common, "FileMimeType can only appear once");
          break;
        }

        ret = gst_ebml_read_ascii (ebml, &id, &mimetype);
        GST_DEBUG_OBJECT (common, "FileMimeType: %s", GST_STR_NULL (mimetype));
        break;
      case GST_MATROSKA_ID_FILEDATA:
        if (data) {
          GST_WARNING_OBJECT (common, "FileData can only appear once");
          break;
        }

        ret = gst_ebml_read_binary (ebml, &id, &data, &datalen);
        GST_DEBUG_OBJECT (common, "FileData of size %" G_GUINT64_FORMAT,
            datalen);
        break;

      default:
        ret = gst_matroska_read_common_parse_skip (common, ebml,
            "AttachedFile", id);
        break;
      case GST_MATROSKA_ID_FILEUID:
        ret = gst_ebml_read_skip (ebml);
        break;
    }
  }

  DEBUG_ELEMENT_STOP (common, ebml, "AttachedFile", ret);

  if (filename && mimetype && data && datalen > 0) {
    GstTagImageType image_type = GST_TAG_IMAGE_TYPE_NONE;
    GstBuffer *tagbuffer = NULL;
    GstSample *tagsample = NULL;
    GstStructure *info = NULL;
    GstCaps *caps = NULL;
    gchar *filename_lc = g_utf8_strdown (filename, -1);

    GST_DEBUG_OBJECT (common, "Creating tag for attachment with "
        "filename '%s', mimetype '%s', description '%s', "
        "size %" G_GUINT64_FORMAT, filename, mimetype,
        GST_STR_NULL (description), datalen);

    /* TODO: better heuristics for different image types */
    if (strstr (filename_lc, "cover")) {
      if (strstr (filename_lc, "back"))
        image_type = GST_TAG_IMAGE_TYPE_BACK_COVER;
      else
        image_type = GST_TAG_IMAGE_TYPE_FRONT_COVER;
    } else if (g_str_has_prefix (mimetype, "image/") ||
        g_str_has_suffix (filename_lc, "png") ||
        g_str_has_suffix (filename_lc, "jpg") ||
        g_str_has_suffix (filename_lc, "jpeg") ||
        g_str_has_suffix (filename_lc, "gif") ||
        g_str_has_suffix (filename_lc, "bmp")) {
      image_type = GST_TAG_IMAGE_TYPE_UNDEFINED;
    }
    g_free (filename_lc);

    /* First try to create an image tag buffer from this */
    if (image_type != GST_TAG_IMAGE_TYPE_NONE) {
      tagsample =
          gst_tag_image_data_to_image_sample (data, datalen, image_type);

      if (!tagsample)
        image_type = GST_TAG_IMAGE_TYPE_NONE;
      else {
        data = NULL;
        tagbuffer = gst_buffer_ref (gst_sample_get_buffer (tagsample));
        caps = gst_caps_ref (gst_sample_get_caps (tagsample));
        info = gst_structure_copy (gst_sample_get_info (tagsample));
        gst_sample_unref (tagsample);
      }
    }

    /* if this failed create an attachment buffer */
    if (!tagbuffer) {
      tagbuffer = gst_buffer_new_wrapped (g_memdup (data, datalen), datalen);

      caps = gst_type_find_helper_for_buffer (NULL, tagbuffer, NULL);
      if (caps == NULL)
        caps = gst_caps_new_empty_simple (mimetype);
    }

    /* Set filename and description in the info */
    if (info == NULL)
      info = gst_structure_new_empty ("GstTagImageInfo");

    gst_structure_set (info, "filename", G_TYPE_STRING, filename, NULL);
    if (description)
      gst_structure_set (info, "description", G_TYPE_STRING, description, NULL);

    tagsample = gst_sample_new (tagbuffer, caps, NULL, info);

    GST_DEBUG_OBJECT (common,
        "Created attachment sample: %" GST_PTR_FORMAT, tagsample);

    /* and append to the tag list */
    if (image_type != GST_TAG_IMAGE_TYPE_NONE)
      gst_tag_list_add (taglist, GST_TAG_MERGE_APPEND, GST_TAG_IMAGE, tagsample,
          NULL);
    else
      gst_tag_list_add (taglist, GST_TAG_MERGE_APPEND, GST_TAG_ATTACHMENT,
          tagsample, NULL);

    /* the list adds it own ref */
    gst_sample_unref (tagsample);
  }

  g_free (filename);
  g_free (mimetype);
  g_free (data);
  g_free (description);

  return ret;
}

GstFlowReturn
gst_matroska_read_common_parse_attachments (GstMatroskaReadCommon * common,
    GstElement * el, GstEbmlRead * ebml)
{
  guint32 id;
  GstFlowReturn ret = GST_FLOW_OK;
  GstTagList *taglist;

  DEBUG_ELEMENT_START (common, ebml, "Attachments");

  if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK) {
    DEBUG_ELEMENT_STOP (common, ebml, "Attachments", ret);
    return ret;
  }

  taglist = gst_tag_list_new_empty ();
  gst_tag_list_set_scope (taglist, GST_TAG_SCOPE_GLOBAL);

  while (ret == GST_FLOW_OK && gst_ebml_read_has_remaining (ebml, 1, TRUE)) {
    if ((ret = gst_ebml_peek_id (ebml, &id)) != GST_FLOW_OK)
      break;

    switch (id) {
      case GST_MATROSKA_ID_ATTACHEDFILE:
        ret = gst_matroska_read_common_parse_attached_file (common, ebml,
            taglist);
        break;

      default:
        ret = gst_matroska_read_common_parse_skip (common, ebml,
            "Attachments", id);
        break;
    }
  }
  DEBUG_ELEMENT_STOP (common, ebml, "Attachments", ret);

  if (gst_tag_list_n_tags (taglist) > 0) {
    GST_DEBUG_OBJECT (common, "Storing attachment tags");
    gst_matroska_read_common_found_global_tag (common, el, taglist);
  } else {
    GST_DEBUG_OBJECT (common, "No valid attachments found");
    gst_tag_list_unref (taglist);
  }

  common->attachments_parsed = TRUE;

  return ret;
}

static void
gst_matroska_read_common_parse_toc_tag (GstTocEntry * entry,
    GArray * edition_targets, GArray * chapter_targtes, GstTagList * tags)
{
  gchar *uid;
  guint i;
  guint64 tgt;
  GArray *targets;
  GList *cur;
  GstTagList *etags;

  targets =
      (gst_toc_entry_get_entry_type (entry) ==
      GST_TOC_ENTRY_TYPE_EDITION) ? edition_targets : chapter_targtes;

  etags = gst_tag_list_new_empty ();

  for (i = 0; i < targets->len; ++i) {
    tgt = g_array_index (targets, guint64, i);

    if (tgt == 0)
      gst_tag_list_insert (etags, tags, GST_TAG_MERGE_APPEND);
    else {
      uid = g_strdup_printf ("%" G_GUINT64_FORMAT, tgt);
      if (g_strcmp0 (gst_toc_entry_get_uid (entry), uid) == 0)
        gst_tag_list_insert (etags, tags, GST_TAG_MERGE_APPEND);
      g_free (uid);
    }
  }

  gst_toc_entry_merge_tags (entry, etags, GST_TAG_MERGE_APPEND);

  cur = gst_toc_entry_get_sub_entries (entry);
  while (cur != NULL) {
    gst_matroska_read_common_parse_toc_tag (cur->data, edition_targets,
        chapter_targtes, tags);
    cur = cur->next;
  }
}

static GstFlowReturn
gst_matroska_read_common_parse_metadata_targets (GstMatroskaReadCommon * common,
    GstEbmlRead * ebml, GArray * edition_targets, GArray * chapter_targets)
{
  GstFlowReturn ret = GST_FLOW_OK;
  guint32 id;
  guint64 uid;

  DEBUG_ELEMENT_START (common, ebml, "TagTargets");

  if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK) {
    DEBUG_ELEMENT_STOP (common, ebml, "TagTargets", ret);
    return ret;
  }

  while (ret == GST_FLOW_OK && gst_ebml_read_has_remaining (ebml, 1, TRUE)) {
    if ((ret = gst_ebml_peek_id (ebml, &id)) != GST_FLOW_OK)
      break;

    switch (id) {
      case GST_MATROSKA_ID_TARGETCHAPTERUID:
        if ((ret = gst_ebml_read_uint (ebml, &id, &uid)) == GST_FLOW_OK)
          g_array_append_val (chapter_targets, uid);
        break;

      case GST_MATROSKA_ID_TARGETEDITIONUID:
        if ((ret = gst_ebml_read_uint (ebml, &id, &uid)) == GST_FLOW_OK)
          g_array_append_val (edition_targets, uid);
        break;

      default:
        ret =
            gst_matroska_read_common_parse_skip (common, ebml, "TagTargets",
            id);
        break;
    }
  }

  DEBUG_ELEMENT_STOP (common, ebml, "TagTargets", ret);

  return ret;
}

static void
gst_matroska_read_common_postprocess_toc_entries (GList * toc_entries,
    guint64 max, const gchar * parent_uid)
{
  GstTocEntry *cur_info, *prev_info, *next_info;
  GList *cur_list, *prev_list, *next_list;
  gint64 cur_start, prev_start, stop;

  cur_list = toc_entries;
  while (cur_list != NULL) {
    cur_info = cur_list->data;

    switch (gst_toc_entry_get_entry_type (cur_info)) {
      case GST_TOC_ENTRY_TYPE_ANGLE:
      case GST_TOC_ENTRY_TYPE_VERSION:
      case GST_TOC_ENTRY_TYPE_EDITION:
        /* in Matroska terms edition has duration of full track */
        gst_toc_entry_set_start_stop_times (cur_info, 0, max);

        gst_matroska_read_common_postprocess_toc_entries
            (gst_toc_entry_get_sub_entries (cur_info), max,
            gst_toc_entry_get_uid (cur_info));
        break;

      case GST_TOC_ENTRY_TYPE_TITLE:
      case GST_TOC_ENTRY_TYPE_TRACK:
      case GST_TOC_ENTRY_TYPE_CHAPTER:
        prev_list = cur_list->prev;
        next_list = cur_list->next;

        if (prev_list != NULL)
          prev_info = prev_list->data;
        else
          prev_info = NULL;

        if (next_list != NULL)
          next_info = next_list->data;
        else
          next_info = NULL;

        /* updated stop time in previous chapter and it's subchapters */
        if (prev_info != NULL) {
          gst_toc_entry_get_start_stop_times (prev_info, &prev_start, &stop);
          gst_toc_entry_get_start_stop_times (cur_info, &cur_start, &stop);

          stop = cur_start;
          gst_toc_entry_set_start_stop_times (prev_info, prev_start, stop);

          gst_matroska_read_common_postprocess_toc_entries
              (gst_toc_entry_get_sub_entries (prev_info), cur_start,
              gst_toc_entry_get_uid (prev_info));
        }

        /* updated stop time in current chapter and it's subchapters */
        if (next_info == NULL) {
          gst_toc_entry_get_start_stop_times (cur_info, &cur_start, &stop);

          if (stop == -1) {
            stop = max;
            gst_toc_entry_set_start_stop_times (cur_info, cur_start, stop);
          }

          gst_matroska_read_common_postprocess_toc_entries
              (gst_toc_entry_get_sub_entries (cur_info), stop,
              gst_toc_entry_get_uid (cur_info));
        }
        break;
      case GST_TOC_ENTRY_TYPE_INVALID:
        break;
    }
    cur_list = cur_list->next;
  }
}

static GstFlowReturn
gst_matroska_read_common_parse_chapter_titles (GstMatroskaReadCommon * common,
    GstEbmlRead * ebml, GstTagList * titles)
{
  guint32 id;
  gchar *title = NULL;
  GstFlowReturn ret = GST_FLOW_OK;

  DEBUG_ELEMENT_START (common, ebml, "ChaptersTitles");


  if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK) {
    DEBUG_ELEMENT_STOP (common, ebml, "ChaptersTitles", ret);
    return ret;
  }

  while (ret == GST_FLOW_OK && gst_ebml_read_has_remaining (ebml, 1, TRUE)) {
    if ((ret = gst_ebml_peek_id (ebml, &id)) != GST_FLOW_OK)
      break;

    switch (id) {
      case GST_MATROSKA_ID_CHAPSTRING:
        ret = gst_ebml_read_utf8 (ebml, &id, &title);
        break;

      default:
        ret =
            gst_matroska_read_common_parse_skip (common, ebml, "ChaptersTitles",
            id);
        break;
    }
  }

  DEBUG_ELEMENT_STOP (common, ebml, "ChaptersTitles", ret);

  if (title != NULL && ret == GST_FLOW_OK)
    gst_tag_list_add (titles, GST_TAG_MERGE_APPEND, GST_TAG_TITLE, title, NULL);

  g_free (title);
  return ret;
}

static GstFlowReturn
gst_matroska_read_common_parse_chapter_element (GstMatroskaReadCommon * common,
    GstEbmlRead * ebml, GList ** subentries)
{
  guint32 id;
  guint64 start_time = -1, stop_time = -1;
  guint64 is_hidden = 0, is_enabled = 1, uid = 0;
  GstFlowReturn ret = GST_FLOW_OK;
  GstTocEntry *chapter_info;
  GstTagList *tags;
  gchar *uid_str;
  GList *subsubentries = NULL, *l;

  DEBUG_ELEMENT_START (common, ebml, "ChaptersElement");

  if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK) {
    DEBUG_ELEMENT_STOP (common, ebml, "ChaptersElement", ret);
    return ret;
  }

  tags = gst_tag_list_new_empty ();

  while (ret == GST_FLOW_OK && gst_ebml_read_has_remaining (ebml, 1, TRUE)) {
    if ((ret = gst_ebml_peek_id (ebml, &id)) != GST_FLOW_OK)
      break;

    switch (id) {
      case GST_MATROSKA_ID_CHAPTERUID:
        ret = gst_ebml_read_uint (ebml, &id, &uid);
        break;

      case GST_MATROSKA_ID_CHAPTERTIMESTART:
        ret = gst_ebml_read_uint (ebml, &id, &start_time);
        break;

      case GST_MATROSKA_ID_CHAPTERTIMESTOP:
        ret = gst_ebml_read_uint (ebml, &id, &stop_time);
        break;

      case GST_MATROSKA_ID_CHAPTERATOM:
        ret =
            gst_matroska_read_common_parse_chapter_element (common, ebml,
            &subsubentries);
        break;

      case GST_MATROSKA_ID_CHAPTERDISPLAY:
        ret =
            gst_matroska_read_common_parse_chapter_titles (common, ebml, tags);
        break;

      case GST_MATROSKA_ID_CHAPTERFLAGHIDDEN:
        ret = gst_ebml_read_uint (ebml, &id, &is_hidden);
        break;

      case GST_MATROSKA_ID_CHAPTERFLAGENABLED:
        ret = gst_ebml_read_uint (ebml, &id, &is_enabled);
        break;

      default:
        ret =
            gst_matroska_read_common_parse_skip (common, ebml,
            "ChaptersElement", id);
        break;
    }
  }

  if (uid == 0)
    uid = (((guint64) g_random_int ()) << 32) | g_random_int ();
  uid_str = g_strdup_printf ("%" G_GUINT64_FORMAT, uid);
  chapter_info = gst_toc_entry_new (GST_TOC_ENTRY_TYPE_CHAPTER, uid_str);
  g_free (uid_str);

  gst_toc_entry_set_tags (chapter_info, tags);
  gst_toc_entry_set_start_stop_times (chapter_info, start_time, stop_time);

  for (l = subsubentries; l; l = l->next)
    gst_toc_entry_append_sub_entry (chapter_info, l->data);
  g_list_free (subsubentries);

  DEBUG_ELEMENT_STOP (common, ebml, "ChaptersElement", ret);

  /* start time is mandatory and has no default value,
   * so we should skip chapters without it */
  if (is_hidden == 0 && is_enabled > 0 &&
      start_time != -1 && ret == GST_FLOW_OK) {
    *subentries = g_list_append (*subentries, chapter_info);
  } else
    gst_toc_entry_unref (chapter_info);

  return ret;
}

static GstFlowReturn
gst_matroska_read_common_parse_chapter_edition (GstMatroskaReadCommon * common,
    GstEbmlRead * ebml, GstToc * toc)
{
  guint32 id;
  guint64 is_hidden = 0, uid = 0;
  GstFlowReturn ret = GST_FLOW_OK;
  GstTocEntry *edition_info;
  GList *subentries = NULL, *l;
  gchar *uid_str;

  DEBUG_ELEMENT_START (common, ebml, "ChaptersEdition");

  if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK) {
    DEBUG_ELEMENT_STOP (common, ebml, "ChaptersEdition", ret);
    return ret;
  }

  while (ret == GST_FLOW_OK && gst_ebml_read_has_remaining (ebml, 1, TRUE)) {
    if ((ret = gst_ebml_peek_id (ebml, &id)) != GST_FLOW_OK)
      break;

    switch (id) {
      case GST_MATROSKA_ID_EDITIONUID:
        ret = gst_ebml_read_uint (ebml, &id, &uid);
        break;

      case GST_MATROSKA_ID_CHAPTERATOM:
        ret =
            gst_matroska_read_common_parse_chapter_element (common, ebml,
            &subentries);
        break;

      case GST_MATROSKA_ID_EDITIONFLAGHIDDEN:
        ret = gst_ebml_read_uint (ebml, &id, &is_hidden);
        break;

      default:
        ret =
            gst_matroska_read_common_parse_skip (common, ebml,
            "ChaptersEdition", id);
        break;
    }
  }

  DEBUG_ELEMENT_STOP (common, ebml, "ChaptersEdition", ret);

  if (uid == 0)
    uid = (((guint64) g_random_int ()) << 32) | g_random_int ();
  uid_str = g_strdup_printf ("%" G_GUINT64_FORMAT, uid);
  edition_info = gst_toc_entry_new (GST_TOC_ENTRY_TYPE_EDITION, uid_str);
  gst_toc_entry_set_start_stop_times (edition_info, -1, -1);
  g_free (uid_str);

  for (l = subentries; l; l = l->next)
    gst_toc_entry_append_sub_entry (edition_info, l->data);

  if (is_hidden == 0 && subentries != NULL && ret == GST_FLOW_OK)
    gst_toc_append_entry (toc, edition_info);
  else {
    GST_DEBUG_OBJECT (common,
        "Skipping empty or hidden edition in the chapters TOC");
    gst_toc_entry_unref (edition_info);
  }

  return ret;
}

GstFlowReturn
gst_matroska_read_common_parse_chapters (GstMatroskaReadCommon * common,
    GstEbmlRead * ebml)
{
  guint32 id;
  GstFlowReturn ret = GST_FLOW_OK;
  GstToc *toc;

  DEBUG_ELEMENT_START (common, ebml, "Chapters");

  if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK) {
    DEBUG_ELEMENT_STOP (common, ebml, "Chapters", ret);
    return ret;
  }

  /* FIXME: create CURRENT toc as well */
  toc = gst_toc_new (GST_TOC_SCOPE_GLOBAL);

  while (ret == GST_FLOW_OK && gst_ebml_read_has_remaining (ebml, 1, TRUE)) {
    if ((ret = gst_ebml_peek_id (ebml, &id)) != GST_FLOW_OK)
      break;

    switch (id) {
      case GST_MATROSKA_ID_EDITIONENTRY:
        ret =
            gst_matroska_read_common_parse_chapter_edition (common, ebml, toc);
        break;

      default:
        ret =
            gst_matroska_read_common_parse_skip (common, ebml, "Chapters", id);
        break;
    }
  }

  if (gst_toc_get_entries (toc) != NULL) {
    gst_matroska_read_common_postprocess_toc_entries (gst_toc_get_entries (toc),
        common->segment.duration, "");

    common->toc = toc;
  } else
    gst_toc_unref (toc);

  common->chapters_parsed = TRUE;

  DEBUG_ELEMENT_STOP (common, ebml, "Chapters", ret);
  return ret;
}

GstFlowReturn
gst_matroska_read_common_parse_header (GstMatroskaReadCommon * common,
    GstEbmlRead * ebml)
{
  GstFlowReturn ret;
  gchar *doctype;
  guint version;
  guint32 id;

  /* this function is the first to be called */

  /* default init */
  doctype = NULL;
  version = 1;

  ret = gst_ebml_peek_id (ebml, &id);
  if (ret != GST_FLOW_OK)
    return ret;

  GST_DEBUG_OBJECT (common, "id: %08x", id);

  if (id != GST_EBML_ID_HEADER) {
    GST_ERROR_OBJECT (common, "Failed to read header");
    goto exit;
  }

  ret = gst_ebml_read_master (ebml, &id);
  if (ret != GST_FLOW_OK)
    return ret;

  while (gst_ebml_read_has_remaining (ebml, 1, TRUE)) {
    ret = gst_ebml_peek_id (ebml, &id);
    if (ret != GST_FLOW_OK)
      return ret;

    switch (id) {
        /* is our read version uptodate? */
      case GST_EBML_ID_EBMLREADVERSION:{
        guint64 num;

        ret = gst_ebml_read_uint (ebml, &id, &num);
        if (ret != GST_FLOW_OK)
          return ret;
        if (num != GST_EBML_VERSION) {
          GST_ERROR_OBJECT (ebml, "Unsupported EBML version %" G_GUINT64_FORMAT,
              num);
          return GST_FLOW_ERROR;
        }

        GST_DEBUG_OBJECT (ebml, "EbmlReadVersion: %" G_GUINT64_FORMAT, num);
        break;
      }

        /* we only handle 8 byte lengths at max */
      case GST_EBML_ID_EBMLMAXSIZELENGTH:{
        guint64 num;

        ret = gst_ebml_read_uint (ebml, &id, &num);
        if (ret != GST_FLOW_OK)
          return ret;
        if (num > sizeof (guint64)) {
          GST_ERROR_OBJECT (ebml,
              "Unsupported EBML maximum size %" G_GUINT64_FORMAT, num);
          return GST_FLOW_ERROR;
        }
        GST_DEBUG_OBJECT (ebml, "EbmlMaxSizeLength: %" G_GUINT64_FORMAT, num);
        break;
      }

        /* we handle 4 byte IDs at max */
      case GST_EBML_ID_EBMLMAXIDLENGTH:{
        guint64 num;

        ret = gst_ebml_read_uint (ebml, &id, &num);
        if (ret != GST_FLOW_OK)
          return ret;
        if (num > sizeof (guint32)) {
          GST_ERROR_OBJECT (ebml,
              "Unsupported EBML maximum ID %" G_GUINT64_FORMAT, num);
          return GST_FLOW_ERROR;
        }
        GST_DEBUG_OBJECT (ebml, "EbmlMaxIdLength: %" G_GUINT64_FORMAT, num);
        break;
      }

      case GST_EBML_ID_DOCTYPE:{
        gchar *text;

        ret = gst_ebml_read_ascii (ebml, &id, &text);
        if (ret != GST_FLOW_OK)
          return ret;

        GST_DEBUG_OBJECT (ebml, "EbmlDocType: %s", GST_STR_NULL (text));

        if (doctype)
          g_free (doctype);
        doctype = text;
        break;
      }

      case GST_EBML_ID_DOCTYPEREADVERSION:{
        guint64 num;

        ret = gst_ebml_read_uint (ebml, &id, &num);
        if (ret != GST_FLOW_OK)
          return ret;
        version = num;
        GST_DEBUG_OBJECT (ebml, "EbmlReadVersion: %" G_GUINT64_FORMAT, num);
        break;
      }

      default:
        ret = gst_matroska_read_common_parse_skip (common, ebml,
            "EBML header", id);
        if (ret != GST_FLOW_OK)
          return ret;
        break;

        /* we ignore these two, as they don't tell us anything we care about */
      case GST_EBML_ID_EBMLVERSION:
      case GST_EBML_ID_DOCTYPEVERSION:
        ret = gst_ebml_read_skip (ebml);
        if (ret != GST_FLOW_OK)
          return ret;
        break;
    }
  }

exit:

  if ((doctype != NULL && !strcmp (doctype, GST_MATROSKA_DOCTYPE_MATROSKA)) ||
      (doctype != NULL && !strcmp (doctype, GST_MATROSKA_DOCTYPE_WEBM)) ||
      (doctype == NULL)) {
    if (version <= 2) {
      if (doctype) {
        GST_INFO_OBJECT (common, "Input is %s version %d", doctype, version);
      } else {
        GST_WARNING_OBJECT (common, "Input is EBML without doctype, assuming "
            "matroska (version %d)", version);
      }
      ret = GST_FLOW_OK;
    } else {
      GST_ELEMENT_ERROR (common, STREAM, DEMUX, (NULL),
          ("Demuxer version (2) is too old to read %s version %d",
              GST_STR_NULL (doctype), version));
      ret = GST_FLOW_ERROR;
    }
    g_free (doctype);
  } else {
    GST_ELEMENT_ERROR (common, STREAM, WRONG_TYPE, (NULL),
        ("Input is not a matroska stream (doctype=%s)", doctype));
    ret = GST_FLOW_ERROR;
    g_free (doctype);
  }

  return ret;
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

  /* (e.g.) lavf typically creates entries without a block number,
   * which is bogus and leads to contradictory information */
  if (common->index->len) {
    GstMatroskaIndex *last_idx;

    last_idx = &g_array_index (common->index, GstMatroskaIndex,
        common->index->len - 1);
    if (last_idx->block == idx.block && last_idx->pos == idx.pos &&
        last_idx->track == idx.track && idx.time > last_idx->time) {
      GST_DEBUG_OBJECT (common, "Cue entry refers to same location, "
          "but has different time than previous entry; discarding");
      idx.track = 0;
    }
  }

  if ((ret == GST_FLOW_OK || ret == GST_FLOW_EOS)
      && idx.pos != (guint64) - 1 && idx.track > 0) {
    g_array_append_val (common->index, idx);
    (*nentries)++;
  } else if (ret == GST_FLOW_OK || ret == GST_FLOW_EOS) {
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

#if 0
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
#endif

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

GstFlowReturn
gst_matroska_read_common_parse_info (GstMatroskaReadCommon * common,
    GstElement * el, GstEbmlRead * ebml)
{
  GstFlowReturn ret = GST_FLOW_OK;
  gdouble dur_f = -1.0;
  guint32 id;

  DEBUG_ELEMENT_START (common, ebml, "SegmentInfo");

  if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK) {
    DEBUG_ELEMENT_STOP (common, ebml, "SegmentInfo", ret);
    return ret;
  }

  while (ret == GST_FLOW_OK && gst_ebml_read_has_remaining (ebml, 1, TRUE)) {
    if ((ret = gst_ebml_peek_id (ebml, &id)) != GST_FLOW_OK)
      break;

    switch (id) {
        /* cluster timecode */
      case GST_MATROSKA_ID_TIMECODESCALE:{
        guint64 num;

        if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK)
          break;


        GST_DEBUG_OBJECT (common, "TimeCodeScale: %" G_GUINT64_FORMAT, num);
        common->time_scale = num;
        break;
      }

      case GST_MATROSKA_ID_DURATION:{
        if ((ret = gst_ebml_read_float (ebml, &id, &dur_f)) != GST_FLOW_OK)
          break;

        if (dur_f <= 0.0) {
          GST_WARNING_OBJECT (common, "Invalid duration %lf", dur_f);
          break;
        }

        GST_DEBUG_OBJECT (common, "Duration: %lf", dur_f);
        break;
      }

      case GST_MATROSKA_ID_WRITINGAPP:{
        gchar *text;

        if ((ret = gst_ebml_read_utf8 (ebml, &id, &text)) != GST_FLOW_OK)
          break;

        GST_DEBUG_OBJECT (common, "WritingApp: %s", GST_STR_NULL (text));
        common->writing_app = text;
        break;
      }

      case GST_MATROSKA_ID_MUXINGAPP:{
        gchar *text;

        if ((ret = gst_ebml_read_utf8 (ebml, &id, &text)) != GST_FLOW_OK)
          break;

        GST_DEBUG_OBJECT (common, "MuxingApp: %s", GST_STR_NULL (text));
        common->muxing_app = text;
        break;
      }

      case GST_MATROSKA_ID_DATEUTC:{
        gint64 time;

        if ((ret = gst_ebml_read_date (ebml, &id, &time)) != GST_FLOW_OK)
          break;

        GST_DEBUG_OBJECT (common, "DateUTC: %" G_GINT64_FORMAT, time);
        common->created = time;
        break;
      }

      case GST_MATROSKA_ID_TITLE:{
        gchar *text;
        GstTagList *taglist;

        if ((ret = gst_ebml_read_utf8 (ebml, &id, &text)) != GST_FLOW_OK)
          break;

        GST_DEBUG_OBJECT (common, "Title: %s", GST_STR_NULL (text));
        taglist = gst_tag_list_new (GST_TAG_TITLE, text, NULL);
        gst_tag_list_set_scope (taglist, GST_TAG_SCOPE_GLOBAL);
        gst_matroska_read_common_found_global_tag (common, el, taglist);
        g_free (text);
        break;
      }

      default:
        ret = gst_matroska_read_common_parse_skip (common, ebml,
            "SegmentInfo", id);
        break;

        /* fall through */
      case GST_MATROSKA_ID_SEGMENTUID:
      case GST_MATROSKA_ID_SEGMENTFILENAME:
      case GST_MATROSKA_ID_PREVUID:
      case GST_MATROSKA_ID_PREVFILENAME:
      case GST_MATROSKA_ID_NEXTUID:
      case GST_MATROSKA_ID_NEXTFILENAME:
      case GST_MATROSKA_ID_SEGMENTFAMILY:
      case GST_MATROSKA_ID_CHAPTERTRANSLATE:
        ret = gst_ebml_read_skip (ebml);
        break;
    }
  }

  if (dur_f > 0.0) {
    GstClockTime dur_u;

    dur_u = gst_gdouble_to_guint64 (dur_f *
        gst_guint64_to_gdouble (common->time_scale));
    if (GST_CLOCK_TIME_IS_VALID (dur_u) && dur_u <= G_MAXINT64)
      common->segment.duration = dur_u;
  }

  DEBUG_ELEMENT_STOP (common, ebml, "SegmentInfo", ret);

  common->segmentinfo_parsed = TRUE;

  return ret;
}

static GstFlowReturn
gst_matroska_read_common_parse_metadata_id_simple_tag (GstMatroskaReadCommon *
    common, GstEbmlRead * ebml, GstTagList ** p_taglist)
{
  /* FIXME: check if there are more useful mappings */
  static const struct
  {
    const gchar *matroska_tagname;
    const gchar *gstreamer_tagname;
  }
  tag_conv[] = {
    {
    GST_MATROSKA_TAG_ID_TITLE, GST_TAG_TITLE}, {
    GST_MATROSKA_TAG_ID_ARTIST, GST_TAG_ARTIST}, {
    GST_MATROSKA_TAG_ID_AUTHOR, GST_TAG_ARTIST}, {
    GST_MATROSKA_TAG_ID_ALBUM, GST_TAG_ALBUM}, {
    GST_MATROSKA_TAG_ID_COMMENTS, GST_TAG_COMMENT}, {
    GST_MATROSKA_TAG_ID_BITSPS, GST_TAG_BITRATE}, {
    GST_MATROSKA_TAG_ID_BPS, GST_TAG_BITRATE}, {
    GST_MATROSKA_TAG_ID_ENCODER, GST_TAG_ENCODER}, {
    GST_MATROSKA_TAG_ID_DATE, GST_TAG_DATE}, {
    GST_MATROSKA_TAG_ID_ISRC, GST_TAG_ISRC}, {
    GST_MATROSKA_TAG_ID_COPYRIGHT, GST_TAG_COPYRIGHT}, {
    GST_MATROSKA_TAG_ID_BPM, GST_TAG_BEATS_PER_MINUTE}, {
    GST_MATROSKA_TAG_ID_TERMS_OF_USE, GST_TAG_LICENSE}, {
    GST_MATROSKA_TAG_ID_COMPOSER, GST_TAG_COMPOSER}, {
    GST_MATROSKA_TAG_ID_LEAD_PERFORMER, GST_TAG_PERFORMER}, {
    GST_MATROSKA_TAG_ID_GENRE, GST_TAG_GENRE}
  };
  GstFlowReturn ret;
  guint32 id;
  gchar *value = NULL;
  gchar *tag = NULL;

  DEBUG_ELEMENT_START (common, ebml, "SimpleTag");

  if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK) {
    DEBUG_ELEMENT_STOP (common, ebml, "SimpleTag", ret);
    return ret;
  }

  while (ret == GST_FLOW_OK && gst_ebml_read_has_remaining (ebml, 1, TRUE)) {
    /* read all sub-entries */

    if ((ret = gst_ebml_peek_id (ebml, &id)) != GST_FLOW_OK)
      break;

    switch (id) {
      case GST_MATROSKA_ID_TAGNAME:
        g_free (tag);
        tag = NULL;
        ret = gst_ebml_read_ascii (ebml, &id, &tag);
        GST_DEBUG_OBJECT (common, "TagName: %s", GST_STR_NULL (tag));
        break;

      case GST_MATROSKA_ID_TAGSTRING:
        g_free (value);
        value = NULL;
        ret = gst_ebml_read_utf8 (ebml, &id, &value);
        GST_DEBUG_OBJECT (common, "TagString: %s", GST_STR_NULL (value));
        break;

      default:
        ret = gst_matroska_read_common_parse_skip (common, ebml, "SimpleTag",
            id);
        break;
        /* fall-through */

      case GST_MATROSKA_ID_TAGLANGUAGE:
      case GST_MATROSKA_ID_TAGDEFAULT:
      case GST_MATROSKA_ID_TAGBINARY:
        ret = gst_ebml_read_skip (ebml);
        break;
    }
  }

  DEBUG_ELEMENT_STOP (common, ebml, "SimpleTag", ret);

  if (tag && value) {
    guint i;

    for (i = 0; i < G_N_ELEMENTS (tag_conv); i++) {
      const gchar *tagname_gst = tag_conv[i].gstreamer_tagname;

      const gchar *tagname_mkv = tag_conv[i].matroska_tagname;

      if (strcmp (tagname_mkv, tag) == 0) {
        GValue dest = { 0, };
        GType dest_type = gst_tag_get_type (tagname_gst);

        /* Ensure that any date string is complete */
        if (dest_type == G_TYPE_DATE) {
          guint year = 1901, month = 1, day = 1;

          /* Dates can be yyyy-MM-dd, yyyy-MM or yyyy, but we need
           * the first type */
          if (sscanf (value, "%04u-%02u-%02u", &year, &month, &day) != 0) {
            g_free (value);
            value = g_strdup_printf ("%04u-%02u-%02u", year, month, day);
          }
        }

        g_value_init (&dest, dest_type);
        if (gst_value_deserialize (&dest, value)) {
          gst_tag_list_add_values (*p_taglist, GST_TAG_MERGE_APPEND,
              tagname_gst, &dest, NULL);
        } else {
          GST_WARNING_OBJECT (common, "Can't transform tag '%s' with "
              "value '%s' to target type '%s'", tag, value,
              g_type_name (dest_type));
        }
        g_value_unset (&dest);
        break;
      }
    }
  }

  g_free (tag);
  g_free (value);

  return ret;
}

static GstFlowReturn
gst_matroska_read_common_parse_metadata_id_tag (GstMatroskaReadCommon * common,
    GstEbmlRead * ebml, GstTagList ** p_taglist)
{
  guint32 id;
  GstFlowReturn ret;
  GArray *chapter_targets, *edition_targets;
  GstTagList *taglist;
  GList *cur;

  DEBUG_ELEMENT_START (common, ebml, "Tag");

  if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK) {
    DEBUG_ELEMENT_STOP (common, ebml, "Tag", ret);
    return ret;
  }

  edition_targets = g_array_new (FALSE, FALSE, sizeof (guint64));
  chapter_targets = g_array_new (FALSE, FALSE, sizeof (guint64));
  taglist = gst_tag_list_new_empty ();

  while (ret == GST_FLOW_OK && gst_ebml_read_has_remaining (ebml, 1, TRUE)) {
    /* read all sub-entries */

    if ((ret = gst_ebml_peek_id (ebml, &id)) != GST_FLOW_OK)
      break;

    switch (id) {
      case GST_MATROSKA_ID_SIMPLETAG:
        ret = gst_matroska_read_common_parse_metadata_id_simple_tag (common,
            ebml, &taglist);
        break;

      case GST_MATROSKA_ID_TARGETS:
        ret =
            gst_matroska_read_common_parse_metadata_targets (common, ebml,
            edition_targets, chapter_targets);
        break;

      default:
        ret = gst_matroska_read_common_parse_skip (common, ebml, "Tag", id);
        break;
    }
  }

  DEBUG_ELEMENT_STOP (common, ebml, "Tag", ret);

  /* if tag is chapter/edition specific - try to find that entry */
  if (G_UNLIKELY (chapter_targets->len > 0 || edition_targets->len > 0)) {
    if (common->toc == NULL)
      GST_WARNING_OBJECT (common,
          "Found chapter/edition specific tag, but TOC doesn't present");
    else {
      cur = gst_toc_get_entries (common->toc);
      while (cur != NULL) {
        gst_matroska_read_common_parse_toc_tag (cur->data, edition_targets,
            chapter_targets, taglist);
        cur = cur->next;
      }
      common->toc_updated = TRUE;
    }
  } else
    gst_tag_list_insert (*p_taglist, taglist, GST_TAG_MERGE_APPEND);

  gst_tag_list_unref (taglist);
  g_array_unref (chapter_targets);
  g_array_unref (edition_targets);

  return ret;
}

GstFlowReturn
gst_matroska_read_common_parse_metadata (GstMatroskaReadCommon * common,
    GstElement * el, GstEbmlRead * ebml)
{
  GstTagList *taglist;
  GstFlowReturn ret = GST_FLOW_OK;
  guint32 id;
  GList *l;
  guint64 curpos;

  curpos = gst_ebml_read_get_pos (ebml);

  /* Make sure we don't parse a tags element twice and
   * post it's tags twice */
  curpos = gst_ebml_read_get_pos (ebml);
  for (l = common->tags_parsed; l; l = l->next) {
    guint64 *pos = l->data;

    if (*pos == curpos) {
      GST_DEBUG_OBJECT (common, "Skipping already parsed Tags at offset %"
          G_GUINT64_FORMAT, curpos);
      return GST_FLOW_OK;
    }
  }

  common->tags_parsed =
      g_list_prepend (common->tags_parsed, g_slice_new (guint64));
  *((guint64 *) common->tags_parsed->data) = curpos;
  /* fall-through */

  if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK) {
    DEBUG_ELEMENT_STOP (common, ebml, "Tags", ret);
    return ret;
  }

  taglist = gst_tag_list_new_empty ();
  gst_tag_list_set_scope (taglist, GST_TAG_SCOPE_GLOBAL);
  common->toc_updated = FALSE;

  while (ret == GST_FLOW_OK && gst_ebml_read_has_remaining (ebml, 1, TRUE)) {
    if ((ret = gst_ebml_peek_id (ebml, &id)) != GST_FLOW_OK)
      break;

    switch (id) {
      case GST_MATROSKA_ID_TAG:
        ret = gst_matroska_read_common_parse_metadata_id_tag (common, ebml,
            &taglist);
        break;

      default:
        ret = gst_matroska_read_common_parse_skip (common, ebml, "Tags", id);
        break;
        /* FIXME: Use to limit the tags to specific pads */
    }
  }

  DEBUG_ELEMENT_STOP (common, ebml, "Tags", ret);

  if (G_LIKELY (!gst_tag_list_is_empty (taglist)))
    gst_matroska_read_common_found_global_tag (common, el, taglist);
  else
    gst_tag_list_unref (taglist);

  return ret;
}

static GstFlowReturn
gst_matroska_read_common_peek_adapter (GstMatroskaReadCommon * common, guint
    peek, const guint8 ** data)
{
  /* Caller needs to gst_adapter_unmap. */
  *data = gst_adapter_map (common->adapter, peek);
  if (*data == NULL)
    return GST_FLOW_EOS;

  return GST_FLOW_OK;
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
    gsize cache_size = gst_buffer_get_size (common->cached_buffer);

    if (cache_offset <= common->offset &&
        (common->offset + size) <= (cache_offset + cache_size)) {
      if (p_buf)
        *p_buf = gst_buffer_copy_region (common->cached_buffer,
            GST_BUFFER_COPY_ALL, common->offset - cache_offset, size);
      if (bytes) {
        if (!common->cached_data) {
          gst_buffer_map (common->cached_buffer, &common->cached_map,
              GST_MAP_READ);
          common->cached_data = common->cached_map.data;
        }
        *bytes = common->cached_data + common->offset - cache_offset;
      }
      return GST_FLOW_OK;
    }
    /* not enough data in the cache, free cache and get a new one */
    if (common->cached_data) {
      gst_buffer_unmap (common->cached_buffer, &common->cached_map);
      common->cached_data = NULL;
    }
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

  if (gst_buffer_get_size (common->cached_buffer) >= size) {
    if (p_buf)
      *p_buf = gst_buffer_copy_region (common->cached_buffer,
          GST_BUFFER_COPY_ALL, 0, size);
    if (bytes) {
      gst_buffer_map (common->cached_buffer, &common->cached_map, GST_MAP_READ);
      common->cached_data = common->cached_map.data;
      *bytes = common->cached_data;
    }
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

  if (gst_buffer_get_size (common->cached_buffer) < size) {
    GST_WARNING_OBJECT (common, "Dropping short buffer at offset %"
        G_GUINT64_FORMAT ": wanted %u bytes, got %" G_GSIZE_FORMAT " bytes",
        common->offset, size, gst_buffer_get_size (common->cached_buffer));

    gst_buffer_unref (common->cached_buffer);
    common->cached_buffer = NULL;
    if (p_buf)
      *p_buf = NULL;
    if (bytes)
      *bytes = NULL;
    return GST_FLOW_EOS;
  }

  if (p_buf)
    *p_buf = gst_buffer_copy_region (common->cached_buffer,
        GST_BUFFER_COPY_ALL, 0, size);
  if (bytes) {
    gst_buffer_map (common->cached_buffer, &common->cached_map, GST_MAP_READ);
    common->cached_data = common->cached_map.data;
    *bytes = common->cached_data;
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_matroska_read_common_peek_pull (GstMatroskaReadCommon * common, guint peek,
    guint8 ** data)
{
  return gst_matroska_read_common_peek_bytes (common, common->offset, peek,
      NULL, data);
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
  GstFlowReturn ret;

  ret = gst_ebml_peek_id_length (_id, _length, _needed,
      (GstPeekData) gst_matroska_read_common_peek_adapter, (gpointer) common,
      el, common->offset);

  gst_adapter_unmap (common->adapter);

  return ret;
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
  if (ret != GST_FLOW_OK && ret != GST_FLOW_EOS)
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
  if (ret != GST_FLOW_OK && ret != GST_FLOW_EOS)
    return ret;

  /* Sort encodings according to their order */
  g_array_sort (context->encodings,
      (GCompareFunc) gst_matroska_read_common_encoding_cmp);

  return gst_matroska_decode_content_encodings (context->encodings);
}

/* call with object lock held */
void
gst_matroska_read_common_reset_streams (GstMatroskaReadCommon * common,
    GstClockTime time, gboolean full)
{
  gint i;

  GST_DEBUG_OBJECT (common, "resetting stream state");

  g_assert (common->src->len == common->num_streams);
  for (i = 0; i < common->src->len; i++) {
    GstMatroskaTrackContext *context = g_ptr_array_index (common->src, i);
    context->pos = time;
    context->set_discont = TRUE;
    context->eos = FALSE;
    context->from_time = GST_CLOCK_TIME_NONE;
    if (full)
      context->last_flow = GST_FLOW_OK;
    if (context->type == GST_MATROSKA_TRACK_TYPE_VIDEO) {
      GstMatroskaTrackVideoContext *videocontext =
          (GstMatroskaTrackVideoContext *) context;
      /* demux object lock held by caller */
      videocontext->earliest_time = GST_CLOCK_TIME_NONE;
    }
  }
}

gboolean
gst_matroska_read_common_tracknumber_unique (GstMatroskaReadCommon * common,
    guint64 num)
{
  gint i;

  g_assert (common->src->len == common->num_streams);
  for (i = 0; i < common->src->len; i++) {
    GstMatroskaTrackContext *context = g_ptr_array_index (common->src, i);

    if (context->num == num)
      return FALSE;
  }

  return TRUE;
}
