/* GStreamer CDXA sync strippper
 * Copyright (C) 2004 Ronald Bultje <rbultje@ronald.bitfreak.net>
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

#ifndef __GST_CDXASTRIP_H__
#define __GST_CDXASTRIP_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_CDXASTRIP \
  (gst_cdxastrip_get_type())
#define GST_CDXASTRIP(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CDXASTRIP,GstCDXAStrip))
#define GST_CDXASTRIP_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CDXASTRIP,GstCDXAStrip))
#define GST_IS_CDXASTRIP(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CDXASTRIP))
#define GST_IS_CDXASTRIP_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CDXASTRIP))

#define GST_CDXA_SECTOR_SIZE    2352
#define GST_CDXA_DATA_SIZE      2324
#define GST_CDXA_HEADER_SIZE    24

typedef struct _GstCDXAStrip GstCDXAStrip;
typedef struct _GstCDXAStripClass GstCDXAStripClass;

struct _GstCDXAStrip {
  GstElement parent;

  /* pads */
  GstPad *sinkpad, *srcpad;
  GstBuffer *cache;
};

struct _GstCDXAStripClass {
  GstElementClass parent_class;
};

GType           gst_cdxastrip_get_type  (void);

/*
 * Also useful for CDXAparse.
 */
GstBuffer *     gst_cdxastrip_strip     (GstBuffer * buf);
gint            gst_cdxastrip_sync      (GstBuffer * buf);

G_END_DECLS

#endif /* __GST_CDXASTRIP_H__ */
