/* GStreamer DVD subtitle parser
 * Copyright (C) 2007 Mark Nauwelaerts <mnauw@users.sourceforge.net>
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

#ifndef __GST_DVDSUBPARSE_H__
#define __GST_DVDSUBPARSE_H__

#include <gst/gst.h>
#include <gst/base/gstadapter.h>

G_BEGIN_DECLS

#define GST_TYPE_DVD_SUB_PARSE \
  (gst_dvd_sub_parse_get_type())
#define GST_DVD_SUB_PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_DVD_SUB_PARSE, GstDvdSubParse))
#define GST_DVD_SUB_PARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_DVD_SUB_PARSE, GstDvdSubParseClass))
#define GST_DVD_SUB_PARSE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_DVD_SUB_PARSE, GstDvdSubParseClass))
#define GST_IS_DVD_SUB_PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_DVD_SUB_PARSE))
#define GST_IS_DVD_SUB_PARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_DVD_SUB_PARSE))

typedef struct _GstDvdSubParse GstDvdSubParse;
typedef struct _GstDvdSubParseClass GstDvdSubParseClass;

struct _GstDvdSubParse {
  GstElement element;

  /*< private >*/
  GstPad       *srcpad;
  GstPad       *sinkpad;

  GstAdapter   *adapter;   /* buffer incoming data                   */
  GstClockTime  stamp;     /* timestamp of current packet            */
  guint         needed;    /* size of current packet to be assembled */
};

struct _GstDvdSubParseClass {
  GstElementClass parent_class;
};

GType gst_dvd_sub_parse_get_type (void);
GST_ELEMENT_REGISTER_DECLARE (dvdsubparse);

G_END_DECLS

#endif /* __GST_DVDSUBPARSE_H__ */

