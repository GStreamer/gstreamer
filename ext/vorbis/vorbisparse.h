/* -*- c-basic-offset: 2 -*-
 * GStreamer
 * Copyright (C) <2004> Thomas Vander Stichele <thomas at apestaart dot org>
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


#ifndef __GST_VORBIS_PARSE_H__
#define __GST_VORBIS_PARSE_H__


#include <gst/gst.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define GST_TYPE_VORBIS_PARSE \
  (gst_vorbis_parse_get_type())
#define GST_VORBIS_PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VORBIS_PARSE,GstVorbisParse))
#define GST_VORBIS_PARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VORBIS_PARSE,GstVorbisParse))
#define GST_IS_VORBIS_PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VORBIS_PARSE))
#define GST_IS_VORBIS_PARSE_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VORBIS_PARSE))

typedef struct _GstVorbisParse GstVorbisParse;
typedef struct _GstVorbisParseClass GstVorbisParseClass;

struct _GstVorbisParse {
  GstElement		element;

  GstPad *		sinkpad;
  GstPad *		srcpad;

  guint			packetno;
  gboolean		streamheader_sent;
  GList	*		streamheader;
};

struct _GstVorbisParseClass {
  GstElementClass parent_class;
};

GType gst_vorbis_parse_get_type(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_VORBIS_PARSE_H__ */
