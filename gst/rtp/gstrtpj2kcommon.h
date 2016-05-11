/* GStreamer
* Copyright (C) 2009 Wim Taymans <wim.taymans@gmail.com>
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


#ifndef __GST_RTP_J2K_COMMON_H__
#define __GST_RTP_J2K_COMMON_H__


/*
* GstRtpJ2KMarker:
* @GST_J2K_MARKER: Prefix for JPEG 2000 marker
* @GST_J2K_MARKER_SOC: Start of Codestream
* @GST_J2K_MARKER_SOT: Start of tile
* @GST_J2K_MARKER_EOC: End of Codestream
*
* Identifers for markers in JPEG 2000 codestreams
*/
typedef enum
{
  GST_J2K_MARKER = 0xFF,
  GST_J2K_MARKER_SOC = 0x4F,
  GST_J2K_MARKER_SOT = 0x90,
  GST_J2K_MARKER_SOP = 0x91,
  GST_J2K_MARKER_EPH = 0x92,
  GST_J2K_MARKER_SOD = 0x93,
  GST_J2K_MARKER_EOC = 0xD9
} GstRtpJ2KMarker;


#define GST_RTP_J2K_HEADER_SIZE 8


#endif /* __GST_RTP_J2K_COMMON_H__ */
