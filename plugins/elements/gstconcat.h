/* GStreamer concat element
 *
 *  Copyright (c) 2014 Sebastian Dröge <sebastian@centricular.com>
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
 *
 */

#ifndef __GST_CONCAT_H__
#define __GST_CONCAT_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_CONCAT (gst_concat_get_type())
#define GST_CONCAT(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_CONCAT, GstConcat))
#define GST_CONCAT_CAST(obj) ((GstConcat*)obj)
#define GST_CONCAT_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_CONCAT,GstConcatClass))
#define GST_IS_CONCAT(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CONCAT))
#define GST_IS_CONCAT_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CONCAT))

typedef struct _GstConcat GstConcat;
typedef struct _GstConcatClass GstConcatClass;

/**
 * GstConcat:
 *
 * The private concat structure
 */
struct _GstConcat
{
  /*< private >*/
  GstElement parent;

  GMutex lock;
  GCond cond;
  GList *sinkpads; /* Last is earliest */
  GstPad *current_sinkpad;
  GstPad *srcpad;
  guint pad_count;

  /* Format we're operating in */
  GstFormat format;
  /* In format, running time or accumulated byte offset */
  guint64 current_start_offset;
  /* Between current pad's segment start and stop */
  guint64 last_stop;

  gboolean adjust_base;
};

struct _GstConcatClass
{
  GstElementClass parent_class;
};

G_GNUC_INTERNAL GType gst_concat_get_type (void);

G_END_DECLS

#endif /* __GST_CONCAT_H__ */
