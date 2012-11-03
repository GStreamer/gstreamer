/* GStreamer
 * Copyright (C) 2003 Benjamin Otte <in7y118@public.uni-hamburg.de>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */


#ifndef __GST_MAD_H__
#define __GST_MAD_H__

#include <gst/gst.h>
#include <gst/tag/tag.h>
#include <gst/audio/gstaudiodecoder.h>

#include <mad.h>

G_BEGIN_DECLS

#define GST_TYPE_MAD \
  (gst_mad_get_type())
#define GST_MAD(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MAD,GstMad))
#define GST_MAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MAD,GstMadClass))
#define GST_IS_MAD(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MAD))
#define GST_IS_MAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MAD))


typedef struct _GstMad GstMad;
typedef struct _GstMadClass GstMadClass;

struct _GstMad
{
  GstAudioDecoder element;

  /* state */
  struct mad_stream stream;
  struct mad_frame frame;
  struct mad_synth synth;

  /* info */
  struct mad_header header;

  /* negotiated format */
  gint rate, pending_rate;
  gint channels, pending_channels;
  gint times_pending;
  gboolean caps_set;            /* used to keep track of whether to change/update caps */

  gboolean eos;

  /* properties */
  gboolean half;
  gboolean ignore_crc;
};

struct _GstMadClass
{
  GstAudioDecoderClass parent_class;
};

GType                   gst_mad_get_type (void);
gboolean                gst_mad_register (GstPlugin * plugin);

G_END_DECLS

#endif /* __GST_MAD_H__ */
