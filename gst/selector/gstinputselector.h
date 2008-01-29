/* GStreamer
 * Copyright (C) 2003 Julien Moutte <julien@moutte.net>
 * Copyright (C) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2008 Nokia Corporation. (contact <stefan.kost@nokia.com>)
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
 
#ifndef __GST_INPUT_SELECTOR_H__
#define __GST_INPUT_SELECTOR_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_INPUT_SELECTOR \
  (gst_input_selector_get_type())
#define GST_INPUT_SELECTOR(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_INPUT_SELECTOR, GstInputSelector))
#define GST_INPUT_SELECTOR_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_INPUT_SELECTOR, GstInputSelectorClass))
#define GST_IS_INPUT_SELECTOR(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_INPUT_SELECTOR))
#define GST_IS_INPUT_SELECTOR_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_INPUT_SELECTOR))

typedef struct _GstInputSelector GstInputSelector;
typedef struct _GstInputSelectorClass GstInputSelectorClass;

struct _GstInputSelector {
  GstElement element;

  GstPad *srcpad;

  GstPad *active_sinkpad;
  guint nb_sinkpads;

  GstSegment segment;

  GCond *blocked_cond;
  gboolean blocked;
  gboolean pending_stop;
  GstSegment pending_stop_segment;
};

struct _GstInputSelectorClass {
  GstElementClass parent_class;

  gint64 (*block)	(GstInputSelector *self);
  void (*switch_)	(GstInputSelector *self, const gchar *pad_name,
                         gint64 stop_time, gint64 start_time);
};

GType gst_input_selector_get_type (void);

G_END_DECLS

#endif /* __GST_INPUT_SELECTOR_H__ */
