/*
 * This library is licensed under 2 different licenses and you
 * can choose to use it under the terms of either one of them. The
 * two licenses are the MPL 1.1 and the LGPL.
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * The Original Code is Fluendo MPEG Demuxer plugin.
 *
 * The Initial Developer of the Original Code is Fluendo, S.L.
 * Portions created by Fluendo, S.L. are Copyright (C) 2005
 * Fluendo, S.L. All Rights Reserved.
 *
 * Contributor(s): Wim Taymans <wim@fluendo.com>
 */

#ifndef __GST_FLUTS_DEMUX_H__
#define __GST_FLUTS_DEMUX_H__

#include <gst/gst.h>
#include <gst/base/gstadapter.h>

#include "gstmpegdesc.h"
#include "gstpesfilter.h"
#include "gstsectionfilter.h"

G_BEGIN_DECLS

#if (POST_10_12)
#define HAVE_LATENCY
#endif

#define FLUTS_MIN_PES_BUFFER_SIZE     4 * 1024
#define FLUTS_MAX_PES_BUFFER_SIZE   256 * 1024

#define FLUTS_MAX_PID 0x1fff
#define FLUTS_NORMAL_TS_PACKETSIZE  188
#define FLUTS_M2TS_TS_PACKETSIZE    192

#define LENGHT_SYNC_LUT             256

#define IS_MPEGTS_SYNC(data) (((data)[0] == 0x47) && \
                                    (((data)[1] & 0x80) == 0x00) && \
                                    (((data)[3] & 0x10) == 0x10))

#define GST_TYPE_FLUTS_DEMUX              (gst_fluts_demux_get_type())
#define GST_FLUTS_DEMUX(obj)              (G_TYPE_CHECK_INSTANCE_CAST((obj),\
                                            GST_TYPE_FLUTS_DEMUX,GstFluTSDemux))
#define GST_FLUTS_DEMUX_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST((klass),\
                                            GST_TYPE_FLUTS_DEMUX,GstFluTSDemuxClass))
#define GST_FLUTS_DEMUX_GET_CLASS(klass)  (G_TYPE_INSTANCE_GET_CLASS((klass),\
                                            GST_TYPE_FLUTS_DEMUX,GstFluTSDemuxClass))
#define GST_IS_FLUTS_DEMUX(obj)           (G_TYPE_CHECK_INSTANCE_TYPE((obj),\
                                            GST_TYPE_FLUTS_DEMUX))
#define GST_IS_FLUTS_DEMUX_CLASS(obj)     (G_TYPE_CHECK_CLASS_TYPE((klass),\
                                            GST_TYPE_FLUTS_DEMUX))

typedef struct _GstFluTSStream GstFluTSStream;
typedef struct _GstFluTSPMTEntry GstFluTSPMTEntry;
typedef struct _GstFluTSPMT GstFluTSPMT;
typedef struct _GstFluTSPATEntry GstFluTSPATEntry;
typedef struct _GstFluTSPAT GstFluTSPAT;
typedef struct _GstFluTSDemux GstFluTSDemux;
typedef struct _GstFluTSDemuxClass GstFluTSDemuxClass;

struct _GstFluTSPMTEntry {
  guint16           PID;
};

struct _GstFluTSPMT {
  guint16           program_number;
  guint8            version_number;
  gboolean          current_next_indicator;
  guint8            section_number;
  guint8            last_section_number;
  guint16           PCR_PID;
  guint16           program_info_length;
  GstMPEGDescriptor * program_info;

  GArray            * entries;
};

struct _GstFluTSPATEntry {
  guint16           program_number;
  guint16           PID;
};

struct _GstFluTSPAT  {
  guint16           transport_stream_id;
  guint8            version_number;
  gboolean          current_next_indicator;
  guint8            section_number;
  guint8            last_section_number;

  GArray            * entries;
};

typedef enum _FluTsStreamFlags {
  FLUTS_STREAM_FLAG_STREAM_TYPE_UNKNOWN = 0x01,
  FLUTS_STREAM_FLAG_PMT_VALID = 0x02,
  FLUTS_STREAM_FLAG_IS_VIDEO  = 0x04
} FluTsStreamFlags;

/* Information associated to a single MPEG stream. */
struct _GstFluTSStream {
  GstFluTSDemux     * demux;

  FluTsStreamFlags  flags;

  /* PID and type */
  guint16           PID;
  guint8            PID_type;

  /* adaptation_field data */
  guint64           last_PCR;
  guint64           base_PCR;
  guint64           last_OPCR;
  guint64           last_PCR_difference;
  gboolean          discont_PCR;
  GstClockTimeDiff  discont_difference;

  /* for PAT streams */
  GstFluTSPAT       PAT;

  /* for PMT streams */
  GstFluTSPMT       PMT;

  /* for CA streams */

  /* for PAT, PMT, CA and private streams */
  GstSectionFilter  section_filter;

  /* for PES streams */
  guint8            id;
  guint8            stream_type;
  GstBuffer         * pes_buffer;
  guint32           pes_buffer_size;
  guint32           pes_buffer_used;
  gboolean          pes_buffer_overflow;
  GstPESFilter      filter;
  GstPad            * pad;
  GstFlowReturn     last_ret;
  GstMPEGDescriptor *ES_info;
  /* needed because 33bit mpeg timestamps wrap around every (approx) 26.5 hrs */
  GstClockTimeDiff  base_time;
  GstClockTime      last_time;
  /* pid of PMT that this stream belongs to */
  guint16           PMT_pid;
};

struct _GstFluTSDemux {
  GstElement        parent;

  /* properties */
  gboolean          check_crc;

  /* sink pad and adapter */
  GstPad            * sinkpad;
  GstAdapter        * adapter;
  guint8            ** sync_lut;

  /* current PMT PID */
  guint16           current_PMT;

  /* Array of FLUTS_MAX_PID + 1 stream entries */
  GstFluTSStream    **  streams;
  /* Array to perform pmts checks at gst_fluts_demux_parse_adaptation_field */
  gboolean          pmts_checked[FLUTS_MAX_PID + 1];
  
  /* Array of Elementary Stream pids for ts with PMT */
  guint16           * elementary_pids;
  guint             nb_elementary_pids;

  /* Program number to use */
  gint              program_number;

  /* indicates that we need to close our pad group, because we've added
   * at least one pad */
  gboolean          need_no_more_pads;
  guint16           packetsize;
  gboolean          m2ts_mode;
#ifdef HAVE_LATENCY
  /* clocking */
  GstClock          * clock;
  GstClockTime      clock_base;
#endif
};

struct _GstFluTSDemuxClass {
  GstElementClass   parent_class;

  GstPadTemplate    * sink_template;
  GstPadTemplate    * video_template;
  GstPadTemplate    * audio_template;
  GstPadTemplate    * private_template;
};

GType     gst_fluts_demux_get_type (void);

gboolean  gst_fluts_demux_plugin_init (GstPlugin *plugin);

G_END_DECLS

#endif /* __GST_FLUTS_DEMUX_H__ */
