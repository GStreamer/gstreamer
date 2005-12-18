/* -*- Mode: C; tab-width: 2; indent-tabs-mode: t; c-basic-offset: 2 -*- */
/* Copyright 2005 Jan Schmidt <thaytan@mad.scientist.com>
 * Copyright 2002,2003 Scott Wheeler <wheeler@kde.org> (portions from taglib)
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
#include <gst/tag/tag.h>

#include "id3tags.h"

GST_DEBUG_CATEGORY_EXTERN (id3demux_debug);
#define GST_CAT_DEFAULT (id3demux_debug)

#define HANDLE_INVALID_SYNCSAFE
static ID3TagsResult
id3demux_id3v2_frames_to_tag_list (ID3TagsWorking * work, guint size);

guint
read_synch_uint (guint8 * data, guint size)
{
  gint i;
  guint result = 0;
  gint invalid = 0;

  g_assert (size <= 4);

  size--;
  for (i = 0; i <= size; i++) {
    invalid |= data[i] & 0x80;
    result |= (data[i] & 0x7f) << ((size - i) * 7);
  }

#ifdef HANDLE_INVALID_SYNCSAFE
  if (invalid) {
    GST_WARNING ("Invalid synch-safe integer in ID3v2 frame "
        "- using the actual value instead");
    result = 0;
    for (i = 0; i <= size; i++) {
      result |= data[i] << ((size - i) * 8);
    }
  }
#endif
  return result;
}

ID3TagsResult
id3demux_read_id3v1_tag (GstBuffer * buffer, guint * id3v1_size,
    GstTagList ** tags)
{
  GstTagList *new_tags;

  guint8 *data;

  g_return_val_if_fail (buffer != NULL, ID3TAGS_V1_BAD_SIZE);

  data = GST_BUFFER_DATA (buffer);

  if (GST_BUFFER_SIZE (buffer) != ID3V1_TAG_SIZE)
    return ID3TAGS_V1_BAD_SIZE;

  /* Check that buffer starts with 'TAG' */
  if (data[0] != 'T' || data[1] != 'A' || data[2] != 'G') {
    if (id3v1_size)
      *id3v1_size = 0;
    GST_DEBUG ("No ID3v1 tag in data");
    return ID3TAGS_READ_TAG;
  }

  g_return_val_if_fail (tags != NULL, ID3TAGS_READ_TAG);

  new_tags = gst_tag_list_new_from_id3v1 (GST_BUFFER_DATA (buffer));
  if (new_tags == NULL)
    return ID3TAGS_BROKEN_TAG;

  if (*tags) {
    GstTagList *merged;

    merged = gst_tag_list_merge (*tags, new_tags, GST_TAG_MERGE_REPLACE);
    gst_tag_list_free (*tags);
    gst_tag_list_free (new_tags);
    *tags = merged;
  } else
    *tags = new_tags;

  return ID3TAGS_READ_TAG;
}

ID3TagsResult
id3demux_read_id3v2_tag (GstBuffer * buffer, guint * id3v2_size,
    GstTagList ** tags)
{
  guint8 *data;
  guint read_size;
  ID3TagsWorking work;
  guint8 flags;
  ID3TagsResult result;
  guint16 version;

  g_return_val_if_fail (buffer != NULL, ID3TAGS_MORE_DATA);

  if (GST_BUFFER_SIZE (buffer) < ID3V2_MARK_SIZE)
    return ID3TAGS_MORE_DATA;   /* Need more data to decide with */

  data = GST_BUFFER_DATA (buffer);

  /* Check for 'ID3' string at start of buffer */
  if (data[0] != 'I' || data[1] != 'D' || data[2] != '3') {
    if (id3v2_size)
      *id3v2_size = 0;
    GST_DEBUG ("No ID3v2 tag in data");
    return ID3TAGS_READ_TAG;
  }

  /* OK, get enough data to read the entire header */
  if (GST_BUFFER_SIZE (buffer) < ID3V2_HDR_SIZE)
    return ID3TAGS_MORE_DATA;   /* Need more data to decide with */

  /* Read the version */
  version = GST_READ_UINT16_BE (data + 3);

  /* Read the flags */
  flags = data[5];

  /* Read the size from the header */
  read_size = read_synch_uint (data + 6, 4);
  if (read_size == 0) {
    return ID3TAGS_BROKEN_TAG;
  }
  read_size += 10;

  /* Expand the read size to include a footer if there is one */
  if (flags & ID3V2_HDR_FLAG_FOOTER) {
    read_size += 10;
  }

  if (id3v2_size)
    *id3v2_size = read_size;

  /* Validate the version. At the moment, we only support up to 2.4.0 */
  if (ID3V2_VER_MAJOR (version) > 4 || ID3V2_VER_MINOR (version) > 0) {
    GST_WARNING ("ID3v2 tag is from revision 2.%d.%d, "
        "but decoder only supports 2.%d.%d. Ignoring as per spec.",
        version >> 8, version & 0xff, ID3V2_VERSION >> 8, ID3V2_VERSION & 0xff);
    return ID3TAGS_READ_TAG;
  }
  GST_DEBUG ("ID3v2 tag with revision 2.%d.%d\n", version >> 8, version & 0xff);

  if (GST_BUFFER_SIZE (buffer) < read_size)
    return ID3TAGS_MORE_DATA;   /* Need more data to decode with */

  g_return_val_if_fail (tags != NULL, ID3TAGS_READ_TAG);

  memset (&work, 0, sizeof (ID3TagsWorking));
  work.buffer = buffer;
  work.hdr.version = version;
  work.hdr.size = read_size;
  work.hdr.flags = flags;
  work.hdr.frame_data = GST_BUFFER_DATA (buffer) + ID3V2_HDR_SIZE;
  if (flags & ID3V2_HDR_FLAG_FOOTER)
    work.hdr.frame_data_size = read_size - ID3V2_HDR_SIZE - 10;
  else
    work.hdr.frame_data_size = read_size - ID3V2_HDR_SIZE;

  result = id3demux_id3v2_frames_to_tag_list (&work, read_size);

  /* Actually read the tags */
  if (work.tags != NULL) {
    if (*tags) {
      GstTagList *merged;

      merged = gst_tag_list_merge (*tags, work.tags, GST_TAG_MERGE_REPLACE);
      gst_tag_list_free (*tags);
      gst_tag_list_free (work.tags);
      *tags = merged;
    } else
      *tags = work.tags;
  }

  return result;
}

static guint
id3demux_id3v2_frame_hdr_size (guint id3v2ver)
{
  /* ID3v2 < 2.3.0 only had 6 byte header */
  switch (ID3V2_VER_MAJOR (id3v2ver)) {
    case 0:
    case 1:
    case 2:
      return 6;
    case 3:
    case 4:
    default:
      return 10;
  }
}

static const gchar *obsolete_frame_ids[] = {
  "CRM", "EQU", "LNK", "RVA", "TIM", "TSI",     /* From 2.2 */
  "EQUA", "RVAD", "TIME", "TRDA", "TSIZ",       /* From 2.3 */
  NULL
};

const struct ID3v2FrameIDConvert
{
  gchar *orig;
  gchar *new;
} frame_id_conversions[] = {
  /* 2.3.x frames */
  {
  "TDAT", "TDRC"}, {
  "TORY", "TDOR"}, {
  "TYER", "TDRC"},
      /* 2.2.x frames */
  {
  "BUF", "RBUF"}, {
  "CNT", "PCNT"}, {
  "COM", "COMM"}, {
  "CRA", "AENC"}, {
  "ETC", "ETCO"}, {
  "GEO", "GEOB"}, {
  "IPL", "TIPL"}, {
  "MCI", "MCDI"}, {
  "MLL", "MLLT"}, {
  "PIC", "APIC"}, {
  "POP", "POPM"}, {
  "REV", "RVRB"}, {
  "SLT", "SYLT"}, {
  "STC", "SYTC"}, {
  "TAL", "TALB"}, {
  "TBP", "TBPM"}, {
  "TCM", "TCOM"}, {
  "TCR", "TCOP"}, {
  "TDA", "TDRC"}, {
  "TDY", "TDLY"}, {
  "TEN", "TENC"}, {
  "TFT", "TFLT"}, {
  "TKE", "TKEY"}, {
  "TLA", "TLAN"}, {
  "TLE", "TLEN"}, {
  "TMT", "TMED"}, {
  "TOA", "TOAL"}, {
  "TOF", "TOFN"}, {
  "TOL", "TOLY"}, {
  "TOR", "TDOR"}, {
  "TOT", "TOAL"}, {
  "TP1", "TPE1"}, {
  "TP2", "TPE2"}, {
  "TP3", "TPE3"}, {
  "TP4", "TPE4"}, {
  "TPA", "TPOS"}, {
  "TPB", "TPUB"}, {
  "TRC", "TSRC"}, {
  "TRD", "TDRC"}, {
  "TRK", "TRCK"}, {
  "TSS", "TSSE"}, {
  "TT1", "TIT1"}, {
  "TT2", "TIT2"}, {
  "TT3", "TIT3"}, {
  "TXT", "TOLY"}, {
  "TXX", "TXXX"}, {
  "TYE", "TDRC"}, {
  "UFI", "UFID"}, {
  "ULT", "USLT"}, {
  "WAF", "WOAF"}, {
  "WAR", "WOAR"}, {
  "WAS", "WOAS"}, {
  "WCM", "WCOM"}, {
  "WCP", "WCOP"}, {
  "WPB", "WPUB"}, {
  "WXX", "WXXX"}, {
  NULL, NULL}
};

static gboolean
convert_fid_to_v240 (gchar * frame_id)
{
  gint i = 0;

  while (obsolete_frame_ids[i] != NULL) {
    if (strncmp (frame_id, obsolete_frame_ids[i], 5) == 0)
      return TRUE;
    i++;
  }

  i = 0;
  while (frame_id_conversions[i].orig != NULL) {
    if (strncmp (frame_id, frame_id_conversions[i].orig, 5) == 0) {
      strcpy (frame_id, frame_id_conversions[i].new);
      return FALSE;
    }
    i++;
  }
  return FALSE;
}

static ID3TagsResult
id3demux_id3v2_frames_to_tag_list (ID3TagsWorking * work, guint size)
{
  guint frame_hdr_size;
  gboolean read_a_frame = FALSE;
  guint8 *start;

  /* Extended header if present */
  if (work->hdr.flags & ID3V2_HDR_FLAG_EXTHDR) {
    work->hdr.ext_hdr_size = read_synch_uint (work->hdr.frame_data, 4);
    if (work->hdr.ext_hdr_size < 6 ||
        (work->hdr.ext_hdr_size) > work->hdr.frame_data_size) {
      return ID3TAGS_BROKEN_TAG;
    }
    work->hdr.ext_flag_bytes = work->hdr.frame_data[4];
    if (5 + work->hdr.ext_flag_bytes > work->hdr.frame_data_size) {
      GST_DEBUG
          ("Tag claims extended header, but doesn't have enough bytes. Broken tag");
      return ID3TAGS_BROKEN_TAG;
    }

    work->hdr.ext_flag_data = work->hdr.frame_data + 5;
    work->hdr.frame_data += work->hdr.ext_hdr_size;
    work->hdr.frame_data_size -= work->hdr.ext_hdr_size;
  }

  start = GST_BUFFER_DATA (work->buffer);
  frame_hdr_size = id3demux_id3v2_frame_hdr_size (work->hdr.version);
  if (work->hdr.frame_data_size <= frame_hdr_size) {
    GST_DEBUG ("Tag has no data frames. Broken tag");
    return ID3TAGS_BROKEN_TAG;  /* Must have at least one frame */
  }

  work->tags = gst_tag_list_new ();
  g_return_val_if_fail (work->tags != NULL, ID3TAGS_READ_TAG);

  while (work->hdr.frame_data_size > frame_hdr_size) {
    guint frame_size = 0;
    gchar frame_id[5] = "";
    guint16 frame_flags = 0x0;
    gboolean obsolete_id = FALSE;

    /* Read the header */
    switch (ID3V2_VER_MAJOR (work->hdr.version)) {
      case 0:
      case 1:
      case 2:
        frame_id[0] = work->hdr.frame_data[0];
        frame_id[1] = work->hdr.frame_data[1];
        frame_id[2] = work->hdr.frame_data[2];
        frame_id[3] = 0;
        frame_id[4] = 0;
        obsolete_id = convert_fid_to_v240 (frame_id);

        frame_size = read_synch_uint (work->hdr.frame_data + 3, 3);
        frame_flags = 0;
        break;
      case 3:
      case 4:
      default:
        frame_id[0] = work->hdr.frame_data[0];
        frame_id[1] = work->hdr.frame_data[1];
        frame_id[2] = work->hdr.frame_data[2];
        frame_id[3] = work->hdr.frame_data[3];
        frame_id[4] = 0;
        frame_size = read_synch_uint (work->hdr.frame_data + 4, 4);
        frame_flags = GST_READ_UINT16_BE (work->hdr.frame_data + 8);

        if (ID3V2_VER_MAJOR (work->hdr.version) == 3) {
          frame_flags &= ID3V2_3_FRAME_FLAGS_MASK;
          obsolete_id = convert_fid_to_v240 (frame_id);
        }
        break;
    }

    work->hdr.frame_data += frame_hdr_size;
    work->hdr.frame_data_size -= frame_hdr_size;

    if (frame_size > work->hdr.frame_data_size ||
        frame_size == 0 || strcmp (frame_id, "") == 0)
      break;                    /* No more frames to read */

#if 0
    g_print
        ("Frame @ %d (0x%02x) id %s size %d, next=%d (0x%02x) obsolete=%d\n",
        work->hdr.frame_data - start, work->hdr.frame_data - start, frame_id,
        frame_size, work->hdr.frame_data + frame_hdr_size + frame_size - start,
        work->hdr.frame_data + frame_hdr_size + frame_size - start,
        obsolete_id);
#endif

    if (!obsolete_id) {
      /* Now, read, decompress etc the contents of the frame
       * into a TagList entry */
      work->cur_frame_size = frame_size;
      work->frame_id = frame_id;
      work->frame_flags = frame_flags;

      if (id3demux_id3v2_parse_frame (work)) {
        read_a_frame = TRUE;
        GST_LOG ("Extracted frame with id %s", frame_id);
      }
    }
    work->hdr.frame_data += frame_size;
    work->hdr.frame_data_size -= frame_size;
  }

  if (!read_a_frame) {
    GST_DEBUG ("Could not extract any frames from tag. Broken tag");
    gst_tag_list_free (work->tags);
    work->tags = NULL;
    return ID3TAGS_BROKEN_TAG;
  }

  return ID3TAGS_READ_TAG;
}
