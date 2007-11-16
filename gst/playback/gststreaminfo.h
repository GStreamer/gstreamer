/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *               <2007> Wim Taymans <wim.taymans@gmail.com>
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


#ifndef __GST_STREAMINFO_H__
#define __GST_STREAMINFO_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_STREAM_INFO            (gst_stream_info_get_type())
#define GST_STREAM_INFO(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_STREAM_INFO,GstStreamInfo))
#define GST_STREAM_INFO_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_STREAM_INFO,GstStreamInfoClass))
#define GST_IS_STREAM_INFO(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_STREAM_INFO))
#define GST_IS_STREAM_INFO_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_STREAM_INFO))

typedef struct _GstStreamInfo GstStreamInfo;
typedef struct _GstStreamInfoClass GstStreamInfoClass;

typedef enum {
  GST_STREAM_TYPE_UNKNOWN = 0,
  GST_STREAM_TYPE_AUDIO   = 1,    /* an audio stream */
  GST_STREAM_TYPE_VIDEO   = 2,    /* a video stream */
  GST_STREAM_TYPE_TEXT    = 3,    /* a subtitle/text stream */
  GST_STREAM_TYPE_SUBPICTURE = 4, /* a subtitle in picture-form */
  GST_STREAM_TYPE_ELEMENT = 5    /* stream handled by an element */
} GstStreamType;

struct _GstStreamInfo {
  GObject        parent;

  GstObject     *object;        /* pad/element providing/handling this stream */
  GstStreamType  type;          /* the type of the provided stream */
  gchar         *decoder;       /* string describing the decoder */
  gboolean       mute;          /* is the stream muted or not */
  GstObject     *origin;        /* the real object providing this stream, this can
                                   be different from the object as the object can be
                                   a queue pad, inserted for preroll. */
  GstCaps       *caps;          /* the caps of the stream */

  /* this is tream information cached here because the streaminfo may be
   * created before the app can know about it. */
  gchar         *langcode,
                *codec;
};

struct _GstStreamInfoClass {
  GObjectClass   parent_class;

  /* signals */
  void (*muted) (GstStreamInfo *info, gboolean mute);
};

GType gst_stream_info_get_type (void);

GstStreamInfo*  gst_stream_info_new             (GstObject     *object,
                                                 GstStreamType  type,
                                                 const gchar   *decoder,
                                                 const GstCaps *caps);

gboolean        gst_stream_info_set_mute        (GstStreamInfo *stream_info, 
                                                 gboolean mute);
gboolean        gst_stream_info_is_mute         (GstStreamInfo *stream_info);


G_END_DECLS

#endif /* __GST_STREAMINFO_H__ */
