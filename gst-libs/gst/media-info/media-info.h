/* GStreamer
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


#ifndef __GST_MEDIA_INFO_H__
#define __GST_MEDIA_INFO_H__

#include <gst/gst.h>

G_BEGIN_DECLS typedef struct GstMediaInfoPriv GstMediaInfoPriv;
typedef struct _GstMediaInfo GstMediaInfo;
typedef struct _GstMediaInfoClass GstMediaInfoClass;

struct _GstMediaInfo
{
  GObject parent;

  GstMediaInfoPriv *priv;

  gpointer _gst_reserved[GST_PADDING];
};

struct _GstMediaInfoClass
{
  GObjectClass parent_class;

  /* signals */
  void (*media_info_signal) (GstMediaInfo * gst_media_info);
  void (*error_signal) (GstMediaInfo * gst_media_info, GError * error,
      const gchar * debug);

  gpointer _gst_reserved[GST_PADDING];
};

/* structure for "physical" stream,
 * which can contain multiple sequential ones */
typedef struct
{
  gboolean seekable;
  gchar *mime;
  gchar *path;
  GstCaps *caps;		/* properties of the complete bitstream */

  guint64 length_time;
  glong length_tracks;
  glong bitrate;

  GList *tracks;
} GstMediaInfoStream;

/* structure for "logical" stream or track,
 * or one of a set of sequentially muxed streams */
typedef struct
{
  GstTagList *metadata;		/* changeable metadata or tags */
  GstTagList *streaminfo;	/* codec property stuff */
  GstCaps *format;		/* properties of the logical stream */

  guint64 length_time;

  GList *con_streams;		/* list of concurrent streams in this
				   sequential stream */
} GstMediaInfoTrack;

typedef struct
{
  GstCaps *caps;		/* properties of the muxed concurrent stream */
} GstMediaInfoConcurrent;

#define GST_MEDIA_INFO_ERROR		gst_media_info_error_quark ()

#define GST_MEDIA_INFO_TYPE		(gst_media_info_get_type ())
#define GST_MEDIA_INFO(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_MEDIA_INFO_TYPE, GstMediaInfo))
#define GST_MEDIA_INFO_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), GST_MEDIA_INFO_TYPE, GstMediaInfoClass))
#define IS_GST_MEDIA_INFO(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_MEDIA_INFO_TYPE))
#define IS_GST_MEDIA_INFO_CLASS(obj)    (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_MEDIA_INFO_TYPE))
#define GST_MEDIA_INFO_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_MEDIA_INFO_TYPE, GstMediaInfoClass))

#define GST_MEDIA_INFO_STREAM		1 << 1
#define GST_MEDIA_INFO_MIME		1 << 2
#define GST_MEDIA_INFO_METADATA		1 << 3
#define GST_MEDIA_INFO_STREAMINFO	1 << 4
#define GST_MEDIA_INFO_FORMAT		1 << 5
#define GST_MEDIA_INFO_ALL		((1 << 6) - 1)

GQuark gst_media_info_error_quark (void);

void gst_media_info_init (void);
GType gst_media_info_get_type (void);

GstMediaInfo *gst_media_info_new (GError ** error);

gboolean gst_media_info_set_source (GstMediaInfo * info,
    const char *source, GError ** error);
void gst_media_info_read_with_idler (GstMediaInfo * media_info,
    const char *location, guint16 GST_MEDIA_INFO_FLAGS, GError ** error);
gboolean gst_media_info_read_idler (GstMediaInfo * media_info,
    GstMediaInfoStream ** streamp, GError ** error);
GstMediaInfoStream *gst_media_info_read (GstMediaInfo * media_info,
    const char *location, guint16 GST_MEDIA_INFO_FLAGS, GError ** error);
gboolean gst_media_info_read_many (GstMediaInfo * media_info,
    GList * locations, guint16 GST_MEDIA_INFO_FLAGS, GError ** error);
GstCaps *gst_media_info_get_next (GstMediaInfo * media_info, GError ** error);

/*
 * FIXME: reset ?
gboolean	gst_media_info_write	(GstMediaInfo *media_info,
                                         const char *location,
					 GstCaps *media_info);
					 */

G_END_DECLS
#endif /* __GST_MEDIA_INFO_H__ */
