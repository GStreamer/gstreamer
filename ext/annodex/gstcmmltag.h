/*
 * gstcmmltag.h - GStreamer annodex CMML tag support
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

#ifndef __GST_CMML_TAG_H__
#define __GST_CMML_TAG_H__

#include <gst/gst.h>

/* GstCmmlTagStream */
#define GST_TYPE_CMML_TAG_STREAM (gst_cmml_tag_stream_get_type ())
#define GST_CMML_TAG_STREAM(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), \
                              GST_TYPE_CMML_TAG_STREAM, GstCmmlTagStream))
#define GST_CMML_TAG_STREAM_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), \
                              GST_TYPE_CMML_TAG_STREAM, GstCmmlTagStreamClass))
#define GST_CMML_TAG_STREAM_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS((obj), \
                             GST_TYPE_CMML_TAG_STREAM, GstCmmlTagStreamClass))

/* GstCmmlTagHead */
#define GST_TYPE_CMML_TAG_HEAD (gst_cmml_tag_head_get_type ())
#define GST_CMML_TAG_HEAD(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_CMML_TAG_HEAD, GstCmmlTagHead))
#define GST_CMML_TAG_HEAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_CMML_TAG_HEAD, GstCmmlTagHeadClass))
#define GST_CMML_TAG_HEAD_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS((obj), \
                             GST_TYPE_CMML_TAG_HEAD, GstCmmlTagHeadClass))

/* GstCmmlTagClip */
#define GST_TYPE_CMML_TAG_CLIP (gst_cmml_tag_clip_get_type ())
#define GST_CMML_TAG_CLIP(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_CMML_TAG_CLIP, GstCmmlTagClip))
#define GST_CMML_TAG_CLIP_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_CMML_TAG_CLIP, GstCmmlTagClipClass))
#define GST_CMML_TAG_CLIP_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS((obj), \
                             GST_TYPE_CMML_TAG_CLIP, GstCmmlTagClipClass))

typedef struct _GstCmmlTagStream GstCmmlTagStream;
typedef struct _GstCmmlTagStreamClass GstCmmlTagStreamClass;
typedef struct _GstCmmlTagHead GstCmmlTagHead;
typedef struct _GstCmmlTagHeadClass GstCmmlTagHeadClass;
typedef struct _GstCmmlTagClip GstCmmlTagClip;
typedef struct _GstCmmlTagClipClass GstCmmlTagClipClass;

struct _GstCmmlTagStream {
  GObject object;

  guchar *timebase;
  guchar *utc;

  GValueArray *imports;
};

struct _GstCmmlTagStreamClass {
  GObjectClass parent_class;
};

struct _GstCmmlTagHead {
  GObject object;
  
  guchar *title;                  /* title of the media */
  guchar *base;
  GValueArray *meta;              /* metadata attached to the media.
                                 * The elements are positioned in key-value
                                 * pairs ie (key, content, key2, content2,
                                 * ...)
                                 */
};

struct _GstCmmlTagHeadClass {
  GObjectClass parent_class;
};

struct _GstCmmlTagClip {
  GObject object;

  gboolean empty;                 /* empty flag. An empty clip marks the
                                   * end of the previous clip.
                                   */
  
  guchar *id;                     /* clip id */
  guchar *track;                  /* clip track */

  GstClockTime start_time;        /* clip start time */
  GstClockTime end_time;          /* clip end time */
  
  guchar *anchor_href;            /* anchor href URI */
  guchar *anchor_text;            /* anchor text */
  
  guchar *img_src;                /* image URI */
  guchar *img_alt;                /* image alternative text */
  
  guchar *desc_text;              /* clip description */
  
  GValueArray *meta;              /* metadata attached to the clip
                                   * The elements are positioned in key-value
                                   * pairs ie (key, content, key2, content2,
                                   * ...)
                                   */
};

struct _GstCmmlTagClipClass {
  GObjectClass parent_class;
};

GType gst_cmml_tag_stream_get_type (void);
GType gst_cmml_tag_head_get_type (void);
GType gst_cmml_tag_clip_get_type (void);

#endif /* __GST_CMML_TAG_H__ */
