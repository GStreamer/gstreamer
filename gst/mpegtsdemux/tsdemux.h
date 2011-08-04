/*
 * tsdemux - GStreamer MPEG transport stream demuxer
 * Copyright (C) 2009 Zaheer Abbas Merali
 *               2010 Edward Hervey
 *
 * Authors:
 *   Zaheer Abbas Merali <zaheerabbas at merali dot org>
 *   Edward Hervey <edward.hervey@collabora.co.uk>
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


#ifndef GST_TS_DEMUX_H
#define GST_TS_DEMUX_H

#include <gst/gst.h>
#include <gst/base/gstbytereader.h>
#include "mpegtsbase.h"
#include "mpegtspacketizer.h"

G_BEGIN_DECLS
#define GST_TYPE_TS_DEMUX \
  (gst_ts_demux_get_type())
#define GST_TS_DEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_TS_DEMUX,GstTSDemux))
#define GST_TS_DEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_TS_DEMUX,GstTSDemuxClass))
#define GST_IS_TS_DEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_TS_DEMUX))
#define GST_IS_TS_DEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_TS_DEMUX))
#define GST_TS_DEMUX_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_TS_DEMUX, GstTSDemuxClass))
#define GST_TS_DEMUX_CAST(obj) ((GstTSDemux*) obj)
typedef struct _GstTSDemux GstTSDemux;
typedef struct _GstTSDemuxClass GstTSDemuxClass;
typedef struct _TSPcrOffset TSPcrOffset;

struct _TSPcrOffset
{
  guint64 gsttime;
  guint64 pcr;
  guint64 offset;
};

struct _GstTSDemux
{
  MpegTSBase parent;

  /* the following vars must be protected with the OBJECT_LOCK as they can be
   * accessed from the application thread and the streaming thread */
  guint program_number;		/* Required program number (ignore:-1) */
  gboolean emit_statistics;

  /*< private >*/
  MpegTSBaseProgram *program;	/* Current program */
  guint	current_program_number;
  gboolean need_newsegment;
  /* Downstream segment */
  GstSegment segment;
  GstClockTime duration;	/* Total duration */

  /* pcr wrap and seeking */
  GArray *index;
  gint index_size;
  TSPcrOffset first_pcr;
  TSPcrOffset last_pcr;
  TSPcrOffset cur_pcr;
  TSPcrOffset index_pcr;
};

struct _GstTSDemuxClass
{
  MpegTSBaseClass parent_class;
};

GType gst_ts_demux_get_type (void);

gboolean gst_ts_demux_plugin_init (GstPlugin * plugin);

G_END_DECLS
#endif /* GST_TS_DEMUX_H */
