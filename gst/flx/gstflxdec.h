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

#ifndef __GST_FLX_DECODER_H__
#define __GST_FLX_DECODER_H__

#include <gst/gst.h>

#include "flx_color.h"
#include <gst/bytestream/bytestream.h>


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef enum {
  GST_FLXDEC_READ_HEADER,
  GST_FLXDEC_PLAYING,
} GstFlxDecState;
	

/* Definition of structure storing data for this element. */
typedef struct _GstFlxDec  GstFlxDec;

struct _GstFlxDec {
  GstElement element;

  GstPad *sinkpad,*srcpad;

  gboolean active, new_meta;

  GstBuffer *delta, *frame;
  GstByteStream *bs;
  gulong size;
  GstFlxDecState state;
  glong frame_time;
  gint64 next_time;

  FlxColorSpaceConverter *converter;

  FlxHeader hdr;
};

/* Standard definition defining a class for this element. */
typedef struct _GstFlxDecClass GstFlxDecClass;
struct _GstFlxDecClass {
  GstElementClass parent_class;
};

/* Standard macros for defining types for this element.  */
#define GST_TYPE_FLXDEC \
  (gst_flxdec_get_type())
#define GST_FLXDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_FLXDEC,GstFlxDec))
#define GST_FLXDEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_FLXDEC,GstFlxDec))
#define GST_IS_FLXDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_FLXDEC))
#define GST_IS_FLXDEC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_FLXDEC))

/* Standard function returning type information. */
GType gst_flxdec_get_type(void);


#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_FLX_DECODER_H__ */
