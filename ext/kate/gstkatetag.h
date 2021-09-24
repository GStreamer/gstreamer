/* -*- c-basic-offset: 2 -*-
 * GStreamer
 * Copyright (C) <2006> James Livingston <doclivingston@gmail.com>
 * Copyright (C) <2008> Vincent Penquerc'h <ogg.k.ogg.k@googlemail.com>
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


#ifndef __GST_KATE_TAG_H__
#define __GST_KATE_TAG_H__

#include "gstkateparse.h"

G_BEGIN_DECLS
#define GST_TYPE_KATE_TAG \
  (gst_kate_tag_get_type())
#define GST_KATE_TAG(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_KATE_TAG,GstKateTag))
#define GST_KATE_TAG_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_KATE_TAG,GstKateTagClass))
#define GST_IS_KATE_TAG(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_KATE_TAG))
#define GST_IS_KATE_TAG_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_KATE_TAG))
typedef struct _GstKateTag GstKateTag;
typedef struct _GstKateTagClass GstKateTagClass;

/**
 * GstKateTag:
 *
 * Opaque data structure.
 */
struct _GstKateTag
{
  GstKateParse parse;

  gchar *language;
  gchar *category;
  gint original_canvas_width;
  gint original_canvas_height;
};

struct _GstKateTagClass
{
  GstKateParseClass parent_class;
};

GType gst_kate_tag_get_type (void);

G_END_DECLS
#endif /* __GST_KATE_TAG_H__ */
