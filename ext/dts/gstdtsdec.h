/* GStreamer DTS decoder plugin based on libdtsdec
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


#ifndef __GST_DTSDEC_H__
#define __GST_DTSDEC_H__

#include <gst/gst.h>
#include <gst/bytestream/bytestream.h>

G_BEGIN_DECLS

#define GST_TYPE_DTSDEC \
  (gst_dtsdec_get_type())
#define GST_DTSDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DTSDEC,GstDtsDec))
#define GST_DTSDEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DTSDEC,GstDtsDecClass))
#define GST_IS_DTSDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DTSDEC))
#define GST_IS_DTSDEC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DTSDEC))

typedef struct _GstDtsDec GstDtsDec;
typedef struct _GstDtsDecClass GstDtsDecClass;

struct _GstDtsDec {
  GstElement 	 element;

  /* pads */
  GstPad 	*sinkpad,
  		*srcpad;

  /* stream properties */
  gint 		 bit_rate;
  gint 		 sample_rate;
  gint 		 stream_channels;
  gint 		 request_channels;
  gint 		 using_channels;

  /* decoding properties */
  sample_t 	 level;
  sample_t 	 bias;
  gboolean 	 dynamic_range_compression;
  sample_t 	*samples;
  dts_state_t 	*state;

  GstByteStream *bs;

  /* keep track of time */
  GstClockTime	last_ts;
  GstClockTime	current_ts;
};

struct _GstDtsDecClass {
  GstElementClass parent_class;
};

G_END_DECLS

#endif /* __GST_DTSDEC_H__ */
