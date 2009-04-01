/* 
 * GStreamer
 * Copyright (C) 2009 Carl-Anton Ingmarsson <ca.ingmarsson@gmail.com>
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
 
#ifndef __GST_VDPAU_DECODER_H__
#define __GST_VDPAU_DECODER_H__

#include <gst/gst.h>

#include "gst-vdpau-device.h"

G_BEGIN_DECLS

#define GST_TYPE_VDPAU_DECODER            (gst_vdpaudecoder_get_type())
#define GST_VDPAU_DECODER(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VDPAU_DECODER,GstVdpauDecoder))
#define GST_VDPAU_DECODER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VDPAU_DECODER,GstVdpauDecoderClass))
#define GST_VDPAU_DECODER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_VDPAU_DECODER, GstVdpauDecoderClass))
#define GST_IS_VDPAU_DECODER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VDPAU_DECODER))
#define GST_IS_VDPAU_DECODER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VDPAU_DECODER))

typedef struct _GstVdpauDecoder      GstVdpauDecoder;
typedef struct _GstVdpauDecoderClass GstVdpauDecoderClass;
typedef struct _VdpauFunctions VdpauFunctions;

struct _GstVdpauDecoder {
  GstElement element;

  gchar *display_name;
  GstVdpauDevice *device;

  GstPad *src;
  GstPad *sink;

  GstCaps *src_caps;

  gint width, height;
  gint framerate_numerator, framerate_denominator;
  guint32 format;

  gint frame_nr;

  gboolean silent;
};

struct _GstVdpauDecoderClass {
  GstElementClass parent_class;

  gboolean (*set_caps) (GstVdpauDecoder *dec, GstCaps *caps);
};

GType gst_vdpaudecoder_get_type (void);

gboolean gst_vdpau_decoder_push_video_surface (GstVdpauDecoder * dec,
                                               VdpVideoSurface surface);
VdpVideoSurface gst_vdpau_decoder_create_video_surface (GstVdpauDecoder *dec);

G_END_DECLS

#endif /* __GST_VDPAU_DECODER_H__ */
