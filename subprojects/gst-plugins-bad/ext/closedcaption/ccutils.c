/*
 * GStreamer
 * Copyright (C) 2022 Matthew Waters <matthew@centricular.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/base/base.h>

#include "ccutils.h"

#define GST_CAT_DEFAULT ccutils_debug_cat
GST_DEBUG_CATEGORY (GST_CAT_DEFAULT);

typedef struct cdp_fps_entry cdp_fps_entry;

static const struct cdp_fps_entry cdp_fps_table[] = {
  {0x1f, 24000, 1001, 25, 22, 3 /* FIXME: alternating max cea608 count! */ },
  {0x2f, 24, 1, 25, 22, 2},
  {0x3f, 25, 1, 24, 22, 2},
  {0x4f, 30000, 1001, 20, 18, 2},
  {0x5f, 30, 1, 20, 18, 2},
  {0x6f, 50, 1, 12, 11, 1},
  {0x7f, 60000, 1001, 10, 9, 1},
  {0x8f, 60, 1, 10, 9, 1},
};
const struct cdp_fps_entry null_fps_entry = { 0, 0, 0, 0 };

const struct cdp_fps_entry *
cdp_fps_entry_from_fps (guint fps_n, guint fps_d)
{
  int i;
  for (i = 0; i < G_N_ELEMENTS (cdp_fps_table); i++) {
    if (cdp_fps_table[i].fps_n == fps_n && cdp_fps_table[i].fps_d == fps_d)
      return &cdp_fps_table[i];
  }
  return &null_fps_entry;
}

const struct cdp_fps_entry *
cdp_fps_entry_from_id (guint8 id)
{
  int i;
  for (i = 0; i < G_N_ELEMENTS (cdp_fps_table); i++) {
    if (cdp_fps_table[i].fps_idx == id)
      return &cdp_fps_table[i];
  }
  return &null_fps_entry;
}

/* Converts raw CEA708 cc_data and an optional timecode into CDP */
guint
convert_cea708_cc_data_to_cdp (GstObject * dbg_obj, GstCCCDPMode cdp_mode,
    guint16 cdp_hdr_sequence_cntr, const guint8 * cc_data, guint cc_data_len,
    guint8 * cdp, guint cdp_len, const GstVideoTimeCode * tc,
    const cdp_fps_entry * fps_entry)
{
  GstByteWriter bw;
  guint8 flags, checksum;
  guint i, len;

  GST_DEBUG_OBJECT (dbg_obj, "writing out cdp packet from cc_data with "
      "length %u", cc_data_len);

  gst_byte_writer_init_with_data (&bw, cdp, cdp_len, FALSE);
  gst_byte_writer_put_uint16_be_unchecked (&bw, 0x9669);
  /* Write a length of 0 for now */
  gst_byte_writer_put_uint8_unchecked (&bw, 0);

  gst_byte_writer_put_uint8_unchecked (&bw, fps_entry->fps_idx);

  if (cc_data_len / 3 > fps_entry->max_cc_count) {
    GST_WARNING_OBJECT (dbg_obj, "Too many cc_data triplets for framerate: %u. "
        "Truncating to %u", cc_data_len / 3, fps_entry->max_cc_count);
    cc_data_len = 3 * fps_entry->max_cc_count;
  }

  /* caption_service_active */
  flags = 0x02;

  /* ccdata_present */
  if ((cdp_mode & GST_CC_CDP_MODE_CC_DATA))
    flags |= 0x40;

  /* time_code_present */
  if ((cdp_mode & GST_CC_CDP_MODE_TIME_CODE) && tc && tc->config.fps_n > 0)
    flags |= 0x80;

  /* reserved */
  flags |= 0x01;

  gst_byte_writer_put_uint8_unchecked (&bw, flags);

  gst_byte_writer_put_uint16_be_unchecked (&bw, cdp_hdr_sequence_cntr);

  if ((cdp_mode & GST_CC_CDP_MODE_TIME_CODE) && tc && tc->config.fps_n > 0) {
    guint8 u8;

    gst_byte_writer_put_uint8_unchecked (&bw, 0x71);
    /* reserved 11 - 2 bits */
    u8 = 0xc0;
    /* tens of hours - 2 bits */
    u8 |= ((tc->hours / 10) & 0x3) << 4;
    /* units of hours - 4 bits */
    u8 |= (tc->hours % 10) & 0xf;
    gst_byte_writer_put_uint8_unchecked (&bw, u8);

    /* reserved 1 - 1 bit */
    u8 = 0x80;
    /* tens of minutes - 3 bits */
    u8 |= ((tc->minutes / 10) & 0x7) << 4;
    /* units of minutes - 4 bits */
    u8 |= (tc->minutes % 10) & 0xf;
    gst_byte_writer_put_uint8_unchecked (&bw, u8);

    /* field flag - 1 bit */
    u8 = tc->field_count < 2 ? 0x00 : 0x80;
    /* tens of seconds - 3 bits */
    u8 |= ((tc->seconds / 10) & 0x7) << 4;
    /* units of seconds - 4 bits */
    u8 |= (tc->seconds % 10) & 0xf;
    gst_byte_writer_put_uint8_unchecked (&bw, u8);

    /* drop frame flag - 1 bit */
    u8 = (tc->config.flags & GST_VIDEO_TIME_CODE_FLAGS_DROP_FRAME) ? 0x80 :
        0x00;
    /* reserved0 - 1 bit */
    /* tens of frames - 2 bits */
    u8 |= ((tc->frames / 10) & 0x3) << 4;
    /* units of frames 4 bits */
    u8 |= (tc->frames % 10) & 0xf;
    gst_byte_writer_put_uint8_unchecked (&bw, u8);
  }

  if ((cdp_mode & GST_CC_CDP_MODE_CC_DATA)) {
    gst_byte_writer_put_uint8_unchecked (&bw, 0x72);
    gst_byte_writer_put_uint8_unchecked (&bw, 0xe0 | fps_entry->max_cc_count);
    gst_byte_writer_put_data_unchecked (&bw, cc_data, cc_data_len);
    while (fps_entry->max_cc_count > cc_data_len / 3) {
      gst_byte_writer_put_uint8_unchecked (&bw, 0xfa);
      gst_byte_writer_put_uint8_unchecked (&bw, 0x00);
      gst_byte_writer_put_uint8_unchecked (&bw, 0x00);
      cc_data_len += 3;
    }
  }

  gst_byte_writer_put_uint8_unchecked (&bw, 0x74);
  gst_byte_writer_put_uint16_be_unchecked (&bw, cdp_hdr_sequence_cntr);
  /* We calculate the checksum afterwards */
  gst_byte_writer_put_uint8_unchecked (&bw, 0);

  len = gst_byte_writer_get_pos (&bw);
  gst_byte_writer_set_pos (&bw, 2);
  gst_byte_writer_put_uint8_unchecked (&bw, len);

  checksum = 0;
  for (i = 0; i < len; i++) {
    checksum += cdp[i];
  }
  checksum &= 0xff;
  checksum = 256 - checksum;
  cdp[len - 1] = checksum;

  return len;
}

/* Converts CDP into raw CEA708 cc_data */
guint
convert_cea708_cdp_to_cc_data (GstObject * dbg_obj,
    const guint8 * cdp, guint cdp_len, guint8 * cc_data,
    GstVideoTimeCode * tc, const cdp_fps_entry ** out_fps_entry)
{
  GstByteReader br;
  guint16 u16;
  guint8 u8;
  guint8 flags;
  guint len = 0;
  const struct cdp_fps_entry *fps_entry;

  *out_fps_entry = &null_fps_entry;
  memset (tc, 0, sizeof (*tc));

  /* Header + footer length */
  if (cdp_len < 11) {
    GST_WARNING_OBJECT (dbg_obj, "cdp packet too short (%u). expected at "
        "least %u", cdp_len, 11);
    return 0;
  }

  gst_byte_reader_init (&br, cdp, cdp_len);
  u16 = gst_byte_reader_get_uint16_be_unchecked (&br);
  if (u16 != 0x9669) {
    GST_WARNING_OBJECT (dbg_obj, "cdp packet does not have initial magic bytes "
        "of 0x9669");
    return 0;
  }

  u8 = gst_byte_reader_get_uint8_unchecked (&br);
  if (u8 != cdp_len) {
    GST_WARNING_OBJECT (dbg_obj, "cdp packet length (%u) does not match passed "
        "in value (%u)", u8, cdp_len);
    return 0;
  }

  u8 = gst_byte_reader_get_uint8_unchecked (&br);
  fps_entry = cdp_fps_entry_from_id (u8);
  if (!fps_entry || fps_entry->fps_n == 0) {
    GST_WARNING_OBJECT (dbg_obj, "cdp packet does not have a valid framerate "
        "id (0x%02x", u8);
    return 0;
  }

  flags = gst_byte_reader_get_uint8_unchecked (&br);
  /* No cc_data? */
  if ((flags & 0x40) == 0) {
    GST_DEBUG_OBJECT (dbg_obj, "cdp packet does have any cc_data");
    return 0;
  }

  /* cdp_hdr_sequence_cntr */
  gst_byte_reader_skip_unchecked (&br, 2);

  /* time_code_present */
  if (flags & 0x80) {
    guint8 hours, minutes, seconds, frames, fields;
    gboolean drop_frame;

    if (gst_byte_reader_get_remaining (&br) < 5) {
      GST_WARNING_OBJECT (dbg_obj, "cdp packet does not have enough data to "
          "contain a timecode (%u). Need at least 5 bytes",
          gst_byte_reader_get_remaining (&br));
      return 0;
    }
    u8 = gst_byte_reader_get_uint8_unchecked (&br);
    if (u8 != 0x71) {
      GST_WARNING_OBJECT (dbg_obj, "cdp packet does not have timecode start "
          "byte of 0x71, found 0x%02x", u8);
      return 0;
    }

    u8 = gst_byte_reader_get_uint8_unchecked (&br);
    if ((u8 & 0xc0) != 0xc0) {
      GST_WARNING_OBJECT (dbg_obj, "reserved bits are not 0xc0, found 0x%02x",
          u8);
      return 0;
    }

    hours = ((u8 >> 4) & 0x3) * 10 + (u8 & 0xf);

    u8 = gst_byte_reader_get_uint8_unchecked (&br);
    if ((u8 & 0x80) != 0x80) {
      GST_WARNING_OBJECT (dbg_obj, "reserved bit is not 0x80, found 0x%02x",
          u8);
      return 0;
    }
    minutes = ((u8 >> 4) & 0x7) * 10 + (u8 & 0xf);

    u8 = gst_byte_reader_get_uint8_unchecked (&br);
    if (u8 & 0x80)
      fields = 2;
    else
      fields = 1;
    seconds = ((u8 >> 4) & 0x7) * 10 + (u8 & 0xf);

    u8 = gst_byte_reader_get_uint8_unchecked (&br);
    if (u8 & 0x40) {
      GST_WARNING_OBJECT (dbg_obj, "reserved bit is not 0x0, found 0x%02x", u8);
      return 0;
    }

    drop_frame = !(!(u8 & 0x80));
    frames = ((u8 >> 4) & 0x3) * 10 + (u8 & 0xf);

    gst_video_time_code_init (tc, fps_entry->fps_n, fps_entry->fps_d, NULL,
        drop_frame ? GST_VIDEO_TIME_CODE_FLAGS_DROP_FRAME :
        GST_VIDEO_TIME_CODE_FLAGS_NONE, hours, minutes, seconds, frames,
        fields);
  }

  /* ccdata_present */
  if (flags & 0x40) {
    guint8 cc_count;

    if (gst_byte_reader_get_remaining (&br) < 2) {
      GST_WARNING_OBJECT (dbg_obj, "not enough data to contain valid cc_data");
      return 0;
    }
    u8 = gst_byte_reader_get_uint8_unchecked (&br);
    if (u8 != 0x72) {
      GST_WARNING_OBJECT (dbg_obj, "missing cc_data start code of 0x72, "
          "found 0x%02x", u8);
      return 0;
    }

    cc_count = gst_byte_reader_get_uint8_unchecked (&br);
    if ((cc_count & 0xe0) != 0xe0) {
      GST_WARNING_OBJECT (dbg_obj, "reserved bits are not 0xe0, found 0x%02x",
          u8);
      return 0;
    }
    cc_count &= 0x1f;

    len = 3 * cc_count;
    if (gst_byte_reader_get_remaining (&br) < len) {
      GST_WARNING_OBJECT (dbg_obj, "not enough bytes (%u) left for the "
          "number of byte triples (%u)", gst_byte_reader_get_remaining (&br),
          cc_count);
      return 0;
    }

    memcpy (cc_data, gst_byte_reader_get_data_unchecked (&br, len), len);
  }

  *out_fps_entry = fps_entry;

  /* skip everything else we don't care about */
  return len;
}
