/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstosssink.h: 
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


#ifndef __GST_OSSSINK_H__
#define __GST_OSSSINK_H__


#include <config.h>
#include <gst/gst.h>

#include "gstossclock.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define GST_TYPE_OSSSINK \
  (gst_osssink_get_type())
#define GST_OSSSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_OSSSINK,GstOssSink))
#define GST_OSSSINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_OSSSINK,GstOssSinkClass))
#define GST_IS_OSSSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_OSSSINK))
#define GST_IS_OSSSINK_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_OSSSINK))

typedef enum {
  GST_OSSSINK_OPEN		= GST_ELEMENT_FLAG_LAST,

  GST_OSSSINK_FLAG_LAST	= GST_ELEMENT_FLAG_LAST+2,
} GstOssSinkFlags;

typedef struct _GstOssSink GstOssSink;
typedef struct _GstOssSinkClass GstOssSinkClass;

struct _GstOssSink {
  GstElement element;

  GstPad *sinkpad;
  GstBufferPool *sinkpool;

  GstClock *provided_clock;
  GstClock *clock;

  /* device */
  gchar *device;

  /* soundcard state */
  int fd;
  int caps; /* the capabilities */
  gint format;
  gint channels;
  gint frequency;
  gint fragment;
  gboolean mute;
  guint bufsize;
  guint bps;
  gboolean have_offset;
  guint64 offset;
  guint64 handled;

  guint64 fragment_time;
};

struct _GstOssSinkClass {
  GstElementClass parent_class;

  /* signals */
  void (*handoff) (GstElement *element,GstPad *pad);
};

GType gst_osssink_get_type(void);

gboolean gst_osssink_factory_init(GstPlugin *plugin);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_OSSSINK_H__ */
