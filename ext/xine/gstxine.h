/* GStreamer
 * Copyright (C) 2004 Benjamin Otte <in7y118@public.uni-hamburg.de>
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

#ifndef __GST_XINE_H__
#define __GST_XINE_H__

#include <gst/gst.h>
#include <xine.h>
#include <xine/buffer.h>

G_BEGIN_DECLS

#define GST_TYPE_XINE \
  (gst_xine_get_type())
#define GST_XINE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_XINE,GstXine))
#define GST_XINE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_XINE, GstXineClass))
#define GST_XINE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_XINE,GstXineClass))
#define GST_IS_XINE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_XINE))
#define GST_IS_XINE_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_XINE))

typedef struct _GstXine      GstXine;
typedef struct _GstXineClass GstXineClass;

struct _GstXine
{
  GstElement		element;

  xine_stream_t *	stream;
  xine_ao_driver_t *	audio_driver;
  xine_vo_driver_t *	video_driver;
};

struct _GstXineClass 
{
  GstElementClass	parent_class;

  xine_t *		xine;

  xine_ao_driver_t *	(* create_audio_driver)		(GstXine *	xine);
  xine_vo_driver_t *	(* create_video_driver)		(GstXine *	xine);
};

GType		gst_xine_get_type		(void);

xine_stream_t *	gst_xine_get_stream		(GstXine *xine);
void		gst_xine_free_stream		(GstXine *xine);

void		gst_buffer_to_xine_buffer	(buf_element_t *element, GstBuffer *buffer);

/* conversion functions from xinecaps.c */

const gchar *	gst_xine_get_caps_for_format	(guint32 format);
guint32		gst_xine_get_format_for_caps	(const GstCaps *caps);

/* init functions for the plugins */

gboolean	gst_xine_audio_dec_init_plugin	(GstPlugin *plugin);
  
G_END_DECLS

#endif /* __GST_XINE_H__ */
