/* GStreamer RIFF I/O
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * riff-read.h: function declarations for parsing a RIFF file
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

#ifndef __GST_RIFF_READ_H__
#define __GST_RIFF_READ_H__

#include <glib.h>
#include <gst/gst.h>
#include <gst/bytestream/bytestream.h>

G_BEGIN_DECLS

#define GST_TYPE_RIFF_READ \
  (gst_riff_read_get_type ())
#define GST_RIFF_READ(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_RIFF_READ, GstRiffRead))
#define GST_RIFF_READ_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_RIFF_READ, GstRiffReadClass))
#define GST_IS_RIFF_READ(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_RIFF_READ))
#define GST_IS_RIFF_READ_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_RIFF_READ))
#define GST_RIFF_READ_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_RIFF_READ, GstRiffReadClass))

typedef struct _GstRiffLevel {
  guint64 start,
	  length;
} GstRiffLevel;

typedef struct _GstRiffRead {
  GstElement parent;

  GstPad *sinkpad;
  GstByteStream *bs;

  GList *level;
} GstRiffRead;

typedef struct _GstRiffReadClass {
  GstElementClass parent;
} GstRiffReadClass;

GType    gst_riff_read_get_type  (void);

guint32  gst_riff_peek_tag       (GstRiffRead *riff,
				  guint       *level_up);
guint32  gst_riff_peek_list      (GstRiffRead *riff);
gboolean gst_riff_peek_head      (GstRiffRead *riff,
				  guint32     *tag,
				  guint32     *length,
				  guint       *level_up);

GstEvent *gst_riff_read_seek      (GstRiffRead *riff,
				  guint64      offset);
gboolean gst_riff_read_skip      (GstRiffRead *riff);
gboolean gst_riff_read_data      (GstRiffRead *riff,
				  guint32     *tag,
				  GstBuffer  **buf);
gboolean gst_riff_read_ascii     (GstRiffRead *riff,
				  guint32     *tag,
				  gchar      **str);
gboolean gst_riff_read_list      (GstRiffRead *riff,
				  guint32     *tag);
gboolean gst_riff_read_header    (GstRiffRead *read,
				  guint32     *doctype);
GstBuffer *gst_riff_read_element_data (GstRiffRead *riff,
				       guint        length,
				       guint       *got_bytes);
/*
 * Utility functions (including byteswapping).
 */
gboolean gst_riff_read_strh      (GstRiffRead *riff,
				  gst_riff_strh **header);
gboolean gst_riff_read_strf_vids (GstRiffRead *riff,
				  gst_riff_strf_vids **header);
gboolean gst_riff_read_strf_vids_with_data
				 (GstRiffRead *riff,
				  gst_riff_strf_vids **header,
				  GstBuffer  **extradata);
gboolean gst_riff_read_strf_auds (GstRiffRead *riff,
				  gst_riff_strf_auds **header);
gboolean gst_riff_read_strf_iavs (GstRiffRead *riff,
				  gst_riff_strf_iavs **header);
gboolean gst_riff_read_info      (GstRiffRead *riff);

G_END_DECLS

#endif /* __GST_RIFF_READ_H__ */
