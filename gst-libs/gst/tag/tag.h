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


#ifndef __GST_TAG_TAG_H__
#define __GST_TAG_TAG_H__

#include <gst/gst.h>

G_BEGIN_DECLS

/* Tag names */

/**
 * GST_TAG_MUSICBRAINZ_TRACKID
 *
 * MusicBrainz track ID
 */
#define GST_TAG_MUSICBRAINZ_TRACKID	"musicbrainz-trackid"
/**
 * GST_TAG_MUSICBRAINZ_ARTISTID
 *
 * MusicBrainz artist ID
 */
#define GST_TAG_MUSICBRAINZ_ARTISTID	"musicbrainz-artistid"
/**
 * GST_TAG_MUSICBRAINZ_ALBUMID
 *
 * MusicBrainz album ID
 */
#define GST_TAG_MUSICBRAINZ_ALBUMID	"musicbrainz-albumid"
/**
 * GST_TAG_MUSICBRAINZ_ALBUMARTISTID
 *
 * MusicBrainz album artist ID
 */
#define GST_TAG_MUSICBRAINZ_ALBUMARTISTID	"musicbrainz-albumartistid"
/**
 * GST_TAG_MUSICBRAINZ_TRMID
 *
 * MusicBrainz track TRM ID
 */
#define GST_TAG_MUSICBRAINZ_TRMID	"musicbrainz-trmid"
/**
 * GST_TAG_MUSICBRAINZ_SORTNAME
 *
 * MusicBrainz artist sort name
 */
#define GST_TAG_MUSICBRAINZ_SORTNAME	"musicbrainz-sortname"
/**
 * GST_TAG_CMML_STREAM
 *
 * Annodex CMML stream element tag
 */
#define GST_TAG_CMML_STREAM "cmml-stream"
/**
 * GST_TAG_CMML_HEAD
 *
 * Annodex CMML head element tag
 */

#define GST_TAG_CMML_HEAD "cmml-head"
/**
 * GST_TAG_CMML_CLIP
 *
 * Annodex CMML clip element tag
 */
#define GST_TAG_CMML_CLIP "cmml-clip"


/* functions for vorbis comment manipulation */

G_CONST_RETURN gchar *  gst_tag_from_vorbis_tag                 (const gchar *          vorbis_tag);
G_CONST_RETURN gchar *  gst_tag_to_vorbis_tag                   (const gchar *          gst_tag);
void                    gst_vorbis_tag_add                      (GstTagList *           list, 
                                                                 const gchar *          tag, 
                                                                 const gchar *          value);

GList *                 gst_tag_to_vorbis_comments              (const GstTagList *     list, 
                                                                 const gchar *          tag);

/* functions to convert GstBuffers with vorbiscomment contents to GstTagLists and back */
GstTagList *            gst_tag_list_from_vorbiscomment_buffer  (const GstBuffer *      buffer,
                                                                 const guint8 *         id_data,
                                                                 const guint            id_data_length,
                                                                 gchar **               vendor_string);
GstBuffer *             gst_tag_list_to_vorbiscomment_buffer    (const GstTagList *     list,
                                                                 const guint8 *         id_data,
                                                                 const guint            id_data_length,
                                                                 const gchar *          vendor_string);

/* functions for ID3 tag manipulation */

guint                   gst_tag_id3_genre_count                 (void);
G_CONST_RETURN gchar *  gst_tag_id3_genre_get                   (const guint            id);
GstTagList *            gst_tag_list_new_from_id3v1             (const guint8 *         data);

G_CONST_RETURN gchar *  gst_tag_from_id3_tag                    (const gchar *          id3_tag);
G_CONST_RETURN gchar *  gst_tag_from_id3_user_tag               (const gchar *          type,
                                                                 const gchar *          id3_user_tag);
G_CONST_RETURN gchar *  gst_tag_to_id3_tag                      (const gchar *          gst_tag);

void gst_tag_register_musicbrainz_tags (void);

G_END_DECLS

#endif /* __GST_TAG_TAG_H__ */
