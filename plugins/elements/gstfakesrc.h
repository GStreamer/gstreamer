/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstfakesrc.h: 
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


#ifndef __GST_FAKESRC_H__
#define __GST_FAKESRC_H__


#include <config.h>
#include <gst/gst.h>


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


GstElementDetails gst_fakesrc_details;

typedef enum {
  FAKESRC_FIRST_LAST_LOOP = 1,
  FAKESRC_LAST_FIRST_LOOP,
  FAKESRC_PING_PONG,
  FAKESRC_ORDERED_RANDOM,
  FAKESRC_RANDOM,
  FAKESRC_PATTERN_LOOP,
  FAKESRC_PING_PONG_PATTERN,
  FAKESRC_GET_ALWAYS_SUCEEDS,
} GstFakeSrcOutputType;

#define GST_TYPE_FAKESRC \
  (gst_fakesrc_get_type())
#define GST_FAKESRC(obj) \
  (GTK_CHECK_CAST((obj),GST_TYPE_FAKESRC,GstFakeSrc))
#define GST_FAKESRC_CLASS(klass) \
  (GTK_CHECK_CLASS_CAST((klass),GST_TYPE_FAKESRC,GstFakeSrcClass))
#define GST_IS_FAKESRC(obj) \
  (GTK_CHECK_TYPE((obj),GST_TYPE_FAKESRC))
#define GST_IS_FAKESRC_CLASS(obj) \
  (GTK_CHECK_CLASS_TYPE((klass),GST_TYPE_FAKESRC))

typedef struct _GstFakeSrc GstFakeSrc;
typedef struct _GstFakeSrcClass GstFakeSrcClass;

struct _GstFakeSrc {
  GstElement element;

  gboolean loop_based;
  gint numsrcpads;
  GSList *srcpads;
  GstFakeSrcOutputType output;
  gchar *pattern;
  GList *patternlist;
  gint num_buffers;
  gboolean silent;
};

struct _GstFakeSrcClass {
  GstElementClass parent_class;

  /* signals */
  void (*handoff) (GstElement *element,GstPad *pad);
};

GtkType gst_fakesrc_get_type(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_FAKESRC_H__ */
