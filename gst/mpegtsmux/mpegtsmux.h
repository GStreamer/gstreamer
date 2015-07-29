/* 
 * Copyright 2006, 2007, 2008, 2009, 2010 Fluendo S.A. 
 *  Authors: Jan Schmidt <jan@fluendo.com>
 *           Kapil Agrawal <kapil@fluendo.com>
 *           Julien Moutte <julien@fluendo.com>
 *
 * This library is licensed under 4 different licenses and you
 * can choose to use it under the terms of any one of them. The
 * four licenses are the MPL 1.1, the LGPL, the GPL and the MIT
 * license.
 *
 * MPL:
 * 
 * The contents of this file are subject to the Mozilla Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/.
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the
 * License for the specific language governing rights and limitations
 * under the License.
 *
 * LGPL:
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
 *
 * GPL:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * MIT:
 *
 * Unless otherwise indicated, Source Code is licensed under MIT license.
 * See further explanation attached in License Statement (distributed in the file
 * LICENSE).
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#ifndef __MPEGTSMUX_H__
#define __MPEGTSMUX_H__

#include <gst/gst.h>
#include <gst/base/gstcollectpads.h>
#include <gst/base/gstadapter.h>

G_BEGIN_DECLS

#include <tsmux/tsmux.h>

#define GST_TYPE_MPEG_TSMUX  (mpegtsmux_get_type())
#define GST_MPEG_TSMUX(obj)  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_MPEG_TSMUX, MpegTsMux))

#define CLOCK_BASE 9LL
#define CLOCK_FREQ (CLOCK_BASE * 10000)   /* 90 kHz PTS clock */
#define CLOCK_FREQ_SCR (CLOCK_FREQ * 300) /* 27 MHz SCR clock */

#define GSTTIME_TO_MPEGTIME(time) \
    (((time) > 0 ? (gint64) 1 : (gint64) -1) * \
    (gint64) gst_util_uint64_scale (ABS(time), CLOCK_BASE, GST_MSECOND/10))

/* 27 MHz SCR conversions: */
#define MPEG_SYS_TIME_TO_GSTTIME(time) (gst_util_uint64_scale ((time), \
                        GST_USECOND, CLOCK_FREQ_SCR / 1000000))
#define GSTTIME_TO_MPEG_SYS_TIME(time) (gst_util_uint64_scale ((time), \
                        CLOCK_FREQ_SCR / 1000000, GST_USECOND))

#define NORMAL_TS_PACKET_LENGTH 188
#define M2TS_PACKET_LENGTH      192

#define DEFAULT_PROG_ID	0

typedef struct MpegTsMux MpegTsMux;
typedef struct MpegTsMuxClass MpegTsMuxClass;
typedef struct MpegTsPadData MpegTsPadData;

typedef GstBuffer * (*MpegTsPadDataPrepareFunction) (GstBuffer * buf,
    MpegTsPadData * data, MpegTsMux * mux);

typedef void (*MpegTsPadDataFreePrepareDataFunction) (gpointer prepare_data);

struct MpegTsMux {
  GstElement parent;

  GstPad *srcpad;

  GstCollectPads *collect;

  TsMux *tsmux;
  GHashTable *programs;

  /* properties */
  gboolean m2ts_mode;
  GstStructure *prog_map;
  guint pat_interval;
  guint pmt_interval;
  gint alignment;
  guint si_interval;

  /* state */
  gboolean first;
  GstClockTime pending_key_unit_ts;
  GstEvent *force_key_unit_event;

  /* write callback handling/state */
  GstFlowReturn last_flow_ret;
  GQueue streamheader;
  gboolean streamheader_sent;
  gboolean is_delta;
  gboolean is_header;
  GstClockTime last_ts;

  /* m2ts specific */
  gint64 previous_pcr;
  gint64 previous_offset;
  gint64 pcr_rate_num;
  gint64 pcr_rate_den;
  GstAdapter *adapter;

  /* output buffer aggregation */
  GstAdapter *out_adapter;
  GstBuffer *out_buffer;

#if 0
  /* SPN/PTS index handling */
  GstIndex *element_index;
  gint spn_count;
#endif
};

struct MpegTsMuxClass {
  GstElementClass parent_class;
};

struct MpegTsPadData {
  /* parent */
  GstCollectData collect;

  gint pid;
  TsMuxStream *stream;

  /* most recent DTS */
  gint64 dts;

#if 0
  /* (optional) index writing */
  gint element_index_writer_id;
#endif

  /* optional codec data available in the caps */
  GstBuffer *codec_data;

  /* Opaque data pointer to a structure used by the prepare function */
  gpointer prepare_data;

  /* handler to prepare input data */
  MpegTsPadDataPrepareFunction prepare_func;
  /* handler to free the private data */
  MpegTsPadDataFreePrepareDataFunction free_func;

  /* program id to which it is attached to (not program pid) */
  gint prog_id;
  /* program this stream belongs to */
  TsMuxProgram *prog;

  gchar *language;
};

GType mpegtsmux_get_type (void);


G_END_DECLS

#endif
