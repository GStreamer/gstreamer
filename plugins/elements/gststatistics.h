/* GStreamer
 * Copyright (C) 2001 David I. Lehn <dlehn@users.sourceforge.net>
 *
 * gststatistics.h: 
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


#ifndef __GST_STATISTICS_H__
#define __GST_STATISTICS_H__


#include <gst/gst.h>

G_BEGIN_DECLS
#define GST_TYPE_STATISTICS \
  (gst_statistics_get_type())
#define GST_STATISTICS(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_STATISTICS,GstStatistics))
#define GST_STATISTICS_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_STATISTICS,GstStatisticsClass))
#define GST_IS_STATISTICS(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_STATISTICS))
#define GST_IS_STATISTICS_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_STATISTICS))
typedef struct _GstStatistics GstStatistics;
typedef struct _GstStatisticsClass GstStatisticsClass;

typedef struct _stats stats;

struct _stats
{
  gint64 buffers;
  gint64 bytes;
  gint64 events;
};

struct _GstStatistics
{
  GstElement element;

  GstPad *sinkpad;
  GstPad *srcpad;

  GTimer *timer;
  GTimer *last_timer;

  stats stats;
  stats last_stats;
  stats update_count;
  stats update_freq;

  gboolean update_on_eos;
  gboolean update;
  gboolean silent;
};

struct _GstStatisticsClass
{
  GstElementClass parent_class;

  /* signals */
  void (*update) (GstElement * element);
};

GType gst_statistics_get_type (void);

G_END_DECLS
#endif /* __GST_STATISTICS_H__ */
