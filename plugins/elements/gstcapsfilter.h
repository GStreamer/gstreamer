/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstidentity.h:
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


#ifndef __GST_CAPS_FILTER_H__
#define __GST_CAPS_FILTER_H__


#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>

G_BEGIN_DECLS

#define GST_TYPE_CAPS_FILTER \
  (gst_capsfilter_get_type())
#define GST_CAPS_FILTER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CAPS_FILTER,GstCapsFilter))
#define GST_CAPS_FILTER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CAPS_FILTER,GstCapsFilterClass))
#define GST_IS_CAPS_FILTER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CAPS_FILTER))
#define GST_IS_CAPS_FILTER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CAPS_FILTER))

typedef struct _GstCapsFilter GstCapsFilter;
typedef struct _GstCapsFilterClass GstCapsFilterClass;

/**
 * GstCapsFilterCapsChangeMode:
 * @GST_CAPS_FILTER_CAPS_CHANGE_MODE_IMMEDIATE: Only accept the current filter caps
 * @GST_CAPS_FILTER_CAPS_CHANGE_MODE_DELAYED: Temporarily accept previous filter caps
 *
 * Filter caps change behaviour
 */
typedef enum {
  GST_CAPS_FILTER_CAPS_CHANGE_MODE_IMMEDIATE,
  GST_CAPS_FILTER_CAPS_CHANGE_MODE_DELAYED
} GstCapsFilterCapsChangeMode;

/**
 * GstCapsFilter:
 *
 * The opaque #GstCapsFilter data structure.
 */
struct _GstCapsFilter {
  GstBaseTransform trans;

  GstCaps *filter_caps;
  gboolean filter_caps_used;
  GstCapsFilterCapsChangeMode caps_change_mode;
  gboolean got_sink_caps;

  GList *pending_events;
  GList *previous_caps;
};

struct _GstCapsFilterClass {
  GstBaseTransformClass trans_class;
};

G_GNUC_INTERNAL GType gst_capsfilter_get_type (void);

G_END_DECLS

#endif /* __GST_CAPS_FILTER_H__ */

