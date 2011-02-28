/* GStreamer
 * Copyright (C) <2007> Jan Schmidt <thaytan@mad.scientist.com>
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
#ifndef __MPEGPACKETISER_H__
#define __MPEGPACKETISER_H__

#include <gst/gst.h>
#include <gst/base/gstadapter.h>

typedef struct MPEGPacketiser MPEGPacketiser;
typedef struct MPEGBlockInfo MPEGBlockInfo;
typedef struct MPEGSeqHdr MPEGSeqHdr;
typedef struct MPEGPictureHdr MPEGPictureHdr;

/* Packet ID codes for different packet types we
 * care about */
#define MPEG_PACKET_PICTURE      0x00
#define MPEG_PACKET_SLICE_MIN    0x01
#define MPEG_PACKET_SLICE_MAX    0xaf
#define MPEG_PACKET_SEQUENCE     0xb3
#define MPEG_PACKET_EXTENSION    0xb5
#define MPEG_PACKET_SEQUENCE_END 0xb7
#define MPEG_PACKET_GOP          0xb8
#define MPEG_PACKET_NONE         0xff

/* Extension codes we care about */
#define MPEG_PACKET_EXT_SEQUENCE         0x01
#define MPEG_PACKET_EXT_SEQUENCE_DISPLAY 0x02
#define MPEG_PACKET_EXT_QUANT_MATRIX     0x03

/* Flags indicating what type of packets are in this block, some are mutually
 * exclusive though - ie, sequence packs are accumulated separately. GOP & 
 * Picture may occur together or separately */
#define MPEG_BLOCK_FLAG_SEQUENCE  0x01
#define MPEG_BLOCK_FLAG_PICTURE   0x02
#define MPEG_BLOCK_FLAG_GOP       0x04

#define MPEG_PICTURE_TYPE_I 0x01
#define MPEG_PICTURE_TYPE_P 0x02
#define MPEG_PICTURE_TYPE_B 0x03
#define MPEG_PICTURE_TYPE_D 0x04

struct MPEGBlockInfo {
  guint8 first_pack_type;
  guint8 flags;

  guint64 offset;
  guint32 length;

  GstClockTime ts;
};

struct MPEGSeqHdr
{
  /* 0 for unknown, else 1 or 2 */
  guint8 mpeg_version;

  /* Pixel-Aspect Ratio from DAR code via set_par_from_dar */
  gint par_w, par_h;
  /* Width and Height of the video */
  gint width, height;
  /* Framerate */
  gint fps_n, fps_d;
  /* Bitrate */
  guint bitrate;
  /* Profile and level */
  guint profile, level;

  gboolean progressive;
};

struct MPEGPictureHdr
{
  guint8 pic_type;
};

struct MPEGPacketiser {
  GstAdapter *adapter;
  /* position in the adapter */
  guint64 adapter_offset;

  /* Sync word accumulator */
  guint32 sync_word;

  /* Offset since the last flush (unrelated to incoming buffer offsets) */
  guint64 tracked_offset;

  /* Number of picture packets currently collected */
  guint n_pictures;

  /* 2 sets of timestamps + offsets used to mark picture blocks
   * The first is used when a sync word overlaps packet boundaries
   * and comes from some buffer in the past. The next one comes from current
   * buffer. These are only ever valid when handling streams from a demuxer,
   * of course. */ 
  GstClockTime prev_buf_ts;
  GstClockTime cur_buf_ts;

  /* MPEG id of the previous SEQUENCE, PICTURE or GOP packet. 
     MPEG_PACKET_NONE after a flush */
  guint8  prev_sync_packet;

  /* Indices into the blocks array. cur_block_idx is where we're writing and
     indicates the end of the populated block entries.
     first_block_idx is the read ptr. It may be -1 to indicate there are no
     complete blocks available */
  gint cur_block_idx;
  gint first_block_idx;

  /* An array of MPEGBlockInfo entries, used as a growable circular buffer
   * indexed by cur_block_idx and bounded by last_block_idx */
  gint n_blocks;
  MPEGBlockInfo *blocks;
};

void mpeg_packetiser_init (MPEGPacketiser *p);
void mpeg_packetiser_free (MPEGPacketiser *p);

void mpeg_packetiser_add_buf (MPEGPacketiser *p, GstBuffer *buf);
void mpeg_packetiser_handle_eos (MPEGPacketiser *p);

void mpeg_packetiser_flush (MPEGPacketiser *p);

/* Get the blockinfo and buffer for the block at the head of the queue */
MPEGBlockInfo *mpeg_packetiser_get_block (MPEGPacketiser *p, GstBuffer **buf);

/* Advance to the next data block */
void mpeg_packetiser_next_block (MPEGPacketiser *p);

/* Utility functions for parsing MPEG packets */
guint8 *mpeg_util_find_start_code (guint32 *sync_word, 
    guint8 *cur, guint8 *end);
gboolean mpeg_util_parse_sequence_hdr (MPEGSeqHdr *hdr, 
    guint8 *data, guint8 *end);
gboolean mpeg_util_parse_picture_hdr (MPEGPictureHdr *hdr,
    guint8 *data, guint8 *end);

#endif
