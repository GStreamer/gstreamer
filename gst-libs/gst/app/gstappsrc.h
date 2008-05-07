/* GStreamer
 * Copyright (C) 2007 David Schleef <ds@schleef.org>
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

#ifndef _GST_APP_SRC_H_
#define _GST_APP_SRC_H_

#include <gst/gst.h>
#include <gst/base/gstpushsrc.h>

G_BEGIN_DECLS

#define GST_TYPE_APP_SRC \
  (gst_app_src_get_type())
#define GST_APP_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_APP_SRC,GstAppSrc))
#define GST_APP_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_APP_SRC,GstAppSrcClass))
#define GST_IS_APP_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_APP_SRC))
#define GST_IS_APP_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_APP_SRC))

typedef struct _GstAppSrc GstAppSrc;
typedef struct _GstAppSrcClass GstAppSrcClass;

struct _GstAppSrc
{
  GstPushSrc pushsrc;

  /*< private >*/
  GCond *cond;
  GMutex *mutex;
  GQueue *queue;

  GstCaps *caps;
  gint64 size;
  gboolean seekable;
  guint max_buffers;

  gboolean flushing;
  gboolean started;
  gboolean is_eos;
};

struct _GstAppSrcClass
{
  GstPushSrcClass pushsrc_class;

  /* signals */
  void        (*need_data)       (GstAppSrc *src);
  void        (*enough_data)     (GstAppSrc *src);
  gboolean    (*seek_data)       (GstAppSrc *src, guint64 offset);

  /* actions */
  void        (*push_buffer)     (GstAppSrc *src, GstBuffer *buffer);
  void        (*end_of_stream)   (GstAppSrc *src);
};

GType gst_app_src_get_type(void);

GST_DEBUG_CATEGORY_EXTERN (app_src_debug);

void        gst_app_src_set_caps         (GstAppSrc *appsrc, const GstCaps *caps);
GstCaps*    gst_app_src_get_caps         (GstAppSrc *appsrc);

void        gst_app_src_set_size         (GstAppSrc *appsrc, gint64 size);
gint64      gst_app_src_get_size         (GstAppSrc *appsrc);

void        gst_app_src_set_seekable     (GstAppSrc *appsrc, gboolean seekable);
gboolean    gst_app_src_get_seekable     (GstAppSrc *appsrc);

void        gst_app_src_set_max_buffers  (GstAppSrc *appsrc, guint max);
guint       gst_app_src_get_max_buffers  (GstAppSrc *appsrc);

void        gst_app_src_push_buffer      (GstAppSrc *appsrc, GstBuffer *buffer);
void        gst_app_src_end_of_stream    (GstAppSrc *appsrc);

G_END_DECLS

#endif

