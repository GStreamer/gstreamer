/* -*- c-basic-offset: 2 -*-
 * GStreamer
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


#ifndef __GST_PLAYONDEMAND_H__
#define __GST_PLAYONDEMAND_H__

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_PLAYONDEMAND \
  (gst_play_on_demand_get_type())
#define GST_PLAYONDEMAND(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_PLAYONDEMAND,GstPlayOnDemand))
#define GST_PLAYONDEMAND_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_PLAYONDEMAND,GstPlayOnDemand))
#define GST_IS_PLAYONDEMAND(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_PLAYONDEMAND))
#define GST_IS_PLAYONDEMAND_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_PLAYONDEMAND))

typedef struct _GstPlayOnDemand       GstPlayOnDemand;
typedef struct _GstPlayOnDemandClass  GstPlayOnDemandClass;
typedef enum   _GstPlayOnDemandFormat GstPlayOnDemandFormat;

enum _GstPlayOnDemandFormat {
  GST_PLAYONDEMAND_FORMAT_INT,
  GST_PLAYONDEMAND_FORMAT_FLOAT
};

struct _GstPlayOnDemand {
  GstElement element;

  GstPad   *sinkpad, *srcpad;
  GstClock *clock;

  /* filter properties */
  gboolean  mute;
  gfloat    buffer_time;
  guint     max_plays;
  gfloat    tick_rate;
  guint     total_ticks;
  guint32  *ticks;

  /* internal buffer info */
  gchar    *buffer;
  guint     buffer_bytes;
  gboolean  eos;

  /* play pointers == internal buffer offsets for producing output sound */
  guint    *plays;
  guint     write;

  /* audio format info (used to calculate buffer_samples) */
  GstPlayOnDemandFormat format;
  guint    rate;
  guint    channels;
  guint    width;
};

struct _GstPlayOnDemandClass {
  GstElementClass parent_class;

  void (*play)    (GstElement *elem);
  void (*clear)   (GstElement *elem);
  void (*reset)   (GstElement *elem);
  void (*played)  (GstElement *elem);
  void (*stopped) (GstElement *elem);
};

GType gst_play_on_demand_get_type(void);

G_END_DECLS

#endif /* __GST_PLAYONDEMAND_H__ */
