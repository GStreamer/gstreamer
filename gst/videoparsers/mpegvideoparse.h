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

#ifndef __GST_MPEGVIDEO_PARAMS_H__
#define __GST_MPEGVIDEO_PARAMS_H__

#include <gst/gst.h>

G_BEGIN_DECLS

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

typedef struct _MPEGVParams MPEGVParams;

struct _MPEGVParams
{
  gint  mpeg_version;

  gint  profile;
  gint  level;

  gint  width, height;
  gint  par_w, par_h;
  gint  fps_n, fps_d;

  gint  bitrate;
  gboolean progressive;
};

GstFlowReturn gst_mpeg_video_params_parse_config (MPEGVParams * params,
                                                  const guint8 * data, guint size);

G_END_DECLS

#endif
