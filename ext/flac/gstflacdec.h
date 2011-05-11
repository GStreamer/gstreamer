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


#ifndef __GST_FLAC_DEC_H__
#define __GST_FLAC_DEC_H__


#include <gst/gst.h>
#include <gst/base/gstadapter.h>

#include <FLAC/all.h>

G_BEGIN_DECLS

#define GST_TYPE_FLAC_DEC gst_flac_dec_get_type()
#define GST_FLAC_DEC(obj) G_TYPE_CHECK_INSTANCE_CAST(obj, GST_TYPE_FLAC_DEC, GstFlacDec)
#define GST_FLAC_DEC_CLASS(klass) G_TYPE_CHECK_CLASS_CAST(klass, GST_TYPE_FLAC_DEC, GstFlacDecClass)
#define GST_IS_FLAC_DEC(obj) G_TYPE_CHECK_INSTANCE_TYPE(obj, GST_TYPE_FLAC_DEC)
#define GST_IS_FLAC_DEC_CLASS(klass) G_TYPE_CHECK_CLASS_TYPE(klass, GST_TYPE_FLAC_DEC)

typedef struct _GstFlacDec GstFlacDec;
typedef struct _GstFlacDecClass GstFlacDecClass;

struct _GstFlacDec {
  GstElement     element;

  /* < private > */

  FLAC__StreamDecoder         *decoder;
  GstAdapter                  *adapter;
  gboolean                     framed;
  gboolean                     streaming;

  gboolean       got_headers; /* if we've parsed the headers (unframed push mode only) */

  GstPad        *sinkpad;
  GstPad        *srcpad;

  gboolean       init;

  guint64        offset;      /* current byte offset of input */

  gboolean       seeking;     /* set to TRUE while seeking to make sure we
                               * don't push any buffers in the write callback
                               * until we are actually at the new position */

  gboolean       eos;         /* set to TRUE if seeked after the end of file */

  GstSegment     segment;     /* the currently configured segment, in
                               * samples/audio frames (DEFAULT format) */
  gboolean       running;
  gboolean       discont;
  GstBuffer     *pending;     /* pending buffer, produced in seek */
  guint          pending_samples;
  GstEvent      *close_segment;
  GstEvent      *start_segment;
  GstTagList    *tags;

  GstFlowReturn  pull_flow;   /* last flow from pull_range */ /* STREAM_LOCK */

  GstFlowReturn  last_flow;   /* the last flow return received from either
                               * gst_pad_push or gst_pad_buffer_alloc */

  gint           channels;
  gint           depth;
  gint           width;
  gint           sample_rate;

  /* from the stream info, needed for scanning */
  guint16        min_blocksize;
  guint16        max_blocksize;

  gint64         cur_granulepos; /* only used in framed mode (flac-in-ogg) */
};

struct _GstFlacDecClass {
  GstElementClass parent_class;
};

GType gst_flac_dec_get_type (void);

G_END_DECLS

#endif /* __GST_FLAC_DEC_H__ */
