/*
 * Copyright (C) 2001 CodeFactory AB
 * Copyright (C) 2001 Thomas Nyberg <thomas@codefactory.se>
 * Copyright (C) 2001-2002 Andy Wingo <apwingo@eos.ncsu.edu>
 * Copyright (C) 2003 Benjamin Otte <in7y118@public.uni-hamburg.de>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __GST_ALSA_SINK_H__
#define __GST_ALSA_SINK_H__

#include "gstalsamixer.h"

G_BEGIN_DECLS

#define GST_ALSA_SINK(obj)            (G_TYPE_CHECK_INSTANCE_CAST(obj, GST_TYPE_ALSA_SINK, GstAlsaSink))
#define GST_ALSA_SINK_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST(klass, GST_TYPE_ALSA_SINK, GstAlsaSinkClass))
#define GST_IS_ALSA_SINK(obj)         (G_TYPE_CHECK_INSTANCE_TYPE(obj, GST_TYPE_ALSA_SINK))
#define GST_IS_ALSA_SINK_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE(klass, GST_TYPE_ALSA_SINK))
#define GST_TYPE_ALSA_SINK            (gst_alsa_sink_get_type())

typedef struct _GstAlsaSink GstAlsaSink;
typedef struct _GstAlsaSinkClass GstAlsaSinkClass;

struct _GstAlsaSink {
  GstAlsaMixer parent;

  /* array of the data on the channels */
  guint8    *buf_data[GST_ALSA_MAX_TRACKS];  /* pointer into buffer */
  guint      size[GST_ALSA_MAX_TRACKS];      /* sink: bytes left in buffer */
  GstData   *gst_data[GST_ALSA_MAX_TRACKS];  /* current data */
  guint      behaviour[GST_ALSA_MAX_TRACKS]; /* 0 = data points into buffer (so unref when size == 0),
                                                  1 = data should be freed, use buffer after that */
};

struct _GstAlsaSinkClass {
  GstAlsaMixerClass parent_class;
};

GType gst_alsa_sink_get_type (void);

gboolean gst_alsa_sink_factory_init (GstPlugin *plugin);

G_END_DECLS

#endif /* __GST_ALSA_SINK_H__ */
