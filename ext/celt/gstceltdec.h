/* GStreamer
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

#ifndef __GST_CELT_DEC_H__
#define __GST_CELT_DEC_H__

#include <gst/gst.h>
#include <gst/audio/gstaudiodecoder.h>
#include <celt/celt.h>
#include <celt/celt_header.h>

G_BEGIN_DECLS

#define GST_TYPE_CELT_DEC \
  (gst_celt_dec_get_type())
#define GST_CELT_DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CELT_DEC,GstCeltDec))
#define GST_CELT_DEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CELT_DEC,GstCeltDecClass))
#define GST_IS_CELT_DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CELT_DEC))
#define GST_IS_CELT_DEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CELT_DEC))

typedef struct _GstCeltDec GstCeltDec;
typedef struct _GstCeltDecClass GstCeltDecClass;

struct _GstCeltDec {
  GstAudioDecoder       element;

  CELTDecoder          *state;
  CELTMode             *mode;
  CELTHeader            header;

  gint                  frame_size;
  guint64               packetno;

  GstBuffer            *streamheader;
  GstBuffer            *vorbiscomment;
  GList                *extra_headers;
};

struct _GstCeltDecClass {
  GstAudioDecoderClass parent_class;
};

GType gst_celt_dec_get_type (void);

G_END_DECLS

#endif /* __GST_CELT_DEC_H__ */
