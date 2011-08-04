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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

/* The purpose of the packetiser is to parse the incoming buffers into 'blocks'
 * that consist of the stream split at certain packet boundaries. It splits into
 * a new block at the start of a GOP, Picture or Sequence packet.
 * A GOP or sequence header always starts a new block. A Picture
 * header starts a new block only if the previous packet was not a GOP - 
 * otherwise it is accumulated with the GOP */
#include "mpegpacketiser.h"

GST_DEBUG_CATEGORY_EXTERN (mpv_parse_debug);
#define GST_CAT_DEFAULT mpv_parse_debug

static void collect_packets (MPEGPacketiser * p, GstBuffer * buf);

void
mpeg_packetiser_init (MPEGPacketiser * p)
{
  p->adapter = gst_adapter_new ();
  p->n_blocks = 0;
  p->blocks = NULL;
  mpeg_packetiser_flush (p);
}

void
mpeg_packetiser_free (MPEGPacketiser * p)
{
  g_object_unref (p->adapter);
  g_free (p->blocks);
}

void
mpeg_packetiser_add_buf (MPEGPacketiser * p, GstBuffer * buf)
{
  /* Add the buffer to our pool */
  gst_adapter_push (p->adapter, buf);

  /* Store the timestamp to apply to the next picture that gets collected */
  if (p->cur_buf_ts != GST_CLOCK_TIME_NONE) {
    p->prev_buf_ts = p->cur_buf_ts;
  }
  p->cur_buf_ts = GST_BUFFER_TIMESTAMP (buf);

  /* read what new packets we have in this buffer */
  collect_packets (p, buf);

  p->tracked_offset += GST_BUFFER_SIZE (buf);
}

void
mpeg_packetiser_flush (MPEGPacketiser * p)
{
  gst_adapter_clear (p->adapter);
  p->adapter_offset = 0;

  p->sync_word = 0xffffffff;
  p->tracked_offset = 0;
  p->prev_sync_packet = MPEG_PACKET_NONE;

  /* Reset our block info */
  p->cur_block_idx = -1;
  p->first_block_idx = -1;

  /* Clear any pending timestamps */
  p->prev_buf_ts = GST_CLOCK_TIME_NONE;
  p->cur_buf_ts = GST_CLOCK_TIME_NONE;
}

guint8 *
mpeg_util_find_start_code (guint32 * sync_word, guint8 * cur, guint8 * end)
{
  guint32 code;

  if (G_UNLIKELY (cur == NULL))
    return NULL;

  code = *sync_word;

  while (cur < end) {
    code <<= 8;

    if (code == 0x00000100) {
      /* Reset the sync word accumulator */
      *sync_word = 0xffffffff;
      return cur;
    }

    /* accelerate search for start code */
    if (*cur > 1) {
      while (cur < (end - 4) && *cur > 1)
        if (cur[3] > 1)
          cur += 4;
        else
          cur++;
      code = 0xffffff00;
    }

    /* Add the next available byte to the collected sync word */
    code |= *cur++;
  }

  *sync_word = code;
  return NULL;
}

/* When we need to reallocate the blocks array, grow it by this much */
#define BLOCKS_INCREMENT 5

/* Get the index of the next unfilled block in the buffer. May need to grow
 * the array first */
static gint
get_next_free_block (MPEGPacketiser * p)
{
  gint next;
  gboolean grow_array = FALSE;

  /* Get a free block from the blocks array. May need to grow
   * the array first */
  if (p->n_blocks == 0) {
    grow_array = TRUE;
    next = 0;
  } else {
    if (G_UNLIKELY (p->cur_block_idx == -1)) {
      next = 0;
    } else {
      next = p->cur_block_idx;
      if (((next + 1) % p->n_blocks) == p->first_block_idx)
        grow_array = TRUE;
    }
  }

  if (grow_array) {
    gint old_n_blocks = p->n_blocks;

    p->n_blocks += BLOCKS_INCREMENT;

    p->blocks = g_realloc (p->blocks, sizeof (MPEGBlockInfo) * p->n_blocks);

    /* Now we may need to move some data up to the end of the array, if the 
     * cur_block_idx is before the first_block_idx in the array. */
    if (p->cur_block_idx < p->first_block_idx) {

      GST_LOG ("Moving %d blocks from idx %d to idx %d of %d",
          old_n_blocks - p->first_block_idx,
          p->first_block_idx, p->first_block_idx + BLOCKS_INCREMENT,
          p->n_blocks);

      memmove (p->blocks + p->first_block_idx + BLOCKS_INCREMENT,
          p->blocks + p->first_block_idx,
          sizeof (MPEGBlockInfo) * (old_n_blocks - p->first_block_idx));
      p->first_block_idx += BLOCKS_INCREMENT;
    }
  }

  return next;
}

/* Mark the current block as complete */
static void
complete_current_block (MPEGPacketiser * p, guint64 offset)
{
  MPEGBlockInfo *block;

  if (G_UNLIKELY (p->cur_block_idx == -1))
    return;                     /* No block is in progress */

  /* If we're pointing at the first_block_idx, then we're about to re-complete
   * a previously completed buffer. Not allowed, because the array should have
   * been previously expanded to cope via a get_next_free_block call. */
  g_assert (p->cur_block_idx != p->first_block_idx);

  /* Get the appropriate entry from the blocks array */
  g_assert (p->blocks != NULL && p->cur_block_idx < p->n_blocks);
  block = p->blocks + p->cur_block_idx;

  /* Extend the block length to the current offset */
  g_assert (block->offset < offset);
  block->length = offset - block->offset;

  GST_LOG ("Completed block of type 0x%02x @ offset %" G_GUINT64_FORMAT
      " with size %u", block->first_pack_type, block->offset, block->length);

  /* If this is the first complete block, set first_block_idx to be this block */
  if (p->first_block_idx == -1)
    p->first_block_idx = p->cur_block_idx;

  /* Update the statistics regarding the packet we're handling */
  if (block->flags & MPEG_BLOCK_FLAG_PICTURE)
    p->n_pictures++;

  /* And advance the cur_block_idx ptr to the next slot */
  p->cur_block_idx = (p->cur_block_idx + 1) % p->n_blocks;
}

/* Accumulate the packet up to 'offset' into the current block 
 * (discard if no block is in progress). Update the block info
 * to indicate what is in it. */
static void
append_to_current_block (MPEGPacketiser * p, guint64 offset, guint8 pack_type)
{
  MPEGBlockInfo *block;

  if (G_UNLIKELY (p->cur_block_idx == -1))
    return;                     /* No block in progress, drop this packet */

  /* Get the appropriate entry from the blocks array */
  g_assert (p->blocks != NULL && p->cur_block_idx < p->n_blocks);
  block = p->blocks + p->cur_block_idx;

  /* Extend the block length to the current offset */
  g_assert (block->offset < offset);
  block->length = offset - block->offset;

  /* Update flags */
  switch (pack_type) {
    case MPEG_PACKET_SEQUENCE:
      g_assert (!(block->flags &
              (MPEG_BLOCK_FLAG_GOP | MPEG_BLOCK_FLAG_PICTURE)));
      block->flags |= MPEG_BLOCK_FLAG_SEQUENCE;
      break;
    case MPEG_PACKET_GOP:
      block->flags |= MPEG_BLOCK_FLAG_GOP;
      break;
    case MPEG_PACKET_PICTURE:
      block->flags |= MPEG_BLOCK_FLAG_PICTURE;
      break;
    default:
      break;
  }
}

static void
start_new_block (MPEGPacketiser * p, guint64 offset, guint8 pack_type)
{
  gint block_idx;
  MPEGBlockInfo *block;

  /* First, append data up to the start of this block to the current one, but
   * not including this packet info */
  complete_current_block (p, offset);

  block_idx = get_next_free_block (p);
  /* FIXME: Retrieve the appropriate entry from the blocks array */
  /* Get the appropriate entry from the blocks array */
  g_assert (p->blocks != NULL && block_idx < p->n_blocks);
  block = p->blocks + block_idx;

  /* Init the block */
  block->first_pack_type = pack_type;
  block->offset = offset;
  block->ts = GST_CLOCK_TIME_NONE;

  /* Initially, the length is 0. It grows as we encounter new sync headers */
  block->length = 0;
  switch (pack_type) {
    case MPEG_PACKET_SEQUENCE:
      block->flags = MPEG_BLOCK_FLAG_SEQUENCE;
      break;
    case MPEG_PACKET_GOP:
      block->flags = MPEG_BLOCK_FLAG_GOP;
      break;
    case MPEG_PACKET_PICTURE:
      block->flags = MPEG_BLOCK_FLAG_PICTURE;
      break;
    default:
      /* We don't start blocks with other packet types */
      g_assert_not_reached ();
  }

  /* Make this our current block */
  p->cur_block_idx = block_idx;

  GST_LOG ("Started new block in slot %d with first pack 0x%02x @ offset %"
      G_GUINT64_FORMAT, block_idx, block->first_pack_type, block->offset);

}

static void
handle_packet (MPEGPacketiser * p, guint64 offset, guint8 pack_type)
{
  GST_LOG ("offset %" G_GUINT64_FORMAT ", pack_type %2x", offset, pack_type);
  switch (pack_type) {
    case MPEG_PACKET_SEQUENCE:
    case MPEG_PACKET_GOP:
      /* Start a new block */
      start_new_block (p, offset, pack_type);
      p->prev_sync_packet = pack_type;
      break;
    case MPEG_PACKET_PICTURE:{
      MPEGBlockInfo *block;
      GstClockTime ts;

      /* Start a new block unless the previous sync packet was a GOP */
      if (p->prev_sync_packet != MPEG_PACKET_GOP) {
        start_new_block (p, offset, pack_type);
      } else {
        append_to_current_block (p, offset, pack_type);
      }
      p->prev_sync_packet = pack_type;

      /* We have a picture packet, apply any pending timestamp. The logic here
       * is that the timestamp on any incoming buffer needs to apply to the next
       * picture packet where the _first_byte_ of the sync word starts after the
       * packet boundary. We track the ts from the current buffer and a
       * previous buffer in order to handle this correctly. It would still be
       * possible to get it wrong if there was a PES packet smaller than 3 bytes
       * but anyone that does that can suck it.  */
      if ((offset >= p->tracked_offset)
          && (p->cur_buf_ts != GST_CLOCK_TIME_NONE)) {
        /* sync word started within this buffer - take the cur ts */
        ts = p->cur_buf_ts;
        p->cur_buf_ts = GST_CLOCK_TIME_NONE;
        p->prev_buf_ts = GST_CLOCK_TIME_NONE;
      } else {
        /* sync word started in a previous buffer - take the old ts */
        ts = p->prev_buf_ts;
        p->prev_buf_ts = GST_CLOCK_TIME_NONE;
      }

      /* If we didn't drop the packet, set the timestamp on it */
      if (G_LIKELY (p->cur_block_idx != -1)) {
        block = p->blocks + p->cur_block_idx;
        block->ts = ts;
        GST_LOG ("Picture @ offset %" G_GINT64_FORMAT " has ts %"
            GST_TIME_FORMAT, block->offset, GST_TIME_ARGS (block->ts));
      }
      break;
    }
    default:
      append_to_current_block (p, offset, pack_type);
      break;
  }
}

static void
collect_packets (MPEGPacketiser * p, GstBuffer * buf)
{
  guint8 *cur;
  guint8 *end = GST_BUFFER_DATA (buf) + GST_BUFFER_SIZE (buf);

  cur = mpeg_util_find_start_code (&(p->sync_word), GST_BUFFER_DATA (buf), end);
  while (cur != NULL) {
    /* Calculate the offset as tracked since the last flush. Note that cur
     * points to the last byte of the sync word, so we adjust by -3 to get the
     * first byte */
    guint64 offset = p->tracked_offset + (cur - GST_BUFFER_DATA (buf) - 3);

    handle_packet (p, offset, *cur);
    cur = mpeg_util_find_start_code (&(p->sync_word), cur, end);
  }
}

void
mpeg_packetiser_handle_eos (MPEGPacketiser * p)
{
  /* Append any remaining data to the current block */
  if (p->tracked_offset > 0) {
    complete_current_block (p, p->tracked_offset);
  }
}

/* Returns a pointer to the block info for the completed block at the
 * head of the queue, and extracts the bytes from the adapter if requested.
 * Caller should move to the next block by calling mpeg_packetiser_next_block
 * afterward.
 */
MPEGBlockInfo *
mpeg_packetiser_get_block (MPEGPacketiser * p, GstBuffer ** buf)
{
  MPEGBlockInfo *block;

  if (buf)
    *buf = NULL;

  if (G_UNLIKELY (p->first_block_idx == -1)) {
    return NULL;                /* No complete blocks to discard */
  }

  /* p->first_block_idx can't get set != -1 unless some block storage got
   * allocated */
  g_assert (p->blocks != NULL && p->n_blocks != 0);
  block = p->blocks + p->first_block_idx;

  /* Can only get the buffer out once, so we'll return NULL on later attempts */
  if (buf != NULL && block->length > 0 && p->adapter_offset <= block->offset) {
    /* Kick excess data out of the adapter */
    if (p->adapter_offset < block->offset) {
      guint64 to_flush = block->offset - p->adapter_offset;

      g_assert (gst_adapter_available (p->adapter) >= to_flush);
      gst_adapter_flush (p->adapter, to_flush);
      p->adapter_offset += to_flush;
    }

    g_assert (gst_adapter_available (p->adapter) >= block->length);
    *buf = gst_adapter_take_buffer (p->adapter, block->length);
    p->adapter_offset += block->length;

    GST_BUFFER_TIMESTAMP (*buf) = block->ts;
  } else {
    GST_DEBUG ("we have a block but do not meet all conditions buf: %p "
        "block length: %d adapter offset %" G_GUINT64_FORMAT " block offset "
        "%" G_GUINT64_FORMAT, buf, block->length, p->adapter_offset,
        block->offset);
  }
  return block;
}

/* Advance the first_block pointer to discard a completed block
 * from the queue */
void
mpeg_packetiser_next_block (MPEGPacketiser * p)
{
  gint next;
  MPEGBlockInfo *block;

  block = mpeg_packetiser_get_block (p, NULL);
  if (G_UNLIKELY (block == NULL))
    return;                     /* No complete blocks to discard */

  /* Update the statistics regarding the block we're discarding */
  if (block->flags & MPEG_BLOCK_FLAG_PICTURE)
    p->n_pictures--;

  next = (p->first_block_idx + 1) % p->n_blocks;
  if (next == p->cur_block_idx)
    p->first_block_idx = -1;    /* Discarding the last block */
  else
    p->first_block_idx = next;
}

/* Set the Pixel Aspect Ratio in our hdr from a DAR code in the data */
static void
set_par_from_dar (MPEGSeqHdr * hdr, guint8 asr_code)
{
  /* Pixel_width = DAR_width * display_vertical_size */
  /* Pixel_height = DAR_height * display_horizontal_size */
  switch (asr_code) {
    case 0x02:                 /* 3:4 DAR = 4:3 pixels */
      hdr->par_w = 4 * hdr->height;
      hdr->par_h = 3 * hdr->width;
      break;
    case 0x03:                 /* 9:16 DAR */
      hdr->par_w = 16 * hdr->height;
      hdr->par_h = 9 * hdr->width;
      break;
    case 0x04:                 /* 1:2.21 DAR */
      hdr->par_w = 221 * hdr->height;
      hdr->par_h = 100 * hdr->width;
      break;
    case 0x01:                 /* Square pixels */
    default:
      hdr->par_w = hdr->par_h = 1;
      break;
  }
}

static void
set_fps_from_code (MPEGSeqHdr * hdr, guint8 fps_code)
{
  const gint framerates[][2] = {
    {30, 1}, {24000, 1001}, {24, 1}, {25, 1},
    {30000, 1001}, {30, 1}, {50, 1}, {60000, 1001},
    {60, 1}, {30, 1}
  };

  if (fps_code < 10) {
    hdr->fps_n = framerates[fps_code][0];
    hdr->fps_d = framerates[fps_code][1];
  } else {
    /* Force a valid framerate */
    hdr->fps_n = 30000;
    hdr->fps_d = 1001;
  }
}

static gboolean
mpeg_util_parse_extension_packet (MPEGSeqHdr * hdr, guint8 * data, guint8 * end)
{
  guint8 ext_code;

  if (G_UNLIKELY (data >= end))
    return FALSE;               /* short extension packet */

  ext_code = data[0] >> 4;

  switch (ext_code) {
    case MPEG_PACKET_EXT_SEQUENCE:
    {
      /* Parse a Sequence Extension */
      guint8 horiz_size_ext, vert_size_ext;
      guint8 fps_n_ext, fps_d_ext;

      if (G_UNLIKELY ((end - data) < 6))
        /* need at least 10 bytes, minus 4 for the start code 000001b5 */
        return FALSE;

      hdr->profile = data[0] & 0x0f;    /* profile (0:2) + escape bit (3) */
      hdr->level = (data[1] >> 4) & 0x0f;
      hdr->progressive = data[1] & 0x08;
      /* chroma_format = (data[1] >> 2) & 0x03; */
      horiz_size_ext = ((data[1] << 1) & 0x02) | ((data[2] >> 7) & 0x01);
      vert_size_ext = (data[2] >> 5) & 0x03;
      /* low_delay = data[5] >> 7; */
      fps_n_ext = (data[5] >> 5) & 0x03;
      fps_d_ext = data[5] & 0x1f;

      hdr->fps_n *= (fps_n_ext + 1);
      hdr->fps_d *= (fps_d_ext + 1);
      hdr->width += (horiz_size_ext << 12);
      hdr->height += (vert_size_ext << 12);
      break;
    }
    default:
      break;
  }

  return TRUE;
}

gboolean
mpeg_util_parse_sequence_hdr (MPEGSeqHdr * hdr, guint8 * data, guint8 * end)
{
  guint32 code;
  guint8 dar_idx, fps_idx;
  guint32 sync_word = 0xffffffff;
  gboolean load_intra_flag;
  gboolean load_non_intra_flag;

  if (G_UNLIKELY ((end - data) < 12))
    return FALSE;               /* Too small to be a sequence header */

  code = GST_READ_UINT32_BE (data);
  if (G_UNLIKELY (code != (0x00000100 | MPEG_PACKET_SEQUENCE)))
    return FALSE;

  /* Skip the sync word */
  data += 4;

  /* Parse the MPEG 1 bits */
  hdr->mpeg_version = 1;

  code = GST_READ_UINT32_BE (data);
  hdr->width = (code >> 20) & 0xfff;
  hdr->height = (code >> 8) & 0xfff;

  dar_idx = (code >> 4) & 0xf;
  set_par_from_dar (hdr, dar_idx);
  fps_idx = code & 0xf;
  set_fps_from_code (hdr, fps_idx);

  hdr->bitrate = ((data[6] >> 6) | (data[5] << 2) | (data[4] << 10));
  if (hdr->bitrate == 0x3ffff) {
    /* VBR stream */
    hdr->bitrate = 0;
  } else {
    /* Value in header is in units of 400 bps */
    hdr->bitrate *= 400;
  }

  /* constrained_flag = (data[7] >> 2) & 0x01; */
  load_intra_flag = (data[7] >> 1) & 0x01;
  if (load_intra_flag) {
    if (G_UNLIKELY ((end - data) < 64))
      return FALSE;
    data += 64;
  }

  load_non_intra_flag = data[7] & 0x01;
  if (load_non_intra_flag) {
    if (G_UNLIKELY ((end - data) < 64))
      return FALSE;
    data += 64;
  }

  /* Advance past the rest of the MPEG-1 header */
  data += 8;

  /* Read MPEG-2 sequence extensions */
  data = mpeg_util_find_start_code (&sync_word, data, end);
  while (data != NULL) {
    if (G_UNLIKELY (data >= end))
      return FALSE;

    /* data points at the last byte of the start code */
    if (data[0] == MPEG_PACKET_EXTENSION) {
      if (!mpeg_util_parse_extension_packet (hdr, data + 1, end))
        return FALSE;

      hdr->mpeg_version = 2;
    }
    data = mpeg_util_find_start_code (&sync_word, data, end);
  }

  return TRUE;
}

gboolean
mpeg_util_parse_picture_hdr (MPEGPictureHdr * hdr, guint8 * data, guint8 * end)
{
  guint32 code;

  if (G_UNLIKELY ((end - data) < 6))
    return FALSE;               /* Packet too small */

  code = GST_READ_UINT32_BE (data);
  if (G_UNLIKELY (code != (0x00000100 | MPEG_PACKET_PICTURE)))
    return FALSE;

  /* Skip the start code */
  data += 4;

  hdr->pic_type = (data[1] >> 3) & 0x07;
  if (hdr->pic_type == 0 || hdr->pic_type > 4)
    return FALSE;               /* Corrupted picture packet */

  return TRUE;
}
