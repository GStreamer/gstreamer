/* GStreamer Musepack decoder plugin
 * Copyright (C) 2004 Ronald Bultje <rbultje@ronald.bitfreak.net>
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

#ifndef __GST_MUSEPACK_DEC_H__
#define __GST_MUSEPACK_DEC_H__

#include <gst/gst.h>
//#include <gst/bytestream/bytestream.h>
#include <mpcdec/mpcdec.h>
//#include "gstmusepackreader.h"

G_BEGIN_DECLS

#define GST_TYPE_MUSEPACK_DEC \
  (gst_musepackdec_get_type ())
#define GST_MUSEPACK_DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_MUSEPACK_DEC, \
                               GstMusepackDec))
#define GST_MUSEPACK_DEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_MUSEPACK_DEC, \
                            GstMusepackDecClass))
#define GST_IS_MUSEPACK_DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_MUSEPACK_DEC))
#define GST_IS_MUSEPACK_DEC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_MUSEPACK_DEC))

typedef struct _GstMusepackDec {
  GstElement element;

  /* pads */
  GstPad *srcpad, *sinkpad;
  //  GstByteStream *bs;
  guint64 offset;

  /* MUSEPACK_DEC object */
  mpc_decoder *d;
  mpc_reader *r;
  gboolean init;

  /* bytes-per-sample */
  int bps, rate;

  /* position and length, in samples */
  guint64 pos, len;

  /* seeks */
  gdouble flush_pending, seek_pending, eos;
  guint64 seek_time;
} GstMusepackDec;

typedef struct _GstMusepackDecClass {
  GstElementClass parent_class;
} GstMusepackDecClass;

GType gst_musepackdec_get_type (void);

extern gboolean gst_musepackdec_src_convert (GstPad * pad,
					     GstFormat src_format,
					     gint64 src_value, GstFormat * dest_format, gint64 * dest_value);

G_END_DECLS

#endif /* __GST_MUSEPACK_DEC_H__ */
