/* GStreamer
 * Copyright (C) <2001> Richard Boulton <richard@tartarus.org>
 *
 * gstsynaesthesia.c: implementation of synaesthesia drawing element
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef __GST_SYNAESTHESIA_H__
#define __GST_SYNAESTHESIA_H__

#include <string.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/audio/audio.h>
#include <gst/base/gstadapter.h>
#include "synaescope.h"

G_BEGIN_DECLS
#define GST_TYPE_SYNAESTHESIA            (gst_synaesthesia_get_type())
#define GST_SYNAESTHESIA(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SYNAESTHESIA,GstSynaesthesia))
#define GST_SYNAESTHESIA_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SYNAESTHESIA,GstSynaesthesiaClass))
#define GST_IS_SYNAESTHESIA(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SYNAESTHESIA))
#define GST_IS_SYNAESTHESIA_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SYNAESTHESIA))
typedef struct _GstSynaesthesia GstSynaesthesia;
typedef struct _GstSynaesthesiaClass GstSynaesthesiaClass;

GST_DEBUG_CATEGORY_STATIC (synaesthesia_debug);
#define GST_CAT_DEFAULT (synaesthesia_debug)

struct _GstSynaesthesia
{
  GstElement element;

  /* pads */
  GstPad *sinkpad, *srcpad;
  GstAdapter *adapter;

  guint64 next_ts;              /* the timestamp of the next frame */
  guint64 frame_duration;
  guint bps;                    /* bytes per sample        */
  guint spf;                    /* samples per video frame */

  gint16 datain[2][FFT_BUFFER_SIZE];

  /* video state */
  gint fps_n, fps_d;
  gint width;
  gint height;
  guint outsize;
  GstBufferPool *pool;

  /* Audio state */
  gint sample_rate;
  gint rate;
  gint channels;

  /* Synaesthesia instance */
  syn_instance *si;
};

struct _GstSynaesthesiaClass
{
  GstElementClass parent_class;
};

GType gst_synaesthesia_get_type (void);

G_END_DECLS
#endif /* __GST_SYNAESTHESIA_H__ */
