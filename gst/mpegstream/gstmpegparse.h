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


#ifndef __MPEG_PARSE_H__
#define __MPEG_PARSE_H__


#include <config.h>
#include <gst/gst.h>
#include <gst/bytestream/bytestream.h>
#include "gstmpegpacketize.h"


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define GST_TYPE_MPEG_PARSE \
  (gst_mpeg_parse_get_type())
#define GST_MPEG_PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MPEG_PARSE,GstMPEGParse))
#define GST_MPEG_PARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MPEG_PARSE,GstMPEGParseClass))
#define GST_IS_MPEG_PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MPEG_PARSE))
#define GST_IS_MPEG_PARSE_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MPEG_PARSE))

#define GST_MPEG_PARSE_IS_MPEG2(parse) (GST_MPEG_PACKETIZE_IS_MPEG2 (GST_MPEG_PARSE (parse)->packetize))

typedef struct _GstMPEGParse GstMPEGParse;
typedef struct _GstMPEGParseClass GstMPEGParseClass;

struct _GstMPEGParse {
  GstElement element;

  GstPad *sinkpad, *srcpad;

  GstMPEGPacketize *packetize;

  /* pack header values */
  guint32 bit_rate;
  guint64 next_ts;
};

struct _GstMPEGParseClass {
  GstElementClass parent_class;

  /* process packet types */
  gboolean 	(*parse_packhead)	(GstMPEGParse *parse, GstBuffer *buffer);
  gboolean 	(*parse_syshead)	(GstMPEGParse *parse, GstBuffer *buffer);
  gboolean 	(*parse_packet)		(GstMPEGParse *parse, GstBuffer *buffer);
  gboolean 	(*parse_pes)		(GstMPEGParse *parse, GstBuffer *buffer);

  /* optional method to send out the data */
  void	 	(*send_data)		(GstMPEGParse *parse, GstData *data);
};

GType gst_mpeg_parse_get_type(void);

gboolean 	gst_mpeg_parse_plugin_init 	(GModule *module, GstPlugin *plugin);


#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __MPEG_PARSE_H__ */
