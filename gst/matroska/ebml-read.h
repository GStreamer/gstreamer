/* GStreamer EBML I/O
 * (c) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * ebml-read.c: read EBML data from file/stream
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

#ifndef __GST_EBML_READ_H__
#define __GST_EBML_READ_H__

#include <glib.h>
#include <gst/gst.h>
#include <gst/bytestream/bytestream.h>

G_BEGIN_DECLS
#define GST_TYPE_EBML_READ \
  (gst_ebml_read_get_type ())
#define GST_EBML_READ(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_EBML_READ, GstEbmlRead))
#define GST_EBML_READ_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_EBML_READ, GstEbmlReadClass))
#define GST_IS_EBML_READ(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_EBML_READ))
#define GST_IS_EBML_READ_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_EBML_READ))
#define GST_EBML_READ_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_EBML_READ, GstEbmlReadClass))
    typedef struct _GstEbmlLevel
{
  guint64 start, length;
} GstEbmlLevel;

typedef struct _GstEbmlRead
{
  GstElement parent;

  GstPad *sinkpad;
  GstByteStream *bs;

  GList *level;
} GstEbmlRead;

typedef struct _GstEbmlReadClass
{
  GstElementClass parent;
} GstEbmlReadClass;

GType gst_ebml_read_get_type (void);

guint32 gst_ebml_peek_id (GstEbmlRead * ebml, guint * level_up);
GstEvent *gst_ebml_read_seek (GstEbmlRead * ebml, guint64 offset);
gboolean gst_ebml_read_skip (GstEbmlRead * ebml);
gboolean gst_ebml_read_buffer (GstEbmlRead * ebml,
    guint32 * id, GstBuffer ** buf);
gboolean gst_ebml_read_uint (GstEbmlRead * ebml, guint32 * id, guint64 * num);
gboolean gst_ebml_read_sint (GstEbmlRead * ebml, guint32 * id, gint64 * num);
gboolean gst_ebml_read_float (GstEbmlRead * ebml, guint32 * id, gdouble * num);
gboolean gst_ebml_read_ascii (GstEbmlRead * ebml, guint32 * id, gchar ** str);
gboolean gst_ebml_read_utf8 (GstEbmlRead * ebml, guint32 * id, gchar ** str);
gboolean gst_ebml_read_date (GstEbmlRead * ebml, guint32 * id, gint64 * date);
gboolean gst_ebml_read_master (GstEbmlRead * ebml, guint32 * id);
gboolean gst_ebml_read_binary (GstEbmlRead * ebml,
    guint32 * id, guint8 ** binary, guint64 * length);
gboolean gst_ebml_read_header (GstEbmlRead * read,
    gchar ** doctype, guint * version);

G_END_DECLS
#endif /* __GST_EBML_READ_H__ */
