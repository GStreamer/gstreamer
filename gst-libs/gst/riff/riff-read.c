/* GStreamer RIFF I/O
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * riff-read.c: RIFF input file parsing
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
#include <gst/gstutils.h>

#include "riff-read.h"

GST_DEBUG_CATEGORY_EXTERN (riff_debug);
#define GST_CAT_DEFAULT riff_debug

/**
 * gst_riff_read_chunk:
 * @element: caller element (used for debugging).
 * @pad: pad to pull data from.
 * @offset: offset to pull from, incremented by this function.
 * @tag: fourcc of the chunk (returned by this function).
 * @chunk_data: buffer (returned by this function).
 *
 * Reads a single chunk of data.
 *
 * Returns: flow status.
 */

GstFlowReturn
gst_riff_read_chunk (GstElement * element,
    GstPad * pad, guint64 * _offset, guint32 * tag, GstBuffer ** _chunk_data)
{
  GstBuffer *buf;
  GstFlowReturn res;
  guint size;
  guint64 offset = *_offset;
  gchar dbg[5] = { 0, };

  if ((res = gst_pad_pull_range (pad, offset, 8, &buf)) != GST_FLOW_OK)
    return res;
  else if (!buf || GST_BUFFER_SIZE (buf) < 8) {
    if (buf)
      gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }

  *tag = GST_READ_UINT32_LE (GST_BUFFER_DATA (buf));
  size = GST_READ_UINT32_LE (GST_BUFFER_DATA (buf) + 4);
  gst_buffer_unref (buf);

  memcpy (dbg, tag, 4);
  GST_DEBUG_OBJECT (element, "tag=%s, size=%u", dbg, size);

  if ((res = gst_pad_pull_range (pad, offset + 8, size, &buf)) != GST_FLOW_OK)
    return res;
  else if (!buf || GST_BUFFER_SIZE (buf) < size) {
    GST_DEBUG_OBJECT (element, "not enough data (available=%u, needed=%u)",
        (buf) ? GST_BUFFER_SIZE (buf) : 0, size);
    if (buf)
      gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }

  *_chunk_data = buf;
  *_offset += 8 + ((size + 1) & ~1);

  return GST_FLOW_OK;
}

/**
 * gst_riff_parse_chunk:
 * @element: caller element (used for debugging).
 * @buf: input buffer.
 * @offset: offset in the buffer in the caller. Is incremented
 *          by the read size by this function.
 * @fourcc: fourcc (returned by this function0 of the chunk.
 * @chunk_data: buffer (returned by the function) containing the
 *              chunk data.
 *
 * Reads a single chunk.
 *
 * Returns: the fourcc tag of this chunk, or 0 on error.
 */

gboolean
gst_riff_parse_chunk (GstElement * element, GstBuffer * buf,
    guint * _offset, guint32 * _fourcc, GstBuffer ** chunk_data)
{
  guint size;
  guint32 fourcc;
  guint8 *data;
  gchar dbg[5] = { 0, };
  guint offset = *_offset;

  *chunk_data = NULL;
  *_fourcc = 0;

  if (buf && GST_BUFFER_SIZE (buf) == offset) {
    GST_DEBUG_OBJECT (element, "End of chunk (offset %d)", offset);
    return FALSE;
  }

  if (!buf || GST_BUFFER_SIZE (buf) < offset + 8) {
    GST_DEBUG_OBJECT (element,
        "Failed to parse chunk header (offset %d, %d available, %d needed)",
        offset, (buf) ? GST_BUFFER_SIZE (buf) : 0, 8);
    return FALSE;
  }

  /* read header */
  data = GST_BUFFER_DATA (buf) + offset;
  fourcc = GST_READ_UINT32_LE (data);
  size = GST_READ_UINT32_LE (data + 4);

  memcpy (dbg, data, 4);
  GST_DEBUG_OBJECT (element, "fourcc=%s, size=%u", dbg, size);

  if (GST_BUFFER_SIZE (buf) < size + 8 + offset) {
    GST_DEBUG_OBJECT (element,
        "Needed chunk data (%d) is more than available (%d), shortcutting",
        size, GST_BUFFER_SIZE (buf) - 8 - offset);
    size = GST_BUFFER_SIZE (buf) - 8 - offset;
  }

  if (size)
    *chunk_data = gst_buffer_create_sub (buf, offset + 8, size);
  else
    *chunk_data = NULL;
  *_fourcc = fourcc;
  *_offset += 8 + ((size + 1) & ~1);

  return TRUE;
}

/**
 * gst_riff_parse_file_header:
 * @element: caller element (used for debugging/error).
 * @buf: input buffer from which the file header will be parsed,
 *       should be at least 12 bytes long.
 * @doctype: a fourcc (returned by this function) to indicate the
 *           type of document (according to the header).
 *
 * Reads the first few bytes from the provided buffer, checks
 * if this stream is a RIFF stream, and determines document type.
 * The input data is discarded after use.
 *
 * Returns: FALSE if this is not a RIFF stream (in which case the
 * caller should error out; we already throw an error), or TRUE
 * if it is.
 */

gboolean
gst_riff_parse_file_header (GstElement * element,
    GstBuffer * buf, guint32 * doctype)
{
  guint8 *data = GST_BUFFER_DATA (buf);
  guint32 tag;

  if (!buf || GST_BUFFER_SIZE (buf) < 12) {
    GST_ELEMENT_ERROR (element, STREAM, WRONG_TYPE, (NULL),
        ("Not enough data to parse RIFF header (%d available, %d needed)",
            buf ? GST_BUFFER_SIZE (buf) : 0, 12));
    if (buf)
      gst_buffer_unref (buf);
    return FALSE;
  }

  tag = GST_READ_UINT32_LE (data);
  if (tag != GST_RIFF_TAG_RIFF) {
    GST_ELEMENT_ERROR (element, STREAM, WRONG_TYPE, (NULL),
        ("Stream is no RIFF stream: %" GST_FOURCC_FORMAT,
            GST_FOURCC_ARGS (tag)));
    gst_buffer_unref (buf);
    return FALSE;
  }

  *doctype = GST_READ_UINT32_LE (data + 8);

  gst_buffer_unref (buf);

  return TRUE;
}

/**
 * gst_riff_parse_strh:
 * @element: caller element (used for debugging/error).
 * @buf: input data to be used for parsing, stripped from header.
 * @strh: a pointer (returned by this function) to a filled-in
 *        strh structure. Caller should free it.
 *
 * Parses a strh structure from input data. The input data is
 * discarded after use.
 *
 * Returns: TRUE if parsing succeeded, otherwise FALSE. The stream
 *          should be skipped on error, but it is not fatal.
 */

gboolean
gst_riff_parse_strh (GstElement * element,
    GstBuffer * buf, gst_riff_strh ** _strh)
{
  gst_riff_strh *strh;

  if (!buf || GST_BUFFER_SIZE (buf) < sizeof (gst_riff_strh)) {
    GST_ERROR_OBJECT (element,
        "Too small strh (%d available, %d needed)",
        buf ? GST_BUFFER_SIZE (buf) : 0, (int) sizeof (gst_riff_strh));
    if (buf)
      gst_buffer_unref (buf);
    return FALSE;
  }

  strh = g_memdup (GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));
  gst_buffer_unref (buf);

#if (G_BYTE_ORDER == G_BIG_ENDIAN)
  strh->type = GUINT32_FROM_LE (strh->type);
  strh->fcc_handler = GUINT32_FROM_LE (strh->fcc_handler);
  strh->flags = GUINT32_FROM_LE (strh->flags);
  strh->priority = GUINT32_FROM_LE (strh->priority);
  strh->init_frames = GUINT32_FROM_LE (strh->init_frames);
  strh->scale = GUINT32_FROM_LE (strh->scale);
  strh->rate = GUINT32_FROM_LE (strh->rate);
  strh->start = GUINT32_FROM_LE (strh->start);
  strh->length = GUINT32_FROM_LE (strh->length);
  strh->bufsize = GUINT32_FROM_LE (strh->bufsize);
  strh->quality = GUINT32_FROM_LE (strh->quality);
  strh->samplesize = GUINT32_FROM_LE (strh->samplesize);
#endif

  /* avoid divisions by zero */
  if (!strh->scale)
    strh->scale = 1;
  if (!strh->rate)
    strh->rate = 1;

  /* debug */
  GST_INFO_OBJECT (element, "strh tag found:");
  GST_INFO_OBJECT (element, " type        %" GST_FOURCC_FORMAT,
      GST_FOURCC_ARGS (strh->type));
  GST_INFO_OBJECT (element, " fcc_handler %" GST_FOURCC_FORMAT,
      GST_FOURCC_ARGS (strh->fcc_handler));
  GST_INFO_OBJECT (element, " flags       0x%08x", strh->flags);
  GST_INFO_OBJECT (element, " priority    %d", strh->priority);
  GST_INFO_OBJECT (element, " init_frames %d", strh->init_frames);
  GST_INFO_OBJECT (element, " scale       %d", strh->scale);
  GST_INFO_OBJECT (element, " rate        %d", strh->rate);
  GST_INFO_OBJECT (element, " start       %d", strh->start);
  GST_INFO_OBJECT (element, " length      %d", strh->length);
  GST_INFO_OBJECT (element, " bufsize     %d", strh->bufsize);
  GST_INFO_OBJECT (element, " quality     %d", strh->quality);
  GST_INFO_OBJECT (element, " samplesize  %d", strh->samplesize);

  *_strh = strh;

  return TRUE;
}

/**
 * gst_riff_parse_strf_vids:
 * @element: caller element (used for debugging/error).
 * @buf: input data to be used for parsing, stripped from header.
 * @strf: a pointer (returned by this function) to a filled-in
 *        strf/vids structure. Caller should free it.
 * @data: a pointer (returned by this function) to a buffer
 *        containing extradata for this particular stream (e.g.
 *        palette, codec initialization data).
 *
 * Parses a video stream´s strf structure plus optionally some
 * extradata from input data. The input data is discarded after
 * use.
 *
 * Returns: TRUE if parsing succeeded, otherwise FALSE. The stream
 *          should be skipped on error, but it is not fatal.
 */

gboolean
gst_riff_parse_strf_vids (GstElement * element,
    GstBuffer * buf, gst_riff_strf_vids ** _strf, GstBuffer ** data)
{
  gst_riff_strf_vids *strf;

  if (!buf || GST_BUFFER_SIZE (buf) < sizeof (gst_riff_strf_vids)) {
    GST_ERROR_OBJECT (element,
        "Too small strf_vids (%d available, %d needed)",
        buf ? GST_BUFFER_SIZE (buf) : 0, (int) sizeof (gst_riff_strf_vids));
    if (buf)
      gst_buffer_unref (buf);
    return FALSE;
  }

  strf = g_memdup (GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));

#if (G_BYTE_ORDER == G_BIG_ENDIAN)
  strf->size = GUINT32_FROM_LE (strf->size);
  strf->width = GUINT32_FROM_LE (strf->width);
  strf->height = GUINT32_FROM_LE (strf->height);
  strf->planes = GUINT16_FROM_LE (strf->planes);
  strf->bit_cnt = GUINT16_FROM_LE (strf->bit_cnt);
  strf->compression = GUINT32_FROM_LE (strf->compression);
  strf->image_size = GUINT32_FROM_LE (strf->image_size);
  strf->xpels_meter = GUINT32_FROM_LE (strf->xpels_meter);
  strf->ypels_meter = GUINT32_FROM_LE (strf->ypels_meter);
  strf->num_colors = GUINT32_FROM_LE (strf->num_colors);
  strf->imp_colors = GUINT32_FROM_LE (strf->imp_colors);
#endif

  /* size checking */
  *data = NULL;
  if (strf->size > GST_BUFFER_SIZE (buf)) {
    GST_WARNING_OBJECT (element,
        "strf_vids header gave %d bytes data, only %d available",
        strf->size, GST_BUFFER_SIZE (buf));
    strf->size = GST_BUFFER_SIZE (buf);
  } else if (strf->size < GST_BUFFER_SIZE (buf)) {
    *data = gst_buffer_create_sub (buf, strf->size,
        GST_BUFFER_SIZE (buf) - strf->size);
  }

  /* debug */
  GST_INFO_OBJECT (element, "strf tag found in context vids:");
  GST_INFO_OBJECT (element, " size        %d", strf->size);
  GST_INFO_OBJECT (element, " width       %d", strf->width);
  GST_INFO_OBJECT (element, " height      %d", strf->height);
  GST_INFO_OBJECT (element, " planes      %d", strf->planes);
  GST_INFO_OBJECT (element, " bit_cnt     %d", strf->bit_cnt);
  GST_INFO_OBJECT (element, " compression %" GST_FOURCC_FORMAT,
      GST_FOURCC_ARGS (strf->compression));
  GST_INFO_OBJECT (element, " image_size  %d", strf->image_size);
  GST_INFO_OBJECT (element, " xpels_meter %d", strf->xpels_meter);
  GST_INFO_OBJECT (element, " ypels_meter %d", strf->ypels_meter);
  GST_INFO_OBJECT (element, " num_colors  %d", strf->num_colors);
  GST_INFO_OBJECT (element, " imp_colors  %d", strf->imp_colors);
  if (*data)
    GST_INFO_OBJECT (element, " %d bytes extradata", GST_BUFFER_SIZE (*data));

  gst_buffer_unref (buf);

  *_strf = strf;

  return TRUE;
}

/**
 * gst_riff_parse_strf_auds:
 * @element: caller element (used for debugging/error).
 * @buf: input data to be used for parsing, stripped from header.
 * @strf: a pointer (returned by this function) to a filled-in
 *        strf/auds structure. Caller should free it.
 * @data: a pointer (returned by this function) to a buffer
 *        containing extradata for this particular stream (e.g.
 *        codec initialization data).
 *
 * Parses an audio stream´s strf structure plus optionally some
 * extradata from input data. The input data is discarded after
 * use.
 *
 * Returns: TRUE if parsing succeeded, otherwise FALSE. The stream
 *          should be skipped on error, but it is not fatal.
 */

gboolean
gst_riff_parse_strf_auds (GstElement * element,
    GstBuffer * buf, gst_riff_strf_auds ** _strf, GstBuffer ** data)
{
  gst_riff_strf_auds *strf;

  if (!buf || GST_BUFFER_SIZE (buf) < sizeof (gst_riff_strf_auds)) {
    GST_ERROR_OBJECT (element,
        "Too small strf_auds (%d available, %d needed)",
        buf ? GST_BUFFER_SIZE (buf) : 0, (int) sizeof (gst_riff_strf_auds));
    if (buf)
      gst_buffer_unref (buf);
    return FALSE;
  }

  strf = g_memdup (GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));

#if (G_BYTE_ORDER == G_BIG_ENDIAN)
  strf->format = GUINT16_FROM_LE (strf->format);
  strf->channels = GUINT16_FROM_LE (strf->channels);
  strf->rate = GUINT32_FROM_LE (strf->rate);
  strf->av_bps = GUINT32_FROM_LE (strf->av_bps);
  strf->blockalign = GUINT16_FROM_LE (strf->blockalign);
  strf->size = GUINT16_FROM_LE (strf->size);
#endif

  /* size checking */
  *data = NULL;
  if (GST_BUFFER_SIZE (buf) > sizeof (gst_riff_strf_auds) + 2) {
    gint len;

    len = GST_READ_UINT16_LE (&GST_BUFFER_DATA (buf)[16]);
    if (len + 2 + sizeof (gst_riff_strf_auds) > GST_BUFFER_SIZE (buf)) {
      GST_WARNING_OBJECT (element,
          "Extradata indicated %d bytes, but only %d available",
          len, GST_BUFFER_SIZE (buf) - 2 - sizeof (gst_riff_strf_auds));
      len = GST_BUFFER_SIZE (buf) - 2 - sizeof (gst_riff_strf_auds);
    }
    *data = gst_buffer_create_sub (buf, sizeof (gst_riff_strf_auds) + 2, len);
  }

  /* debug */
  GST_INFO_OBJECT (element, "strf tag found in context auds:");
  GST_INFO_OBJECT (element, " format      %d", strf->format);
  GST_INFO_OBJECT (element, " channels    %d", strf->channels);
  GST_INFO_OBJECT (element, " rate        %d", strf->rate);
  GST_INFO_OBJECT (element, " av_bps      %d", strf->av_bps);
  GST_INFO_OBJECT (element, " blockalign  %d", strf->blockalign);
  GST_INFO_OBJECT (element, " size        %d", strf->size);
  if (*data)
    GST_INFO_OBJECT (element, " %d bytes extradata", GST_BUFFER_SIZE (*data));

  gst_buffer_unref (buf);

  *_strf = strf;

  return TRUE;
}

/**
 * gst_riff_parse_strf_iavs:
 * @element: caller element (used for debugging/error).
 * @buf: input data to be used for parsing, stripped from header.
 * @strf: a pointer (returned by this function) to a filled-in
 *        strf/iavs structure. Caller should free it.
 * @data: a pointer (returned by this function) to a buffer
 *        containing extradata for this particular stream (e.g.
 *        codec initialization data).
 *
 * Parses a interleaved (also known as "complex")  stream´s strf
 * structure plus optionally some extradata from input data. The
 * input data is discarded after use.
 *
 * Returns: TRUE if parsing succeeded, otherwise FALSE.
 */

gboolean
gst_riff_parse_strf_iavs (GstElement * element,
    GstBuffer * buf, gst_riff_strf_iavs ** _strf, GstBuffer ** data)
{
  gst_riff_strf_iavs *strf;

  if (!buf || GST_BUFFER_SIZE (buf) < sizeof (gst_riff_strf_iavs)) {
    GST_ERROR_OBJECT (element,
        "Too small strf_iavs (%d available, %d needed)",
        buf ? GST_BUFFER_SIZE (buf) : 0, (int) sizeof (gst_riff_strf_iavs));
    gst_buffer_unref (buf);
    return FALSE;
  }

  strf = g_memdup (GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));
  gst_buffer_unref (buf);

#if (G_BYTE_ORDER == G_BIG_ENDIAN)
  strf->DVAAuxSrc = GUINT32_FROM_LE (strf->DVAAuxSrc);
  strf->DVAAuxCtl = GUINT32_FROM_LE (strf->DVAAuxCtl);
  strf->DVAAuxSrc1 = GUINT32_FROM_LE (strf->DVAAuxSrc1);
  strf->DVAAuxCtl1 = GUINT32_FROM_LE (strf->DVAAuxCtl1);
  strf->DVVAuxSrc = GUINT32_FROM_LE (strf->DVVAuxSrc);
  strf->DVVAuxCtl = GUINT32_FROM_LE (strf->DVVAuxCtl);
  strf->DVReserved1 = GUINT32_FROM_LE (strf->DVReserved1);
  strf->DVReserved2 = GUINT32_FROM_LE (strf->DVReserved2);
#endif

  /* debug */
  GST_INFO_OBJECT (element, "strf tag found in context iavs:");
  GST_INFO_OBJECT (element, " DVAAuxSrc   %08x", strf->DVAAuxSrc);
  GST_INFO_OBJECT (element, " DVAAuxCtl   %08x", strf->DVAAuxCtl);
  GST_INFO_OBJECT (element, " DVAAuxSrc1  %08x", strf->DVAAuxSrc1);
  GST_INFO_OBJECT (element, " DVAAuxCtl1  %08x", strf->DVAAuxCtl1);
  GST_INFO_OBJECT (element, " DVVAuxSrc   %08x", strf->DVVAuxSrc);
  GST_INFO_OBJECT (element, " DVVAuxCtl   %08x", strf->DVVAuxCtl);
  GST_INFO_OBJECT (element, " DVReserved1 %08x", strf->DVReserved1);
  GST_INFO_OBJECT (element, " DVReserved2 %08x", strf->DVReserved2);

  *_strf = strf;
  *data = NULL;

  return TRUE;
}

/**
 * gst_riff_parse_info:
 * @element: caller element (used for debugging/error).
 * @buf: input data to be used for parsing, stripped from header.
 * @taglist: a pointer to a taglist (returned by this function)
 *           containing information about this stream. May be
 *           NULL if no supported tags were found.
 *
 * Parses stream metadata from input data. The input data is
 * discarded after use.
 */

void
gst_riff_parse_info (GstElement * element,
    GstBuffer * buf, GstTagList ** _taglist)
{
  guint8 *data;
  guint size, tsize;
  guint32 tag;
  const gchar *type;
  gchar *name;
  GstTagList *taglist;
  gboolean have_tags = FALSE;

  if (!buf) {
    *_taglist = NULL;
    return;
  }
  data = GST_BUFFER_DATA (buf);
  size = GST_BUFFER_SIZE (buf);
  taglist = gst_tag_list_new ();

  while (size > 8) {
    tag = GST_READ_UINT32_LE (data);
    tsize = GST_READ_UINT32_LE (data + 4);
    size -= 8;
    data += 8;
    if (tsize > size) {
      GST_WARNING_OBJECT (element,
          "Tagsize %d is larger than available data %d", tsize, size);
      tsize = size;
    }

    /* find out the type of metadata */
    switch (tag) {
      case GST_RIFF_INFO_IARL:
        type = GST_TAG_LOCATION;
        break;
      case GST_RIFF_INFO_IART:
        type = GST_TAG_ARTIST;
        break;
      case GST_RIFF_INFO_ICMS:
        type = NULL;            /*"Commissioner"; */
        break;
      case GST_RIFF_INFO_ICMT:
        type = GST_TAG_COMMENT;
        break;
      case GST_RIFF_INFO_ICOP:
        type = GST_TAG_COPYRIGHT;
        break;
      case GST_RIFF_INFO_ICRD:
        type = GST_TAG_DATE;
        break;
      case GST_RIFF_INFO_ICRP:
        type = NULL;            /*"Cropped"; */
        break;
      case GST_RIFF_INFO_IDIM:
        type = NULL;            /*"Dimensions"; */
        break;
      case GST_RIFF_INFO_IDPI:
        type = NULL;            /*"Dots per Inch"; */
        break;
      case GST_RIFF_INFO_IENG:
        type = NULL;            /*"Engineer"; */
        break;
      case GST_RIFF_INFO_IGNR:
        type = GST_TAG_GENRE;
        break;
      case GST_RIFF_INFO_IKEY:
        type = NULL; /*"Keywords"; */ ;
        break;
      case GST_RIFF_INFO_ILGT:
        type = NULL;            /*"Lightness"; */
        break;
      case GST_RIFF_INFO_IMED:
        type = NULL;            /*"Medium"; */
        break;
      case GST_RIFF_INFO_INAM:
        type = GST_TAG_TITLE;
        break;
      case GST_RIFF_INFO_IPLT:
        type = NULL;            /*"Palette"; */
        break;
      case GST_RIFF_INFO_IPRD:
        type = NULL;            /*"Product"; */
        break;
      case GST_RIFF_INFO_ISBJ:
        type = NULL;            /*"Subject"; */
        break;
      case GST_RIFF_INFO_ISFT:
        type = GST_TAG_ENCODER;
        break;
      case GST_RIFF_INFO_ISHP:
        type = NULL;            /*"Sharpness"; */
        break;
      case GST_RIFF_INFO_ISRC:
        type = GST_TAG_ISRC;
        break;
      case GST_RIFF_INFO_ISRF:
        type = NULL;            /*"Source Form"; */
        break;
      case GST_RIFF_INFO_ITCH:
        type = NULL;            /*"Technician"; */
        break;
      default:
        type = NULL;
        GST_WARNING_OBJECT (element,
            "Unknown INFO (metadata) tag entry %" GST_FOURCC_FORMAT,
            GST_FOURCC_ARGS (tag));
        break;
    }

    if (type) {
      if (data[0] != '\0') {
        /* read, NULL-terminate */
        name = g_new (gchar, tsize + 1);
        name[tsize] = '\0';
        memcpy (name, data, tsize);

        /* add to list */
        have_tags = TRUE;
        gst_tag_list_add (taglist, GST_TAG_MERGE_APPEND, type, name, NULL);
        g_free (name);
      }
    }

    data += tsize;
    size -= tsize;
  }

  if (have_tags) {
    *_taglist = taglist;
  } else {
    *_taglist = NULL;
    gst_tag_list_free (taglist);
  }

  return;
}
