/* GStreamer
 * Copyright (C) 2010 Mark Nauwelaerts <mark.nauwelaerts@collabora.co.uk>
 * Copyright (C) 2010 Nokia Corporation. All rights reserved.
 *   Contact: Stefan Kost <stefan.kost@nokia.com>
 *
 * Tremor modifications <2006>:
 *   Chris Lord, OpenedHand Ltd. <chris@openedhand.com>, http://www.o-hand.com/
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

#ifndef __GST_VORBIS_DEC_LIB_H__
#define __GST_VORBIS_DEC_LIB_H__

#include <gst/gst.h>

#ifndef TREMOR

#include <vorbis/codec.h>

typedef float                          vorbis_sample_t;
typedef ogg_packet                     ogg_packet_wrapper;

#define GST_VORBIS_DEC_DESCRIPTION "decode raw vorbis streams to float audio"

#define GST_VORBIS_DEC_SRC_CAPS \
    GST_STATIC_CAPS ("audio/x-raw-float, " \
        "rate = (int) [ 1, MAX ], " \
        "channels = (int) [ 1, 256 ], " \
        "endianness = (int) BYTE_ORDER, " \
        "width = (int) 32")

#define GST_VORBIS_DEC_DEFAULT_SAMPLE_WIDTH           (32)

#define GST_VORBIS_DEC_GLIB_TYPE_NAME      GstVorbisDec

static inline guint8 *
gst_ogg_packet_data (ogg_packet * p)
{
  return (guint8 *) p->packet;
}

static inline gint
gst_ogg_packet_size (ogg_packet * p)
{
  return p->bytes;
}

static inline void
gst_ogg_packet_wrapper_from_buffer (ogg_packet * packet, GstBuffer * buffer)
{
  packet->packet = GST_BUFFER_DATA (buffer);
  packet->bytes = GST_BUFFER_SIZE (buffer);
}

static inline ogg_packet *
gst_ogg_packet_from_wrapper (ogg_packet_wrapper * packet)
{
  return packet;
}

#else

#ifdef USE_TREMOLO
  #include <Tremolo/ivorbiscodec.h>
  #include <Tremolo/codec_internal.h>
  typedef ogg_int16_t                    vorbis_sample_t;
#else
  #include <tremor/ivorbiscodec.h>
  typedef ogg_int32_t                    vorbis_sample_t;
#endif

typedef struct _ogg_packet_wrapper     ogg_packet_wrapper;

struct _ogg_packet_wrapper {
  ogg_packet          packet;
  ogg_reference       ref;
  ogg_buffer          buf;
};

#define GST_VORBIS_DEC_DESCRIPTION "decode raw vorbis streams to integer audio"

#define GST_VORBIS_DEC_SRC_CAPS \
    GST_STATIC_CAPS ("audio/x-raw-int, "   \
        "rate = (int) [ 1, MAX ], "        \
        "channels = (int) [ 1, 6 ], "      \
        "endianness = (int) BYTE_ORDER, "  \
        "width = (int) { 16, 32 }, "       \
        "depth = (int) 16, "               \
        "signed = (boolean) true")

#define GST_VORBIS_DEC_DEFAULT_SAMPLE_WIDTH           (16)

/* we need a different type name here */
#define GST_VORBIS_DEC_GLIB_TYPE_NAME      GstIVorbisDec

/* and still have it compile */
typedef struct _GstVorbisDec               GstIVorbisDec;
typedef struct _GstVorbisDecClass          GstIVorbisDecClass;

/* compensate minor variation */
#define vorbis_synthesis(a, b)             vorbis_synthesis (a, b, 1)

static inline guint8 *
gst_ogg_packet_data (ogg_packet * p)
{
  return (guint8 *) p->packet->buffer->data;
}

static inline gint
gst_ogg_packet_size (ogg_packet * p)
{
  return p->packet->buffer->size;
}

static inline void
gst_ogg_packet_wrapper_from_buffer (ogg_packet_wrapper * packet,
    GstBuffer * buffer)
{
  ogg_reference *ref = &packet->ref;
  ogg_buffer *buf = &packet->buf;

  buf->data = GST_BUFFER_DATA (buffer);
  buf->size = GST_BUFFER_SIZE (buffer);
  buf->refcount = 1;
  buf->ptr.owner = NULL;
  buf->ptr.next = NULL;

  ref->buffer = buf;
  ref->begin = 0;
  ref->length = buf->size;
  ref->next = NULL;

  packet->packet.packet = ref;
  packet->packet.bytes = ref->length;
}

static inline ogg_packet *
gst_ogg_packet_from_wrapper (ogg_packet_wrapper * packet)
{
  return &(packet->packet);
}

#endif

typedef void (*CopySampleFunc)(vorbis_sample_t *out, vorbis_sample_t **in,
                           guint samples, gint channels, gint width);

CopySampleFunc get_copy_sample_func (gint channels, gint width);

#endif /* __GST_VORBIS_DEC_LIB_H__ */
