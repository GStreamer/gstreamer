/* GStreamer taglib-based ID3v2 muxer
 * Copyright (C) 2006 Christophe Fergeau <teuf@gnome.org>
 * Copyright (C) 2006 Tim-Philipp Müller <tim centricular net>
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

/**
 * SECTION:element-id3v2mux
 * @see_also: #GstID3Demux, #GstTagSetter
 *
 * <refsect2>
 * <para>
 * This element adds ID3v2 tags to the beginning of a stream using the taglib
 * library. More precisely, the tags written are ID3 version 2.4.0 tags (which
 * means in practice that some hardware players or outdated programs might not
 * be able to read them properly).
 * </para>
 * <para>
 * Applications can set the tags to write using the #GstTagSetter interface.
 * Tags sent by upstream elements will be picked up automatically (and merged
 * according to the merge mode set via the tag setter interface).
 * </para>
 * <para>
 * Here is a simple pipeline that transcodes a file from Ogg/Vorbis to mp3
 * format with an ID3v2 that contains the same as the the Ogg/Vorbis file:
 * <programlisting>
 * gst-launch -v filesrc location=foo.ogg ! decodebin ! audioconvert ! lame ! id3v2mux ! filesink location=foo.mp3
 * </programlisting>
 * Make sure the Ogg/Vorbis file actually has comments to preserve.
 * You can verify the tags were written using:
 * <programlisting>
 * gst-launch -m filesrc location=foo.mp3 ! id3demux ! fakesink silent=TRUE 2&gt; /dev/null | grep taglist
 * </programlisting>
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstid3v2mux.h"

#include <string.h>

#include <textidentificationframe.h>
#include <uniquefileidentifierframe.h>
#include <id3v2tag.h>
#include <gst/tag/tag.h>

using namespace TagLib;

GST_DEBUG_CATEGORY_STATIC (gst_id3v2_mux_debug);
#define GST_CAT_DEFAULT gst_id3v2_mux_debug

static const GstElementDetails gst_id3v2_mux_details =
GST_ELEMENT_DETAILS ("TagLib-based ID3v2 Muxer",
    "Formatter/Metadata",
    "Adds an ID3v2 header to the beginning of MP3 files using taglib",
    "Christophe Fergeau <teuf@gnome.org>");

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-id3"));


GST_BOILERPLATE (GstId3v2Mux, gst_id3v2_mux, GstTagLibMux,
    GST_TYPE_TAG_LIB_MUX);

static GstBuffer *gst_id3v2_mux_render_tag (GstTagLibMux * mux,
    GstTagList * taglist);

static void
gst_id3v2_mux_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));

  gst_element_class_set_details (element_class, &gst_id3v2_mux_details);

  GST_DEBUG_CATEGORY_INIT (gst_id3v2_mux_debug, "id3v2mux", 0,
      "taglib-based ID3v2 tag muxer");
}

static void
gst_id3v2_mux_class_init (GstId3v2MuxClass * klass)
{
  GST_TAG_LIB_MUX_CLASS (klass)->render_tag =
      GST_DEBUG_FUNCPTR (gst_id3v2_mux_render_tag);
}

static void
gst_id3v2_mux_init (GstId3v2Mux * id3v2mux, GstId3v2MuxClass * id3v2mux_class)
{
  /* nothing to do */
}

static void
add_one_txxx_musicbrainz_tag (ID3v2::Tag * id3v2tag, const gchar * spec_id,
    const gchar * realworld_id, const gchar * id_str)
{
  ID3v2::UserTextIdentificationFrame * frame;

  if (id_str == NULL)
    return;

  GST_DEBUG ("Setting %s to %s", GST_STR_NULL (spec_id), id_str);

  if (spec_id) {
    frame = new ID3v2::UserTextIdentificationFrame (String::Latin1);
    id3v2tag->addFrame (frame);
    frame->setDescription (spec_id);
    frame->setText (id_str);
  }

  if (realworld_id) {
    frame = new ID3v2::UserTextIdentificationFrame (String::Latin1);
    id3v2tag->addFrame (frame);
    frame->setDescription (realworld_id);
    frame->setText (id_str);
  }
}

static void
add_one_tag (const GstTagList * list, const gchar * tag, gpointer user_data)
{
  ID3v2::Tag * id3v2tag = (ID3v2::Tag *) user_data;
  gboolean result;

  /* FIXME: if there are several values set for the same tag, this won't
   * work, only the first value will be taken into account
   */
  if (strcmp (tag, GST_TAG_TITLE) == 0) {
    char *title;

    result = gst_tag_list_get_string_index (list, tag, 0, &title);
    if (result != FALSE) {
      GST_DEBUG ("Setting title to %s", title);
      id3v2tag->setTitle (String::String (title, String::UTF8));
    }
    g_free (title);
  } else if (strcmp (tag, GST_TAG_ALBUM) == 0) {
    char *album;

    result = gst_tag_list_get_string_index (list, tag, 0, &album);
    if (result != FALSE) {
      GST_DEBUG ("Setting album to %s", album);
      id3v2tag->setAlbum (String::String (album, String::UTF8));
    }
    g_free (album);
  } else if (strcmp (tag, GST_TAG_ARTIST) == 0) {
    char *artist;

    result = gst_tag_list_get_string_index (list, tag, 0, &artist);
    if (result != FALSE) {
      GST_DEBUG ("Setting artist to %s", artist);
      id3v2tag->setArtist (String::String (artist, String::UTF8));
    }
    g_free (artist);
  } else if (strcmp (tag, GST_TAG_GENRE) == 0) {
    char *genre;

    result = gst_tag_list_get_string_index (list, tag, 0, &genre);
    if (result != FALSE) {
      GST_DEBUG ("Setting genre to %s", genre);
      id3v2tag->setGenre (String::String (genre, String::UTF8));
    }
    g_free (genre);
  } else if (strcmp (tag, GST_TAG_COMMENT) == 0) {
    char *comment;

    result = gst_tag_list_get_string_index (list, tag, 0, &comment);
    if (result != FALSE) {
      GST_DEBUG ("Setting comment to %s", comment);
      id3v2tag->setComment (String::String (comment, String::UTF8));
    }
    g_free (comment);
  } else if (strcmp (tag, GST_TAG_DATE) == 0) {
    GDate *date;

    result = gst_tag_list_get_date_index (list, tag, 0, &date);
    if (result != FALSE) {
      GDateYear year;

      year = g_date_get_year (date);
      GST_DEBUG ("Setting track year to %d", year);
      id3v2tag->setYear (year);
      g_date_free (date);
    }
  } else if (strcmp (tag, GST_TAG_TRACK_NUMBER) == 0) {
    guint track_number;

    result = gst_tag_list_get_uint_index (list, tag, 0, &track_number);
    if (result != FALSE) {
      guint total_tracks;

      result = gst_tag_list_get_uint_index (list, GST_TAG_TRACK_COUNT,
          0, &total_tracks);
      if (result) {
        gchar *tag_str;

        ID3v2::TextIdentificationFrame * frame;

        frame = new ID3v2::TextIdentificationFrame ("TRCK", String::UTF8);
        tag_str = g_strdup_printf ("%d/%d", track_number, total_tracks);
        GST_DEBUG ("Setting track number to %s", tag_str);
        id3v2tag->addFrame (frame);
        frame->setText (tag_str);
        g_free (tag_str);
      } else {
        GST_DEBUG ("Setting track number to %d", track_number);
        id3v2tag->setTrack (track_number);
      }
    }
  } else if (strcmp (tag, GST_TAG_ALBUM_VOLUME_NUMBER) == 0) {
    guint volume_number;

    result = gst_tag_list_get_uint_index (list, tag, 0, &volume_number);

    if (result != FALSE) {
      guint volume_count;
      gchar *tag_str;

      ID3v2::TextIdentificationFrame * frame;

      frame = new ID3v2::TextIdentificationFrame ("TPOS", String::UTF8);
      result = gst_tag_list_get_uint_index (list, GST_TAG_ALBUM_VOLUME_COUNT,
          0, &volume_count);
      if (result) {
        tag_str = g_strdup_printf ("%d/%d", volume_number, volume_count);
      } else {
        tag_str = g_strdup_printf ("%d", volume_number);
      }

      GST_DEBUG ("Setting album number to %s", tag_str);

      id3v2tag->addFrame (frame);
      frame->setText (tag_str);
      g_free (tag_str);
    }
  } else if (strcmp (tag, GST_TAG_COPYRIGHT) == 0) {
    gchar *copyright;

    result = gst_tag_list_get_string_index (list, tag, 0, &copyright);

    if (result != FALSE) {
      ID3v2::TextIdentificationFrame * frame;

      GST_DEBUG ("Setting copyright to %s", copyright);

      frame = new ID3v2::TextIdentificationFrame ("TCOP", String::UTF8);

      id3v2tag->addFrame (frame);
      frame->setText (copyright);
      g_free (copyright);
    }
  } else if (strcmp (tag, GST_TAG_MUSICBRAINZ_ARTISTID) == 0) {
    gchar *id_str;

    if (gst_tag_list_get_string_index (list, tag, 0, &id_str) && id_str) {
      add_one_txxx_musicbrainz_tag (id3v2tag, "MusicBrainz Artist Id",
          "musicbrainz_artistid", id_str);
      g_free (id_str);
    }
  } else if (strcmp (tag, GST_TAG_MUSICBRAINZ_ALBUMID) == 0) {
    gchar *id_str;

    if (gst_tag_list_get_string_index (list, tag, 0, &id_str) && id_str) {
      add_one_txxx_musicbrainz_tag (id3v2tag, "MusicBrainz Album Id",
          "musicbrainz_albumid", id_str);
      g_free (id_str);
    }
  } else if (strcmp (tag, GST_TAG_MUSICBRAINZ_ALBUMARTISTID) == 0) {
    gchar *id_str;

    if (gst_tag_list_get_string_index (list, tag, 0, &id_str) && id_str) {
      add_one_txxx_musicbrainz_tag (id3v2tag, "MusicBrainz Album Artist Id",
          "musicbrainz_albumartistid", id_str);
      g_free (id_str);
    }
  } else if (strcmp (tag, GST_TAG_MUSICBRAINZ_TRMID) == 0) {
    gchar *id_str;

    if (gst_tag_list_get_string_index (list, tag, 0, &id_str) && id_str) {
      add_one_txxx_musicbrainz_tag (id3v2tag, "MusicBrainz TRM Id",
          "musicbrainz_trmid", id_str);
      g_free (id_str);
    }
  } else if (strcmp (tag, GST_TAG_MUSICBRAINZ_TRACKID) == 0) {
    gchar *id_str;

    if (gst_tag_list_get_string_index (list, tag, 0, &id_str) && id_str) {
      ID3v2::UniqueFileIdentifierFrame * frame;

      GST_DEBUG ("Setting Musicbrainz Track Id to %s", id_str);

      frame = new ID3v2::UniqueFileIdentifierFrame ("http://musicbrainz.org",
          id_str);
      id3v2tag->addFrame (frame);
      g_free (id_str);
    }
  } else {
    GST_WARNING ("Unsupported tag: %s", tag);
  }
}

static GstBuffer *
gst_id3v2_mux_render_tag (GstTagLibMux * mux, GstTagList * taglist)
{
  ID3v2::Tag id3v2tag;
  ByteVector rendered_tag;
  GstBuffer *buf;
  guint tag_size;

  /* Render the tag */
  gst_tag_list_foreach (taglist, add_one_tag, &id3v2tag);

  rendered_tag = id3v2tag.render ();
  tag_size = rendered_tag.size ();

  GST_LOG_OBJECT (mux, "tag size = %d bytes", tag_size);

  /* Create buffer with tag */
  buf = gst_buffer_new_and_alloc (tag_size);
  memcpy (GST_BUFFER_DATA (buf), rendered_tag.data (), tag_size);
  gst_buffer_set_caps (buf, GST_PAD_CAPS (mux->srcpad));

  return buf;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "id3v2mux", GST_RANK_NONE,
          GST_TYPE_ID3V2_MUX))
    return FALSE;

  gst_tag_register_musicbrainz_tags ();

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "taglib",
    "Tag writing plug-in based on taglib",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
