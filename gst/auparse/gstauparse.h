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


#ifndef __GST_AUPARSE_H__
#define __GST_AUPARSE_H__


#include <gst/gst.h>


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define GST_TYPE_AUPARSE \
  (gst_auparse_get_type())
#define GST_AUPARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AUPARSE,GstAuParse))
#define GST_AUPARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AUPARSE,GstAuParse))
#define GST_IS_AUPARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AUPARSE))
#define GST_IS_AUPARSE_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AUPARSE))

typedef struct _GstAuParse GstAuParse;
typedef struct _GstAuParseClass GstAuParseClass;

struct _GstAuParse {
  GstElement element;

  GstPad *sinkpad,*srcpad;

  guchar le;
  glong offset;
  glong size;
  glong encoding;
  glong frequency;
  glong channels;

};

struct _GstAuParseClass {
  GstElementClass parent_class;
};

GType gst_auparse_get_type (void);


#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_AUPARSE_H__ */
