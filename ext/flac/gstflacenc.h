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


#ifndef __FLACENC_H__
#define __FLACENC_H__


#include <gst/gst.h>

#include <FLAC/all.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define GST_TYPE_FLACENC flacenc_get_type()
#define GST_FLACENC(obj) G_TYPE_CHECK_INSTANCE_CAST(obj, GST_TYPE_FLACENC, FlacEnc)
#define GST_FLACENC_CLASS(klass) G_TYPE_CHECK_CLASS_CAST(klass, GST_TYPE_FLACENC, FlacEnc)
#define GST_IS_FLACENC(obj) G_TYPE_CHECK_INSTANCE_TYPE(obj, GST_TYPE_FLACENC)
#define GST_IS_FLACENC_CLASS(obj) G_TYPE_CHECK_CLASS_TYPE(klass, GST_TYPE_FLACENC)

typedef struct _FlacEnc FlacEnc;
typedef struct _FlacEncClass FlacEncClass;

struct _FlacEnc {
  GstElement 	 element;

  GstPad *sinkpad,*srcpad;

  gboolean 	 first;
  GstBuffer	*first_buf;
  gboolean 	 eos;
  gint 		 channels;
  gint 		 depth;
  gint 		 sample_rate;
  gboolean	 negotiated;
  gint 		 quality;
  gboolean	 stopped;
  FLAC__int32 	*data;

  FLAC__SeekableStreamEncoder *encoder;
  FLAC__StreamMetadata **meta;

  GstTagList *	   tags;
};

struct _FlacEncClass {
  GstElementClass parent_class;
};

GType flacenc_get_type(void);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __FLACENC_H__ */
