/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *               <2002> Wim Tayans <wim.taymans@chello.be>
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

#ifndef __GST_CDXA_PARSE_H__
#define __GST_CDXA_PARSE_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_CDXA_PARSE \
  (gst_cdxa_parse_get_type())
#define GST_CDXA_PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CDXA_PARSE,GstCDXAParse))
#define GST_CDXA_PARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CDXA_PARSE,GstCDXAParseClass))
#define GST_IS_CDXA_PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CDXA_PARSE))
#define GST_IS_CDXA_PARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CDXA_PARSE))

typedef enum {
  GST_CDXA_PARSE_START,
  GST_CDXA_PARSE_FMT,
  GST_CDXA_PARSE_OTHER,
  GST_CDXA_PARSE_DATA,
} GstCDXAParseState;

typedef struct _GstCDXAParse GstCDXAParse;
typedef struct _GstCDXAParseClass GstCDXAParseClass;

struct _GstCDXAParse {
  GstElement  element;

  /* pads */
  GstPad    *sinkpad;
  GstPad    *srcpad;

  /* CDXA decoding state */
  GstCDXAParseState state;

  gint64     offset;    /* current byte offset in file     */
  gint64     datasize;  /* upstream size in bytes          */
  gint64     datastart; /* byte offset of first frame sync */
  gint64     bytes_skipped;
  gint64     bytes_sent;
};

struct _GstCDXAParseClass {
  GstElementClass parent_class;
};

GType 		gst_cdxa_parse_get_type		(void);

G_END_DECLS

#endif /* __GST_CDXA_PARSE_H__ */

