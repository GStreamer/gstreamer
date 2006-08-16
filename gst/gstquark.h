/* GStreamer
 * Copyright (C) 2006 Jan Schmidt <thaytan@noraisin.net>
 *
 * gstquark.h: Private header for storing quark info 
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

#ifndef __GST_QUARK_H__
#define __GST_QUARK_H__

/* These enums need to match the number and order
 * of strings declared in _quark_table, in gstquark.c */
typedef enum _GstQuarkId
{
  GST_QUARK_FORMAT = 0,
  GST_QUARK_CURRENT = 1,
  GST_QUARK_DURATION = 2,
  GST_QUARK_RATE = 3,
  GST_QUARK_SEEKABLE = 4,
  GST_QUARK_SEGMENT_START = 5,
  GST_QUARK_SEGMENT_END = 6,
  GST_QUARK_SRC_FORMAT = 7,
  GST_QUARK_SRC_VALUE = 8,
  GST_QUARK_DEST_FORMAT = 9,
  GST_QUARK_DEST_VALUE = 10,
  GST_QUARK_START_FORMAT = 11,
  GST_QUARK_START_VALUE = 12,
  GST_QUARK_STOP_FORMAT = 13,
  GST_QUARK_STOP_VALUE = 14,

  GST_QUARK_MAX = 15
} GstQuarkId;

extern GQuark _priv_gst_quark_table[GST_QUARK_MAX];

#define GST_QUARK(q) _priv_gst_quark_table[GST_QUARK_##q]

#endif
