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


#include <config.h>
#include <gst/gst.h>


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define GST_TYPE_PLAYONDEMAND \
  (gst_play_on_demand_get_type())
#define GST_PLAYONDEMAND(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_PLAYONDEMAND,GstPlayOnDemand))
#define GST_PLAYONDEMAND_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ULAW,GstPlayOnDemand))
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

  GstPad *sinkpad, *srcpad;
  GstBufferPool *bufpool;

  /* these next data elements are for the filter's internal buffers and list of
     play pointers (offsets in the internal buffers). there are also flags for
     repeating from the beginning or end of the input stream, and a max buffer
     size. */
  gchar    *buffer;
  guint     buffer_size;
  guint     buffer_samples;

  guint    *plays;
  guint     write;
  guint     start;

  gboolean  play_from_beginning;
  gboolean  buffer_filled_once;

  gboolean  eos;
  gboolean  silent;

  /* the playondemand filter needs to keep track of a number of 'measures'
     consisting of 'beats'. these are represented as an array of guint64s, with
     each guint64 being one measure, and the bits in each measure being beats
     (lower order bits come first). each measure can therefore have a maximum of
     64 beats, though there are a potentially unlimited number of measures.

     this is basically a way to figure out when incoming clock signals should
     add a play pointer. */
  GstClock *clock;
  guint     last_time;

  guint64  *times;
  guint     num_measures;
  guint     num_beats;
  guint     total_beats;

  /* the next three are valid for both int and float */
  GstPlayOnDemandFormat format;
  guint                 rate;
  guint                 channels;

  /* the next five are valid only for format == GST_PLAYONDEMAND_FORMAT_INT */
  guint    width;
  guint    depth;
  guint    endianness;
  guint    law;
  gboolean is_signed;

  /* the next three are valid only for format == GST_PLAYONDEMAND_FORMAT_FLOAT */
  const gchar *layout;
  gfloat       slope;
  gfloat       intercept;
};

struct _GstPlayOnDemandClass {
  GstElementClass parent_class;

  void (*play)  (GstElement *elem);
  void (*reset) (GstElement *elem);
};

GType gst_play_on_demand_get_type(void);

void gst_play_on_demand_set_beat     (GstPlayOnDemand *filter,
                                      const guint measure,
                                      const guint beat,
                                      const gboolean value);
gboolean gst_play_on_demand_get_beat (GstPlayOnDemand *filter,
                                      const guint measure,
                                      const guint beat);
void gst_play_on_demand_toggle_beat  (GstPlayOnDemand *filter,
                                      const guint measure,
                                      const guint beat);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_PLAYONDEMAND_H__ */
