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


#ifndef __OGGVORBISENC_H__
#define __OGGVORBISENC_H__


#include <gst/gst.h>

#include <vorbis/codec.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define GST_TYPE_OGGVORBISENC \
  (oggvorbisenc_get_type())
#define GST_OGGVORBISENC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_OGGVORBISENC,OggVorbisEnc))
#define GST_OGGVORBISENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_OGGVORBISENC,OggVorbisEncClass))
#define GST_IS_OGGVORBISENC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_OGGVORBISENC))
#define GST_IS_OGGVORBISENC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_OGGVORBISENC))

typedef struct _OggVorbisEnc OggVorbisEnc;
typedef struct _OggVorbisEncClass OggVorbisEncClass;

struct _OggVorbisEnc {
  GstElement 	   element;

  GstPad          *sinkpad,
                  *srcpad;

  ogg_stream_state os; /* take physical pages, weld into a logical
			                              stream of packets */
  ogg_page         og; /* one Ogg bitstream page.  Vorbis packets are inside */
  ogg_packet       op; /* one raw packet of data for decode */

  vorbis_info      vi; /* struct that stores all the static vorbis bitstream
				                            settings */
  vorbis_comment   vc; /* struct that stores all the user comments */

  vorbis_dsp_state vd; /* central working state for the packet->PCM decoder */
  vorbis_block     vb; /* local working space for packet->PCM decode */

  gboolean         eos;

  gboolean         managed;
  gint             bitrate;
  gint             min_bitrate;
  gint             max_bitrate;
  gfloat           quality;
  gboolean	   quality_set;
  gint             serial;

  gint             channels;
  gint             frequency;

  guint64	   samples_in;
  guint64	   bytes_out;

  GstTagList *	   tags;

  gboolean         setup;
  gboolean         header_sent;
  gchar		  *last_message;
};

struct _OggVorbisEncClass {
  GstElementClass parent_class;
};

GType oggvorbisenc_get_type(void);


#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __OGGVORBISENC_H__ */
