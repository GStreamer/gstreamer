/* GStreamer
 * Copyright (C) 2004 Benjamin Otte <in7y118@public.uni-hamburg.de>
 *               2006 Michael Smith <msmith@fluendo.com>
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

#define GST_TYPE_THEORA_DEC_EXP \
  (gst_theoradec_get_type())
#define GST_THEORA_DEC_EXP(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_THEORA_DEC_EXP,GstTheoraExpDec))
#define GST_THEORA_DEC_EXP_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_THEORA_DEC_EXP,GstTheoraExpDecClass))
#define GST_IS_THEORA_DEC_EXP(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_THEORA_DEC_EXP))
#define GST_IS_THEORA_DEC_EXP_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_THEORA_DEC_EXP))

typedef struct _GstTheoraExpDec GstTheoraExpDec;
typedef struct _GstTheoraExpDecClass GstTheoraExpDecClass;

/**
 * GstTheoraExpDec:
 *
 * Decoder using theora-exp
 */
struct _GstTheoraExpDec
{
  /* <private> */
  GstElement element;

  /* Pads */
  GstPad *sinkpad;
  GstPad *srcpad;

  /* theora decoder state */
  th_dec_ctx *dec;
  th_setup_info *setup;

  th_info info;
  th_comment comment;

  gboolean have_header;
  guint64 granulepos;
  guint64 granule_shift;

  GstClockTime last_timestamp;
  gboolean need_keyframe;
  gint width, height;
  gint offset_x, offset_y;
  gint output_bpp;

  int frame_nr;
  gboolean discont;

  GList *queued;

  /* segment info */ /* with STREAM_LOCK */
  GstSegment segment;

  /* QoS stuff */ /* with LOCK*/
  gboolean proportion;
  GstClockTime earliest_time;
};

struct _GstTheoraExpDecClass
{
  GstElementClass parent_class;
};

G_END_DECLS

#endif /* __GST_THEORADEC_H__ */
