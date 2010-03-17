/*
 * gstcmmldec.h - GStreamer annodex CMML decoder
 * Copyright (C) 2005 Alessandro Decina
 * 
 * Authors:
 *   Alessandro Decina <alessandro@nnva.org>
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

#ifndef __GST_CMML_DEC_H__
#define __GST_CMML_DEC_H__

#include <gst/gst.h>
#include <gst/gstformat.h>
#include <gst/controller/gstcontroller.h>

#include "gstcmmlparser.h"

/* GstCmmlDec */
#define GST_TYPE_CMML_DEC (gst_cmml_dec_get_type())
#define GST_CMML_DEC(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_CMML_DEC, GstCmmlDec))
#define GST_CMML_DEC_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_CMML_DEC, GstCmmlDecClass))
#define GST_IS_CMML_DEC(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_CMML_DEC))
#define GST_IS_CMML_DEC_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_CMML_DEC))
#define GST_CMML_DEC_GET_CLASS(klass) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_CMML_DEC, GstCmmlDecClass))

typedef struct _GstCmmlDec GstCmmlDec;
typedef struct _GstCmmlDecClass GstCmmlDecClass;
typedef enum _GstCmmlPacketType GstCmmlPacketType;

enum _GstCmmlPacketType
{
  GST_CMML_PACKET_UNKNOWN,
  GST_CMML_PACKET_IDENT_HEADER,
  GST_CMML_PACKET_FIRST_HEADER,
  GST_CMML_PACKET_SECOND_HEADER,
  GST_CMML_PACKET_CLIP
};

struct _GstCmmlDec
{
  GstElement element;

  /* element part */
  GstPad *sinkpad;
  GstPad *srcpad;

  /* bitstream part */
  gint16 major;                 /* bitstream version major */
  gint16 minor;                 /* bitstream version minor */
  gint64 granulerate_n;         /* bitrstream granulerate numerator */
  gint64 granulerate_d;         /* bitstream granulerate denominator */
  gint8 granuleshift;           /* bitstreamgranuleshift */
  gint64 granulepos;            /* bitstream granule position */
  GstClockTime timestamp;       /* timestamp of the last buffer */

  /* decoder part */
  GstCmmlParser *parser;        /* cmml parser */
  gboolean sent_root;
  GstFlowReturn flow_return;   /* _chain return value */
  gboolean wait_clip_end;        /* when TRUE, the GST_TAG_MESSAGE for a
                                 * clip is sent when the next clip (or EOS)
                                 * is found, so that the clip end-time is
                                 * known. This is useful for pre-extracting
                                 * the clips.
                                 */
  GHashTable *tracks;
};

struct _GstCmmlDecClass
{
  GstElementClass parent_class;
};

GType gst_cmml_dec_get_type (void);

gboolean gst_cmml_dec_plugin_init (GstPlugin * plugin);

#endif /* __GST_CMML_DEC_H__ */
