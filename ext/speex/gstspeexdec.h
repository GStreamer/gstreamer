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


#ifndef __GST_SPEEXDEC_H__
#define __GST_SPEEXDEC_H__


#include <gst/gst.h>
#include <speex.h>

#ifdef __cplusplus
extern "C"
{
#endif				/* __cplusplus */


#define GST_TYPE_SPEEXDEC \
  (gst_speexdec_get_type())
#define GST_SPEEXDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SPEEXDEC,GstSpeexDec))
#define GST_SPEEXDEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SPEEXDEC,GstSpeexDec))
#define GST_IS_SPEEXDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SPEEXDEC))
#define GST_IS_SPEEXDEC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SPEEXDEC))

  typedef struct _GstSpeexDec GstSpeexDec;
  typedef struct _GstSpeexDecClass GstSpeexDecClass;

  struct _GstSpeexDec
  {
    GstElement element;

    /* pads */
    GstPad *sinkpad, *srcpad;
  };

  struct _GstSpeexDecClass
  {
    GstElementClass parent_class;
  };

  GType gst_speexdec_get_type (void);


#ifdef __cplusplus
}
#endif				/* __cplusplus */


#endif				/* __GST_SPEEXDEC_H__ */
