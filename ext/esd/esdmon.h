/* GStreamer
 * Copyright (C) <2001,2002> Richard Boulton <richard-gst@tartarus.org>
 *
 * Based on example.c:
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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

#ifndef __GST_ESDMON_H__
#define __GST_ESDMON_H__

#include <gst/gst.h>

G_BEGIN_DECLS
#define GST_TYPE_ESDMON \
  (gst_esdmon_get_type())
#define GST_ESDMON(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ESDMON,GstEsdmon))
#define GST_ESDMON_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ESDMON,GstEsdmon))
#define GST_IS_ESDMON(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_ESDMON))
#define GST_IS_ESDMON_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_ESDMON))
    typedef enum
{
  GST_ESDMON_OPEN = GST_ELEMENT_FLAG_LAST,
  GST_ESDMON_FLAG_LAST = GST_ELEMENT_FLAG_LAST + 2,
} GstEsdSrcFlags;

typedef struct _GstEsdmon GstEsdmon;
typedef struct _GstEsdmonClass GstEsdmonClass;

struct _GstEsdmon
{
  GstElement element;

  GstPad *srcpad;

  gchar *host;

  int fd;

  gint depth;
  gint channels;
  gint frequency;

  guint64 basetime;
  guint64 samples_since_basetime;
  guint64 curoffset;
  guint64 bytes_per_read;
};

struct _GstEsdmonClass
{
  GstElementClass parent_class;
};

GType gst_esdmon_get_type (void);
gboolean gst_esdmon_factory_init (GstPlugin * plugin);

G_END_DECLS
#endif /* __GST_ESDMON_H__ */
