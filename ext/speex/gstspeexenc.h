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
#include <gst/base/gstadapter.h>

#include <speex/speex.h>
#include <speex/speex_header.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


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

#define MAX_FRAME_SIZE 2000*2
#define MAX_FRAME_BYTES 2000

typedef enum
{
  GST_SPEEXENC_MODE_AUTO,
  GST_SPEEXENC_MODE_UWB,
  GST_SPEEXENC_MODE_WB,
  GST_SPEEXENC_MODE_NB,
} GstSpeexMode;

typedef struct _GstSpeexEnc GstSpeexEnc;
typedef struct _GstSpeexEncClass GstSpeexEncClass;

struct _GstSpeexEnc {
  GstElement            element;

  /* pads */
  GstPad                *sinkpad,
                        *srcpad;

  gint                  packet_count;
  gint                  n_packets;

  SpeexBits             bits;
  SpeexHeader           header;
#if SPEEX_1_0
  SpeexMode             *speex_mode;
#else
  const SpeexMode       *speex_mode;
#endif
  void                  *state;
  GstSpeexMode          mode;
  GstAdapter            *adapter;

  gfloat                quality;
  gint                  bitrate;
  gboolean              vbr;
  gint                  abr;
  gboolean              vad;
  gboolean              dtx;
  gint                  complexity;
  gint                  nframes;

  gint                  lookahead;

  gint                  channels;
  gint                  rate;

  gboolean              setup;
  gboolean              header_sent;
  gboolean              eos;

  guint64               samples_in;
  guint64               bytes_out;

  GstTagList            *tags;

  gchar                 *last_message;

  gint                  frame_size;
  guint64               frameno;

  guint8                *comments;
  gint                  comment_len;

  gfloat                input[MAX_FRAME_SIZE];
};

struct _GstSpeexEncClass {
  GstElementClass parent_class;

  /* signals */
  void (*frame_encoded) (GstElement *element);
};

GType gst_speexenc_get_type(void);


#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_SPEEXENC_H__ */
