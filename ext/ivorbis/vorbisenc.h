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


#ifndef __VORBISENC_H__
#define __VORBISENC_H__


#include <gst/gst.h>

#include <tremor/ivorbiscodec.h>

#ifdef __cplusplus
extern "C"
{
#endif				/* __cplusplus */

#define GST_TYPE_VORBISENC \
  (vorbisenc_get_type())
#define GST_VORBISENC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VORBISENC,VorbisEnc))
#define GST_VORBISENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VORBISENC,VorbisEncClass))
#define GST_IS_VORBISENC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VORBISENC))
#define GST_IS_VORBISENC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VORBISENC))

  typedef struct _VorbisEnc VorbisEnc;
  typedef struct _VorbisEncClass VorbisEncClass;

  struct _VorbisEnc
  {
    GstElement element;

    GstPad *sinkpad, *srcpad;

    ogg_stream_state os;	/* take physical pages, weld into a logical
				   stream of packets */
    ogg_page og;		/* one Ogg bitstream page.  Vorbis packets are inside */
    ogg_packet op;		/* one raw packet of data for decode */

    vorbis_info vi;		/* struct that stores all the static vorbis bitstream
				   settings */
    vorbis_comment vc;		/* struct that stores all the user comments */

    vorbis_dsp_state vd;	/* central working state for the packet->PCM decoder */
    vorbis_block vb;		/* local working space for packet->PCM decode */

    gboolean eos;

    gboolean managed;
    gint bitrate;
    gint min_bitrate;
    gint max_bitrate;
    gfloat quality;
    gboolean quality_set;
    gint serial;

    gint channels;
    gint frequency;

    guint64 samples_in;
    guint64 bytes_out;

    GstCaps *metadata;

    gboolean setup;
    gboolean flush_header;
    gchar *last_message;
  };

  struct _VorbisEncClass
  {
    GstElementClass parent_class;
  };

  GType vorbisenc_get_type (void);


#ifdef __cplusplus
}
#endif				/* __cplusplus */


#endif				/* __VORBISENC_H__ */
