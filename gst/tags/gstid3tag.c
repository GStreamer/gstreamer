/* GStreamer
 * Copyright (C) 2003 Benjamin Otte <in7y118@public.uni-hamburg.de>
 *
 * gstid3tag.c: plugin for reading / modifying id3 tags
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gsttageditingprivate.h"
#include <string.h>

static const gchar *genres[] = {
  "Blues",
  "Classic Rock",
  "Country",
  "Dance",
  "Disco",
  "Funk",
  "Grunge",
  "Hip-Hop",
  "Jazz",
  "Metal",
  "New Age",
  "Oldies",
  "Other",
  "Pop",
  "R&B",
  "Rap",
  "Reggae",
  "Rock",
  "Techno",
  "Industrial",
  "Alternative",
  "Ska",
  "Death Metal",
  "Pranks",
  "Soundtrack",
  "Euro-Techno",
  "Ambient",
  "Trip-Hop",
  "Vocal",
  "Jazz+Funk",
  "Fusion",
  "Trance",
  "Classical",
  "Instrumental",
  "Acid",
  "House",
  "Game",
  "Sound Clip",
  "Gospel",
  "Noise",
  "Alternative Rock",
  "Bass",
  "Soul",
  "Punk",
  "Space",
  "Meditative",
  "Instrumental Pop",
  "Instrumental Rock",
  "Ethnic",
  "Gothic",
  "Darkwave",
  "Techno-Industrial",
  "Electronic",
  "Pop-Folk",
  "Eurodance",
  "Dream",
  "Southern Rock",
  "Comedy",
  "Cult",
  "Gangsta",
  "Top 40",
  "Christian Rap",
  "Pop/Funk",
  "Jungle",
  "Native American",
  "Cabaret",
  "New Wave",
  "Psychadelic",
  "Rave",
  "Showtunes",
  "Trailer",
  "Lo-Fi",
  "Tribal",
  "Acid Punk",
  "Acid Jazz",
  "Polka",
  "Retro",
  "Musical",
  "Rock & Roll",
  "Hard Rock",
  "Folk",
  "Folk/Rock",
  "National Folk",
  "Swing",
  "Fusion",
  "Bebob",
  "Latin",
  "Revival",
  "Celtic",
  "Bluegrass",
  "Avantgarde",
  "Gothic Rock",
  "Progressive Rock",
  "Psychadelic Rock",
  "Symphonic Rock",
  "Slow Rock",
  "Big Band",
  "Chorus",
  "Easy Listening",
  "Acoustic",
  "Humour",
  "Speech",
  "Chanson",
  "Opera",
  "Chamber Music",
  "Sonata",
  "Symphony",
  "Booty Bass",
  "Primus",
  "Porn Groove",
  "Satire",
  "Slow Jam",
  "Club",
  "Tango",
  "Samba",
  "Folklore",
  "Ballad",
  "Power Ballad",
  "Rhythmic Soul",
  "Freestyle",
  "Duet",
  "Punk Rock",
  "Drum Solo",
  "A Capella",
  "Euro-House",
  "Dance Hall",
  "Goa",
  "Drum & Bass",
  "Club-House",
  "Hardcore",
  "Terror",
  "Indie",
  "BritPop",
  "Negerpunk",
  "Polsk Punk",
  "Beat",
  "Christian Gangsta Rap",
  "Heavy Metal",
  "Black Metal",
  "Crossover",
  "Contemporary Christian",
  "Christian Rock",
  "Merengue",
  "Salsa",
  "Thrash Metal",
  "Anime",
  "Jpop",
  "Synthpop"
};

static GstTagEntryMatch tag_matches[] = {
  { GST_TAG_TITLE,		 "TIT2"	},
  { GST_TAG_ALBUM,		 "TALB"	},
  { GST_TAG_TRACK_NUMBER,	 "TRCK"	},
  { GST_TAG_ARTIST,		 "TPE1"	},
  { GST_TAG_COPYRIGHT,		 "TCOP" },
  { GST_TAG_GENRE,		 "TCON" },
  { GST_TAG_DATE,		 "TDRC" },
  { GST_TAG_COMMENT,		 "COMM"	},
  { GST_TAG_ALBUM_VOLUME_NUMBER, "TPOS" },
  { GST_TAG_DURATION,            "TLEN" },
  { NULL,			 NULL	}
};
/**
* gst_tag_from_id3_tag:
* @id3_tag: ID3v2 tag to convert to GStreamer tag
*
* Looks up the GStreamer tag for a ID3v2 tag.
*
* Returns: The corresponding GStreamer tag or NULL if none exists.
*/
G_CONST_RETURN gchar *
gst_tag_from_id3_tag (const gchar *id3_tag)
{
  int i = 0;

  g_return_val_if_fail (id3_tag != NULL, NULL);

  while (tag_matches[i].gstreamer_tag != NULL) {
    if (strcmp (id3_tag, tag_matches[i].original_tag) == 0) {
      break;
    }
    i++;
  }
  return tag_matches[i].gstreamer_tag;
}
/**
* gst_tag_to_id3_tag:
* @gst_tag: GStreamer tag to convert to vorbiscomment tag
*
* Looks up the ID3v2 tag for a GStreamer tag.
*
* Returns: The corresponding ID3v2 tag or NULL if none exists.
*/
G_CONST_RETURN gchar *
gst_tag_to_id3_tag (const gchar *gst_tag)
{
  int i = 0;

  g_return_val_if_fail (gst_tag != NULL, NULL);

  while (tag_matches[i].gstreamer_tag != NULL) {
    if (strcmp (gst_tag, tag_matches[i].gstreamer_tag) == 0) {
      return tag_matches[i].original_tag;
    }
  i++;
  }
return NULL;
}
static void
gst_tag_extract (GstTagList *list, const gchar *tag, const gchar *start, const guint size)
{
  gsize bytes_read;
  gchar *conv;
  
  /* FIXME: better charset detection? */
  if (g_utf8_validate (start, size, NULL)) {
    conv = g_strchomp (g_strndup (start, size));
  } else {
    conv = g_locale_to_utf8 (start, size, &bytes_read, NULL, NULL);
    if (bytes_read != size) {
      g_free (conv);
      conv = g_convert (start, size, "UTF-8", "ISO-8859-1", &bytes_read, NULL, NULL);
      if (bytes_read != size) {
	g_free (conv);
	return;
      }
    }
    conv = g_strchomp (conv);
  }
  if (conv[0] != '\0') {
    gst_tag_list_add (list, GST_TAG_MERGE_REPLACE, tag, conv, NULL);
  }
  g_free (conv);
}
/**
* gst_tag_list_new_from_id3v1:
* @data: 128 bytes of data containing the ID3v1 tag
*
* Parses the data containing an ID3v1 tag and returns a #GstTagList from the
* parsed data.
*
* Returns: A new tag list or NULL if the data was not an ID3v1 tag.
*/
GstTagList *
gst_tag_list_new_from_id3v1 (const guint8 *data)
{
  guint year;
  gchar *ystr;
  GstTagList *list;

  g_return_val_if_fail (data != NULL, NULL);
  
  if (data[0] != 'T' || data[1] != 'A' || data[2] != 'G') return NULL;
  list = gst_tag_list_new ();
  gst_tag_extract (list, GST_TAG_TITLE, &data[3], 30);
  gst_tag_extract (list, GST_TAG_ARTIST, &data[33], 30);
  gst_tag_extract (list, GST_TAG_ALBUM, &data[63], 30);
  ystr = g_strndup (&data[93], 4);
  year = strtoul (ystr, NULL, 10);
  g_free (ystr);
  if (year > 0) {
    GDate *date = g_date_new_dmy (1, 1, year);
    year = g_date_get_julian (date);
    g_date_free (date);
    gst_tag_list_add (list, GST_TAG_MERGE_REPLACE, GST_TAG_DATE, year, NULL);
  }
  if (data[125] == 0) {
    gst_tag_extract (list, GST_TAG_COMMENT, &data[97], 28);
    gst_tag_list_add (list, GST_TAG_MERGE_REPLACE, GST_TAG_TRACK_NUMBER, (guint) data[126], NULL);
  } else {
    gst_tag_extract (list, GST_TAG_COMMENT, &data[97], 30);
  }
  if (data[127] < gst_tag_id3_genre_count ()) {
    gst_tag_list_add (list, GST_TAG_MERGE_REPLACE, GST_TAG_GENRE, gst_tag_id3_genre_get (data[127]), NULL);
  }

  return list;
}
/**
 * gst_tag_id3_genre_count:
 *
 * Gets the number of ID3v1 genres that can be identified. Winamp genres are 
 * included.
 *
 * Returns: the number of ID3v1 genres that can be identified
 */
guint
gst_tag_id3_genre_count (void)
{
  return G_N_ELEMENTS (genres);
}
/**
 * gst_tag_id3_genre_get:
 * @id: ID of genre to query
 *
 * Gets the ID3v1 genre name for a given ID.
 *
 * Returns: the genre or NULL if no genre is associated with that ID.
 */
G_CONST_RETURN gchar *
gst_tag_id3_genre_get (const guint id)
{
  if (id >= G_N_ELEMENTS (genres)) return NULL;
  return genres[id];
}

