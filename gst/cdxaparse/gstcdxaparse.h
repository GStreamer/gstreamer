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


#ifndef __GST_CDXA_PARSE_H__
#define __GST_CDXA_PARSE_H__

#include <gst/gst.h>
#include "gst/riff/riff-ids.h"
#include "gst/riff/riff-read.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define GST_TYPE_CDXA_PARSE \
  (gst_cdxa_parse_get_type())
#define GST_CDXA_PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CDXA_PARSE,GstCDXAParse))
#define GST_CDXA_PARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CDXA_PARSE,GstCDXAParse))
#define GST_IS_CDXA_PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CDXA_PARSE))
#define GST_IS_CDXA_PARSE_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CDXA_PARSE))

#define GST_CDXA_SECTOR_SIZE  	2352
#define GST_CDXA_DATA_SIZE  	2324

typedef enum {
  GST_CDXA_PARSE_START,
  GST_CDXA_PARSE_FMT,
  GST_CDXA_PARSE_OTHER,
  GST_CDXA_PARSE_DATA,
} GstCDXAParseState;

typedef struct _GstCDXAParse GstCDXAParse;
typedef struct _GstCDXAParseClass GstCDXAParseClass;

struct _GstCDXAParse {
  GstRiffRead parent;

  /* pads */
  GstPad *sinkpad,*srcpad;

  /* CDXA decoding state */
  GstCDXAParseState state;

  /* useful CDXA data 
  guint32 riff_size;
  guint32 data_size;
  guint32 sectors;

#define CDXA_SUB_MODE_EOF(c)    ((c&0x80)>>7)
#define CDXA_SUB_MODE_RT(c)     ((c&0x40)>>6)
#define CDXA_SUB_MODE_FORM(c)   ((c&0x20)>>5)
#define CDXA_SUB_MODE_TRIGGER(c)((c&0x10)>>4)
#define CDXA_SUB_MODE_DATA(c)   ((c&0x08)>>3)
#define CDXA_SUB_MODE_VIDEO(c)  ((c&0x04)>>2)
#define CDXA_SUB_MODE_AUDIO(c)  ((c&0x02)>>1)
#define CDXA_SUB_MODE_EOR(c)    ((c&0x01)   )

*/

  guint64 dataleft;
  int byteoffset;
  
  gboolean seek_pending;
  guint64 seek_offset;
};

struct _GstCDXAParseClass {
  GstElementClass parent_class;
};

GType 		gst_cdxa_parse_get_type		(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_CDXA_PARSE_H__ */

