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


#ifndef __GST_SPEEXENC_H__
#define __GST_SPEEXENC_H__


#include <gst/gst.h>

#include <speex.h>
#include <speex_header.h>

#ifdef __cplusplus
extern "C"
{
#endif				/* __cplusplus */


#define GST_TYPE_SPEEXENC \
  (gst_speexenc_get_type())
#define GST_SPEEXENC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SPEEXENC,GstSpeexEnc))
#define GST_SPEEXENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SPEEXENC,GstSpeexEnc))
#define GST_IS_SPEEXENC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SPEEXENC))
#define GST_IS_SPEEXENC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SPEEXENC))

  typedef struct _GstSpeexEnc GstSpeexEnc;
  typedef struct _GstSpeexEncClass GstSpeexEncClass;

  struct _GstSpeexEnc
  {
    GstElement element;

    /* pads */
    GstPad *sinkpad, *srcpad;

    gint packet_count;
    gint n_packets;

    SpeexBits bits;
    SpeexHeader header;
    SpeexMode *mode;
    void *state;
    gint frame_size;
    gint16 buffer[2000];
    gint bufsize;
    guint64 next_ts;

    gint rate;
  };

  struct _GstSpeexEncClass
  {
    GstElementClass parent_class;

    /* signals */
    void (*frame_encoded) (GstElement * element);
  };

  GType gst_speexenc_get_type (void);


#ifdef __cplusplus
}
#endif				/* __cplusplus */


#endif				/* __GST_SPEEXENC_H__ */
