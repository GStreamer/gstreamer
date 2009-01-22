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

/* types for the media bin */
#define GST_TYPE_RTSP_MEDIA_BIN              (gst_rtsp_media_bin_get_type ())
#define GST_IS_RTSP_MEDIA_BIN(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_RTSP_MEDIA_BIN))
#define GST_IS_RTSP_MEDIA_BIN_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_RTSP_MEDIA_BIN))
#define GST_RTSP_MEDIA_BIN_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_RTSP_MEDIA_BIN, GstRTSPMediaBinClass))
#define GST_RTSP_MEDIA_BIN(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_RTSP_MEDIA_BIN, GstRTSPMediaBin))
#define GST_RTSP_MEDIA_BIN_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_RTSP_MEDIA_BIN, GstRTSPMediaBinClass))
#define GST_RTSP_MEDIA_BIN_CAST(obj)         ((GstRTSPMediaBin*)(obj))
#define GST_RTSP_MEDIA_BIN_CLASS_CAST(klass) ((GstRTSPMediaBinClass*)(klass))

typedef struct _GstRTSPMediaStream GstRTSPMediaStream;
typedef struct _GstRTSPMediaBin GstRTSPMediaBin;
typedef struct _GstRTSPMediaBinClass GstRTSPMediaBinClass;

/**
 * GstRTSPMediaStream:
 *
 * @idx: the stream index
 * @element: the toplevel element
 * @srcpad: the srcpad of the stream
 * @payloader: the payloader of the formattt
 * @caps_sig: the signal id for detecting caps
 * @caps: the caps of the stream
 *
 * The definition of a media stream. The streams are identified by @id.
 */
struct _GstRTSPMediaStream {
  GstRTSPMediaBin *mediabin;

  guint       idx;

  GstElement *element;
  GstPad     *srcpad;
  GstElement *payloader;
  gulong      caps_sig;
  GstCaps    *caps;
};

/**
 * GstRTSPMediaBin:
 * @media: the owner #GstRTSPMedia
 *
 * A class that contains the elements to handle the media
 * provided by @media.
 */
struct _GstRTSPMediaBin {
  GObject       parent;

  GstElement   *element;
  GArray       *streams;
};

struct _GstRTSPMediaBinClass {
  GObjectClass  parent_class;
};

GType                 gst_rtsp_media_bin_get_type         (void);

/* dealing with the media bin */
guint                 gst_rtsp_media_bin_n_streams        (GstRTSPMediaBin *bin);
GstRTSPMediaStream *  gst_rtsp_media_bin_get_stream       (GstRTSPMediaBin *bin, guint idx);

G_END_DECLS

#endif /* __GST_RTSP_MEDIA_H__ */
