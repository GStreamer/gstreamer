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


#ifndef __FLACDEC_H__
#define __FLACDEC_H__


#include <gst/gst.h>
#include <gst/bytestream/bytestream.h>

#include <FLAC/all.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define GST_TYPE_FLACDEC flacdec_get_type()
#define GST_FLACDEC(obj) G_TYPE_CHECK_INSTANCE_CAST(obj, GST_TYPE_FLACDEC, FlacDec)
#define GST_FLACDEC_CLASS(klass) G_TYPE_CHECK_CLASS_CAST(klass, GST_TYPE_FLACDEC, FlacDec)
#define GST_IS_FLACDEC(obj) G_TYPE_CHECK_INSTANCE_TYPE(obj, GST_TYPE_FLACDEC)
#define GST_IS_FLACDEC_CLASS(obj) G_TYPE_CHECK_CLASS_TYPE(klass, GST_TYPE_FLACDEC)

typedef struct _FlacDec FlacDec;
typedef struct _FlacDecClass FlacDecClass;

struct _FlacDec {
  GstElement 	 element;

  GstPad 	*sinkpad,*srcpad;
  GstByteStream *bs;

  FLAC__SeekableStreamDecoder *decoder;
  gint		 channels;
  gint		 depth;
  gint		 frequency;

  gboolean	 need_discont;
  gboolean 	 seek_pending;
  gint64	 seek_value;

  gboolean	 init;
  guint64	 total_samples;
  guint64	 stream_samples;

  gboolean	 eos;
};

struct _FlacDecClass {
  GstElementClass parent_class;
};

GType flacdec_get_type(void);


#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __FLACDEC_H__ */
