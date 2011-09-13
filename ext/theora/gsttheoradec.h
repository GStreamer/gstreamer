/* GStreamer
 * Copyright (C) 2004 Benjamin Otte <in7y118@public.uni-hamburg.de>
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

#ifndef __GST_THEORADEC_H__
#define __GST_THEORADEC_H__

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <gst/gst.h>
#include <theora/theoradec.h>
#include <string.h>

G_BEGIN_DECLS

#define GST_TYPE_THEORA_DEC \
  (gst_theora_dec_get_type())
#define GST_THEORA_DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_THEORA_DEC,GstTheoraDec))
#define GST_THEORA_DEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_THEORA_DEC,GstTheoraDecClass))
#define GST_IS_THEORA_DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_THEORA_DEC))
#define GST_IS_THEORA_DEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_THEORA_DEC))

typedef struct _GstTheoraDec GstTheoraDec;
typedef struct _GstTheoraDecClass GstTheoraDecClass;

/**
 * GstTheoraDec:
 *
 * Opaque object data structure.
 */
struct _GstTheoraDec
{
  GstElement element;

  /* Pads */
  GstPad *sinkpad;
  GstPad *srcpad;

  /* theora decoder state */
  th_dec_ctx *decoder;
  //theora_state state;
  th_setup_info *setup;
  th_info info;
  th_comment comment;

  gboolean have_header;

  GstClockTime last_timestamp;
  guint64 frame_nr;
  gboolean need_keyframe;
  gint width, height;
  gint offset_x, offset_y;
  gint output_bpp;

  /* telemetry debugging options */
  gint telemetry_mv;
  gint telemetry_mbmode;
  gint telemetry_qi;
  gint telemetry_bits;

  gboolean crop;

  /* list of buffers that need timestamps */
  GList *queued;
  /* list of raw output buffers */
  GList *output;
  /* gather/decode queues for reverse playback */
  GList *gather;
  GList *decode;
  GList *pendingevents;

  GstTagList *tags;

  /* segment info */ /* with STREAM_LOCK */
  GstSegment segment;
  gboolean discont;
  guint32 seqnum;

  /* QoS stuff */ /* with LOCK*/
  gdouble proportion;
  GstClockTime earliest_time;
  guint64 processed;
  guint64 dropped;

  gboolean have_par;
  gint par_num;
  gint par_den;
};

struct _GstTheoraDecClass
{
  GstElementClass parent_class;
};

GType gst_theora_dec_get_type (void);

G_END_DECLS

#endif /* __GST_THEORADEC_H__ */
