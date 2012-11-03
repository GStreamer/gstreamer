/*
 * Copyright (c) 2006 Christophe Fergeau  <teuf@gnome.org>
 * Copyright (c) 2008 Sebastian Dröge  <slomo@circular-chaos.org>
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

#include <gst/gst.h>
#include <gst/base/gstadapter.h>

#ifndef __GST_XINGMUX_H__
#define __GST_XINGMUX_H__

G_BEGIN_DECLS

/* Standard macros for defining types for this element.  */
#define GST_TYPE_XING_MUX \
  (gst_xing_mux_get_type())
#define GST_XING_MUX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_XING_MUX,GstXingMux))
#define GST_XING_MUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_XING_MUX,GstXingMuxClass))
#define GST_IS_XING_MUX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_XING_MUX))
#define GST_IS_XING_MUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_XING_MUX))

typedef struct _GstXingMux GstXingMux;
typedef struct _GstXingMuxClass GstXingMuxClass;

/* Definition of structure storing data for this element. */

/**
 * GstXingMux:
 *
 * Opaque data structure.
 */
struct _GstXingMux {
  GstElement element;

  GstPad *sinkpad, *srcpad;

  /* < private > */

  GstAdapter *adapter;
  GstClockTime duration;
  guint64 byte_count;
  guint64 frame_count;
  GList *seek_table;
  gboolean sent_xing;

  /* Copy of the first frame header */
  guint32 first_header;
};

/* Standard definition defining a class for this element. */

/**
 * GstXingMuxClass:
 *
 * Opaque data structure.
 */
struct _GstXingMuxClass {
  GstElementClass parent_class;
};

/* Standard function returning type information. */
GType gst_xing_mux_get_type (void);

G_END_DECLS

#endif /* __GST_XINGMUX_H__ */
