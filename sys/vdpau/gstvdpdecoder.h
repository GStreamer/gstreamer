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
 
#ifndef __GST_VDP_DECODER_H__
#define __GST_VDP_DECODER_H__

#include <gst/gst.h>

#include "gstvdpdevice.h"
#include "gstvdpvideobuffer.h"

G_BEGIN_DECLS

#define GST_TYPE_VDP_DECODER            (gst_vdp_decoder_get_type())
#define GST_VDP_DECODER(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VDP_DECODER,GstVdpDecoder))
#define GST_VDP_DECODER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VDP_DECODER,GstVdpDecoderClass))
#define GST_VDP_DECODER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_VDP_DECODER, GstVdpDecoderClass))
#define GST_IS_VDPAU_DECODER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VDP_DECODER))
#define GST_IS_VDPAU_DECODER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VDP_DECODER))

typedef struct _GstVdpDecoder      GstVdpDecoder;
typedef struct _GstVdpDecoderClass GstVdpDecoderClass;
typedef struct _VdpauFunctions VdpauFunctions;

struct _GstVdpDecoder {
  GstElement element;

  gchar *display_name;
  GstVdpDevice *device;

  GstPad *src;
  GstPad *sink;

  gint width, height;
  gint framerate_numerator, framerate_denominator;
  guint32 format;

  GstClockTime time;
};

struct _GstVdpDecoderClass {
  GstElementClass parent_class;

  gboolean (*set_caps) (GstVdpDecoder *dec, GstCaps *caps);
};

GType gst_vdp_decoder_get_type (void);

gboolean gst_vdp_decoder_push_video_buffer (GstVdpDecoder * dec,
                                              GstVdpVideoBuffer *buffer);
VdpVideoSurface gst_vdp_decoder_create_video_surface (GstVdpDecoder *dec);

G_END_DECLS

#endif /* __GST_VDP_DECODER_H__ */
