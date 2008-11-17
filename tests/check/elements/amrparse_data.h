/*
 * GStreamer
 *
 * unit test data for amrparse
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

#define FRAME_DATA_NB_LEN 14

static const unsigned char frame_data_nb[]=
{
  0x0c,0x56,0x3c,0x52,0xe0,0x61,0xbc,0x45,0x0f,0x98,0x2e,0x01,0x42,0x02
};

#define FRAME_DATA_WB_LEN 24

static const unsigned char frame_data_wb[]=
{
  0x08,0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,
  0x0d,0x0e,0x0f,0x10,0x11,0x12,0x13,0x14,0x15,0x16
};

#define FRAME_HDR_NB_LEN 6

static const unsigned char frame_hdr_nb[]=
{
  '#','!','A','M','R','\n'
};

#define FRAME_HDR_WB_LEN 9

static const unsigned char frame_hdr_wb[]=
{
  '#','!','A','M','R','-','W','B','\n'
};

#define GARBAGE_FRAME_LEN 5

static const unsigned char garbage_frame[]=
{
  0xff, 0xff, 0xff, 0xff, 0xff
};
