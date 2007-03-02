/* -*- c-basic-offset: 2 -*-
 * GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *
 * Tremor modifications <2006>:
 *   Chris Lord, OpenedHand Ltd. <chris@openedhand.com>, http://www.o-hand.com/
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


#ifndef __GST_IVORBIS_DEC_H__
#define __GST_IVORBIS_DEC_H__


#include <gst/gst.h>
#include <tremor/ivorbiscodec.h>

G_BEGIN_DECLS

#define GST_TYPE_IVORBIS_DEC \
  (gst_ivorbis_dec_get_type())
#define GST_IVORBIS_DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_IVORBIS_DEC,GstIVorbisDec))
#define GST_IVORBIS_DEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_IVORBIS_DEC,GstIVorbisDecClass))
#define GST_IS_IVORBIS_DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_IVORBIS_DEC))
#define GST_IS_IVORBIS_DEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_IVORBIS_DEC))

typedef struct _GstIVorbisDec GstIVorbisDec;
typedef struct _GstIVorbisDecClass GstIVorbisDecClass;

/**
 * GstIVorbisDec:
 *
 * Opaque data structure.
 */
struct _GstIVorbisDec {
  GstElement            element;

  GstPad *              sinkpad;
  GstPad *              srcpad;

  vorbis_dsp_state      vd;
  vorbis_info           vi;
  vorbis_comment        vc;
  vorbis_block          vb;
  guint64               granulepos;

  gboolean              initialized;

  GList                 *queued;

  GstSegment		segment;
  gboolean		discont;

  GstClockTime          cur_timestamp; /* only used with non-ogg container formats */
  GstClockTime          prev_timestamp; /* only used with non-ogg container formats */

  GList			*pendingevents;
  GstTagList		*taglist;
};

struct _GstIVorbisDecClass {
  GstElementClass parent_class;
};

GType gst_ivorbis_dec_get_type(void);

G_END_DECLS

#endif /* __GST_IVORBIS_DEC_H__ */
