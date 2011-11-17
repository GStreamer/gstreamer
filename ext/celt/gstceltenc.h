/* GStreamer Celt Encoder
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2008> Sebastian Dröge <sebastian.droege@collabora.co.uk>
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


#ifndef __GST_CELT_ENC_H__
#define __GST_CELT_ENC_H__


#include <gst/gst.h>
#include <gst/audio/gstaudioencoder.h>

#include <celt/celt.h>
#include <celt/celt_header.h>

G_BEGIN_DECLS

#define GST_TYPE_CELT_ENC \
  (gst_celt_enc_get_type())
#define GST_CELT_ENC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CELT_ENC,GstCeltEnc))
#define GST_CELT_ENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CELT_ENC,GstCeltEncClass))
#define GST_IS_CELT_ENC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CELT_ENC))
#define GST_IS_CELT_ENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CELT_ENC))

#define MAX_FRAME_SIZE 2000*2
#define MAX_FRAME_BYTES 2000

typedef struct _GstCeltEnc GstCeltEnc;
typedef struct _GstCeltEncClass GstCeltEncClass;

struct _GstCeltEnc {
  GstAudioEncoder      element;

  CELTHeader            header;
  CELTMode             *mode;
  CELTEncoder          *state;

  gint                  bitrate;
  gint                  frame_size;
  gint                  requested_frame_size;
  gboolean              cbr;
  gint                  complexity;
  gint                  max_bitrate;
  gint                  prediction;
  gint                  start_band;

  gint                  channels;
  gint                  rate;

  gboolean              header_sent;
  GSList               *headers;
};

struct _GstCeltEncClass {
  GstAudioEncoderClass  parent_class;
};

GType gst_celt_enc_get_type (void);

G_END_DECLS

#endif /* __GST_CELT_ENC_H__ */
