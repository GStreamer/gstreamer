/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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


#ifndef __GST_WAVPARSE_H__
#define __GST_WAVPARSE_H__


#include <gst/gst.h>
#include "gst/riff/riff-ids.h"
#include "gst/riff/riff-read.h"

G_BEGIN_DECLS

#define GST_TYPE_WAVPARSE \
  (gst_wavparse_get_type())
#define GST_WAVPARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_WAVPARSE,GstWavParse))
#define GST_WAVPARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_WAVPARSE,GstWavParse))
#define GST_IS_WAVPARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_WAVPARSE))
#define GST_IS_WAVPARSE_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_WAVPARSE))

typedef enum {
  GST_WAVPARSE_START,
  GST_WAVPARSE_HEADER,
  GST_WAVPARSE_DATA,
} GstWavParseState;

typedef struct _GstWavParse GstWavParse;
typedef struct _GstWavParseClass GstWavParseClass;

struct _GstWavParse {
  GstElement parent;

  /* pads */
  GstPad *sinkpad,*srcpad;

  /* WAVE decoding state */
  GstWavParseState state;
  guint64	offset;

  /* format of audio, see defines below */
  gint format;

  /* useful audio data */
  guint16 depth;
  gint rate;
  guint16 channels;
  guint16 blockalign;
  guint16 width;
  guint32 bps;

  guint bytes_per_sample;

  guint64 dataleft, datasize, datastart;
  
  gboolean seek_pending;
  GstEvent *seek_event;

  /* configured segment, start/stop expressed in
   * bytes */
  gdouble segment_rate;
  GstSeekFlags segment_flags;
  gint64 segment_start;
  gint64 segment_stop;
};

struct _GstWavParseClass {
  GstElementClass parent_class;
};

GType gst_wavparse_get_type(void);

G_END_DECLS

#endif /* __GST_WAVPARSE_H__ */
