/* GStreamer
 * Copyright (C) 2003-2004 Benjamin Otte <otte@gnome.org>
 *               2006 Stefan Kost <ensonic at users dot sf dot net>
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
#include "gstmad.h"
#include <stdlib.h>
#include <string.h>
#include <gst/gsttagsetter.h>

#define ID3_TYPE_FIND_MIN_SIZE 3072
#define ID3_TYPE_FIND_MAX_SIZE 40960

GST_DEBUG_CATEGORY_STATIC (gst_id3_tag_debug);
#define GST_CAT_DEFAULT gst_id3_tag_debug

#define GST_TYPE_ID3_TAG (gst_id3_tag_get_type(GST_ID3_TAG_PARSE_BASE ))
#define GST_ID3_TAG(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ID3_TAG, GstID3Tag))
#define GST_ID3_TAG_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ID3_TAG, GstID3TagClass))
#define GST_IS_ID3_TAG(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_ID3_TAG))
#define GST_IS_ID3_TAG_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_ID3_TAG))
#define GST_ID3_TAG_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_ID3_TAG, GstID3TagClass))

typedef struct _GstID3Tag GstID3Tag;
typedef struct _GstID3TagClass GstID3TagClass;

typedef enum
{
  GST_ID3_TAG_STATE_READING_V2_TAG,
  GST_ID3_TAG_STATE_SEEKING_TO_V1_TAG,
  GST_ID3_TAG_STATE_READING_V1_TAG,
  GST_ID3_TAG_STATE_SEEKING_TO_NORMAL,
  GST_ID3_TAG_STATE_NORMAL_START,
  GST_ID3_TAG_STATE_NORMAL
}
GstID3TagState;

#ifndef GST_DISABLE_GST_DEBUG
static const char *state_names[] = {
  "READING_V2_TAG",
  "SEEKING_TO_V1_TAG",
  "READING_V1_TAG",
  "SEEKING_TO_NORMAL",
  "NORMAL_START2",
  "NORMAL"
};
#endif

#define GST_ID3_TAG_GET_STATE_NAME(state) state_names[state]

typedef enum
{
  GST_ID3_TAG_PARSE_BASE = 0,
  GST_ID3_TAG_PARSE_DEMUX = 1,
  GST_ID3_TAG_PARSE_MUX = 2,
  GST_ID3_TAG_PARSE_ANY = 3
}
GstID3ParseMode;

#ifndef GST_DISABLE_GST_DEBUG
static const char *mode_names[] = {
  "BASE",
  "DEMUX",
  "MUX",
  "ANY"
};
#endif

#define GST_ID3_TAG_GET_MODE_NAME(mode) mode_names[mode]


#define IS_DEMUXER(tag) ((tag)->parse_mode & GST_ID3_TAG_PARSE_DEMUX)
#define IS_MUXER(tag) ((tag)->parse_mode & GST_ID3_TAG_PARSE_MUX)
#define CAN_BE_DEMUXER(tag) (GST_ID3_TAG_GET_CLASS(tag)->type & GST_ID3_TAG_PARSE_DEMUX)
#define CAN_BE_MUXER(tag) (GST_ID3_TAG_GET_CLASS(tag)->type & GST_ID3_TAG_PARSE_MUX)

struct _GstID3Tag
{
  GstElement element;

  /* pads */
  GstPad *sinkpad;
  GstPad *srcpad;

  /* caps */
  GstID3ParseMode parse_mode;
  GstCaps *found_caps;

  /* tags */
  GstTagList *event_tags;
  GstTagList *parsed_tags;

  /* state */
  GstID3TagState state;

  GstEvent *segment;
  GstBuffer *buffer;
  gboolean prefer_v1tag;
  glong v1tag_size;
  glong v1tag_size_new;
  guint64 v1tag_offset;
  gboolean v1tag_render;
  glong v2tag_size;
  glong v2tag_size_new;
  gboolean v2tag_render;
};

struct _GstID3TagClass
{
  GstElementClass parent_class;

  GstID3ParseMode type;
};

/* signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_V1_TAG,
  ARG_V2_TAG,
  ARG_PREFER_V1
      /* FILL ME */
};

GST_DEBUG_CATEGORY_EXTERN (mad_debug);

static GstStaticPadTemplate id3_tag_src_any_template_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
  /* FIXME: for spider - should be GST_PAD_ALWAYS, */
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("ANY")
    );

static GstStaticPadTemplate id3_tag_src_id3_template_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-id3")
    );

static GstStaticPadTemplate id3_tag_sink_any_template_factory =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    /* FIXME: find a way to extend this generically */
    GST_STATIC_CAPS ("audio/mpeg, mpegversion=(int)1; audio/x-flac")
    );

static GstStaticPadTemplate id3_tag_sink_id3_template_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-id3")
    );


static void gst_id3_tag_class_init (gpointer g_class, gpointer class_data);
static void gst_id3_tag_init (GTypeInstance * instance, gpointer g_class);
static void gst_id3_tag_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_id3_tag_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static gboolean gst_id3_tag_src_event (GstPad * pad, GstEvent * event);
static const GstQueryType *gst_id3_tag_get_query_types (GstPad * pad);

static gboolean gst_id3_tag_src_query (GstPad * pad, GstQuery * query);

static gboolean gst_id3_tag_sink_event (GstPad * pad, GstEvent * event);
static GstFlowReturn gst_id3_tag_chain (GstPad * pad, GstBuffer * buffer);
static GstPadLinkReturn gst_id3_tag_src_link (GstPad * pad, GstPad * peer);

static GstStateChangeReturn gst_id3_tag_change_state (GstElement * element,
    GstStateChange transition);

static GstElementClass *parent_class = NULL;

/* static guint gst_id3_tag_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_id3_tag_get_type (guint type)
{
  static GType id3_tag_type[3] = { 0, 0, 0 };
  static gchar *name[3] = { "GstID3TagBase", "GstID3Mux", "GstID3Tag" };

  g_assert (type < 3);

  if (!id3_tag_type[type]) {
    GTypeInfo id3_tag_info = {
      sizeof (GstID3TagClass),
      NULL,
      NULL,
      gst_id3_tag_class_init,
      NULL,
      GUINT_TO_POINTER (type),
      sizeof (GstID3Tag),
      0,
      gst_id3_tag_init,
    };
    static const GInterfaceInfo tag_setter_info = {
      NULL,
      NULL,
      NULL
    };

    id3_tag_type[type] = g_type_register_static (
        (type == GST_ID3_TAG_PARSE_BASE) ? GST_TYPE_ELEMENT :
        GST_TYPE_ID3_TAG, name[type], &id3_tag_info, 0);

    if (type & GST_ID3_TAG_PARSE_MUX) {
      g_type_add_interface_static (id3_tag_type[type], GST_TYPE_TAG_SETTER,
          &tag_setter_info);
    }
  }
  return id3_tag_type[type];
}

/* elementfactory information */
static const GstElementDetails gst_id3_tag_details[3] = {
  GST_ELEMENT_DETAILS ("id3 tag extractor",
      "Codec/Demuxer/Audio",
      "Extract ID3 tagging information",
      "Benjamin Otte <otte@gnome.org>"),
  GST_ELEMENT_DETAILS ("id3 tag muxer",
      "Codec/Muxer/Audio",
      "Add ID3 tagging information",
      "Benjamin Otte <otte@gnome.org>"),
  GST_ELEMENT_DETAILS ("id3 tag extractor",
      "Tag",
      "Extract tagging information from mp3s",
      "Benjamin Otte <otte@gnome.org>")
};

static void
gst_id3_tag_class_init (gpointer g_class, gpointer class_data)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);
  GObjectClass *gobject_class = G_OBJECT_CLASS (g_class);
  GstID3TagClass *tag_class = GST_ID3_TAG_CLASS (g_class);

  tag_class->type = GPOINTER_TO_UINT (class_data);

  if (tag_class->type == GST_ID3_TAG_PARSE_BASE) {
    parent_class = g_type_class_peek_parent (g_class);
    gstelement_class->change_state = gst_id3_tag_change_state;
  } else {
    gst_element_class_set_details (gstelement_class,
        &gst_id3_tag_details[tag_class->type - 1]);
  }

  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_id3_tag_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_id3_tag_get_property);

  if (tag_class->type & GST_ID3_TAG_PARSE_DEMUX) {
    g_object_class_install_property (gobject_class, ARG_PREFER_V1,
        g_param_spec_boolean ("prefer-v1", "prefer version 1 tag",
            "Prefer tags from tag at end of file", FALSE,
            G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
    gst_element_class_add_pad_template (gstelement_class,
        gst_static_pad_template_get (&id3_tag_src_any_template_factory));
  } else if (tag_class->type & GST_ID3_TAG_PARSE_MUX) {
    gst_element_class_add_pad_template (gstelement_class,
        gst_static_pad_template_get (&id3_tag_src_id3_template_factory));
  }

  if (tag_class->type & GST_ID3_TAG_PARSE_MUX) {
    g_object_class_install_property (gobject_class, ARG_V2_TAG,
        g_param_spec_boolean ("v2-tag", "add version 2 tag",
            "Add version 2 tag at start of file", TRUE,
            G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
    g_object_class_install_property (gobject_class, ARG_V1_TAG,
        g_param_spec_boolean ("v1-tag", "add version 1 tag",
            "Add version 1 tag at end of file", FALSE,
            G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
  }
  if (tag_class->type == GST_ID3_TAG_PARSE_MUX) {
    gst_element_class_add_pad_template (gstelement_class,
        gst_static_pad_template_get (&id3_tag_sink_any_template_factory));
  } else if (tag_class->type & GST_ID3_TAG_PARSE_DEMUX) {
    gst_element_class_add_pad_template (gstelement_class,
        gst_static_pad_template_get (&id3_tag_sink_id3_template_factory));
  }
}

static GstCaps *
gst_id3_tag_get_caps (GstPad * pad)
{
  GstID3Tag *tag = GST_ID3_TAG (gst_pad_get_parent (pad));

  if (tag->found_caps) {
    GstCaps *caps = gst_caps_copy (tag->found_caps);

    if (CAN_BE_MUXER (tag)) {
      gst_caps_append (caps, gst_caps_from_string ("application/x-id3"));
    }
    return caps;
  } else {
    return gst_caps_copy (gst_pad_get_pad_template_caps (pad));
  }
}

static void
gst_id3_tag_add_src_pad (GstID3Tag * tag)
{
  g_assert (tag->srcpad == NULL);
  tag->srcpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template
      (GST_ELEMENT_GET_CLASS (tag), "src"), "src");
  gst_pad_set_event_function (tag->srcpad,
      GST_DEBUG_FUNCPTR (gst_id3_tag_src_event));
  gst_pad_set_query_function (tag->srcpad,
      GST_DEBUG_FUNCPTR (gst_id3_tag_src_query));
  gst_pad_set_query_type_function (tag->srcpad,
      GST_DEBUG_FUNCPTR (gst_id3_tag_get_query_types));
  gst_pad_set_getcaps_function (tag->srcpad,
      GST_DEBUG_FUNCPTR (gst_id3_tag_get_caps));
  gst_pad_set_link_function (tag->srcpad,
      GST_DEBUG_FUNCPTR (gst_id3_tag_src_link));
  gst_element_add_pad (GST_ELEMENT (tag), tag->srcpad);
}

static void
gst_id3_tag_init (GTypeInstance * instance, gpointer g_class)
{
  GstID3Tag *tag = GST_ID3_TAG (instance);

  if (GST_ID3_TAG_GET_CLASS (tag)->type != GST_ID3_TAG_PARSE_BASE) {
    /* create the sink and src pads */
    tag->sinkpad =
        gst_pad_new_from_template (gst_element_class_get_pad_template
        (GST_ELEMENT_CLASS (g_class), "sink"), "sink");
    gst_element_add_pad (GST_ELEMENT (tag), tag->sinkpad);
    gst_pad_set_event_function (tag->sinkpad,
        GST_DEBUG_FUNCPTR (gst_id3_tag_sink_event));
    gst_pad_set_chain_function (tag->sinkpad,
        GST_DEBUG_FUNCPTR (gst_id3_tag_chain));
  }
  if (GST_ID3_TAG_GET_CLASS (tag)->type == GST_ID3_TAG_PARSE_MUX) {
    /* only the muxer class here, all other use sometimes pads */
    gst_id3_tag_add_src_pad (tag);
    gst_pad_use_fixed_caps (tag->srcpad);
    gst_pad_set_caps (tag->srcpad,
        gst_static_pad_template_get_caps (&id3_tag_src_id3_template_factory));
  }
  /* FIXME: for the alli^H^H^H^Hspider - gst_id3_tag_add_src_pad (tag); */
  tag->parse_mode = GST_ID3_TAG_PARSE_BASE;
  tag->buffer = NULL;
  tag->segment = NULL;
}

static void
gst_id3_tag_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstID3Tag *tag;

  tag = GST_ID3_TAG (object);

  switch (prop_id) {
    case ARG_V1_TAG:
      tag->v1tag_render = g_value_get_boolean (value);
      break;
    case ARG_V2_TAG:
      tag->v2tag_render = g_value_get_boolean (value);
      break;
    case ARG_PREFER_V1:
      tag->prefer_v1tag = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  /* make sure we render at least one tag */
  if (GST_ID3_TAG_GET_CLASS (tag)->type == GST_ID3_TAG_PARSE_MUX &&
      !tag->v1tag_render && !tag->v2tag_render) {
    g_object_set (object, prop_id == ARG_V1_TAG ? "v2-tag" : "v1-tag", TRUE,
        NULL);
  }
}

static void
gst_id3_tag_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstID3Tag *tag;

  tag = GST_ID3_TAG (object);

  switch (prop_id) {
    case ARG_V1_TAG:
      g_value_set_boolean (value, tag->v1tag_render);
      break;
    case ARG_V2_TAG:
      g_value_set_boolean (value, tag->v2tag_render);
      break;
    case ARG_PREFER_V1:
      g_value_set_boolean (value, tag->prefer_v1tag);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

#define gst_id3_tag_set_state(tag,new_state) G_STMT_START {                             \
  GST_LOG_OBJECT (tag, "setting state to %s", #new_state );                             \
  tag->state = new_state;                                                               \
}G_STMT_END

static const GstQueryType *
gst_id3_tag_get_query_types (GstPad * pad)
{
  static const GstQueryType gst_id3_tag_src_query_types[] = {
    GST_QUERY_POSITION,
    0
  };

  return gst_id3_tag_src_query_types;
}

static gboolean
gst_id3_tag_src_query (GstPad * pad, GstQuery * query)
{
  gboolean res = FALSE;
  GstID3Tag *tag;

  tag = GST_ID3_TAG (gst_pad_get_parent (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:{
      GstFormat format;
      gint64 current;

      gst_query_parse_position (query, &format, NULL);
      switch (format) {
        case GST_FORMAT_BYTES:{
          GstPad *peer;

          if ((peer = gst_pad_get_peer (tag->sinkpad)) == NULL)
            break;

          if (tag->state == GST_ID3_TAG_STATE_NORMAL &&
              gst_pad_query_position (peer, &format, &current)) {
            if (tag->state == GST_ID3_TAG_STATE_NORMAL) {
              current -= tag->v2tag_size + tag->v2tag_size_new;
            } else {
              current = 0;
            }
            gst_query_set_position (query, format, current);

            res = TRUE;
          }
          gst_object_unref (peer);
          break;
        }
        default:
          break;
      }
      break;
    }
    case GST_QUERY_DURATION:{
      GstFormat format;
      gint64 total;

      gst_query_parse_duration (query, &format, NULL);
      switch (format) {
        case GST_FORMAT_BYTES:{
          GstPad *peer;

          if ((peer = gst_pad_get_peer (tag->sinkpad)) == NULL)
            break;

          if (tag->state == GST_ID3_TAG_STATE_NORMAL &&
              gst_pad_query_duration (peer, &format, &total)) {
            total -= tag->v2tag_size + tag->v1tag_size;
            total += tag->v2tag_size_new + tag->v1tag_size_new;

            gst_query_set_duration (query, format, total);

            res = TRUE;
          }
          gst_object_unref (peer);
          break;
        }
        default:
          break;
      }
      break;
    }
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }
  return res;
}

static gboolean
gst_id3_tag_src_event (GstPad * pad, GstEvent * event)
{
  GstID3Tag *tag;
  gboolean res = FALSE;

  tag = GST_ID3_TAG (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      gdouble rate;
      GstFormat format;
      GstSeekType cur_type, stop_type;
      GstSeekFlags flags;
      gint64 cur, stop;

      gst_event_parse_seek (event, &rate, &format, &flags,
          &cur_type, &cur, &stop_type, &stop);

      if (format == GST_FORMAT_BYTES &&
          tag->state == GST_ID3_TAG_STATE_NORMAL &&
          gst_pad_is_linked (tag->sinkpad)) {
        GstEvent *new;
        gint diff = 0;

        switch (cur_type) {
          case GST_SEEK_TYPE_SET:
            diff = tag->v2tag_size - tag->v2tag_size_new;
            break;
          case GST_SEEK_TYPE_CUR:
            break;
          case GST_SEEK_TYPE_END:
            diff = cur ? tag->v1tag_size_new - tag->v1tag_size : 0;
            break;
          default:
            g_assert_not_reached ();
            break;
        }
        gst_event_unref (event);
        new = gst_event_new_seek (rate, format, flags,
            cur_type, cur + diff, stop_type, stop);
        res = gst_pad_push_event (tag->sinkpad, new);
      }
      break;
    }
    default:
      res = gst_pad_push_event (tag->sinkpad, event);
      break;
  }

  gst_object_unref (tag);

  return res;
}

GstTagList *
gst_mad_id3_to_tag_list (const struct id3_tag * tag)
{
  const struct id3_frame *frame;
  const id3_ucs4_t *ucs4;
  id3_utf8_t *utf8;
  GstTagList *tag_list;
  GType tag_type;
  guint i = 0;

  tag_list = gst_tag_list_new ();

  while ((frame = id3_tag_findframe (tag, NULL, i++)) != NULL) {
    const union id3_field *field;
    unsigned int nstrings, j;
    const gchar *tag_name;

    /* find me the function to query the frame id */
    gchar *id = g_strndup (frame->id, 5);

    tag_name = gst_tag_from_id3_tag (id);
    if (tag_name == NULL) {
      g_free (id);
      continue;
    }

    if (strcmp (id, "COMM") == 0) {
      if (frame->nfields < 4)
        continue;

      ucs4 = id3_field_getfullstring (&frame->fields[3]);
      g_assert (ucs4);

      utf8 = id3_ucs4_utf8duplicate (ucs4);
      if (utf8 == 0)
        continue;

      if (!g_utf8_validate ((char *) utf8, -1, NULL)) {
        GST_ERROR ("converted string is not valid utf-8");
        g_free (utf8);
        continue;
      }

      gst_tag_list_add (tag_list, GST_TAG_MERGE_APPEND,
          GST_TAG_COMMENT, utf8, NULL);

      g_free (utf8);
      continue;
    }

    if (frame->nfields < 2)
      continue;

    field = &frame->fields[1];
    nstrings = id3_field_getnstrings (field);

    for (j = 0; j < nstrings; ++j) {
      ucs4 = id3_field_getstrings (field, j);
      g_assert (ucs4);

      if (strcmp (id, ID3_FRAME_GENRE) == 0)
        ucs4 = id3_genre_name (ucs4);

      utf8 = id3_ucs4_utf8duplicate (ucs4);
      if (utf8 == 0)
        continue;

      if (!g_utf8_validate ((char *) utf8, -1, NULL)) {
        GST_ERROR ("converted string is not valid utf-8");
        free (utf8);
        continue;
      }

      tag_type = gst_tag_get_type (tag_name);

      /* be sure to add non-string tags here */
      switch (tag_type) {
        case G_TYPE_UINT:
        {
          guint tmp;
          gchar *check;

          tmp = strtoul ((char *) utf8, &check, 10);

          if (strcmp (tag_name, GST_TAG_DATE) == 0) {
            GDate *d;

            if (*check != '\0')
              break;
            if (tmp == 0)
              break;
            d = g_date_new_dmy (1, 1, tmp);
            tmp = g_date_get_julian (d);
            g_date_free (d);
          } else if (strcmp (tag_name, GST_TAG_TRACK_NUMBER) == 0) {
            if (*check == '/') {
              guint total;

              check++;
              total = strtoul (check, &check, 10);
              if (*check != '\0')
                break;

              gst_tag_list_add (tag_list, GST_TAG_MERGE_APPEND,
                  GST_TAG_TRACK_COUNT, total, NULL);
            }
          } else if (strcmp (tag_name, GST_TAG_ALBUM_VOLUME_NUMBER) == 0) {
            if (*check == '/') {
              guint total;

              check++;
              total = strtoul (check, &check, 10);
              if (*check != '\0')
                break;

              gst_tag_list_add (tag_list, GST_TAG_MERGE_APPEND,
                  GST_TAG_ALBUM_VOLUME_COUNT, total, NULL);
            }
          }

          if (*check != '\0')
            break;
          gst_tag_list_add (tag_list, GST_TAG_MERGE_APPEND, tag_name, tmp,
              NULL);
          break;
        }
        case G_TYPE_UINT64:
        {
          guint64 tmp;

          g_assert (strcmp (tag_name, GST_TAG_DURATION) == 0);
          tmp = strtoul ((char *) utf8, NULL, 10);
          if (tmp == 0) {
            break;
          }
          gst_tag_list_add (tag_list, GST_TAG_MERGE_APPEND,
              GST_TAG_DURATION, tmp * 1000 * 1000, NULL);
          break;
        }
        case G_TYPE_STRING:{
          gst_tag_list_add (tag_list, GST_TAG_MERGE_APPEND,
              tag_name, (const gchar *) utf8, NULL);
          break;
        }
          /* handles GST_TYPE_DATE and anything else */
        default:{
          GValue src = { 0, };
          GValue dest = { 0, };

          g_value_init (&src, G_TYPE_STRING);
          g_value_set_string (&src, (const gchar *) utf8);

          g_value_init (&dest, tag_type);
          if (g_value_transform (&src, &dest)) {
            gst_tag_list_add_values (tag_list, GST_TAG_MERGE_APPEND,
                tag_name, &dest, NULL);
          } else {
            GST_WARNING ("Failed to transform tag from string to type '%s'",
                g_type_name (tag_type));
          }
          g_value_unset (&src);
          g_value_unset (&dest);
          break;
        }
      }
      free (utf8);
    }
    g_free (id);
  }

  return tag_list;
}

static void
tag_list_to_id3_tag_foreach (const GstTagList * list, const gchar * tag_name,
    gpointer user_data)
{
  struct id3_frame *frame;
  union id3_field *field;
  guint values = gst_tag_list_get_tag_size (list, tag_name);
  const gchar *id = gst_tag_to_id3_tag (tag_name);
  struct id3_tag *tag = (struct id3_tag *) user_data;

  if (id == NULL)
    return;

  if (values == 0)
    return;

  GST_DEBUG ("mapping tags to id3 for %s", tag_name);

  frame = id3_frame_new (id);
  if (id3_tag_attachframe (tag, frame) != 0) {
    GST_WARNING ("could not attach frame (%s) to id3 tag", id);
    return;
  }
  /* encode in UTF-8 - libid3tag uses Latin1 by default... */
  field = id3_frame_field (frame, 0);
  id3_field_settextencoding (field, ID3_FIELD_TEXTENCODING_UTF_8);
  field = id3_frame_field (frame, 1);
  g_assert (field);
  while (values-- > 0) {
    gunichar *put;

    if (strcmp (tag_name, GST_TAG_DATE) == 0) {
      gchar *str;
      GDate *d;

      if (!gst_tag_list_get_date_index (list, tag_name, values, &d))
        g_assert_not_reached ();
      str = g_strdup_printf ("%u", (guint) (g_date_get_year (d)));
      put = g_utf8_to_ucs4_fast (str, -1, NULL);
      g_date_free (d);
      g_free (str);
    } else if (strcmp (tag_name, GST_TAG_TRACK_NUMBER) == 0) {
      gchar *str;
      guint u;

      if (!gst_tag_list_get_uint_index (list, tag_name, values, &u))
        g_assert_not_reached ();
      str = g_strdup_printf ("%u", u);
      put = g_utf8_to_ucs4_fast (str, -1, NULL);
      g_free (str);
    } else if (strcmp (tag_name, GST_TAG_ALBUM_VOLUME_NUMBER) == 0) {
      gchar *str;
      guint u;

      if (!gst_tag_list_get_uint_index (list, tag_name, values, &u))
        g_assert_not_reached ();
      str = g_strdup_printf ("%u", u);
      put = g_utf8_to_ucs4_fast (str, -1, NULL);
      g_free (str);
    } else if (strcmp (tag_name, GST_TAG_COMMENT) == 0) {
      gchar *str;
      id3_ucs4_t ucs4_empty[] = { 0 };

      if (!gst_tag_list_get_string_index (list, tag_name, values, &str))
        g_assert_not_reached ();
      put = g_utf8_to_ucs4_fast (str, -1, NULL);
      g_free (str);

      if (id3_field_setlanguage (&frame->fields[1], "XXX") == -1 ||
          id3_field_setstring (&frame->fields[2], ucs4_empty) == -1 ||
          id3_field_setfullstring (&frame->fields[3], (id3_ucs4_t *) put) == -1)
        GST_WARNING ("could not add a string to the id3 COMM field");

      g_free (put);
      return;
    } else {
      gchar *str;

      if (gst_tag_get_type (tag_name) != G_TYPE_STRING) {
        GST_WARNING ("unhandled GStreamer tag %s", tag_name);
        return;
      }
      if (!gst_tag_list_get_string_index (list, tag_name, values, &str))
        g_assert_not_reached ();
      put = g_utf8_to_ucs4_fast (str, -1, NULL);
      g_free (str);
    }
    if (id3_field_addstring (field, (id3_ucs4_t *) put) != 0) {
      GST_WARNING ("could not add a string to id3 tag field");
      g_free (put);
      return;
    }
  }
  id3_field_settextencoding (field, ID3_FIELD_TEXTENCODING_UTF_8);
}

struct id3_tag *
gst_mad_tag_list_to_id3_tag (GstTagList * list)
{
  struct id3_tag *tag;

  tag = id3_tag_new ();

  gst_tag_list_foreach (list, tag_list_to_id3_tag_foreach, tag);
  return tag;
}

static GstTagList *
gst_id3_tag_get_tag_to_render (GstID3Tag * tag)
{
  GstTagList *ret = NULL;
  const GstTagList *taglist =
      gst_tag_setter_get_tag_list (GST_TAG_SETTER (tag));

  GST_DEBUG ("preparing taglist to render:");
  GST_DEBUG (" event_tags  = %" GST_PTR_FORMAT, tag->event_tags);
  GST_DEBUG (" parsed_tags = %" GST_PTR_FORMAT, tag->parsed_tags);
  GST_DEBUG (" taglist     = %" GST_PTR_FORMAT, taglist);

  if (tag->event_tags)
    ret = gst_tag_list_copy (tag->event_tags);
  if (ret) {
    if (tag->parsed_tags)
      gst_tag_list_insert (ret, tag->parsed_tags, GST_TAG_MERGE_KEEP);
  } else if (tag->parsed_tags) {
    ret = gst_tag_list_copy (tag->parsed_tags);
  }
  if (taglist) {
    if (ret) {
      GstTagList *tmp;

      tmp = gst_tag_list_merge (taglist, ret,
          gst_tag_setter_get_tag_merge_mode (GST_TAG_SETTER (tag)));
      gst_tag_list_free (ret);
      ret = tmp;
    } else {
      ret = gst_tag_list_copy (taglist);
    }
  }
  return ret;
}

static gboolean
gst_id3_tag_sink_event (GstPad * pad, GstEvent * event)
{
  GstID3Tag *tag = GST_ID3_TAG (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:
      GST_DEBUG_OBJECT (tag, "Have new segment event in mode %s",
          GST_ID3_TAG_GET_STATE_NAME (tag->state));
      switch (tag->state) {
        case GST_ID3_TAG_STATE_READING_V2_TAG:{
          GstFormat format = GST_FORMAT_UNDEFINED;
          gint64 value, end_value;

          gst_event_parse_new_segment (event, NULL, NULL, &format, &value,
              &end_value, NULL);

          if (format == GST_FORMAT_BYTES || format == GST_FORMAT_DEFAULT) {
            if (value !=
                (tag->buffer ? GST_BUFFER_OFFSET (tag->buffer) +
                    GST_BUFFER_SIZE (tag->buffer)
                    : 0))
              GST_ELEMENT_ERROR (tag, CORE, EVENT, (NULL),
                  ("Got seek to %" G_GINT64_FORMAT " during ID3v2 tag reading"
                      " (allowed was %" G_GINT64_FORMAT ")", value,
                      (guint64) (tag->buffer ? GST_BUFFER_OFFSET (tag->buffer)
                          + GST_BUFFER_SIZE (tag->buffer) : 0)));
          }
          tag->segment = event;
          break;
        }
        case GST_ID3_TAG_STATE_SEEKING_TO_V1_TAG:
          /* just assume it's the right seek for now */
          gst_id3_tag_set_state (tag, GST_ID3_TAG_STATE_READING_V1_TAG);
          gst_event_unref (event);
          break;
        case GST_ID3_TAG_STATE_READING_V1_TAG:
          GST_ELEMENT_ERROR (tag, CORE, EVENT, (NULL),
              ("Seek during ID3v1 tag reading"));
          gst_event_unref (event);
          break;
        case GST_ID3_TAG_STATE_SEEKING_TO_NORMAL:
          /* just assume it's the right seek for now */
          gst_id3_tag_set_state (tag, GST_ID3_TAG_STATE_NORMAL_START);
          if (tag->segment != NULL)
            gst_event_unref (tag->segment);
          tag->segment = event;
          break;
        case GST_ID3_TAG_STATE_NORMAL_START:
          if (!CAN_BE_DEMUXER (tag)) {
            /* initial discont, ignore */
            GST_LOG_OBJECT (tag, "Ignoring initial newsegment");
            gst_event_unref (event);
            break;
          } else {
            GST_ERROR_OBJECT (tag, "tag event not sent, FIXME");
            gst_id3_tag_set_state (tag, GST_ID3_TAG_STATE_NORMAL);
            /* fall through */
          }
        case GST_ID3_TAG_STATE_NORMAL:{
          GstFormat format = GST_FORMAT_UNDEFINED;
          gdouble rate;
          gint64 value, end_value, base;

          gst_event_parse_new_segment (event, NULL, &rate, &format, &value,
              &end_value, &base);
          if (format == GST_FORMAT_BYTES || format == GST_FORMAT_DEFAULT) {
            if (value > tag->v2tag_size) {
              value -= tag->v2tag_size;
              gst_event_unref (event);
              event =
                  gst_event_new_new_segment (FALSE, rate, format, value,
                  end_value, base);
            }
          }
          if (tag->srcpad)
            gst_pad_push_event (tag->srcpad, event);
          else
            gst_event_unref (event);
          break;
        }
        default:
          g_assert_not_reached ();
      }
      break;
    case GST_EVENT_TAG:
    {
      GstTagList *list;

      GST_DEBUG_OBJECT (tag, "Have tags event in mode %s",
          GST_ID3_TAG_GET_STATE_NAME (tag->state));
      gst_event_parse_tag (event, &list);

      if (tag->event_tags) {
        gst_tag_list_insert (tag->event_tags, list, GST_TAG_MERGE_PREPEND);
      } else {
        tag->event_tags = gst_tag_list_copy (list);
      }
      gst_event_unref (event);
      break;
    }
    case GST_EVENT_EOS:
      GST_DEBUG_OBJECT (tag, "Have EOS in mode %s",
          GST_ID3_TAG_GET_STATE_NAME (tag->state));
      if (tag->v1tag_render && IS_MUXER (tag)) {
        GstTagList *merged;
        struct id3_tag *id3;

        GST_LOG_OBJECT (tag, "rendering v1 tag after eos event");
        merged = gst_id3_tag_get_tag_to_render (tag);
        if (merged) {
          id3 = gst_mad_tag_list_to_id3_tag (merged);
          if (id3) {
            GstBuffer *tag_buffer;

            id3_tag_options (id3, ID3_TAG_OPTION_ID3V1, ID3_TAG_OPTION_ID3V1);
            tag_buffer = gst_buffer_new_and_alloc (128);
            if (128 != id3_tag_render (id3, tag_buffer->data))
              g_assert_not_reached ();
            gst_buffer_set_caps (tag_buffer, GST_PAD_CAPS (tag->srcpad));
            gst_pad_push (tag->srcpad, tag_buffer);
            id3_tag_delete (id3);
          }
          gst_tag_list_free (merged);
        }
      }
      if (tag->state == GST_ID3_TAG_STATE_SEEKING_TO_NORMAL) {
        /* Absorb EOS while finishing reading V1 TAG */
        GST_LOG_OBJECT (tag, "Ignoring EOS event after reading id3v1");
        gst_event_unref (event);
        break;
      }
      /* fall through */
    default:
      gst_pad_event_default (pad, event);
      break;
  }
  return TRUE;
}

typedef struct
{
  guint best_probability;
  GstCaps *caps;
  GstBuffer *buffer;
}
SimpleTypeFind;
guint8 *
simple_find_peek (gpointer data, gint64 offset, guint size)
{
  SimpleTypeFind *find = (SimpleTypeFind *) data;

  if (offset < 0)
    return NULL;

  if (GST_BUFFER_SIZE (find->buffer) >= offset + size) {
    return GST_BUFFER_DATA (find->buffer) + offset;
  }
  return NULL;
}

static void
simple_find_suggest (gpointer data, guint probability, const GstCaps * caps)
{
  SimpleTypeFind *find = (SimpleTypeFind *) data;

  if (probability > find->best_probability) {
    GstCaps *copy = gst_caps_copy (caps);

    gst_caps_replace (&find->caps, copy);
    gst_caps_unref (copy);
    find->best_probability = probability;
  }
}

static GstCaps *
gst_id3_tag_do_typefind (GstID3Tag * tag, GstBuffer * buffer)
{
  GList *walk, *type_list;
  SimpleTypeFind find;
  GstTypeFind gst_find;

  /* this will help us detecting the media stream type after
   * this id3 thingy... Please note that this is a cruel hack
   * for as long as spider doesn't support multi-type-finding.
   */
  walk = type_list = gst_type_find_factory_get_list ();

  find.buffer = buffer;
  find.best_probability = 0;
  find.caps = NULL;
  gst_find.data = &find;
  gst_find.peek = simple_find_peek;
  gst_find.get_length = NULL;
  gst_find.suggest = simple_find_suggest;
  while (walk) {
    GstTypeFindFactory *factory = GST_TYPE_FIND_FACTORY (walk->data);

    gst_type_find_factory_call_function (factory, &gst_find);
    if (find.best_probability >= GST_TYPE_FIND_MAXIMUM)
      break;
    walk = g_list_next (walk);
  }
  gst_plugin_feature_list_free (type_list);
  if (find.best_probability > 0) {
    return find.caps;
  }

  return NULL;
}

static gboolean
gst_id3_tag_do_caps_nego (GstID3Tag * tag, GstBuffer * buffer)
{
  if (buffer != NULL && CAN_BE_DEMUXER (tag)) {
    tag->found_caps = gst_id3_tag_do_typefind (tag, buffer);
    if (!tag->found_caps) {
      return FALSE;
    }
    GST_DEBUG_OBJECT (tag, "Found contained data caps %" GST_PTR_FORMAT,
        tag->found_caps);
  }
  if (tag->srcpad == NULL) {
    gst_id3_tag_add_src_pad (tag);
    gst_element_no_more_pads (GST_ELEMENT (tag));
  }
  if (!gst_pad_is_linked (tag->srcpad)) {
    GST_DEBUG_OBJECT (tag, "srcpad not linked, not proceeding");
    tag->parse_mode = GST_ID3_TAG_GET_CLASS (tag)->type;
    return TRUE;
  } else {
    GST_DEBUG_OBJECT (tag, "renegotiating");
    //return gst_pad_renegotiate (tag->srcpad) != GST_PAD_LINK_REFUSED;
    return TRUE;
  }
}

static GstPadLinkReturn
gst_id3_tag_src_link (GstPad * pad, GstPad * peer)
{
  GstID3Tag *tag;

  const gchar *mimetype;

  tag = GST_ID3_TAG (gst_pad_get_parent (pad));

/*#if 0*/
  if (!tag->found_caps && CAN_BE_DEMUXER (tag))
    return GST_PAD_LINK_REFUSED;
  /*return GST_PAD_LINK_DELAYED; */
  if (!CAN_BE_MUXER (tag) || !CAN_BE_DEMUXER (tag)) {
    tag->parse_mode = GST_ID3_TAG_GET_CLASS (tag)->type;
    return GST_PAD_LINK_OK;
  }

  mimetype =
      gst_structure_get_name (gst_caps_get_structure (tag->found_caps, 0));

  if (strcmp (mimetype, "application/x-id3") == 0) {
    tag->parse_mode = GST_ID3_TAG_PARSE_MUX;
    GST_LOG_OBJECT (tag, "mux operation, using application/x-id3 output");
  } else {
    tag->parse_mode = GST_ID3_TAG_PARSE_DEMUX;
    GST_LOG_OBJECT (tag, "demux operation, extracting tags");
  }
/*#endif*/
  if (GST_PAD_LINKFUNC (peer))
    return GST_PAD_LINKFUNC (peer) (peer, pad);
  else
    return GST_PAD_LINK_OK;
}

static void
gst_id3_tag_send_tag_event (GstID3Tag * tag)
{
  /* FIXME: what's the correct merge mode? Docs need to tell... */
  GstTagList *merged = gst_tag_list_merge (tag->event_tags, tag->parsed_tags,
      GST_TAG_MERGE_KEEP);

  GST_DEBUG ("Sending tag event");

  if (tag->parsed_tags)
    gst_element_post_message (GST_ELEMENT (tag),
        gst_message_new_tag (GST_OBJECT (tag),
            gst_tag_list_copy (tag->parsed_tags)));

  if (merged) {
    GstEvent *event = gst_event_new_tag (merged);

    GST_EVENT_TIMESTAMP (event) = 0;
    gst_pad_push_event (tag->srcpad, event);
  }
}

static GstFlowReturn
gst_id3_tag_chain (GstPad * pad, GstBuffer * buffer)
{
  GstID3Tag *tag;
  GstBuffer *temp;

  tag = GST_ID3_TAG (gst_pad_get_parent (pad));
  GST_LOG_OBJECT (tag, "Chain, mode = %s, state = %s",
      GST_ID3_TAG_GET_MODE_NAME (tag->parse_mode),
      GST_ID3_TAG_GET_STATE_NAME (tag->state));

  if (tag->buffer) {
    buffer = gst_buffer_join (tag->buffer, buffer);
    tag->buffer = NULL;
  }

  switch (tag->state) {
    case GST_ID3_TAG_STATE_SEEKING_TO_V1_TAG:
    case GST_ID3_TAG_STATE_SEEKING_TO_NORMAL:
      /* we're waiting for the seek to finish, just discard all the stuff */
      gst_buffer_unref (buffer);
      return GST_FLOW_OK;
    case GST_ID3_TAG_STATE_READING_V1_TAG:
      if (GST_BUFFER_SIZE (buffer) < 128) {
        tag->buffer = buffer;
        return GST_FLOW_OK;
      }

      g_assert (tag->v1tag_size == 0);
      tag->v1tag_size = id3_tag_query (GST_BUFFER_DATA (buffer),
          GST_BUFFER_SIZE (buffer));
      if (tag->v1tag_size == 128) {
        GstTagList *newtag;

        newtag = gst_tag_list_new_from_id3v1 (GST_BUFFER_DATA (buffer));
        GST_LOG_OBJECT (tag, "have read ID3v1 tag");
        if (GST_BUFFER_OFFSET_IS_VALID (buffer))
          tag->v1tag_offset = GST_BUFFER_OFFSET (buffer);
        else
          tag->v1tag_offset = G_MAXUINT64;

        if (newtag) {
          if (tag->parsed_tags) {
            /* FIXME: use append/prepend here ? */
            gst_tag_list_insert (tag->parsed_tags, newtag,
                tag->prefer_v1tag ? GST_TAG_MERGE_REPLACE : GST_TAG_MERGE_KEEP);
            gst_tag_list_free (newtag);
          } else {
            tag->parsed_tags = newtag;
          }
        } else {
          GST_WARNING_OBJECT (tag, "detected ID3v1 tag, but couldn't parse it");
        }
      } else {
        if (tag->v1tag_size != 0) {
          GST_WARNING_OBJECT (tag, "bad non-ID3v1 tag at end of file");
        } else {
          GST_LOG_OBJECT (tag, "no ID3v1 tag (%" G_GUINT64_FORMAT ")",
              GST_BUFFER_OFFSET (buffer));
          tag->v1tag_offset = G_MAXUINT64;
        }
      }
      gst_buffer_unref (buffer);
      if (tag->parse_mode != GST_ID3_TAG_PARSE_ANY) {
        /* seek to beginning */
        GST_LOG_OBJECT (tag, "seeking back to beginning (offset %ld)",
            tag->v2tag_size);
        gst_id3_tag_set_state (tag, GST_ID3_TAG_STATE_SEEKING_TO_NORMAL);
        if (!gst_pad_push_event (tag->sinkpad,
                gst_event_new_seek (1.0, GST_FORMAT_BYTES, GST_SEEK_FLAG_FLUSH,
                    GST_SEEK_TYPE_SET, tag->v2tag_size, GST_SEEK_TYPE_NONE,
                    0))) {
          GST_ELEMENT_ERROR (tag, CORE, SEEK, (NULL),
              ("can't seek back to beginning from reading ID3v1 tag"));
        }
      } else {
        gst_id3_tag_send_tag_event (tag);
        /* set eos, we're done parsing tags */
        GST_LOG_OBJECT (tag, "Finished reading ID3v1 tag");
        gst_id3_tag_set_state (tag, GST_ID3_TAG_STATE_NORMAL_START);
        gst_pad_push_event (tag->srcpad, gst_event_new_eos ());
      }
      return GST_FLOW_OK;
    case GST_ID3_TAG_STATE_READING_V2_TAG:
      if (GST_BUFFER_SIZE (buffer) < 10) {
        tag->buffer = buffer;
        return GST_FLOW_OK;
      }
      if (tag->v2tag_size == 0) {
        tag->v2tag_size = id3_tag_query (GST_BUFFER_DATA (buffer),
            GST_BUFFER_SIZE (buffer));
        /* no footers supported */
        if (tag->v2tag_size < 0)
          tag->v2tag_size = 0;
      }
      /* Collect a large enough chunk to read the tag */
      if (GST_BUFFER_SIZE (buffer) < tag->v2tag_size) {
        GST_DEBUG_OBJECT (tag,
            "Not enough data to read ID3v2. Need %ld have %d, waiting for more",
            tag->v2tag_size, GST_BUFFER_SIZE (buffer));
        tag->buffer = buffer;
        return GST_FLOW_OK;
      }

      if (tag->v2tag_size != 0) {
        struct id3_tag *v2tag;

        v2tag = id3_tag_parse (GST_BUFFER_DATA (buffer),
            GST_BUFFER_SIZE (buffer));
        if (v2tag) {
          GstTagList *list;

          list = gst_mad_id3_to_tag_list (v2tag);
          id3_tag_delete (v2tag);
          GST_LOG_OBJECT (tag, "parsed ID3v2 tag");
          /* no other tag parsed yet */
          g_assert (tag->parsed_tags == NULL);
          tag->parsed_tags = list;
        } else {
          GST_WARNING_OBJECT (tag, "detected ID3v2 tag, but couldn't parse it");
        }
      }

      /* seek to ID3v1 tag */
      gst_id3_tag_set_state (tag, GST_ID3_TAG_STATE_SEEKING_TO_V1_TAG);

      if (gst_pad_push_event (tag->sinkpad,
              gst_event_new_seek (1.0, GST_FORMAT_BYTES, GST_SEEK_FLAG_FLUSH,
                  GST_SEEK_TYPE_END, -128, GST_SEEK_TYPE_NONE, 0))) {
        gst_buffer_unref (buffer);
        return GST_FLOW_OK;
      } else {
        GST_DEBUG_OBJECT (tag, "Can't seek to read ID3v1 tag");
      }

      /* Can't seek, strip off the id3v2 tag and send buffer */
      GST_LOG_OBJECT (tag,
          "removing first %ld bytes, because they're the ID3v2 tag",
          tag->v2tag_size);
      temp =
          gst_buffer_create_sub (buffer, tag->v2tag_size,
          GST_BUFFER_SIZE (buffer) - tag->v2tag_size);
      /* the offsets will be corrected further down, we just copy them */
      if (GST_BUFFER_OFFSET_IS_VALID (buffer))
        GST_BUFFER_OFFSET (temp) = GST_BUFFER_OFFSET (buffer) + tag->v2tag_size;
      if (GST_BUFFER_OFFSET_END_IS_VALID (buffer))
        GST_BUFFER_OFFSET_END (temp) =
            GST_BUFFER_OFFSET_END (buffer) + tag->v2tag_size;

      gst_buffer_unref (buffer);
      buffer = temp;

      gst_id3_tag_set_state (tag, GST_ID3_TAG_STATE_NORMAL_START);
      /* fall through */
    case GST_ID3_TAG_STATE_NORMAL_START:
      if (!IS_MUXER (tag) && (tag->found_caps == NULL)) {
        /* Don't do caps nego until we have at least ID3_TYPE_FIND_SIZE bytes */
        if (GST_BUFFER_SIZE (buffer) < ID3_TYPE_FIND_MIN_SIZE) {
          GST_DEBUG_OBJECT (tag,
              "Not enough data (%d) for typefind, waiting for more",
              GST_BUFFER_SIZE (buffer));
          tag->buffer = buffer;
          return GST_FLOW_OK;
        }

        if (!gst_id3_tag_do_caps_nego (tag, buffer)) {
          if (GST_BUFFER_SIZE (buffer) < ID3_TYPE_FIND_MAX_SIZE) {
            /* Just break for more */
            tag->buffer = buffer;
            return GST_FLOW_OK;
          }

          /* We failed typefind */
          GST_ELEMENT_ERROR (tag, CORE, CAPS, (NULL), ("no caps found"));
          gst_buffer_unref (buffer);
          return GST_FLOW_ERROR;
        }
      }

      GST_DEBUG ("Found type with size %u", GST_BUFFER_SIZE (buffer));

      /* If we didn't get a segment event to pass on, someone
       * downstream is going to complain */
      if (tag->segment != NULL) {
        gst_pad_push_event (tag->srcpad, tag->segment);
        tag->segment = NULL;
      }

      gst_id3_tag_send_tag_event (tag);

      if (IS_MUXER (tag) && tag->v2tag_render) {
        struct id3_tag *id3;
        GstTagList *merged;
        GstBuffer *tag_buffer;

        /* render tag */
        GST_LOG_OBJECT (tag, "rendering v2 tag");
        tag->v2tag_size_new = 0;
        merged = gst_id3_tag_get_tag_to_render (tag);
        if (merged) {
          id3 = gst_mad_tag_list_to_id3_tag (merged);
          if (id3) {
            glong estimated;

            estimated = id3_tag_render (id3, NULL);
            tag_buffer = gst_buffer_new_and_alloc (estimated);
            tag->v2tag_size_new =
                id3_tag_render (id3, GST_BUFFER_DATA (tag_buffer));
            g_assert (estimated >= tag->v2tag_size_new);
            GST_BUFFER_SIZE (tag_buffer) = tag->v2tag_size_new;
            gst_buffer_set_caps (tag_buffer, GST_PAD_CAPS (tag->srcpad));
            gst_pad_push (tag->srcpad, tag_buffer);
            id3_tag_delete (id3);
          }
          gst_tag_list_free (merged);
        } else {
          GST_INFO ("no tags to render");
        }
      } else {
        GST_INFO ("tag-mode=%s, v2tag_render=%d",
            GST_ID3_TAG_GET_MODE_NAME (tag->parse_mode), tag->v2tag_render);
      }

      gst_id3_tag_set_state (tag, GST_ID3_TAG_STATE_NORMAL);
      tag->v1tag_size_new = (tag->v1tag_render &&
          IS_MUXER (tag) &&
          (tag->parsed_tags != NULL ||
              gst_tag_setter_get_tag_list (GST_TAG_SETTER (tag)) !=
              NULL)) ? 128 : 0;
      /* fall through */
    case GST_ID3_TAG_STATE_NORMAL:
      if (tag->parse_mode == GST_ID3_TAG_PARSE_ANY) {
        gst_buffer_unref (buffer);
        gst_pad_push_event (tag->srcpad, gst_event_new_eos ());
      } else {
        if (GST_BUFFER_OFFSET_IS_VALID (buffer)) {
          if (GST_BUFFER_OFFSET (buffer) >= tag->v1tag_offset) {
            gst_buffer_unref (buffer);
            return GST_FLOW_OK;
          } else if (GST_BUFFER_OFFSET (buffer) + GST_BUFFER_SIZE (buffer) >
              tag->v1tag_offset) {
            GstBuffer *sub = gst_buffer_create_sub (buffer, 0,
                GST_BUFFER_SIZE (buffer) - 128);

            gst_buffer_unref (buffer);
            buffer = sub;
          }
        }
        if (tag->v2tag_size) {
          GstBuffer *sub =
              gst_buffer_create_sub (buffer, 0, GST_BUFFER_SIZE (buffer));
          if (GST_BUFFER_OFFSET_IS_VALID (buffer))
            GST_BUFFER_OFFSET (sub) =
                GST_BUFFER_OFFSET (buffer) - tag->v2tag_size +
                tag->v2tag_size_new;
          if (GST_BUFFER_OFFSET_END_IS_VALID (buffer))
            GST_BUFFER_OFFSET_END (sub) =
                GST_BUFFER_OFFSET_END (buffer) - tag->v2tag_size +
                tag->v2tag_size_new;
          gst_buffer_unref (buffer);
          buffer = sub;
        }
        gst_buffer_set_caps (buffer, GST_PAD_CAPS (tag->srcpad));
        gst_pad_push (tag->srcpad, buffer);
      }
      return GST_FLOW_OK;
  }
  return GST_FLOW_OK;
}

static GstStateChangeReturn
gst_id3_tag_change_state (GstElement * element, GstStateChange transition)
{
  GstID3Tag *tag;
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  tag = GST_ID3_TAG (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      g_assert (tag->parsed_tags == NULL);
      g_assert (tag->buffer == NULL);
      tag->v1tag_size = 0;
      tag->v1tag_offset = G_MAXUINT64;
      tag->v2tag_size = 0;
      if (CAN_BE_DEMUXER (tag)) {
        gst_id3_tag_set_state (tag, GST_ID3_TAG_STATE_READING_V2_TAG);
      } else {
        gst_id3_tag_set_state (tag, GST_ID3_TAG_STATE_NORMAL_START);
      }
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (tag->parsed_tags) {
        gst_tag_list_free (tag->parsed_tags);
        tag->parsed_tags = NULL;
      }
      if (tag->event_tags) {
        gst_tag_list_free (tag->event_tags);
        tag->event_tags = NULL;
      }
      if (tag->buffer) {
        gst_buffer_unref (tag->buffer);
        tag->buffer = NULL;
      }
      if (tag->found_caps) {
        gst_caps_unref (tag->found_caps);
        tag->found_caps = NULL;
      }
      if (tag->segment) {
        gst_event_unref (tag->segment);
        tag->segment = NULL;
      }
      tag->parse_mode = GST_ID3_TAG_PARSE_BASE;
      break;
    default:
      break;
  }


  return ret;
}

/*** PLUGIN INITIALIZATION ****************************************************/

static gboolean
plugin_init (GstPlugin * plugin)
{

  if (!gst_element_register (plugin, "mad", GST_RANK_SECONDARY,
          gst_mad_get_type ())
      || !gst_element_register (plugin, "id3mux", GST_RANK_NONE,
          gst_id3_tag_get_type (GST_ID3_TAG_PARSE_MUX))) {
    return FALSE;
  }

  GST_DEBUG_CATEGORY_INIT (gst_id3_tag_debug, "id3mux", 0, "id3 tag setter");

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "mad",
    "id3 tag manipulation and mp3 decoding based on the mad library",
    plugin_init, VERSION, "GPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
