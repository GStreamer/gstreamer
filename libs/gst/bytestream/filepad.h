/* GStreamer
 * Copyright (C) 2004 Benjamin Otte <otte@gnome.org>
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
#include <gst/bytestream/adapter.h>

#ifndef __GST_FILE_PAD_H__
#define __GST_FILE_PAD_H__

G_BEGIN_DECLS


#define GST_TYPE_FILE_PAD \
  (gst_file_pad_get_type())
#define GST_FILE_PAD(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_FILE_PAD,GstFilePad))
#define GST_FILE_PAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_FILE_PAD,GstFilePadClass))
#define GST_IS_FILE_PAD(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_FILE_PAD))
#define GST_IS_FILE_PAD_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_FILE_PAD))

typedef struct _GstFilePad GstFilePad;
typedef struct _GstFilePadClass GstFilePadClass;

typedef void (* GstFilePadIterateFunction) (GstFilePad *pad);

struct _GstFilePad {
  GstRealPad	pad;

  GstAdapter *	adapter;
  gint64	position; /* FIXME: add to adapter? */
  gboolean	in_seek;
  gboolean	eos;
  int		error_number;

  GstFilePadIterateFunction iterate_func;
  GstPadEventFunction event_func;

  gpointer _gst_reserved[GST_PADDING];
};

struct _GstFilePadClass {
  GstRealPadClass	parent_class;

  gpointer _gst_reserved[GST_PADDING];
};

GType			gst_file_pad_get_type		(void);

/* FIXME: need this? */
GstPad *		gst_file_pad_new		(GstPadTemplate* templ,
							 gchar *name);

void			gst_file_pad_set_event_function	(GstFilePad *file_pad,
							 GstPadEventFunction event);
void			gst_file_pad_set_iterate_function (GstFilePad *file_pad,
							 GstFilePadIterateFunction iterate);

guint			gst_file_pad_available		(GstFilePad *pad);
gint64			gst_file_pad_get_length		(GstFilePad *pad);
/* this is a file like interface */
/* FIXME: is gint64 the correct type? (it must be signed to get error return vals */
gint64			gst_file_pad_read		(GstFilePad *pad, 
							 void *buf,
							 gint64 count);
gint64			gst_file_pad_try_read		(GstFilePad *pad, 
							 void *buf,
							 gint64 count);
int			gst_file_pad_seek		(GstFilePad *pad, 
							 gint64 offset,
							 GstSeekType whence);
gint64			gst_file_pad_tell      		(GstFilePad *pad);
int			gst_file_pad_error		(GstFilePad *pad);
int			gst_file_pad_eof		(GstFilePad *pad);


G_END_DECLS

#endif /* __GST_FILE_PAD_H__ */
