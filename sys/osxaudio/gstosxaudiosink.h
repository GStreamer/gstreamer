/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstosxaudiosink.h: 
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


#ifndef __GST_OSXAUDIOSINK_H__
#define __GST_OSXAUDIOSINK_H__


#include <gst/gst.h>

#include "gstosxaudioelement.h"
#include <CoreAudio/CoreAudio.h>

G_BEGIN_DECLS

#define GST_TYPE_OSXAUDIOSINK \
  (gst_osxaudiosink_get_type())
#define GST_OSXAUDIOSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_OSXAUDIOSINK,GstOsxAudioSink))
#define GST_OSXAUDIOSINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_OSXAUDIOSINK,GstOsxAudioSinkClass))
#define GST_IS_OSXAUDIOSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_OSXAUDIOSINK))
#define GST_IS_OSXAUDIOSINK_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_OSXAUDIOSINK))

typedef enum {
  GST_OSXAUDIOSINK_OPEN		= GST_ELEMENT_FLAG_LAST,

  GST_OSXAUDIOSINK_FLAG_LAST	= GST_ELEMENT_FLAG_LAST+2,
} GstOsxAudioSinkFlags;

typedef struct _GstOsxAudioSink GstOsxAudioSink;
typedef struct _GstOsxAudioSinkClass GstOsxAudioSinkClass;

struct _GstOsxAudioSink {
  GstOsxAudioElement	 element;

  GstPad 	*sinkpad;

/*  GstClock 	*provided_clock;
  GstClock 	*clock;
  gboolean	 resync;
  gboolean	 sync;
  guint64	 handled;

  gboolean 	 mute;
  guint 	 bufsize;
  guint 	 chunk_size;*/
};

struct _GstOsxAudioSinkClass {
  GstOsxAudioElementClass parent_class;

  /* signals */
  void (*handoff) (GstElement *element,GstPad *pad);
};

GType gst_osxaudiosink_get_type(void);

G_END_DECLS

#endif /* __GST_OSXAUDIOSINK_H__ */
