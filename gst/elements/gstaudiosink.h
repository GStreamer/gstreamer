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


#ifndef __GST_AUDIOSINK_H__
#define __GST_AUDIOSINK_H__


#include <config.h>
#include <gst/gst.h>


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


GstElementDetails gst_audiosink_details;


#define GST_TYPE_AUDIOSINK \
  (gst_audiosink_get_type())
#define GST_AUDIOSINK(obj) \
  (GTK_CHECK_CAST((obj),GST_TYPE_AUDIOSINK,GstAudioSink))
#define GST_AUDIOSINK_CLASS(klass) \
  (GTK_CHECK_CLASS_CAST((klass),GST_TYPE_AUDIOSINK,GstAudioSinkClass))
#define GST_IS_AUDIOSINK(obj) \
  (GTK_CHECK_TYPE((obj),GST_TYPE_AUDIOSINK))
#define GST_IS_AUDIOSINK_CLASS(obj) \
  (GTK_CHECK_CLASS_TYPE((klass),GST_TYPE_AUDIOSINK)))

typedef struct _GstAudioSink GstAudioSink;
typedef struct _GstAudioSinkClass GstAudioSinkClass;

struct _GstAudioSink {
  GstFilter filter;

  GstPad *sinkpad;

  /* soundcard state */
  int fd;
  gint format;
  gint channels;
  gint frequency;
};

struct _GstAudioSinkClass {
  GstFilterClass parent_class;

  /* signals */
  void (*handoff) (GstElement *element,GstPad *pad);
};

GtkType gst_audiosink_get_type(void);
GstElement *gst_audiosink_new(gchar *name);
void gst_audiosink_chain(GstPad *pad,GstBuffer *buf);

void gst_audiosink_sync_parms(GstAudioSink *audiosink);

void gst_audiosink_set_format(GstAudioSink *audiosink,gint format);
void gst_audiosink_set_channels(GstAudioSink *audiosink,gint channels);
void gst_audiosink_set_frequency(GstAudioSink *audiosink,gint frequency);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_AUDIOSINK_H__ */
