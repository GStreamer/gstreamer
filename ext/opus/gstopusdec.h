/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2008> Sebastian Dröge <sebastian.droege@collabora.co.uk>
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

#ifndef __GST_OPUS_DEC_H__
#define __GST_OPUS_DEC_H__

#include <gst/gst.h>
#include <opus/opus.h>

G_BEGIN_DECLS

#define GST_TYPE_OPUS_DEC \
  (gst_opus_dec_get_type())
#define GST_OPUS_DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_OPUS_DEC,GstOpusDec))
#define GST_OPUS_DEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_OPUS_DEC,GstOpusDecClass))
#define GST_IS_OPUS_DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_OPUS_DEC))
#define GST_IS_OPUS_DEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_OPUS_DEC))

typedef struct _GstOpusDec GstOpusDec;
typedef struct _GstOpusDecClass GstOpusDecClass;

struct _GstOpusDec {
  GstElement            element;

  /* pads */
  GstPad                *sinkpad;
  GstPad                *srcpad;

  OpusDecoder          *state;
  int frame_samples;

  gint                  frame_size;
  GstClockTime          frame_duration;
  guint64               packetno;

  GstSegment            segment;    /* STREAM LOCK */
  gint64                granulepos; /* -1 = needs to be set from current time */
  gboolean              discont;

  GstBuffer            *streamheader;
  GstBuffer            *vorbiscomment;
  GList                *extra_headers;

  int sample_rate;
  int n_channels;
};

struct _GstOpusDecClass {
  GstElementClass parent_class;
};

GType gst_opus_dec_get_type (void);

G_END_DECLS

#endif /* __GST_OPUS_DEC_H__ */
