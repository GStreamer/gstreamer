/* GStreamer
 *
 * Copyright (C) 2007 Rene Stadler <mail@renestadler.de>
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

#ifndef __GSTGIO_H__
#define __GSTGIO_H__

#include <gio/gioerror.h>
#include <gio/gseekable.h>
#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_GIO_ERROR_MATCHES(err, code) g_error_matches (err, G_IO_ERROR, G_IO_ERROR_##code)

gboolean gst_gio_error (gpointer element, const gchar *func_name,
    GError **err, GstFlowReturn *ret);
GstFlowReturn gst_gio_seek (gpointer element, GSeekable *stream, guint64 offset,
    GCancellable *cancel);
void gst_gio_uri_handler_do_init (GType type);

G_END_DECLS

#endif /* __GSTGIO_H__ */
