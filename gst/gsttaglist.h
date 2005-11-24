/* GStreamer
 * Copyright (C) 2003 Benjamin Otte <in7y118@public.uni-hamburg.de>
 *
 * gsttaglist.h: Header for tag support
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


#ifndef __GST_TAGLIST_H__
#define __GST_TAGLIST_H__

#include <gst/gststructure.h>
#include <gst/glib-compat.h>

G_BEGIN_DECLS

/**
 * GstTagMergeMode:
 * @GST_TAG_MERGE_UNDEFINED: undefined merge mode
 * @GST_TAG_MERGE_REPLACE_ALL: replace all tags
 * @GST_TAG_MERGE_REPLACE: replace tags
 * @GST_TAG_MERGE_APPEND: append tags
 * @GST_TAG_MERGE_PREPEND: prepend tags
 * @GST_TAG_MERGE_KEEP: keep existing tags
 * @GST_TAG_MERGE_KEEP_ALL: keep all existing tags
 * @GST_TAG_MERGE_COUNT: the number of merge modes
 *
 * The different tag merging modes.
 */
typedef enum {
  GST_TAG_MERGE_UNDEFINED,
  GST_TAG_MERGE_REPLACE_ALL,
  GST_TAG_MERGE_REPLACE,
  GST_TAG_MERGE_APPEND,
  GST_TAG_MERGE_PREPEND,
  GST_TAG_MERGE_KEEP,
  GST_TAG_MERGE_KEEP_ALL,
  /* add more */
  GST_TAG_MERGE_COUNT
} GstTagMergeMode;

#define GST_TAG_MODE_IS_VALID(mode)     (((mode) > GST_TAG_MERGE_UNDEFINED) && ((mode) < GST_TAG_MERGE_COUNT))

/**
 * GstTagFlag:
 * @GST_TAG_FLAG_UNDEFINED: undefined flag
 * @GST_TAG_FLAG_META: tag is meta data
 * @GST_TAG_FLAG_ENCODED: tag is encoded
 * @GST_TAG_FLAG_DECODED: tag is decoded
 * @GST_TAG_FLAG_COUNT: number of tag flags
 *
 * Extra tag flags used when registering tags.
 */
typedef enum {
  GST_TAG_FLAG_UNDEFINED,
  GST_TAG_FLAG_META,
  GST_TAG_FLAG_ENCODED,
  GST_TAG_FLAG_DECODED,
  GST_TAG_FLAG_COUNT
} GstTagFlag;

#define GST_TAG_FLAG_IS_VALID(flag)     (((flag) > GST_TAG_FLAG_UNDEFINED) && ((flag) < GST_TAG_FLAG_COUNT))

/**
 * GstTagList:
 *
 * Opaque #GstTagList data structure.
 */
typedef GstStructure GstTagList;
#define GST_TAG_LIST(x)		((GstTagList *) (x))
#define GST_IS_TAG_LIST(x)	(gst_is_tag_list (GST_TAG_LIST (x)))
#define GST_TYPE_TAG_LIST       (gst_tag_list_get_type ())

/**
 * GstTagForeachFunc:
 * @list: the #GstTagList
 * @tag: a name of a tag in @list
 * @user_data: user data
 *
 * A function that will be called in gst_tag_list_foreach(). The function may
 * not modify the tag list.
 */
typedef void (*GstTagForeachFunc) (const GstTagList *list,
				   const gchar * tag,
				   gpointer user_data);

/**
 * GstTagMergeFunc:
 * @dest: the destination #GValue
 * @src: the source #GValue
 *
 * A function for merging multiple values of a tag used when registering
 * tags.
 */
typedef void		(* GstTagMergeFunc)	(GValue *dest, const GValue *src);

/* initialize tagging system */
void		_gst_tag_initialize		(void);
GType           gst_tag_list_get_type           (void);

void		gst_tag_register		(const gchar *		name,
						 GstTagFlag		flag,
						 GType			type,
						 const gchar *		nick,
						 const gchar *		blurb,
						 GstTagMergeFunc	func);
/* some default merging functions */
void		gst_tag_merge_use_first		(GValue *		dest,
						 const GValue *		src);
void		gst_tag_merge_strings_with_comma (GValue *		dest,
						 const GValue *		src);

/* basic tag support */
gboolean	gst_tag_exists			(const gchar *		tag);
GType		gst_tag_get_type		(const gchar *		tag);
G_CONST_RETURN gchar *
		gst_tag_get_nick		(const gchar *		tag);
G_CONST_RETURN gchar *
		gst_tag_get_description		(const gchar *		tag);
GstTagFlag	gst_tag_get_flag		(const gchar *		tag);
gboolean	gst_tag_is_fixed		(const gchar *		tag);

/* tag lists */
GstTagList *	gst_tag_list_new		(void);
gboolean	gst_is_tag_list			(gconstpointer		p);
GstTagList *	gst_tag_list_copy		(const GstTagList *	list);
void		gst_tag_list_insert		(GstTagList *		into,
						 const GstTagList *	from,
						 GstTagMergeMode	mode);
GstTagList *	gst_tag_list_merge		(const GstTagList *	list1,
						 const GstTagList *	list2,
						 GstTagMergeMode	mode);
void		gst_tag_list_free		(GstTagList *		list);
guint		gst_tag_list_get_tag_size	(const GstTagList *	list,
						 const gchar *		tag);
void		gst_tag_list_add		(GstTagList *		list,
						 GstTagMergeMode	mode,
						 const gchar *		tag,
						 ...) G_GNUC_NULL_TERMINATED;
void		gst_tag_list_add_values		(GstTagList *		list,
						 GstTagMergeMode	mode,
						 const gchar *		tag,
						 ...) G_GNUC_NULL_TERMINATED;
void		gst_tag_list_add_valist		(GstTagList *		list,
						 GstTagMergeMode	mode,
						 const gchar *		tag,
						 va_list		var_args);
void		gst_tag_list_add_valist_values	(GstTagList *		list,
						 GstTagMergeMode	mode,
						 const gchar *		tag,
						 va_list		var_args);
void		gst_tag_list_remove_tag		(GstTagList *		list,
						 const gchar *		tag);
void		gst_tag_list_foreach		(const GstTagList *	list,
						 GstTagForeachFunc	func,
						 gpointer		user_data);

G_CONST_RETURN GValue *
		gst_tag_list_get_value_index	(const GstTagList *	list,
						 const gchar *		tag,
						 guint			index);
gboolean	gst_tag_list_copy_value		(GValue *		dest,
						 const GstTagList *	list,
						 const gchar *		tag);

/* simplifications (FIXME: do we want them?) */
gboolean	gst_tag_list_get_char		(const GstTagList *	list,
						 const gchar *		tag,
						 gchar *		value);
gboolean	gst_tag_list_get_char_index	(const GstTagList *	list,
						 const gchar *		tag,
						 guint			index,
						 gchar *		value);
gboolean	gst_tag_list_get_uchar		(const GstTagList *	list,
						 const gchar *		tag,
						 guchar *		value);
gboolean	gst_tag_list_get_uchar_index	(const GstTagList *	list,
						 const gchar *		tag,
						 guint			index,
						 guchar *		value);
gboolean	gst_tag_list_get_boolean	(const GstTagList *	list,
						 const gchar *		tag,
						 gboolean *		value);
gboolean	gst_tag_list_get_boolean_index	(const GstTagList *	list,
						 const gchar *		tag,
						 guint			index,
						 gboolean *		value);
gboolean	gst_tag_list_get_int		(const GstTagList *	list,
						 const gchar *		tag,
						 gint *			value);
gboolean	gst_tag_list_get_int_index	(const GstTagList *	list,
						 const gchar *		tag,
						 guint			index,
						 gint *			value);
gboolean	gst_tag_list_get_uint		(const GstTagList *	list,
						 const gchar *		tag,
						 guint *		value);
gboolean	gst_tag_list_get_uint_index	(const GstTagList *	list,
						 const gchar *		tag,
						 guint			index,
						 guint *		value);
gboolean	gst_tag_list_get_long		(const GstTagList *	list,
						 const gchar *		tag,
						 glong *		value);
gboolean	gst_tag_list_get_long_index	(const GstTagList *	list,
						 const gchar *		tag,
						 guint			index,
						 glong *		value);
gboolean	gst_tag_list_get_ulong		(const GstTagList *	list,
						 const gchar *		tag,
						 gulong *		value);
gboolean	gst_tag_list_get_ulong_index	(const GstTagList *	list,
						 const gchar *		tag,
						 guint			index,
						 gulong *		value);
gboolean	gst_tag_list_get_int64		(const GstTagList *	list,
						 const gchar *		tag,
						 gint64 *		value);
gboolean	gst_tag_list_get_int64_index	(const GstTagList *	list,
						 const gchar *		tag,
						 guint			index,
						 gint64 *		value);
gboolean	gst_tag_list_get_uint64		(const GstTagList *	list,
						 const gchar *		tag,
						 guint64 *		value);
gboolean	gst_tag_list_get_uint64_index	(const GstTagList *	list,
						 const gchar *		tag,
						 guint			index,
						 guint64 *		value);
gboolean	gst_tag_list_get_float		(const GstTagList *	list,
						 const gchar *		tag,
						 gfloat *		value);
gboolean	gst_tag_list_get_float_index	(const GstTagList *	list,
						 const gchar *		tag,
						 guint			index,
						 gfloat *		value);
gboolean	gst_tag_list_get_double		(const GstTagList *	list,
						 const gchar *		tag,
						 gdouble *		value);
gboolean	gst_tag_list_get_double_index	(const GstTagList *	list,
						 const gchar *		tag,
						 guint			index,
						 gdouble *		value);
gboolean	gst_tag_list_get_string		(const GstTagList *	list,
						 const gchar *		tag,
						 gchar **		value);
gboolean	gst_tag_list_get_string_index	(const GstTagList *	list,
						 const gchar *		tag,
						 guint			index,
						 gchar **		value);
gboolean	gst_tag_list_get_pointer	(const GstTagList *	list,
						 const gchar *		tag,
						 gpointer *		value);
gboolean	gst_tag_list_get_pointer_index	(const GstTagList *	list,
						 const gchar *		tag,
						 guint			index,
						 gpointer *		value);
gboolean        gst_tag_list_get_date           (const GstTagList     * list,
                                                 const gchar          * tag,
                                                 GDate               ** value);
gboolean        gst_tag_list_get_date_index     (const GstTagList     * list,
                                                 const gchar          * tag,
                                                 guint                  index,
                                                 GDate               ** value);

/* GStreamer core tags (need to be discussed) */
/**
 * GST_TAG_TITLE:
 *
 * commonly used title
 */
#define GST_TAG_TITLE			"title"
/**
 * GST_TAG_ARTIST:
 *
 * person(s) responsible for the recording
 */
#define GST_TAG_ARTIST			"artist"
/**
 * GST_TAG_ALBUM:
 *
 * album containing this data
 */
#define GST_TAG_ALBUM			"album"
/**
 * GST_TAG_DATE:
 *
 * date the data was created (#GDate structure)
 */
#define GST_TAG_DATE			"date"
/**
 * GST_TAG_GENRE:
 *
 * genre this data belongs to
 */
#define GST_TAG_GENRE			"genre"
/**
 * GST_TAG_COMMENT:
 *
 * free text commenting the data
 */
#define GST_TAG_COMMENT			"comment"
/**
 * GST_TAG_TRACK_NUMBER:
 *
 * track number inside a collection
 */
#define GST_TAG_TRACK_NUMBER		"track-number"
/**
 * GST_TAG_TRACK_COUNT:
 *
 * count of tracks inside collection this track belongs to
 */
#define GST_TAG_TRACK_COUNT		"track-count"
/**
 * GST_TAG_ALBUM_VOLUME_NUMBER:
 *
 * disc number inside a collection
 */
#define GST_TAG_ALBUM_VOLUME_NUMBER	"album-disc-number"
/**
 * GST_TAG_ALBUM_VOLUME_COUNT:
 *
 * count of discs inside collection this disc belongs to
 */
#define GST_TAG_ALBUM_VOLUME_COUNT	"album-disc-count"
/**
 * GST_TAG_LOCATION:
 *
 * original location of file as a URI
 */
#define GST_TAG_LOCATION		"location"
/**
 * GST_TAG_DESCRIPTION:
 *
 * short text describing the content of the data
 */
#define GST_TAG_DESCRIPTION		"description"
/**
 * GST_TAG_VERSION:
 *
 * version of this data
 */
#define GST_TAG_VERSION			"version"
/**
 * GST_TAG_ISRC:
 *
 * International Standard Recording Code - see http://www.ifpi.org/isrc/
 */
#define GST_TAG_ISRC			"isrc"
/**
 * GST_TAG_ORGANIZATION:
 *
 * organization
 */
#define GST_TAG_ORGANIZATION		"organization"
/**
 * GST_TAG_COPYRIGHT:
 *
 * copyright notice of the data
 */
#define GST_TAG_COPYRIGHT		"copyright"
/**
 * GST_TAG_CONTACT:
 *
 * contact information
 */
#define GST_TAG_CONTACT			"contact"
/**
 * GST_TAG_LICENSE:
 *
 * license of data
 */
#define GST_TAG_LICENSE			"license"
/**
 * GST_TAG_PERFORMER:
 *
 * person(s) performing
 */
#define GST_TAG_PERFORMER		"performer"
/**
 * GST_TAG_DURATION:
 *
 * length in GStreamer time units (nanoseconds)
 */
#define GST_TAG_DURATION		"duration"
/**
 * GST_TAG_CODEC:
 *
 * codec the data is stored in
 */
#define GST_TAG_CODEC			"codec"
/**
 * GST_TAG_VIDEO_CODEC:
 *
 * codec the video data is stored in
 */
#define GST_TAG_VIDEO_CODEC		"video-codec"
/**
 * GST_TAG_AUDIO_CODEC:
 *
 * codec the audio data is stored in
 */
#define GST_TAG_AUDIO_CODEC		"audio-codec"
/**
 * GST_TAG_BITRATE:
 *
 * exact or average bitrate in bits/s
 */
#define GST_TAG_BITRATE			"bitrate"
/**
 * GST_TAG_NOMINAL_BITRATE:
 *
 * nominal bitrate in bits/s
 */
#define GST_TAG_NOMINAL_BITRATE		"nominal-bitrate"
/**
 * GST_TAG_MINIMUM_BITRATE:
 *
 * minimum bitrate in bits/s
 */
#define GST_TAG_MINIMUM_BITRATE		"minimum-bitrate"
/**
 * GST_TAG_MAXIMUM_BITRATE:
 *
 * maximum bitrate in bits/s
 */
#define GST_TAG_MAXIMUM_BITRATE		"maximum-bitrate"
/**
 * GST_TAG_SERIAL:
 *
 * serial number of track
 */
#define GST_TAG_SERIAL			"serial"
/**
 * GST_TAG_ENCODER:
 *
 * encoder used to encode this stream
 */
#define GST_TAG_ENCODER			"encoder"
/**
 * GST_TAG_ENCODER_VERSION:
 *
 * version of the encoder used to encode this stream
 */
#define GST_TAG_ENCODER_VERSION		"encoder-version"
/**
 * GST_TAG_TRACK_GAIN:
 *
 * track gain in db
 */
#define GST_TAG_TRACK_GAIN		"replaygain-track-gain"
/**
 * GST_TAG_TRACK_PEAK:
 *
 * peak of the track
 */
#define GST_TAG_TRACK_PEAK		"replaygain-track-peak"
/**
 * GST_TAG_ALBUM_GAIN:
 *
 * album gain in db
 */
#define GST_TAG_ALBUM_GAIN  		"replaygain-album-gain"
/**
 * GST_TAG_ALBUM_PEAK:
 *
 * peak of the album
 */
#define GST_TAG_ALBUM_PEAK		"replaygain-album-peak"
/**
 * GST_TAG_LANGUAGE_CODE:
 *
 * Language code (ISO-639-1)
 */
#define GST_TAG_LANGUAGE_CODE		"language-code"

G_END_DECLS

#endif /* __GST_TAGLIST_H__ */
