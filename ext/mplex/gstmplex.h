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


#ifndef __GST_MPLEX_H__
#define __GST_MPLEX_H__


#include <config.h>
#include <stdlib.h>

#include <gst/gst.h>
#include <gst/bytestream/bytestream.h>

#include "outputstream.hh"
#include "bits.hh"


G_BEGIN_DECLS

#define GST_TYPE_MPLEX \
  (gst_mplex_get_type())
#define GST_MPLEX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MPLEX,GstMPlex))
#define GST_MPLEX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MPLEX,GstMPlex))
#define GST_IS_MPLEX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MPLEX))
#define GST_IS_MPLEX_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MPLEX))

typedef enum {
  GST_MPLEX_INIT,
  GST_MPLEX_OPEN_STREAMS,
  GST_MPLEX_RUN,
  GST_MPLEX_END
} GstMPlexState;

typedef enum {
  GST_MPLEX_STREAM_UNKOWN,
  GST_MPLEX_STREAM_AC3,
  GST_MPLEX_STREAM_MPA,
  GST_MPLEX_STREAM_LPCM,
  GST_MPLEX_STREAM_VIDEO,
  GST_MPLEX_STREAM_DVD_VIDEO,
} GstMPlexStreamType;

typedef struct _GstMPlex GstMPlex;
typedef struct _GstMPlexStream GstMPlexStream;
typedef struct _GstMPlexClass GstMPlexClass;

struct _GstMPlexStream {
  IBitStream 		*bitstream;
  ElementaryStream 	*elem_stream;
  GstPad 		*pad;
  GstMPlexStreamType	 type;
  GstByteStream		*bytestream;
  gboolean		 eos;
};

struct _GstMPlex {
  GstElement 	 element;

  GstMPlexState	 state;
  /* pads */
  GstPad 	*srcpad;
  GList		*streams;

  vector<ElementaryStream *> *strms;
  OutputStream 	*ostrm;
  PS_Stream	*ps_stream;
  gint		 data_rate;
  gint 		 sync_offset;

  gint 		 num_video;
  gint 		 num_audio;
  gint 		 num_private1;
};

struct _GstMPlexClass {
  GstElementClass parent_class;
};

GType gst_mplex_get_type (void);
	
G_END_DECLS

#endif /* __GST_MPLEX_H__ */
