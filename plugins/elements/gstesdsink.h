/* Gnome-Streamer
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


#ifndef __GST_ESDSINK_H__
#define __GST_ESDSINK_H__


#include <gst/gstfilter.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define GST_TYPE_ESDSINK \
  (gst_esdsink_get_type())
#define GST_ESDSINK(obj) \
  (GTK_CHECK_CAST((obj),GST_TYPE_ESDSINK,GstEsdSink))
#define GST_ESDSINK_CLASS(klass) \
  (GTK_CHECK_CLASS_CAST((klass),GST_TYPE_ESDSINK,GstEsdSinkClass))
#define GST_IS_ESDSINK(obj) \
  (GTK_CHECK_TYPE((obj),GST_TYPE_ESDSINK))
#define GST_IS_ESDSINK_CLASS(obj) \
  (GTK_CHECK_CLASS_TYPE((klass),GST_TYPE_ESDSINK)))

typedef struct _GstEsdSink GstEsdSink;
typedef struct _GstEsdSinkClass GstEsdSinkClass;

struct _GstEsdSink {
  GstFilter filter;

  GstPad *sinkpad;

  /* soundcard state */
  
  int fd;
  gint format;
  gint channels;
  gint frequency;
};

struct _GstEsdSinkClass {
  GstFilterClass parent_class;

  /* signals */
  void (*handoff) (GstElement *element,GstPad *pad);
};

GtkType gst_esdsink_get_type(void);
GstElement *gst_esdsink_new(gchar *name);
void gst_esdsink_chain(GstPad *pad,GstBuffer *buf);

void gst_esdsink_sync_parms(GstEsdSink *esdsink);

void gst_esdsink_set_format(GstEsdSink *esdsink,gint format);
void gst_esdsink_set_channels(GstEsdSink *esdsink,gint channels);
void gst_esdsink_set_frequency(GstEsdSink *esdsink,gint frequency);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_ESDSINK_H__ */
