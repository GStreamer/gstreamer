/* GStreamer FAAC (Free AAC Encoder) plugin
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __GST_FAAC_H__
#define __GST_FAAC_H__

#include <gst/gst.h>
#include <faac.h>

G_BEGIN_DECLS
#define GST_TYPE_FAAC \
  (gst_faac_get_type ())
#define GST_FAAC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_FAAC, GstFaac))
#define GST_FAAC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_FAAC, GstFaacClass))
#define GST_IS_FAAC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_FAAC))
#define GST_IS_FAAC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_FAAC))
    typedef struct _GstFaac
{
  GstElement element;

  /* pads */
  GstPad *srcpad, *sinkpad;

  /* stream properties */
  gint samplerate, channels, format, bps, bitrate, profile, shortctl;
  gboolean tns, midside;
  gulong bytes, samples;

  /* FAAC object */
  faacEncHandle handle;

  /* cache of the input */
  GstBuffer *cache;
  guint64 cache_time, cache_duration;
} GstFaac;

typedef struct _GstFaacClass
{
  GstElementClass parent_class;
} GstFaacClass;

GType gst_faac_get_type (void);

G_END_DECLS
#endif /* __GST_FAAC_H__ */
