/* GStreamer xvid decoder plugin
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef __GST_XVIDDEC_H__
#define __GST_XVIDDEC_H__

#include <gst/gst.h>
#include "gstxvid.h"

G_BEGIN_DECLS

#define GST_TYPE_XVIDDEC \
  (gst_xviddec_get_type())
#define GST_XVIDDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_XVIDDEC, GstXvidDec))
#define GST_XVIDDEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_XVIDDEC, GstXvidDecClass))
#define GST_IS_XVIDDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_XVIDDEC))
#define GST_IS_XVIDDEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_XVIDDEC))

typedef struct _GstXvidDec GstXvidDec;
typedef struct _GstXvidDecClass GstXvidDecClass;

struct _GstXvidDec {
  GstElement element;

  /* pads */
  GstPad *sinkpad, *srcpad;

  /* xvid handle */
  void *handle;

  /* video (output) settings */
  gint csp;
  gint width, height;
  gint fps_n, fps_d, par_n, par_d;
  gint outbuf_size;

  /* whether in need for keyframe */
  gboolean waiting_for_key;

  /* retain some info on delayed frame */
  gboolean have_ts;
  GstClockTime next_ts, next_dur;
};

struct _GstXvidDecClass {
  GstElementClass parent_class;
};

GType gst_xviddec_get_type(void);

G_END_DECLS

#endif /* __GST_XVIDDEC_H__ */
