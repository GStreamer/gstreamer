/* GStreamer
 * Copyright (C) 2003 Julien Moutte <julien@moutte.net>
 * Copyright (C) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
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
 
#ifndef __RSN_STREAM_SELECTOR_H__
#define __RSN_STREAM_SELECTOR_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define RSN_TYPE_STREAM_SELECTOR \
  (rsn_stream_selector_get_type())
#define RSN_STREAM_SELECTOR(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), RSN_TYPE_STREAM_SELECTOR, RsnStreamSelector))
#define RSN_STREAM_SELECTOR_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), RSN_TYPE_STREAM_SELECTOR, RsnStreamSelectorClass))
#define RSN_IS_STREAM_SELECTOR(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), RSN_TYPE_STREAM_SELECTOR))
#define RSN_IS_STREAM_SELECTOR_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), RSN_TYPE_STREAM_SELECTOR))

typedef struct _RsnStreamSelector RsnStreamSelector;
typedef struct _RsnStreamSelectorClass RsnStreamSelectorClass;

struct _RsnStreamSelector {
  GstElement element;

  GstPad *srcpad;

  GstPad *active_sinkpad;
  guint n_pads;
  guint padcount;

  GstSegment segment;
  gboolean mark_discont;
};

struct _RsnStreamSelectorClass {
  GstElementClass parent_class;
};

GType rsn_stream_selector_get_type (void);

G_END_DECLS

#endif /* __RSN_STREAM_SELECTOR_H__ */
