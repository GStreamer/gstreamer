/*
 * GStreamer
 *
 * unit test data for aacparse
 *
 * Copyright (C) 2008 Nokia Corporation. All rights reserved.
 *
 * Contact: Stefan Kost <stefan.kost@nokia.com>
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

#define ADIF_HEADER_LEN 4

static const unsigned char adif_header[]=
{
  'A','D','I','F'
};

#define ADTS_FRAME_LEN 15

static const unsigned char adts_frame_mpeg2[]=
{
  0xff, 0xf9, 0x4c, 0x80, 0x01, 0xff, 0xfc, 0x21, 0x10, 0xd3, 0x20, 0x0c,
  0x32, 0x00, 0xc7
};

static const unsigned char adts_frame_mpeg4[]=
{
  0xff, 0xf1, 0x4c, 0x80, 0x01, 0xff, 0xfc, 0x21, 0x10, 0xd3, 0x20, 0x0c,
  0x32, 0x00, 0xc7
};

#define GARBAGE_FRAME_LEN 5

static const unsigned char garbage_frame[]=
{
  0xff, 0xff, 0xff, 0xff, 0xff
};
