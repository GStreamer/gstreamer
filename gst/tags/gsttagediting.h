/* GStreamer
 * Copyright (C) 2003 Benjamin Otte <in7y118@public.uni-hamburg.de>
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


#ifndef __GST_TAG_EDITING_H__
#define __GST_TAG_EDITING_H__

#include <gst/gst.h>

G_BEGIN_DECLS


/* functions for vorbis comment manipulation */

G_CONST_RETURN gchar *	gst_tag_from_vorbis_tag			(const gchar *		vorbis_tag);
G_CONST_RETURN gchar *	gst_tag_to_vorbis_tag			(const gchar *		gst_tag);
/* functions to convert GstBuffers with vorbiscomment contents to GstTagLists and back */
GstTagList *		gst_tag_list_from_vorbiscomment_buffer	(const GstBuffer *	buffer,
								 const guint8 *		id_data,
								 const guint		id_data_length,
								 gchar **		vendor_string);
GstBuffer *		gst_tag_list_to_vorbiscomment_buffer	(const GstTagList *	list,
								 const guint8 *		id_data,
								 const guint		id_data_length,
								 const gchar *		vendor_string);

/* functions for ID3 tag manipulation */

guint			gst_tag_id3_genre_count			(void);
G_CONST_RETURN gchar *	gst_tag_id3_genre_get			(const guint	      	id);
GstTagList *		gst_tag_list_new_from_id3v1		(const guint8 *		data);

G_CONST_RETURN gchar *	gst_tag_from_id3_tag			(const gchar *		vorbis_tag);
G_CONST_RETURN gchar *	gst_tag_to_id3_tag			(const gchar *		gst_tag);


G_END_DECLS

#endif /* __GST_TAG_EDITING_H__ */
