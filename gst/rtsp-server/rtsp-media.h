/* GStreamer
 * Copyright (C) 2008 Wim Taymans <wim.taymans at gmail.com>
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

#include <gst/gst.h>
#include <gst/rtsp/gstrtspurl.h>

#ifndef __GST_RTSP_MEDIA_H__
#define __GST_RTSP_MEDIA_H__

G_BEGIN_DECLS

/* types for the media */
#define GST_TYPE_RTSP_MEDIA              (gst_rtsp_media_get_type ())
#define GST_IS_RTSP_MEDIA(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_RTSP_MEDIA))
#define GST_IS_RTSP_MEDIA_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_RTSP_MEDIA))
#define GST_RTSP_MEDIA_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_RTSP_MEDIA, GstRTSPMediaClass))
#define GST_RTSP_MEDIA(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_RTSP_MEDIA, GstRTSPMedia))
#define GST_RTSP_MEDIA_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_RTSP_MEDIA, GstRTSPMediaClass))
#define GST_RTSP_MEDIA_CAST(obj)         ((GstRTSPMedia*)(obj))
#define GST_RTSP_MEDIA_CLASS_CAST(klass) ((GstRTSPMediaClass*)(klass))

typedef struct _GstRTSPMediaStream GstRTSPMediaStream;
typedef struct _GstRTSPMedia GstRTSPMedia;
typedef struct _GstRTSPMediaClass GstRTSPMediaClass;

/**
 * GstRTSPMediaStream:
 *
 * @idx: the stream index
 * @element: the toplevel element
 * @srcpad: the srcpad of the stream
 * @payloader: the payloader of the format
 * @caps_sig: the signal id for detecting caps
 * @caps: the caps of the stream
 *
 * The definition of a media stream. The streams are identified by @id.
 */
struct _GstRTSPMediaStream {
  GstRTSPMedia *media;

  guint       idx;

  GstElement *element;
  GstPad     *srcpad;
  GstElement *payloader;
  gulong      caps_sig;
  GstCaps    *caps;
};

/**
 * GstRTSPMedia:
 * @media: the owner #GstRTSPMedia
 *
 * A class that contains the elements to handle the media
 * provided by @media.
 */
struct _GstRTSPMedia {
  GObject       parent;

  GstElement   *element;
  GArray       *streams;
};

struct _GstRTSPMediaClass {
  GObjectClass  parent_class;
};

GType                 gst_rtsp_media_get_type         (void);

/* dealing with the media */
guint                 gst_rtsp_media_n_streams        (GstRTSPMedia *media);
GstRTSPMediaStream *  gst_rtsp_media_get_stream       (GstRTSPMedia *media, guint idx);

G_END_DECLS

#endif /* __GST_RTSP_MEDIA_H__ */
