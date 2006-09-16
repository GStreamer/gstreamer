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


#ifndef __RFC2250_ENC_H__
#define __RFC2250_ENC_H__


#include <gst/gst.h>
#include "gstmpegpacketize.h"

G_BEGIN_DECLS

#define GST_TYPE_RFC2250_ENC \
  (gst_rfc2250_enc_get_type())
#define GST_RFC2250_ENC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_RFC2250_ENC,GstRFC2250Enc))
#define GST_RFC2250_ENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_RFC2250_ENC,GstRFC2250EncClass))
#define GST_IS_RFC2250_ENC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_RFC2250_ENC))
#define GST_IS_RFC2250_ENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_RFC2250_ENC))

#define GST_RFC2250_ENC_IS_MPEG2(parse) (GST_MPEG_PACKETIZE_IS_MPEG2 (GST_RFC2250_ENC (parse)->packetize))

typedef enum {
  ENC_HAVE_SEQ          = (1 << 0),
  ENC_HAVE_GOP          = (1 << 1),
  ENC_HAVE_PIC          = (1 << 2),
  ENC_HAVE_DATA         = (1 << 3),
} GstEncFlags;
  
typedef struct _GstRFC2250Enc GstRFC2250Enc;
typedef struct _GstRFC2250EncClass GstRFC2250EncClass;

struct _GstRFC2250Enc {
  GstElement element;

  GstPad *sinkpad, *srcpad;

  GstMPEGPacketize *packetize;

  /* pack header values */
  guint32 bit_rate;
  guint64 next_ts;
  GstBuffer *packet;
  GstEncFlags flags;
  gint MTU;
  gint remaining;
};

struct _GstRFC2250EncClass {
  GstElementClass parent_class;
};

GType gst_rfc2250_enc_get_type(void);

gboolean gst_rfc2250_enc_plugin_init    (GstPlugin *plugin);

G_END_DECLS

#endif /* __RFC2250_ENC_H__ */
