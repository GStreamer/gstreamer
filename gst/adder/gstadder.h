/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstadder.h: Header for GstAdder element
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

#ifndef __GST_ADDER_H__
#define __GST_ADDER_H__

#include <gst/gst.h>
#include <gst/base/gstcollectpads.h>
#include <gst/audio/audio.h>

G_BEGIN_DECLS

#define GST_TYPE_ADDER            (gst_adder_get_type())
#define GST_ADDER(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ADDER,GstAdder))
#define GST_IS_ADDER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_ADDER))
#define GST_ADDER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass) ,GST_TYPE_ADDER,GstAdderClass))
#define GST_IS_ADDER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass) ,GST_TYPE_ADDER))
#define GST_ADDER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj) ,GST_TYPE_ADDER,GstAdderClass))

typedef struct _GstAdder             GstAdder;
typedef struct _GstAdderClass        GstAdderClass;

typedef struct _GstAdderPad GstAdderPad;
typedef struct _GstAdderPadClass GstAdderPadClass;

/**
 * GstAdder:
 *
 * The adder object structure.
 */
struct _GstAdder {
  GstElement      element;

  GstPad         *srcpad;
  GstCollectPads *collect;
  /* pad counter, used for creating unique request pads */
  gint            padcount;

  /* the next are valid for both int and float */
  GstAudioInfo    info;

  /* counters to keep track of timestamps */
  gint64          offset;

  /* sink event handling */
  GstSegment      segment;
  volatile gboolean new_segment_pending;
  volatile gboolean flush_stop_pending;

  /* current caps */
  GstCaps *current_caps;

  /* target caps (set via property) */
  GstCaps *filter_caps;

  /* Pending inline events */
  GList *pending_events;
  
  gboolean send_stream_start;
  gboolean send_caps;
};

struct _GstAdderClass {
  GstElementClass parent_class;
};

GType    gst_adder_get_type (void);

#define GST_TYPE_ADDER_PAD            (gst_adder_pad_get_type())
#define GST_ADDER_PAD(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ADDER_PAD,GstAdderPad))
#define GST_IS_ADDER_PAD(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_ADDER_PAD))
#define GST_ADDER_PAD_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass) ,GST_TYPE_ADDER_PAD,GstAdderPadClass))
#define GST_IS_ADDER_PAD_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass) ,GST_TYPE_ADDER_PAD))
#define GST_ADDER_PAD_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj) ,GST_TYPE_ADDER_PAD,GstAdderPadClass))

struct _GstAdderPad {
  GstPad parent;

  gdouble volume;
  gint volume_i32;
  gint volume_i16;
  gint volume_i8;
  gboolean mute;
};

struct _GstAdderPadClass {
  GstPadClass parent_class;
};

GType gst_adder_pad_get_type (void);

G_END_DECLS


#endif /* __GST_ADDER_H__ */
