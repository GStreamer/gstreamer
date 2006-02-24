/*
 * gstskeltag.h - GStreamer annodex skeleton tags
 * Copyright (C) 2005 Alessandro Decina
 * 
 * Authors:
 *   Alessandro Decina <alessandro@nnva.org>
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
#ifndef __GST_SKEL_TAG_H__
#define __GST_SKEL_TAG_H__

#include <gst/gst.h>

/* GstSkelTagFishead */
#define GST_TYPE_SKEL_TAG_FISHEAD (gst_skel_tag_fishead_get_type ())
#define GST_SKEL_TAG_FISHEAD(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_SKEL_TAG_FISHEAD, \
                           GstSkelTagFishead))
#define GST_SKEL_TAG_FISHEAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_SKEL_TAG_FISHEAD, \
                           GstSkelTagFishead))
#define GST_SKEL_TAG_FISHEAD_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_SKEL_TAG_FISHEAD, \
                             GstSkelTagFisheadClass))

/* GstSkelTagFisbone */
#define GST_TYPE_SKEL_TAG_FISBONE (gst_skel_tag_fisbone_get_type ())
#define GST_SKEL_TAG_FISBONE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_SKEL_TAG_FISBONE, \
                           GstSkelTagFisbone))
#define GST_SKEL_TAG_FISBONE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_SKEL_TAG_FISBONE, \
                           GstSkelTagFisbone))
#define GST_SKEL_TAG_FISBONE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_SKEL_TAG_FISBONE, \
                             GstSkelTagFisboneClass))

typedef struct _GstSkelTagFishead GstSkelTagFishead;
typedef struct _GstSkelTagFisheadClass GstSkelTagFisheadClass;

typedef struct _GstSkelTagFisbone GstSkelTagFisbone;
typedef struct _GstSkelTagFisboneClass GstSkelTagFisboneClass;


struct _GstSkelTagFishead {
  GObject object;

  guint16 major;
  guint16 minor;

  gint64 prestime_n;
  gint64 prestime_d;

  gint64 basetime_n;
  gint64 basetime_d;

  gchar *utc;
};

struct _GstSkelTagFisheadClass {
  GObjectClass parent_class;
};

struct _GstSkelTagFisbone {
  GObject object;

  guint32 hdr_offset;
  guint32 serialno;
  guint32 hdr_num;
  gint64 granulerate_n;
  gint64 granulerate_d;
  gint64 start_granule;
  guint32 preroll;
  guint8 granuleshift;
  gchar *content_type;
  gchar *encoding;
  GValueArray *headers;
};

struct _GstSkelTagFisboneClass {
  GObjectClass parent_class;
};

GType gst_skel_tag_fishead_get_type (void);
GType gst_skel_tag_fisbone_get_type (void);

#endif /* __GST_SKEL_TAG_H__ */
