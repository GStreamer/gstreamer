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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */


#ifndef __GST_CDXAPARSE_H__
#define __GST_CDXAPARSE_H__

#include <gst/gst.h>
#include "gst/riff/riff-ids.h"
#include "gst/riff/riff-read.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define GST_TYPE_CDXAPARSE \
  (gst_cdxaparse_get_type())
#define GST_CDXAPARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CDXAPARSE,GstCDXAParse))
#define GST_CDXAPARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CDXAPARSE,GstCDXAParse))
#define GST_IS_CDXAPARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CDXAPARSE))
#define GST_IS_CDXAPARSE_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CDXAPARSE))

#define GST_CDXA_SECTOR_SIZE  	2352
#define GST_CDXA_DATA_SIZE  	2324
#define GST_CDXA_HEADER_SIZE	24

typedef enum {
  GST_CDXAPARSE_START,
  GST_CDXAPARSE_FMT,
  GST_CDXAPARSE_OTHER,
  GST_CDXAPARSE_DATA,
} GstCDXAParseState;

typedef struct _GstCDXAParse GstCDXAParse;
typedef struct _GstCDXAParseClass GstCDXAParseClass;

struct _GstCDXAParse {
  GstRiffRead parent;

  /* pads */
  GstPad *sinkpad,*srcpad;

  /* CDXA decoding state */
  GstCDXAParseState state;

  guint64 dataleft, datasize, datastart;
  int byteoffset;
  
  gboolean seek_pending;
  guint64 seek_offset;
};

struct _GstCDXAParseClass {
  GstElementClass parent_class;
};

GType 		gst_cdxaparse_get_type		(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_CDXAPARSE_H__ */

