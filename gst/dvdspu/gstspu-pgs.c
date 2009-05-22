/* GStreamer Sub-Picture Unit - PGS handling
 * Copyright (C) 2009 Jan Schmidt <thaytan@noraisin.net>
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
#  include <config.h>
#endif

#include <string.h>
#include <gst/gst.h>

#include "gstspu-pgs.h"

const struct PgsFrameRateEntry
{
  guint8 id;
  guint fps_n;
  guint fps_d;
} PgsFrameRates[] = {
  {
  64, 30000, 1001}              /* 29.97 FPS */
};

gboolean in_presentation_segment = FALSE;
guint8 *rle_data = NULL;
guint32 rle_data_size = 0, rle_data_used = 0;
PgsPaletteEntry palette[256];

#define DUMP_FULL_IMAGE 0

static void
dump_bytes (guint8 * data, guint16 len)
{
  gint i;

  /* Dump the numbers */
  for (i = 0; i < len; i++) {
    g_print ("0x%02x ", data[i]);
    if (!((i + 1) % 16))
      g_print ("\n");
  }
  if (len > 0 && (i % 16))
    g_print ("\n");
}

static void
dump_rle_data (guint8 * data, guint32 len)
{
  guint8 *end = data + len;
  guint16 obj_w, obj_h;

  if (data + 4 > end)
    return;

  /* RLE data: */
  obj_w = GST_READ_UINT16_BE (data);
  obj_h = GST_READ_UINT16_BE (data + 2);
  data += 4;
  g_print ("RLE image is %ux%u\n", obj_w, obj_h);

  while (data < end) {
    guint8 pal_id;
    guint16 run_len;

    if (data[0] != 0) {
      // g_print ("data 0x%02x\n", data[0]);
      pal_id = *data++;
      run_len = 1;
    } else {
      data++;

      if (data + 1 > end)
        return;
      switch (data[0] & 0xC0) {
        case 0x00:
          //g_print ("data 0x%02x\n", data[0]);
          run_len = (data[0] & 0x3f);
          if (run_len > 0)
            pal_id = 0;
          data++;
          break;
        case 0x40:
          if (data + 2 > end)
            return;
          //g_print ("data 0x%02x 0x%02x\n", data[0], data[1]);
          run_len = ((data[0] << 8) | data[1]) & 0x3fff;
          if (run_len > 0)
            pal_id = 0;
          data += 2;
          break;
        case 0x80:
          if (data + 2 > end)
            return;
          //g_print ("data 0x%02x 0x%02x\n", data[0], data[1]);
          run_len = (data[0] & 0x3f);
          pal_id = data[1];
          data += 2;
          break;
        case 0xC0:
          if (data + 3 > end)
            return;
          //g_print ("data 0x%02x 0x%02x 0x%02x\n", data[0], data[1], data[2]);
          run_len = ((data[0] << 8) | data[1]) & 0x3fff;
          pal_id = data[2];
          data += 3;
          break;
      }
    }

#if DUMP_FULL_IMAGE
    {
      gint i;
      guint x = 0;
#if 1
      if (palette[pal_id].A) {
        for (i = 0; i < run_len; i++)
          g_print ("%02x ", pal_id);
      } else {
        for (i = 0; i < run_len; i++)
          g_print ("   ");
      }
      x += run_len;
      if (!run_len || x > obj_w) {
        g_print ("\n");
        x = 0;
      }
#else
      g_print ("Run x: %d pix: %d col: %d\n", x, run_len, pal_id);
      x += run_len;
      if (x >= obj_w)
        x = 0;
#endif
    }
#endif

  };

  g_print ("\n");
}

static int
parse_presentation_segment (guint8 type, guint8 * payload, guint16 len)
{
  guint8 *end = payload + len;
  guint16 vid_w, vid_h;
  gint8 vid_fps;
  guint16 composition_desc_no;
  guint8 composition_desc_state;
  guint8 pres_seg_flags;
  guint8 palette_id;
  guint8 n_objects;
  gint i;

  /* Parse video descriptor */
  if (payload + 5 > end)
    return 0;
  vid_w = GST_READ_UINT16_BE (payload);
  vid_h = GST_READ_UINT16_BE (payload + 2);
  vid_fps = payload[4];
  payload += 5;

  /* Parse composition descriptor */
  if (payload + 3 > end)
    return 0;
  composition_desc_no = GST_READ_UINT16_BE (payload);
  composition_desc_state = payload[2];
  payload += 3;

  /* Parse other bits */
  if (payload + 3 > end)
    return 0;

  pres_seg_flags = payload[0];
  palette_id = payload[1];
  n_objects = payload[2];
  payload += 3;

  g_print ("Video width %u height %u fps code %u\n", vid_w, vid_h, vid_fps);
  g_print
      ("Composition num %u state %u flags 0x%02x palette id %u n_objects %u\n",
      composition_desc_no, composition_desc_state, pres_seg_flags, palette_id,
      n_objects);

  for (i = 0; i < (gint) n_objects; i++) {
    guint16 obj_id;
    guint8 win_id;
    guint8 obj_flags;
    guint16 x, y;

    if (payload + 8 > end)
      break;
    obj_id = GST_READ_UINT16_BE (payload);
    win_id = payload[2];
    obj_flags = payload[3];
    x = GST_READ_UINT16_BE (payload + 4);
    y = GST_READ_UINT16_BE (payload + 6);
    payload += 8;

    g_print ("Composition object %d Object ID %u Window ID %u flags 0x%02x "
        "x %u y %u\n", i, obj_id, win_id, obj_flags, x, y);

    if (obj_flags & PGS_COMP_OBJECT_FLAG_CROPPED) {
      guint16 crop_x, crop_y, crop_w, crop_h;
      if (payload + 8 > end)
        break;

      crop_x = GST_READ_UINT16_BE (payload);
      crop_y = GST_READ_UINT16_BE (payload + 2);
      crop_w = GST_READ_UINT16_BE (payload + 4);
      crop_h = GST_READ_UINT16_BE (payload + 6);
      payload += 8;

      g_print ("Cropping window x %u y %u w %u h %u\n",
          crop_x, crop_y, crop_w, crop_h);
    }
  }

  if (payload != end) {
    g_print ("%u bytes left over:\n", end - payload);
    dump_bytes (payload, end - payload);
  }

  return 0;
}

static int
parse_set_palette (guint8 type, guint8 * payload, guint16 len)
{
  const gint PGS_PALETTE_ENTRY_SIZE = 5;
  guint8 *end = payload + len;
  guint8 palette_id;
  guint8 palette_version;
  gint n_entries, i;

  if (len < 2)                  /* Palette command too short */
    return 0;
  palette_id = payload[0];
  palette_version = payload[1];
  payload += 2;

  n_entries = (len - 2) / PGS_PALETTE_ENTRY_SIZE;

  g_print ("Palette ID %u version %u. %d entries\n",
      palette_id, palette_version, n_entries);
  for (i = 0; i < n_entries; i++) {
    guint8 n, Y, Cb, Cr, A;
    n = payload[0];
    palette[n].n = n;
    palette[n].Y = Y = payload[1];
    palette[n].Cb = Cb = payload[2];
    palette[n].Cr = Cr = payload[3];
    palette[n].A = A = payload[4];

    g_print ("Entry %3d: Y %3d Cb %3d Cr %3d A %3d  ", n, Y, Cb, Cr, A);
    if (((i + 1) % 2) == 0)
      g_print ("\n");

    payload += PGS_PALETTE_ENTRY_SIZE;
  }
  for (i = n_entries; i < 256; i++) {
    palette[i].n = i;
    palette[i].A = 0;
  }

  if (n_entries > 0 && (i % 2))
    g_print ("\n");

  if (payload != end) {
    g_print ("%u bytes left over:\n", end - payload);
    dump_bytes (payload, end - payload);
  }

  return 0;
}

static int
parse_set_window (guint8 type, guint8 * payload, guint16 len)
{
  guint8 *end = payload + len;
  guint8 win_id, win_ver;
  guint16 x, y, w, h;

  if (payload + 10 > end)
    return 0;

  dump_bytes (payload, len);

  /* FIXME: This is just a guess as to what the numbers mean: */
  win_id = payload[0];
  win_ver = payload[1];
  x = GST_READ_UINT16_BE (payload + 2);
  y = GST_READ_UINT16_BE (payload + 4);
  w = GST_READ_UINT16_BE (payload + 6);
  h = GST_READ_UINT16_BE (payload + 8);
  payload += 10;

  g_print ("Win ID %u version %d x %d y %d w %d h %d\n",
      win_id, win_ver, x, y, w, h);

  if (payload != end) {
    g_print ("%u bytes left over:\n", end - payload);
    dump_bytes (payload, end - payload);
  }

  return 0;
}

static int
parse_set_object_data (guint8 type, guint8 * payload, guint16 len)
{
  guint8 *end = payload + len;
  guint16 obj_id;
  guint8 obj_ver, obj_flags;

  if (payload + 4 > end)
    return 0;
  obj_id = GST_READ_UINT16_BE (payload);
  obj_ver = payload[2];
  obj_flags = payload[3];
  payload += 4;

  g_print ("Object ID %d ver %u flags 0x%02x\n", obj_id, obj_ver, obj_flags);

  if (obj_flags & PGS_OBJECT_UPDATE_FLAG_START_RLE) {

    if (payload + 3 > end)
      return 0;

    rle_data_size = GST_READ_UINT24_BE (payload);
    payload += 3;

    g_print ("%d bytes of RLE data, of %d bytes total.\n",
        end - payload, rle_data_size);

    rle_data = g_realloc (rle_data, rle_data_size);
    rle_data_used = end - payload;
    memcpy (rle_data, payload, end - payload);
    payload = end;
  } else {
    g_print ("%d bytes of additional RLE data\n", end - payload);
    if (rle_data_size < rle_data_used + end - payload)
      return 0;

    memcpy (rle_data + rle_data_used, payload, end - payload);
    rle_data_used += end - payload;
    payload = end;
  }

  if (rle_data_size == rle_data_used)
    dump_rle_data (rle_data, rle_data_size);

  if (payload != end) {
    g_print ("%u bytes left over:\n", end - payload);
    dump_bytes (payload, end - payload);
  }

  return 0;
}

static int
parse_pgs_packet (guint8 type, guint8 * payload, guint16 len)
{
  int ret = 0;

  if (!in_presentation_segment && type != PGS_COMMAND_PRESENTATION_SEGMENT) {
    g_print ("Expected BEGIN PRESENTATION SEGMENT command. "
        "Got command type 0x%02x len %u. Skipping\n", type, len);
    return 0;
  }

  switch (type) {
    case PGS_COMMAND_PRESENTATION_SEGMENT:
      g_print ("*******************************************\n"
          "Begin PRESENTATION_SEGMENT (0x%02x) packet len %u\n", type, len);
      in_presentation_segment = TRUE;
      ret = parse_presentation_segment (type, payload, len);
      break;
    case PGS_COMMAND_SET_OBJECT_DATA:
      g_print ("***   Set Object Data (0x%02x) packet len %u\n", type, len);
      ret = parse_set_object_data (type, payload, len);
      break;
    case PGS_COMMAND_SET_PALETTE:
      g_print ("***   Set Palette (0x%02x) packet len %u\n", type, len);
      ret = parse_set_palette (type, payload, len);
      break;
    case PGS_COMMAND_SET_WINDOW:
      g_print ("***   Set Window command (0x%02x) packet len %u\n", type, len);
      ret = parse_set_window (type, payload, len);
      break;
    case PGS_COMMAND_INTERACTIVE_SEGMENT:
      g_print ("***   Interactive Segment command(0x%02x) packet len %u\n",
          type, len);
      dump_bytes (payload, len);
      break;
    case PGS_COMMAND_END_DISPLAY:
      g_print ("***   End Display command (0x%02x) packet len %u\n", type, len);
      in_presentation_segment = FALSE;
      break;
    default:
      g_print ("*** Unknown command: type 0x%02x len %u. Skipping\n", type,
          len);
      break;
  }
  g_print ("\n");

  return ret;
}

gint
gstspu_dump_pgs_buffer (GstBuffer * buf)
{
  guint8 *pos, *end;
  guint8 type;
  guint16 packet_len;

  pos = GST_BUFFER_DATA (buf);
  end = pos + GST_BUFFER_SIZE (buf);

  /* Need at least 3 bytes */
  if (pos + 3 > end) {
    g_print ("Not enough bytes to be a PGS packet\n");
    return -1;
  }

  g_print ("Begin dumping command buffer of size %u ts %" GST_TIME_FORMAT "\n",
      end - pos, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)));
  do {
    type = *pos++;
    packet_len = GST_READ_UINT16_BE (pos);
    pos += 2;

    if (pos + packet_len > end) {
      g_print ("Invalid packet length %u (only have %u bytes)\n", packet_len,
          end - pos);
      return -1;
    }

    if (parse_pgs_packet (type, pos, packet_len))
      return -1;

    pos += packet_len;
  } while (pos + 3 <= end);

  g_print ("End dumping command buffer with %u bytes remaining\n", end - pos);
  return (pos - GST_BUFFER_DATA (buf));
}
