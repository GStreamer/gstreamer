/* GStreamer
 * Copyright (C) <2006> Lutz Mueller <lutz at topfrose dot de>
 *               <2006> Wim Taymans <wim@fluendo.com>
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

#ifndef __GST_RDT_DEPAY_H__
#define __GST_RDT_DEPAY_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_RDT_DEPAY \
  (gst_rdt_depay_get_type())
#define GST_RDT_DEPAY(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_RDT_DEPAY,GstRDTDepay))
#define GST_RDT_DEPAY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_RDT_DEPAY,GstRDTDepayClass))
#define GST_IS_RDT_DEPAY(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_RDT_DEPAY))
#define GST_IS_RDT_DEPAY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_RDT_DEPAY))

typedef struct _GstRDTDepay GstRDTDepay;
typedef struct _GstRDTDepayClass GstRDTDepayClass;

struct _GstRDTDepay
{
  GstElement parent;

  GstPad *sinkpad;
  GstPad *srcpad;

  guint clock_rate;
  GstClockTime npt_start;
  GstClockTime npt_stop;
  gdouble play_speed;
  gdouble play_scale;

  guint32 next_seqnum;

  gboolean discont;
  gboolean need_newsegment;
  GstSegment segment;
  GstBuffer *header;
};

struct _GstRDTDepayClass
{
  GstElementClass parent_class;
};

GType gst_rdt_depay_get_type (void);

gboolean gst_rdt_depay_plugin_init (GstPlugin * plugin);

G_END_DECLS

#endif /* __GST_RDT_DEPAY_H__ */
