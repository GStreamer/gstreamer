/* GStreamer
 * Copyright (C) 2007 Jan Schmidt <thaytan@mad.scientist.com>
 * Copyright (C) 2009 Carl-Anton Ingmarsson <ca.ingmarsson@gmail.com>
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

#ifndef __MPEGUTIL_H__
#define __MPEGUTIL_H__

#include <gst/gst.h>

typedef struct MPEGSeqHdr MPEGSeqHdr;

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

  /* mpeg2 decoder profile */
  gint profile;
};

gboolean mpeg_util_parse_sequence_hdr (MPEGSeqHdr *hdr, 
    guint8 *data, guint8 *end);

#endif
