/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2008> Sebastian Dröge <sebastian.droege@collabora.co.uk>
 * Copyright (C) <2011-2012> Vincent Penquerc'h <vincent.penquerch@collabora.co.uk>
 * Copyright (C) <2025> Jan Schmidt <jan@centricular.com>
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

#ifndef __GST_ID3_META_PARSE_H__
#define __GST_ID3_META_PARSE_H__

#include <gst/gst.h>
#include <gst/base/gstbaseparse.h>

G_BEGIN_DECLS

#define GST_TYPE_ID3_META_PARSE \
  (gst_id3meta_parse_get_type())
#define GST_ID3_META_PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ID3_META_PARSE,GstId3MetaParse))
#define GST_ID3_META_PARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ID3_META_PARSE,GstId3MetaParseClass))
#define GST_IS_ID3_META_PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_ID3_META_PARSE))
#define GST_IS_ID3_META_PARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_ID3_META_PARSE))

typedef struct _GstId3MetaParse GstId3MetaParse;
typedef struct _GstId3MetaParseClass GstId3MetaParseClass;

struct _GstId3MetaParse {
  GstBaseParse       element;
};

struct _GstId3MetaParseClass {
  GstBaseParseClass parent_class;
};

GType gst_id3meta_parse_get_type (void);

GST_ELEMENT_REGISTER_DECLARE (id3metaparse);

G_END_DECLS

#endif /* __GST_ID3_META_PARSE_H__ */
