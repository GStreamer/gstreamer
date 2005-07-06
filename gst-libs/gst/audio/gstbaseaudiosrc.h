/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2005 Wim Taymans <wim@fluendo.com>
 *
 * gstbaseaudiosrc.h: 
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

/* a base class for audio sources.
 */

#ifndef __GST_BASEAUDIOSRC_H__
#define __GST_BASEAUDIOSRC_H__

#include <gst/gst.h>
#include <gst/base/gstpushsrc.h>
#include "gstringbuffer.h"
#include "gstaudioclock.h"

G_BEGIN_DECLS

#define GST_TYPE_BASEAUDIOSRC  	 	(gst_baseaudiosrc_get_type())
#define GST_BASEAUDIOSRC(obj) 		(G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_BASEAUDIOSRC,GstBaseAudioSrc))
#define GST_BASEAUDIOSRC_CLASS(klass) 	(G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_BASEAUDIOSRC,GstBaseAudioSrcClass))
#define GST_BASEAUDIOSRC_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_BASEAUDIOSRC, GstBaseAudioSrcClass))
#define GST_IS_BASEAUDIOSRC(obj)  	(G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_BASEAUDIOSRC))
#define GST_IS_BASEAUDIOSRC_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_BASEAUDIOSRC))

#define GST_BASEAUDIOSRC_CLOCK(obj)	 (GST_BASEAUDIOSRC (obj)->clock)
#define GST_BASEAUDIOSRC_PAD(obj)	 (GST_BASESRC (obj)->srcpad)

typedef struct _GstBaseAudioSrc GstBaseAudioSrc;
typedef struct _GstBaseAudioSrcClass GstBaseAudioSrcClass;

struct _GstBaseAudioSrc {
  GstPushSrc 	 element;

  /*< protected >*/ /* with LOCK */
  /* our ringbuffer */
  GstRingBuffer *ringbuffer;

  /* required buffer and latency */
  GstClockTime   buffer_time;
  GstClockTime   latency_time;

  /* clock */
  GstClock	*clock;
};

struct _GstBaseAudioSrcClass {
  GstPushSrcClass parent_class;

  /* subclass ringbuffer allocation */
  GstRingBuffer* (*create_ringbuffer)  (GstBaseAudioSrc *src);
};

GType gst_baseaudiosrc_get_type(void);

GstRingBuffer *gst_baseaudiosrc_create_ringbuffer (GstBaseAudioSrc *src);

G_END_DECLS

#endif /* __GST_BASEAUDIOSRC_H__ */
