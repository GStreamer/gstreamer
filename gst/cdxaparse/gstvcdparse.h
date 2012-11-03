/* GStreamer CDXA sync strippper / VCD parser
 * Copyright (C) 2004 Ronald Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2008 Tim-Philipp Müller <tim centricular net>
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

#ifndef __GST_VCD_PARSE_H__
#define __GST_VCD_PARSE_H__

#include <gst/gst.h>
#include <gst/base/gstadapter.h>

G_BEGIN_DECLS

#define GST_TYPE_VCD_PARSE \
  (gst_vcd_parse_get_type())
#define GST_VCD_PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VCD_PARSE,GstVcdParse))
#define GST_VCD_PARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VCD_PARSE,GstVcdParseClass))
#define GST_IS_VCD_PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VCD_PARSE))
#define GST_IS_VCD_PARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VCD_PARSE))

#define GST_CDXA_SECTOR_SIZE    2352
#define GST_CDXA_DATA_SIZE      2324
#define GST_CDXA_HEADER_SIZE    24

typedef struct _GstVcdParse GstVcdParse;
typedef struct _GstVcdParseClass GstVcdParseClass;

struct _GstVcdParse {
  GstElement element;

  GstPad     *sinkpad;
  GstPad     *srcpad;
  GstAdapter *adapter;
};

struct _GstVcdParseClass {
  GstElementClass element_class;
};

GType           gst_vcd_parse_get_type  (void);

G_END_DECLS

#endif /* __GST_VCD_PARSE_H__ */

