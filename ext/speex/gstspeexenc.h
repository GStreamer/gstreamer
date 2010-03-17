/* GStreamer Speex Encoder
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


#ifndef __GST_SPEEX_ENC_H__
#define __GST_SPEEX_ENC_H__


#include <gst/gst.h>
#include <gst/base/gstadapter.h>

#include <speex/speex.h>
#include <speex/speex_header.h>

G_BEGIN_DECLS

#define GST_TYPE_SPEEX_ENC \
  (gst_speex_enc_get_type())
#define GST_SPEEX_ENC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SPEEX_ENC,GstSpeexEnc))
#define GST_SPEEX_ENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SPEEX_ENC,GstSpeexEncClass))
#define GST_IS_SPEEX_ENC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SPEEX_ENC))
#define GST_IS_SPEEX_ENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SPEEX_ENC))

#define MAX_FRAME_SIZE 2000*2
#define MAX_FRAME_BYTES 2000

typedef enum
{
  GST_SPEEX_ENC_MODE_AUTO,
  GST_SPEEX_ENC_MODE_UWB,
  GST_SPEEX_ENC_MODE_WB,
  GST_SPEEX_ENC_MODE_NB
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
#ifdef SPEEX_1_0
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

  guint64               samples_in;
  guint64               bytes_out;

  GstTagList            *tags;

  gchar                 *last_message;

  gint                  frame_size;
  guint64               frameno;
  guint64               frameno_out;

  guint8                *comments;
  gint                  comment_len;

  /* Timestamp and granulepos tracking */
  GstClockTime     start_ts;
  GstClockTime     next_ts;
  guint64          granulepos_offset;
};

struct _GstSpeexEncClass {
  GstElementClass parent_class;

  /* signals */
  void (*frame_encoded) (GstElement *element);
};

GType gst_speex_enc_get_type (void);

G_END_DECLS

#endif /* __GST_SPEEXENC_H__ */
