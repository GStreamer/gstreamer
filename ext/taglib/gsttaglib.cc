/*
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


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <textidentificationframe.h>
#include <id3v2tag.h>
#include <gst/gsttagsetter.h>
#include "gsttaglib.h"

using namespace TagLib;

GST_DEBUG_CATEGORY_STATIC (gst_tag_lib_mux_debug);
#define GST_CAT_DEFAULT gst_tag_lib_mux_debug

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
  GstTagLibMux *taglib = GST_TAGLIB_MUX (obj);

  if (taglib->tags) {
    gst_tag_list_free (taglib->tags);
    taglib->tags = NULL;
  }
  G_OBJECT_CLASS (parent_class)->finalize (obj);
}


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
gst_tag_lib_mux_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  static GstElementDetails gst_tag_lib_mux_details = {
    "TagLib ID3 Muxer",
    "Formatter/Metadata",
    "Adds an ID3v2 header to the beginning of MP3 files",
    "Christophe Fergeau <teuf@gnome.org>"
  };


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
gst_tag_lib_mux_init (GstTagLibMux * taglib,
    GstTagLibMuxClass * taglibmux_class)
{
  GstElementClass *klass = GST_ELEMENT_CLASS (taglibmux_class);

  /* pad through which data comes in to the element */
  taglib->sinkpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "sink"), "sink");
  gst_pad_set_setcaps_function (taglib->sinkpad,
      GST_DEBUG_FUNCPTR (gst_pad_proxy_setcaps));
  gst_pad_set_chain_function (taglib->sinkpad,
      GST_DEBUG_FUNCPTR (gst_tag_lib_mux_chain));
  gst_pad_set_event_function (taglib->sinkpad,
      GST_DEBUG_FUNCPTR (gst_tag_lib_mux_sink_event));
  gst_element_add_pad (GST_ELEMENT (taglib), taglib->sinkpad);

  /* pad through which data goes out of the element */
  taglib->srcpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "src"), "src");
  gst_element_add_pad (GST_ELEMENT (taglib), taglib->srcpad);

  taglib->render_tag = TRUE;
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

      frame = new ID3v2::TextIdentificationFrame ("TRCK", String::UTF8);
      result = gst_tag_list_get_uint_index (list, GST_TAG_ALBUM_VOLUME_COUNT,
          0, &volume_count);
      if (result) {
        tag_str = g_strdup_printf ("%d/%d", volume_number, volume_count);
      } else {
        tag_str = g_strdup_printf ("%d", volume_number);
      }

      id3v2tag->addFrame (frame);
      frame->setText (tag_str);
      g_free (tag_str);
      GST_DEBUG ("Setting track number to %s", tag_str);
    }
  } else {
    GST_WARNING ("Unsupported tag: %s", tag);
  }
}


static GstBuffer *
gst_tag_lib_mux_render_tag (GstTagLibMux * taglib)
{
  ID3v2::Tag id3v2tag;
  ByteVector rendered_tag;
  GstBuffer *buffer;
  GstTagSetter *tagsetter = GST_TAG_SETTER (taglib);
  GstTagList *taglist;
  GstEvent *event;

  if (taglib->tags != NULL) {
    taglist = gst_tag_list_copy (taglib->tags);
  } else {
    taglist = gst_tag_list_new ();
  }

  if (gst_tag_setter_get_tag_list (tagsetter)) {
    gst_tag_list_insert (taglist,
        gst_tag_setter_get_tag_list (tagsetter),
        gst_tag_setter_get_tag_merge_mode (tagsetter));
  }


  /* Render the tag */
  gst_tag_list_foreach (taglist, add_one_tag, &id3v2tag);
  rendered_tag = id3v2tag.render ();
  taglib->tag_size = rendered_tag.size ();
  buffer = gst_buffer_new_and_alloc (rendered_tag.size ());
  memcpy (GST_BUFFER_DATA (buffer), rendered_tag.data (), rendered_tag.size ());
  gst_buffer_set_caps (buffer, GST_PAD_CAPS (taglib->srcpad));
  /*  gst_util_dump_mem (GST_BUFFER_DATA (buffer), rendered_tag.size()); */

  /* Send an event about the new tags to downstream elements */
  /* gst_event_new_tag takes ownership of the list, so no need to unref it */
  event = gst_event_new_tag (taglist);
  gst_pad_push_event (taglib->srcpad, event);

  return buffer;
}


static GstFlowReturn
gst_tag_lib_mux_chain (GstPad * pad, GstBuffer * buffer)
{
  GstTagLibMux *taglib = GST_TAGLIB_MUX (GST_OBJECT_PARENT (pad));

  if (taglib->render_tag) {
    GstFlowReturn ret;

    GST_INFO ("Adding tags to stream");
    ret = gst_pad_push (taglib->srcpad, gst_tag_lib_mux_render_tag (taglib));
    if (ret != GST_FLOW_OK) {
      gst_buffer_unref (buffer);
      return ret;
    }
    taglib->render_tag = FALSE;
  }

  gst_buffer_set_caps (buffer, GST_PAD_CAPS (taglib->srcpad));
  return gst_pad_push (taglib->srcpad, buffer);
}

static gboolean
gst_tag_lib_mux_sink_event (GstPad * pad, GstEvent * event)
{
  GstTagLibMux *taglib;
  gboolean result;

  taglib = GST_TAGLIB_MUX (gst_pad_get_parent (pad));
  result = FALSE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_TAG:
    {
      GstTagList *tags;

      GST_INFO ("Got tag event");

      gst_event_parse_tag (event, &tags);
      if (taglib->tags != NULL) {
        /* FIXME: which policy is the best here? PREPEND or something else? */
        gst_tag_list_insert (taglib->tags, tags, GST_TAG_MERGE_PREPEND);
      } else {
        taglib->tags = gst_tag_list_copy (tags);
      }
      /* We'll push a new tag event in render_tag */
      gst_event_unref (event);
      result = TRUE;
      break;
    }
    case GST_EVENT_NEWSEGMENT:
      if (taglib->tag_size == 0) {
        result = gst_pad_push_event (taglib->srcpad, event);
      } else {
        gboolean update;
        gdouble rate;
        GstFormat format;
        gint64 value, end_value, base;

        gst_event_parse_new_segment (event, &update, &rate, &format,
            &value, &end_value, &base);
        gst_event_unref (event);
        if (format == GST_FORMAT_BYTES && gst_pad_is_linked (taglib->srcpad)) {
          GstEvent *new_event;

          GST_INFO ("Adjusting NEW_SEGMENT event by %d", taglib->tag_size);
          value += taglib->tag_size;
          if (end_value != -1) {
            end_value += taglib->tag_size;
          }

          new_event = gst_event_new_new_segment (update, rate, format,
              value, end_value, base);
          result = gst_pad_push_event (taglib->srcpad, new_event);
        } else {
          result = FALSE;
        }
      }
      break;

    default:
      result = gst_pad_event_default (pad, event);
      break;
  }
  gst_object_unref (GST_OBJECT (taglib));

  return result;
}


static GstStateChangeReturn
gst_tag_lib_mux_change_state (GstElement * element, GstStateChange transition)
{
  GstTagLibMux *taglib;
  GstStateChangeReturn result;

  taglib = GST_TAGLIB_MUX (element);

  result = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (result != GST_STATE_CHANGE_SUCCESS) {
    return result;
  }

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (taglib->tags) {
        gst_tag_list_free (taglib->tags);
        taglib->tags = NULL;
      }
      taglib->tag_size = 0;
      taglib->render_tag = TRUE;
      break;
    default:
      break;
  }

  return result;
}


static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "tagid3v2mux", GST_RANK_NONE,
          GST_TYPE_TAGLIB_MUX))
    return FALSE;

  GST_DEBUG_CATEGORY_INIT (gst_tag_lib_mux_debug, "taglibmux", 0,
      "ID3 Tag Muxer");

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "taglibmux",
    "Tag-writing plug-in based on taglib",
    plugin_init, VERSION, "LGPL", GST_PACKAGE, GST_ORIGIN)
