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


#ifndef __RTJPEGENC_H__
#define __RTJPEGENC_H__


#include <gst/gst.h>

#include "RTjpeg.h"


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define GST_TYPE_RTJPEGENC \
  (gst_rtjpegenc_get_type())
#define GST_RTJPEGENC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_RTJPEGENC,GstRTJpegEnc))
#define GST_RTJPEGENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_RTJPEGENC,GstRTJpegEnc))
#define GST_IS_RTJPEGENC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_RTJPEGENC))
#define GST_IS_RTJPEGENC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_RTJPEGENC))

typedef struct _GstRTJpegEnc GstRTJpegEnc;
typedef struct _GstRTJpegEncClass GstRTJpegEncClass;

struct _GstRTJpegEnc {
  GstElement element;

  GstPad *sinkpad,*srcpad;

  gint width,height;
  gint quality;
  gint quant[128];
};

struct _GstRTJpegEncClass {
  GstElementClass parent_class;
};

GType gst_rtjpegenc_get_type(void);


#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __RTJPEGENC_H__ */
