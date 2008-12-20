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
  GST_QUARK_GERROR = 15,
  GST_QUARK_DEBUG = 16,
  GST_QUARK_BUFFER_PERCENT = 17,
  GST_QUARK_BUFFERING_MODE = 18,
  GST_QUARK_AVG_IN_RATE = 19,
  GST_QUARK_AVG_OUT_RATE = 20,
  GST_QUARK_BUFFERING_LEFT = 21,
  GST_QUARK_ESTIMATED_TOTAL = 22,
  GST_QUARK_OLD_STATE = 23,
  GST_QUARK_NEW_STATE = 24,
  GST_QUARK_PENDING_STATE = 25,
  GST_QUARK_CLOCK = 26,
  GST_QUARK_READY = 27,
  GST_QUARK_POSITION = 28,
  GST_QUARK_NEW_BASE_TIME = 29,
  GST_QUARK_LIVE = 30,
  GST_QUARK_MIN_LATENCY = 31,
  GST_QUARK_MAX_LATENCY = 32,
  GST_QUARK_BUSY = 33,
  GST_QUARK_TYPE = 34,
  GST_QUARK_OWNER = 35,
  GST_QUARK_UPDATE = 36,
  GST_QUARK_APPLIED_RATE = 37,
  GST_QUARK_START = 38,
  GST_QUARK_STOP = 39,
  GST_QUARK_MINSIZE = 40,
  GST_QUARK_MAXSIZE = 41,
  GST_QUARK_ASYNC = 42,
  GST_QUARK_PROPORTION = 43,
  GST_QUARK_DIFF = 44,
  GST_QUARK_TIMESTAMP = 45,
  GST_QUARK_FLAGS = 46,
  GST_QUARK_CUR_TYPE = 47,
  GST_QUARK_CUR = 48,
  GST_QUARK_STOP_TYPE = 49,
  GST_QUARK_LATENCY = 50,
  GST_QUARK_URI = 51,

  GST_QUARK_MAX = 52
} GstQuarkId;

extern GQuark _priv_gst_quark_table[GST_QUARK_MAX];

#define GST_QUARK(q) _priv_gst_quark_table[GST_QUARK_##q]

#endif
