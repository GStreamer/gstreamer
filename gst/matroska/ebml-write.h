/* GStreamer EBML I/O
 * (c) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * ebml-write.c: write EBML data to file/stream
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

#ifndef __GST_EBML_WRITE_H__
#define __GST_EBML_WRITE_H__

#include <glib.h>
#include <gst/gst.h>

G_BEGIN_DECLS
#define GST_TYPE_EBML_WRITE \
  (gst_ebml_write_get_type ())
#define GST_EBML_WRITE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_EBML_WRITE, GstEbmlWrite))
#define GST_EBML_WRITE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_EBML_WRITE, GstEbmlWriteClass))
#define GST_IS_EBML_WRITE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_EBML_WRITE))
#define GST_IS_EBML_WRITE_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_EBML_WRITE))
#define GST_EBML_WRITE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_EBML_WRITE, GstEbmlWriteClass))
    typedef struct _GstEbmlWrite
{
  GstElement parent;

  GstPad *srcpad;
  guint64 pos;

  GstBuffer *cache;
  guint handled;
} GstEbmlWrite;

typedef struct _GstEbmlWriteClass
{
  GstElementClass parent;
} GstEbmlWriteClass;

GType gst_ebml_write_get_type (void);

/*
 * Caching means that we do not push one buffer for
 * each element, but fill this one until a flush.
 */
void gst_ebml_write_set_cache (GstEbmlWrite * ebml, guint size);
void gst_ebml_write_flush_cache (GstEbmlWrite * ebml);

/*
 * Seeking.
 */
void gst_ebml_write_seek (GstEbmlWrite * ebml, guint64 pos);

/*
 * Data writing. 
 */
void gst_ebml_write_uint (GstEbmlWrite * ebml, guint32 id, guint64 num);
void gst_ebml_write_sint (GstEbmlWrite * ebml, guint32 id, gint64 num);
void gst_ebml_write_float (GstEbmlWrite * ebml, guint32 id, gdouble num);
void gst_ebml_write_ascii (GstEbmlWrite * ebml, guint32 id, const gchar * str);
void gst_ebml_write_utf8 (GstEbmlWrite * ebml, guint32 id, const gchar * str);
void gst_ebml_write_date (GstEbmlWrite * ebml, guint32 id, gint64 date);
guint64 gst_ebml_write_master_start (GstEbmlWrite * ebml, guint32 id);
void gst_ebml_write_master_finish (GstEbmlWrite * ebml, guint64 startpos);
void gst_ebml_write_binary (GstEbmlWrite * ebml,
    guint32 id, guchar * binary, guint64 length);
void gst_ebml_write_header (GstEbmlWrite * ebml,
    gchar * doctype, guint version);

/*
 * Note: this is supposed to be used only for media data.
 */
void gst_ebml_write_buffer_header (GstEbmlWrite * ebml,
    guint32 id, guint64 length);
void gst_ebml_write_buffer (GstEbmlWrite * ebml, GstBuffer * data);

/*
 * A hack, basically... See matroska-mux.c. I should actually
 * make a nice _replace_element_with_size() or so, but this
 * works for now.
 */
void gst_ebml_replace_uint (GstEbmlWrite * ebml, guint64 pos, guint64 num);

G_END_DECLS
#endif /* __GST_EBML_WRITE_H__ */
