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
#include <gst/gstclock.h>


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
  (GTK_CHECK_CLASS_TYPE((klass),GST_TYPE_AUDIOSINK))

// NOTE: per-element flags start with 16 for now
typedef enum {
  GST_AUDIOSINK_OPEN		= (1 << 16),
} GstAudioSinkFlags;

typedef struct _GstAudioSink GstAudioSink;
typedef struct _GstAudioSinkClass GstAudioSinkClass;

struct _GstAudioSink {
  GstSink sink;

  GstPad *sinkpad;

  //GstClockTime clocktime;
  GstClock *clock;
  /* soundcard state */
  int fd;
  int caps; /* the capabilities */
  gint format;
  gint channels;
  gint frequency;
  gboolean mute;
};

struct _GstAudioSinkClass {
  GstSinkClass parent_class;

  /* signals */
  void (*handoff) (GstElement *element,GstPad *pad);
};

GtkType gst_audiosink_get_type(void);

gboolean gst_audiosink_factory_init(GstElementFactory *factory);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_AUDIOSINK_H__ */
