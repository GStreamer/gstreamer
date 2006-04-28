/* GStreamer taglib-based ID3 muxer
 * (c) 2006 Christophe Fergeau  <teuf@gnome.org>
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
 * SECTION:element-tagid3v2mux
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
 * gst-launch -m filesrc location=foo.mp3 ! id3demux ! fakesink silent=TRUE 2&uml; /dev/null | grep taglist
 * </programlisting>
 * </para>
 * </refsect2>
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <textidentificationframe.h>
#include <uniquefileidentifierframe.h>
#include <id3v2tag.h>
#include <gst/gsttagsetter.h>
#include <gst/tag/tag.h>
#include "gsttaglib.h"

using namespace TagLib;

GST_DEBUG_CATEGORY_STATIC (gst_tag_lib_mux_debug);
#define GST_CAT_DEFAULT gst_tag_lib_mux_debug

static const GstElementDetails gst_tag_lib_mux_details =
GST_ELEMENT_DETAILS ("TagLib ID3v2 Muxer",
    "Formatter/Metadata",
    "Adds an ID3v2 header to the beginning of MP3 files using taglib",
    "Christophe Fergeau <teuf@gnome.org>");

static GstStaticPadTemplate gst_tag_lib_mux_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/mpeg"));


static GstStaticPadTemplate gst_tag_lib_mux_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-id3"));


static void
gst_tag_lib_mux_iface_init (GType taglib_type)
{
  static const GInterfaceInfo tag_setter_info = {
    NULL,
    NULL,
    NULL
  };

  g_type_add_interface_static (taglib_type, GST_TYPE_TAG_SETTER,
      &tag_setter_info);
}

GST_BOILERPLATE_FULL (GstTagLibMux, gst_tag_lib_mux,
    GstElement, GST_TYPE_ELEMENT, gst_tag_lib_mux_iface_init);


static GstStateChangeReturn
gst_tag_lib_mux_change_state (GstElement * element, GstStateChange transition);
static GstFlowReturn gst_tag_lib_mux_chain (GstPad * pad, GstBuffer * buffer);
static gboolean gst_tag_lib_mux_sink_event (GstPad * pad, GstEvent * event);

static void
gst_tag_lib_mux_finalize (GObject * obj)
{
  GstTagLibMux *mux = GST_TAG_LIB_MUX (obj);

  if (mux->newsegment_ev) {
    gst_event_unref (mux->newsegment_ev);
    mux->newsegment_ev = NULL;
  }

  if (mux->event_tags) {
    gst_tag_list_free (mux->event_tags);
    mux->event_tags = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
gst_tag_lib_mux_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_tag_lib_mux_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_tag_lib_mux_sink_template));
  gst_element_class_set_details (element_class, &gst_tag_lib_mux_details);
}

static void
gst_tag_lib_mux_class_init (GstTagLibMuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_tag_lib_mux_finalize);
  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_tag_lib_mux_change_state);
}

static void
gst_tag_lib_mux_init (GstTagLibMux * mux, GstTagLibMuxClass * muxmux_class)
{
  GstCaps *srccaps;

  /* pad through which data comes in to the element */
  mux->sinkpad =
      gst_pad_new_from_static_template (&gst_tag_lib_mux_sink_template, "sink");
  gst_pad_set_chain_function (mux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_tag_lib_mux_chain));
  gst_pad_set_event_function (mux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_tag_lib_mux_sink_event));
  gst_element_add_pad (GST_ELEMENT (mux), mux->sinkpad);

  /* pad through which data goes out of the element */
  mux->srcpad =
      gst_pad_new_from_static_template (&gst_tag_lib_mux_src_template, "src");
  srccaps = gst_static_pad_template_get_caps (&gst_tag_lib_mux_src_template);
  gst_pad_use_fixed_caps (mux->srcpad);
  gst_pad_set_caps (mux->srcpad, srccaps);
  gst_element_add_pad (GST_ELEMENT (mux), mux->srcpad);

  mux->render_tag = TRUE;
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
gst_tag_lib_mux_render_tag (GstTagLibMux * mux)
{
  ID3v2::Tag id3v2tag;
  ByteVector rendered_tag;
  GstBuffer *buffer;
  GstTagSetter *tagsetter = GST_TAG_SETTER (mux);
  const GstTagList *tagsetter_tags;
  GstTagList *taglist;
  GstEvent *event;

  if (mux->event_tags != NULL) {
    taglist = gst_tag_list_copy (mux->event_tags);
  } else {
    taglist = gst_tag_list_new ();
  }

  tagsetter_tags = gst_tag_setter_get_tag_list (tagsetter);
  if (tagsetter_tags) {
    GstTagMergeMode merge_mode;

    merge_mode = gst_tag_setter_get_tag_merge_mode (tagsetter);
    GST_LOG_OBJECT (mux, "merging tags, merge mode = %d", merge_mode);
    GST_LOG_OBJECT (mux, "event tags: %" GST_PTR_FORMAT, taglist);
    GST_LOG_OBJECT (mux, "set   tags: %" GST_PTR_FORMAT, tagsetter_tags);
    gst_tag_list_insert (taglist, tagsetter_tags, merge_mode);
  }

  GST_LOG_OBJECT (mux, "final tags: %" GST_PTR_FORMAT, taglist);

  /* Render the tag */
  gst_tag_list_foreach (taglist, add_one_tag, &id3v2tag);

  rendered_tag = id3v2tag.render ();
  mux->tag_size = rendered_tag.size ();

  GST_LOG_OBJECT (mux, "tag size = %d bytes", mux->tag_size);

  /* Create buffer with tag */
  buffer = gst_buffer_new_and_alloc (mux->tag_size);
  memcpy (GST_BUFFER_DATA (buffer), rendered_tag.data (), mux->tag_size);
  gst_buffer_set_caps (buffer, GST_PAD_CAPS (mux->srcpad));

  /* Send newsegment event from byte position 0, so the tag really gets
   * written to the start of the file, independent of the upstream segment */
  gst_pad_push_event (mux->srcpad,
      gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_BYTES, 0, -1, 0));

  /* Send an event about the new tags to downstream elements */
  /* gst_event_new_tag takes ownership of the list, so no need to unref it */
  event = gst_event_new_tag (taglist);
  gst_pad_push_event (mux->srcpad, event);

  GST_BUFFER_OFFSET (buffer) = 0;

  return buffer;
}

static GstEvent *
gst_tag_lib_mux_adjust_event_offsets (GstTagLibMux * mux,
    const GstEvent * newsegment_event)
{
  GstFormat format;
  gint64 start, stop, cur;

  gst_event_parse_new_segment ((GstEvent *) newsegment_event, NULL, NULL,
      &format, &start, &stop, &cur);

  g_assert (format == GST_FORMAT_BYTES);

  if (start != -1)
    start += mux->tag_size;
  if (stop != -1)
    stop += mux->tag_size;
  if (cur != -1)
    cur += mux->tag_size;

  GST_DEBUG_OBJECT (mux, "adjusting newsegment event offsets to start=%"
      G_GINT64_FORMAT ", stop=%" G_GINT64_FORMAT ", cur=%" G_GINT64_FORMAT
      " (delta = +%u)", start, stop, cur, mux->tag_size);

  return gst_event_new_new_segment (TRUE, 1.0, format, start, stop, cur);
}

static GstFlowReturn
gst_tag_lib_mux_chain (GstPad * pad, GstBuffer * buffer)
{
  GstTagLibMux *mux = GST_TAG_LIB_MUX (GST_OBJECT_PARENT (pad));

  if (mux->render_tag) {
    GstFlowReturn ret;

    GST_INFO_OBJECT (mux, "Adding tags to stream");
    ret = gst_pad_push (mux->srcpad, gst_tag_lib_mux_render_tag (mux));
    if (ret != GST_FLOW_OK) {
      GST_DEBUG_OBJECT (mux, "flow: %s", gst_flow_get_name (ret));
      gst_buffer_unref (buffer);
      return ret;
    }

    /* Now send the cached newsegment event that we got from upstream */
    if (mux->newsegment_ev) {
      GST_DEBUG_OBJECT (mux, "sending cached newsegment event");
      gst_pad_push_event (mux->srcpad,
          gst_tag_lib_mux_adjust_event_offsets (mux, mux->newsegment_ev));
      gst_event_unref (mux->newsegment_ev);
      mux->newsegment_ev = NULL;
    } else {
      /* upstream sent no newsegment event or only one in a non-BYTE format */
    }

    mux->render_tag = FALSE;
  }

  buffer = gst_buffer_make_metadata_writable (buffer);

  if (GST_BUFFER_OFFSET (buffer) != GST_BUFFER_OFFSET_NONE) {
    GST_LOG_OBJECT (mux, "Adjusting buffer offset from %" G_GINT64_FORMAT
        " to %" G_GINT64_FORMAT, GST_BUFFER_OFFSET (buffer),
        GST_BUFFER_OFFSET (buffer) + mux->tag_size);
    GST_BUFFER_OFFSET (buffer) += mux->tag_size;
  }

  gst_buffer_set_caps (buffer, GST_PAD_CAPS (mux->srcpad));
  return gst_pad_push (mux->srcpad, buffer);
}

static gboolean
gst_tag_lib_mux_sink_event (GstPad * pad, GstEvent * event)
{
  GstTagLibMux *mux;
  gboolean result;

  mux = GST_TAG_LIB_MUX (gst_pad_get_parent (pad));
  result = FALSE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_TAG:{
      GstTagList *tags;

      gst_event_parse_tag (event, &tags);

      GST_INFO_OBJECT (mux, "Got tag event: %" GST_PTR_FORMAT, tags);

      if (mux->event_tags != NULL) {
        gst_tag_list_insert (mux->event_tags, tags, GST_TAG_MERGE_REPLACE);
      } else {
        mux->event_tags = gst_tag_list_copy (tags);
      }

      GST_INFO_OBJECT (mux, "Event tags are now: %" GST_PTR_FORMAT,
          mux->event_tags);

      /* just drop the event, we'll push a new tag event in render_tag */
      gst_event_unref (event);
      result = TRUE;
      break;
    }
    case GST_EVENT_NEWSEGMENT:{
      GstFormat fmt;

      gst_event_parse_new_segment (event, NULL, NULL, &fmt, NULL, NULL, NULL);

      if (fmt != GST_FORMAT_BYTES) {
        GST_WARNING_OBJECT (mux, "dropping newsegment event in %s format",
            gst_format_get_name (fmt));
        gst_event_unref (event);
        break;
      }

      if (mux->render_tag) {
        /* we have not rendered the tag yet, which means that we don't know
         * how large it is going to be yet, so we can't adjust the offsets
         * here at this point and need to cache the newsegment event for now
         * (also, there could be tag events coming after this newsegment event
         *  and before the first buffer). */
        if (mux->newsegment_ev) {
          GST_WARNING_OBJECT (mux, "discarding old cached newsegment event");
          gst_event_unref (mux->newsegment_ev);
        }

        GST_LOG_OBJECT (mux, "caching newsegment event for later");
        mux->newsegment_ev = event;
      } else {
        GST_DEBUG_OBJECT (mux, "got newsegment event, adjusting offsets");
        gst_pad_push_event (mux->srcpad,
            gst_tag_lib_mux_adjust_event_offsets (mux, event));
        gst_event_unref (event);
      }
      event = NULL;
      result = TRUE;
      break;
    }
    default:
      result = gst_pad_event_default (pad, event);
      break;
  }

  gst_object_unref (mux);

  return result;
}


static GstStateChangeReturn
gst_tag_lib_mux_change_state (GstElement * element, GstStateChange transition)
{
  GstTagLibMux *mux;
  GstStateChangeReturn result;

  mux = GST_TAG_LIB_MUX (element);

  result = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (result != GST_STATE_CHANGE_SUCCESS) {
    return result;
  }

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:{
      if (mux->newsegment_ev) {
        gst_event_unref (mux->newsegment_ev);
        mux->newsegment_ev = NULL;
      }
      if (mux->event_tags) {
        gst_tag_list_free (mux->event_tags);
        mux->event_tags = NULL;
      }
      mux->tag_size = 0;
      mux->render_tag = TRUE;
      break;
    }
    default:
      break;
  }

  return result;
}


static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "id3v2mux", GST_RANK_NONE,
          GST_TYPE_TAG_LIB_MUX))
    return FALSE;

  GST_DEBUG_CATEGORY_INIT (gst_tag_lib_mux_debug, "taglibmux", 0,
      "taglib-based muxer");

  gst_tag_register_musicbrainz_tags ();

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "taglib",
    "Tag-writing plug-in based on taglib",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
