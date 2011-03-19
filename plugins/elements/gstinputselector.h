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

#define GST_INPUT_SELECTOR_GET_LOCK(sel) (((GstInputSelector*)(sel))->lock)
#define GST_INPUT_SELECTOR_GET_COND(sel) (((GstInputSelector*)(sel))->cond)
#define GST_INPUT_SELECTOR_LOCK(sel) (g_mutex_lock (GST_INPUT_SELECTOR_GET_LOCK(sel)))
#define GST_INPUT_SELECTOR_UNLOCK(sel) (g_mutex_unlock (GST_INPUT_SELECTOR_GET_LOCK(sel)))
#define GST_INPUT_SELECTOR_WAIT(sel) (g_cond_wait (GST_INPUT_SELECTOR_GET_COND(sel), \
			GST_INPUT_SELECTOR_GET_LOCK(sel)))
#define GST_INPUT_SELECTOR_BROADCAST(sel) (g_cond_broadcast (GST_INPUT_SELECTOR_GET_COND(sel)))

struct _GstInputSelector {
  GstElement element;

  GstPad *srcpad;

  GstPad *active_sinkpad;
  guint n_pads;
  guint padcount;
  gboolean sync_streams;

  GstSegment segment;      /* the output segment */
  gboolean pending_close;  /* if we should push a close first */

  GMutex *lock;
  GCond *cond;
  gboolean blocked;
  gboolean flushing;
};

struct _GstInputSelectorClass {
  GstElementClass parent_class;

  gint64 (*block)	(GstInputSelector *self);
  void (*switch_)	(GstInputSelector *self, GstPad *pad,
                         gint64 stop_time, gint64 start_time);
};

GType gst_input_selector_get_type (void);

G_END_DECLS

#endif /* __GST_INPUT_SELECTOR_H__ */
