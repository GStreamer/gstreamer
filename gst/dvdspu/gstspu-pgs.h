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

#ifndef __GSTSPU_PGS_H__
#define __GSTSPU_PGS_H__

typedef enum PgsCommandType {
  PGS_COMMAND_SET_PALETTE                       = 0x14,
  PGS_COMMAND_SET_OBJECT_DATA                   = 0x15,
  PGS_COMMAND_PRESENTATION_SEGMENT              = 0x16,
  PGS_COMMAND_SET_WINDOW                        = 0x17,
  PGS_COMMAND_INTERACTIVE_SEGMENT               = 0x18,

  PGS_COMMAND_END_DISPLAY                       = 0x80,

  PGS_COMMAND_INVALID                           = 0xFFFF
} PgsCommandType;

typedef enum PgsPresSegmentFlags {
  PGS_PRES_SEGMENT_FLAG_UPDATE_PALETTE          = 0x80
} PgsPresSegmentFlags;

typedef enum PgsCompObjectFlags {
  PGS_COMP_OBJECT_FLAG_CROPPED                  = 0x80,
  PGS_COMP_OBJECT_FLAG_FORCED                   = 0x40
} PgsCompObjectFlags;

typedef enum PgsObjectUpdateFlags {
  /* Set in an object_update if this is the beginning of new RLE data.
   * If not set, the data is a continuation to be appended */
  PGS_OBJECT_UPDATE_FLAG_START_RLE              = 0x80
} PgsObjectUpdateFlags;

typedef struct PgsPaletteEntry {
  guint8 n;
  guint8 Y;
  guint8 Cb;
  guint8 Cr;
  guint8 A;
} PgsPaletteEntry;

gint gstspu_dump_pgs_buffer (GstBuffer *buf);

#endif
