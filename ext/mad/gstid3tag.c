/* GStreamer
 * Copyright (C) 2003 Benjamin Otte <in7y118@public.uni-hamburg.de>
 *
 * gstid3tagsetter.c: plugin for reading / modifying id3 tags
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
#include <gst/gsttaginterface.h>
#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_id3_tag_debug);
#define GST_CAT_DEFAULT gst_id3_tag_debug

#define GST_TYPE_ID3_TAG (gst_id3_tag_get_type())
#define GST_ID3_TAG(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ID3_TAG, GstID3Tag))
#define GST_ID3_TAG_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ID3_TAG, GstID3Tag))
#define GST_IS_ID3_TAG(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_ID3_TAG))
#define GST_IS_ID3_TAG_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_ID3_TAG))

typedef struct _GstID3Tag GstID3Tag;
typedef struct _GstID3TagClass GstID3TagClass;

typedef enum {
  GST_ID3_TAG_STATE_READING_V2_TAG,
  GST_ID3_TAG_STATE_SEEKING_TO_V1_TAG,
  GST_ID3_TAG_STATE_READING_V1_TAG,
  GST_ID3_TAG_STATE_SEEKING_TO_NORMAL,
  GST_ID3_TAG_STATE_NORMAL_START,
  GST_ID3_TAG_STATE_NORMAL,
} GstID3TagState;

struct _GstID3Tag {
  GstElement	 element;

  /* pads */
  GstPad *		sinkpad;
  GstPad *		srcpad;

  /* caps */
  gint			layer;
  gboolean		parse_mode;

  /* tags */
  GstTagList *		parsed_tags;

  /* state */
  GstID3TagState	state;

  GstBuffer *		buffer;
  gboolean		prefer_v1tag;
  glong			v1tag_size;
  glong			v1tag_size_new;
  guint64		v1tag_offset;
  gboolean		v1tag_render;
  glong			v2tag_size;
  glong			v2tag_size_new;
  gboolean		v2tag_render;
};

struct _GstID3TagClass {
  GstElementClass parent_class;
};

/* elementfactory information */
static GstElementDetails gst_id3_tag_details = GST_ELEMENT_DETAILS (
  "id3 tag extractor",
  "Tag",
  "Extract tagging information from mp3s",
  "Benjamin Otte <in7y118@public.uni-hamburg.de>"
);


/* signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_V1_TAG,
  ARG_V2_TAG,
  ARG_PREFER_V1
  /* FILL ME */
};

GST_PAD_TEMPLATE_FACTORY (id3_tag_src_template_factory,
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "id3_tag_data_src",
    "audio/mpeg",
      "layer",		GST_PROPS_INT_RANGE (1, 3)
  ),
  GST_CAPS_NEW (
    "id3_tag_tag_src",
    "application/x-gst-tags",
    NULL
  )
)

GST_PAD_TEMPLATE_FACTORY (id3_tag_sink_template_factory,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "id3_tag_data_sink",
    "audio/mpeg",
      "layer",		GST_PROPS_INT_RANGE (1, 3)
  )
)


static void		gst_id3_tag_base_init		(gpointer		g_class);
static void		gst_id3_tag_class_init		(GstID3TagClass *	klass);
static void		gst_id3_tag_init		(GstID3Tag *		tag);
static void             gst_id3_tag_set_property	(GObject *		object,
							 guint			prop_id,
							 const GValue *		value,
							 GParamSpec *		pspec);
static void             gst_id3_tag_get_property	(GObject *		object,
							 guint			prop_id,
							 GValue *		value,
							 GParamSpec *		pspec);

static gboolean		gst_id3_tag_src_event		(GstPad *		pad, 
							 GstEvent *		event);
static const GstEventMask* gst_id3_tag_get_event_masks	(GstPad *		pad);
static const GstQueryType* gst_id3_tag_get_query_types	(GstPad *		pad);

static GstPadLinkReturn gst_id3_tag_link_sink		(GstPad *		pad, 
							 GstCaps *		caps);
static gboolean		gst_id3_tag_src_query		(GstPad *		pad,
							 GstQueryType		type,
							 GstFormat *		format, 
							 gint64 *		value);

static void		gst_id3_tag_chain		(GstPad *		pad,
							 GstData *		data);

static GstElementStateReturn gst_id3_tag_change_state	(GstElement *		element);

static GstElementClass *parent_class = NULL;
/* static guint gst_id3_tag_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_id3_tag_get_type (void)
{
  static GType id3_tag_type = 0;

  if (!id3_tag_type) {
    static const GTypeInfo id3_tag_info = {
      sizeof (GstID3TagClass),
      gst_id3_tag_base_init,
      NULL,
      (GClassInitFunc) gst_id3_tag_class_init,
      NULL,
      NULL,
      sizeof (GstID3Tag),
      0,
      (GInstanceInitFunc) gst_id3_tag_init,
    };
    static const GInterfaceInfo tag_setter_info = {
      NULL,
      NULL,
      NULL
    };
    
    id3_tag_type = g_type_register_static(GST_TYPE_ELEMENT, "GstID3Tag", &id3_tag_info, 0);

    g_type_add_interface_static (id3_tag_type, GST_TYPE_TAG_SETTER, &tag_setter_info);
    
    GST_DEBUG_CATEGORY_INIT (gst_id3_tag_debug, "id3tag", 0, "id3 tag reader / setter");
  }
  return id3_tag_type;
}
static void
gst_id3_tag_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &gst_id3_tag_details);

  gst_element_class_add_pad_template (element_class,
		  GST_PAD_TEMPLATE_GET (id3_tag_sink_template_factory));
  gst_element_class_add_pad_template (element_class,
		  GST_PAD_TEMPLATE_GET (id3_tag_src_template_factory));
}
static void
gst_id3_tag_class_init (GstID3TagClass *klass)
{
  GstElementClass *gstelement_class;
  GObjectClass *gobject_class;
  
  gstelement_class = (GstElementClass*) klass;
  gobject_class = (GObjectClass*) klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  gstelement_class->change_state = gst_id3_tag_change_state;

  g_object_class_install_property (gobject_class, ARG_V1_TAG,
	  g_param_spec_boolean ("v1-tag", "add version 1 tag", "Add version 1 tag at end of file",
	                        FALSE, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
  g_object_class_install_property (gobject_class, ARG_V2_TAG,
	  g_param_spec_boolean ("v2-tag", "add version 2 tag", "Add version 2 tag at start of file",
	                        TRUE, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
  g_object_class_install_property (gobject_class, ARG_PREFER_V1,
	  g_param_spec_boolean ("prefer-v1", "prefer version 1 tag", "Prefer tags from tag at end of file",
	                        FALSE, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
  
  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_id3_tag_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_id3_tag_get_property);
}

static void
gst_id3_tag_init (GstID3Tag *tag)
{
  /* create the sink and src pads */
  tag->sinkpad = gst_pad_new_from_template(
		  GST_PAD_TEMPLATE_GET (id3_tag_sink_template_factory), "sink");
  gst_element_add_pad (GST_ELEMENT (tag), tag->sinkpad);
  gst_pad_set_chain_function (tag->sinkpad, GST_DEBUG_FUNCPTR (gst_id3_tag_chain));
  gst_pad_set_link_function (tag->sinkpad, GST_DEBUG_FUNCPTR (gst_id3_tag_link_sink));

  tag->srcpad = gst_pad_new_from_template(
		  GST_PAD_TEMPLATE_GET (id3_tag_src_template_factory), "src");
  gst_element_add_pad (GST_ELEMENT (tag), tag->srcpad);
  gst_pad_set_event_function (tag->srcpad, GST_DEBUG_FUNCPTR (gst_id3_tag_src_event));
  gst_pad_set_event_mask_function (tag->srcpad, GST_DEBUG_FUNCPTR (gst_id3_tag_get_event_masks));
  gst_pad_set_query_function (tag->srcpad, GST_DEBUG_FUNCPTR (gst_id3_tag_src_query));
  gst_pad_set_query_type_function (tag->srcpad, GST_DEBUG_FUNCPTR (gst_id3_tag_get_query_types));

  tag->parsed_tags = NULL;
  tag->state = GST_ID3_TAG_STATE_READING_V2_TAG;
  tag->buffer = NULL;
  
  GST_FLAG_SET (tag, GST_ELEMENT_EVENT_AWARE);
}
static void
gst_id3_tag_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstID3Tag *tag;
  
  tag = GST_ID3_TAG (object);
  
  switch (prop_id) {
    case ARG_V1_TAG:
      tag->v1tag_render = g_value_get_boolean (value);
      tag->v1tag_size_new = (tag->v1tag_render && (tag->parsed_tags != NULL || 
	    gst_tag_setter_get_list (GST_TAG_SETTER (tag)) != NULL)) ? 128 : 0;
      g_object_notify (object, "v1-tag"); 
      break;
    case ARG_V2_TAG:
      tag->v2tag_render = g_value_get_boolean (value);
      g_object_notify (object, "v2-tag"); 
      break;
    case ARG_PREFER_V1:
      tag->prefer_v1tag = g_value_get_boolean (value);
      g_object_notify (object, "prefer-v1"); 
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
static void
gst_id3_tag_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
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
static const GstEventMask*
gst_id3_tag_get_event_masks (GstPad *pad)
{
  static const GstEventMask gst_id3_tag_src_event_masks[] = {
    { GST_EVENT_SEEK, GST_SEEK_METHOD_SET |
                      GST_SEEK_FLAG_FLUSH },
    { 0, }
  };
  return gst_id3_tag_src_event_masks;
}
static const GstQueryType*
gst_id3_tag_get_query_types (GstPad *pad)
{
  static const GstQueryType gst_id3_tag_src_query_types[] = {
    GST_QUERY_TOTAL,
    GST_QUERY_POSITION,
    0
  };
  return gst_id3_tag_src_query_types;
}

static gboolean
gst_id3_tag_src_query (GstPad *pad, GstQueryType type,
		       GstFormat *format, gint64 *value)
{
  gboolean res = FALSE;
  GstID3Tag *tag;

  tag = GST_ID3_TAG (gst_pad_get_parent (pad));

  switch (type) {
    case GST_QUERY_TOTAL: {
      switch (*format) {
	case GST_FORMAT_BYTES: 
	  if (GST_PAD_PEER (tag->sinkpad) && 
	      tag->state == GST_ID3_TAG_STATE_NORMAL && 
	      gst_pad_query (GST_PAD_PEER (tag->sinkpad), GST_QUERY_TOTAL, format, value)) {
	    *value -= tag->v2tag_size + tag->v1tag_size;
	    *value += tag->v2tag_size_new + tag->v1tag_size;
	    res = TRUE;
	  }
	  break;
	default:
	  break;
      }
      break;
    }
    case GST_QUERY_POSITION:
      switch (*format) {
	case GST_FORMAT_BYTES:
	  if (GST_PAD_PEER (tag->sinkpad) &&
	      gst_pad_query (GST_PAD_PEER (tag->sinkpad), GST_QUERY_POSITION, format, value)) {
	    if (tag->state == GST_ID3_TAG_STATE_NORMAL) {
	      *value -= tag->v2tag_size + tag->v2tag_size_new;
	    } else {
	      *value = 0;
	    }
	    res = TRUE;
	  }
	  break;
	default:
	  break;
      }
      break;
    default:
      break;
  }
  return res;
}

static gboolean
gst_id3_tag_src_event (GstPad *pad, GstEvent *event)
{
  GstID3Tag *tag;

  tag = GST_ID3_TAG (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
      if (GST_EVENT_SEEK_FORMAT (event) == GST_FORMAT_BYTES && 
	  tag->state == GST_ID3_TAG_STATE_NORMAL && 
	  GST_PAD_PEER (tag->sinkpad)) {
	GstEvent *new;
	gint diff;

	switch (GST_EVENT_SEEK_METHOD (event)) {
	  case GST_SEEK_METHOD_SET: diff = tag->v2tag_size_new - tag->v2tag_size;
	  case GST_SEEK_METHOD_CUR: diff = 0;
	  case GST_SEEK_METHOD_END: diff = GST_EVENT_SEEK_OFFSET(event) ? tag->v1tag_size_new - tag->v1tag_size : 0;
	  default: g_assert_not_reached();
	}
	new = gst_event_new_seek (GST_EVENT_SEEK_TYPE (event), 
				  GST_EVENT_SEEK_OFFSET(event) + diff);
	gst_event_unref (event);
	return gst_pad_send_event (GST_PAD_PEER (tag->sinkpad), new);
      }
      break;
    default:
      break;
  }

  gst_event_unref (event);
  return FALSE;
}
static GstPadLinkReturn
gst_id3_tag_link_sink (GstPad *pad, GstCaps *caps)
{
  GstID3Tag *tag;

  tag = GST_ID3_TAG (gst_pad_get_parent (pad));

  if (!GST_CAPS_IS_FIXED (caps))
      return GST_PAD_LINK_DELAYED;
  
  if (!gst_caps_get_int (caps, "layer", &tag->layer))
    return GST_PAD_LINK_DELAYED;

  return GST_PAD_LINK_OK;
}
GstTagList*
gst_mad_id3_to_tag_list(const struct id3_tag *tag)
{
  const struct id3_frame *frame;
  const id3_ucs4_t *ucs4;
  id3_utf8_t *utf8;
  GstTagList *tag_list;
  guint i = 0;

  tag_list = gst_tag_list_new ();
  
  while ((frame = id3_tag_findframe(tag, NULL, i++)) != NULL) {
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

    field    = &frame->fields[1];
    nstrings = id3_field_getnstrings(field);
    
    for (j = 0; j < nstrings; ++j) {
      ucs4 = id3_field_getstrings(field, j);
      g_assert(ucs4);

      if (strcmp(id, ID3_FRAME_GENRE) == 0)
	ucs4 = id3_genre_name(ucs4);

      utf8 = id3_ucs4_utf8duplicate(ucs4);
      if (utf8 == 0)
	continue;

      /* be sure to add non-string tags here */
      switch (gst_tag_get_type (tag_name)) {
	case G_TYPE_UINT:
	{
	  guint tmp;
	  gchar *check;
	  tmp = strtoul (utf8, &check, 10);
	  if (*check != '\0') break;
	  if (strcmp (tag_name, GST_TAG_DATE) == 0) {
	    if (tmp == 0) break;
	    GDate *d = g_date_new_dmy (1, 1, tmp);
	    tmp = g_date_get_julian (d);
	    g_date_free (d);
	  }
	  gst_tag_list_add (tag_list, GST_TAG_MERGE_APPEND, tag_name, tmp, NULL);
	  break;
	}
	default:
	  g_assert (gst_tag_get_type (tag_name) == G_TYPE_STRING);
	  gst_tag_list_add (tag_list, GST_TAG_MERGE_APPEND, tag_name, utf8, NULL);
	  break;
      }
      free (utf8);
    }
    g_free (id);
  }
    
  return tag_list;
}
static void
tag_list_to_id3_tag_foreach (const GstTagList *list, const gchar *tag_name, gpointer user_data)
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

  frame = id3_frame_new (id);
  if (id3_tag_attachframe (tag, frame) != 0) {
    GST_WARNING ("could not attach frame (%s) to id3 tag", id);
    return;
  }
  field = id3_frame_field (frame, 1);
  g_assert (field);
  while (values-- > 0) {
    id3_ucs4_t *put;

    if (strcmp (tag_name, GST_TAG_DATE) == 0) {
      gchar *str;
      guint u;
      GDate *d;
      
      g_assert (gst_tag_list_get_uint_index (list, tag_name, values, &u));
      d = g_date_new_julian (u);
      str = g_strdup_printf ("%u", (guint) (g_date_get_year (d)));
      put = id3_utf8_ucs4duplicate (str);
      g_date_free (d);
      g_free (str);
    } else if (strcmp (tag_name, GST_TAG_TRACK_NUMBER) == 0) {
      gchar *str;
      guint u;
      
      g_assert (gst_tag_list_get_uint_index (list, tag_name, values, &u));
      str = g_strdup_printf ("%u", u);
      put = id3_utf8_ucs4duplicate (str);
      g_free (str);
    } else {
      gchar *str;

      if (gst_tag_get_type (tag_name) != G_TYPE_STRING) {
	GST_WARNING ("unhandled GStreamer tag %s", tag_name);
	return;
      }
      g_assert (gst_tag_list_get_string_index (list, tag_name, values, &str));
      put = id3_utf8_ucs4duplicate (str);
      g_free (str);
    }
    if (id3_field_addstring (field, put) != 0) {
      GST_WARNING ("could not add a string to id3 tag field");
      return;
    }
  }
}
struct id3_tag *
gst_mad_tag_list_to_id3_tag (GstTagList *list)
{
  struct id3_tag *tag;

  tag = id3_tag_new ();

  gst_tag_list_foreach (list, tag_list_to_id3_tag_foreach, tag);
  return tag;
}
static void
gst_id3_tag_handle_event (GstPad *pad, GstEvent *event)
{
  GstID3Tag *tag = GST_ID3_TAG (gst_pad_get_parent (pad));
  
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_DISCONTINUOUS:
      switch (tag->state) {
	case GST_ID3_TAG_STATE_READING_V2_TAG:
	  gst_element_error (GST_ELEMENT (tag), "Seek during ID3v2 tag reading");
	  break;
	case GST_ID3_TAG_STATE_SEEKING_TO_V1_TAG:
	  /* just assume it's the right seek for now */
	  tag->state = GST_ID3_TAG_STATE_READING_V1_TAG;
	  break;
	case GST_ID3_TAG_STATE_READING_V1_TAG:
	  gst_element_error (GST_ELEMENT (tag), "Seek during ID3v1 tag reading");
	  break;
	case GST_ID3_TAG_STATE_SEEKING_TO_NORMAL:
	  /* just assume it's the right seek for now */
	  tag->state = GST_ID3_TAG_STATE_NORMAL_START;
	  break;
	case GST_ID3_TAG_STATE_NORMAL_START:
	  GST_ERROR_OBJECT (tag, "tag event loss, FIXME");
	  tag->state = GST_ID3_TAG_STATE_NORMAL;
	  /* fall through */
	case GST_ID3_TAG_STATE_NORMAL: {
	  gint64 value;
	  GstEvent *new;
	  
	  if (gst_event_discont_get_value (event, GST_FORMAT_BYTES, &value)) {
	    value += tag->v1tag_size;
	    new = gst_event_new_discontinuous (GST_FORMAT_BYTES, value, 0);
	    gst_data_unref (GST_DATA (event));
	    gst_pad_push (tag->srcpad, GST_DATA (new));
	  } else {
	    gst_pad_event_default (pad, event);
	  }
	  break;
	}
	default:
	  g_assert_not_reached ();
      }
      break;
    case GST_EVENT_TAG:
      gst_tag_list_insert (tag->parsed_tags, gst_event_tag_get_list (event), GST_TAG_MERGE_PREPEND);
      gst_data_unref (GST_DATA (event));
      break;
    case GST_EVENT_EOS:
      if (tag->v1tag_render && !tag->parse_mode) {
	GstTagList *merged;
	struct id3_tag *id3;

	GST_LOG_OBJECT (tag, "rendering v1 tag after eos event");
	merged = gst_tag_list_merge (gst_tag_setter_get_list (GST_TAG_SETTER (tag)), tag->parsed_tags, 
		gst_tag_setter_get_merge_mode (GST_TAG_SETTER (tag)));
	if (merged) {
	  id3 = gst_mad_tag_list_to_id3_tag (merged);
	  if (id3) {
	    GstBuffer *tag_buffer;

	    id3_tag_options (id3, ID3_TAG_OPTION_ID3V1, ID3_TAG_OPTION_ID3V1);
	    tag_buffer = gst_buffer_new_and_alloc (128);
	    g_assert (128 == id3_tag_render (id3, tag_buffer->data));
	    gst_pad_push (tag->srcpad, GST_DATA (tag_buffer));
	    id3_tag_delete (id3);
	  }
	  gst_tag_list_free (merged);
	}
      }
      /* fall through */
    default:
      gst_pad_event_default (pad, event);
      break;
  }
  return;
}
static void
gst_id3_tag_chain (GstPad *pad, GstData *data)
{
  GstID3Tag *tag;
  GstBuffer *buffer;

  /* handle events */
  if (GST_IS_EVENT (data)) {
    gst_id3_tag_handle_event (pad, GST_EVENT (data));
    return;
  }
  buffer = GST_BUFFER (data);

  tag = GST_ID3_TAG (gst_pad_get_parent (pad));

  switch (tag->state) {
    case GST_ID3_TAG_STATE_READING_V2_TAG:
      if (tag->buffer) {
	tag->buffer = gst_buffer_merge (tag->buffer, buffer);
      } else {
	GstCaps *caps;
      /* do caps nego */
capsnego:
	if (tag->layer > 0) {
	  caps = GST_CAPS_NEW ("id3_tag_data_src", "audio/mpeg", "layer", GST_PROPS_INT (tag->layer));
	} else {
	  caps = GST_CAPS_NEW ("id3_tag_data_src", "audio/mpeg", NULL);
	}
	if (gst_pad_try_set_caps (tag->srcpad, caps) != GST_PAD_LINK_REFUSED) {
	  tag->parse_mode = FALSE;
	  GST_LOG_OBJECT (tag, "normal operation, using audio/mpeg output");
	} else {
	  if (gst_pad_try_set_caps (tag->srcpad, 
				    GST_CAPS_NEW ("id3_tag_tag_src", "application/x-gst-tags", NULL))
	      != GST_PAD_LINK_REFUSED) {
	    tag->parse_mode = TRUE;
	    GST_LOG_OBJECT (tag, "fast operation, just outputting tags");
	  } else {
	    GstCaps *caps = gst_pad_template_get_caps (GST_PAD_TEMPLATE_GET (id3_tag_src_template_factory));
	    if (gst_pad_recover_caps_error (tag->srcpad, caps)) {
	      goto capsnego;
	    } else {
	      return;
	    }
	  }
	}
	/* done caps nego */
	tag->buffer = buffer;
      }
      if (GST_BUFFER_SIZE (tag->buffer) < 10)
	return;
      if (tag->v2tag_size == 0) {
	tag->v2tag_size = id3_tag_query (GST_BUFFER_DATA (tag->buffer),
					 GST_BUFFER_SIZE (tag->buffer));
	/* no footers supported */
	if (tag->v2tag_size < 0)
	  tag->v2tag_size = 0;
      }
      if (tag->v2tag_size != 0) {
	if (GST_BUFFER_SIZE (tag->buffer) > tag->v2tag_size) {
	  struct id3_tag *v2tag;

	  v2tag = id3_tag_parse (GST_BUFFER_DATA (tag->buffer), 
				 GST_BUFFER_SIZE (tag->buffer));
	  if (v2tag) {
	    g_assert (tag->parsed_tags == NULL);
	    tag->parsed_tags = gst_mad_id3_to_tag_list (v2tag); 
	    id3_tag_delete (v2tag);
	  } else {
	    GST_WARNING_OBJECT (tag, "detected ID3v2 tag, but couldn't parse it");
	    tag->v2tag_size = 0;
	  }
	} else {
	  return;
	}
      }
      /* seek to ID3v1 tag */
      if (gst_pad_send_event (GST_PAD_PEER (tag->sinkpad),
			  gst_event_new_seek (GST_FORMAT_BYTES | GST_SEEK_METHOD_END | 
					      GST_SEEK_FLAG_FLUSH, -128))) {
	tag->state = GST_ID3_TAG_STATE_SEEKING_TO_V1_TAG;
      } else {
	GstBuffer *sub;
	
	tag->state = GST_ID3_TAG_STATE_NORMAL_START;
	sub = gst_buffer_create_sub (tag->buffer, tag->v2tag_size, 
				     GST_BUFFER_SIZE (tag->buffer) - tag->v2tag_size);
	gst_pad_push (tag->srcpad, GST_DATA (sub));
      }
      gst_data_unref (GST_DATA (tag->buffer));
      tag->buffer = NULL;
      return;
    case GST_ID3_TAG_STATE_SEEKING_TO_V1_TAG:
    case GST_ID3_TAG_STATE_SEEKING_TO_NORMAL:
      /* we're waiting for the seek to finish, just discard all the stuff */
      gst_data_unref (GST_DATA (buffer));
      return;
    case GST_ID3_TAG_STATE_READING_V1_TAG:
      if (tag->buffer) {
	tag->buffer = gst_buffer_merge (tag->buffer, buffer);
      } else {
	tag->buffer = buffer;
	tag->v1tag_offset = buffer->offset;
      }
      if (GST_BUFFER_SIZE (tag->buffer) < 128)
	return;
      g_assert (tag->v1tag_size == 0);
      tag->v1tag_size = id3_tag_query (GST_BUFFER_DATA (tag->buffer),
				       GST_BUFFER_SIZE (tag->buffer));
      if (tag->v1tag_size == 128) {
	struct id3_tag *v1tag;

	v1tag = id3_tag_parse (GST_BUFFER_DATA (tag->buffer), 
			       GST_BUFFER_SIZE (tag->buffer));
	if (v1tag) {
	  GstTagList *newtag;
	  
	  newtag = gst_mad_id3_to_tag_list (v1tag);
	  id3_tag_delete (v1tag);
	  if (tag->parsed_tags) {
	    gst_tag_list_insert (tag->parsed_tags, newtag,
				 tag->prefer_v1tag ? GST_TAG_MERGE_REPLACE : GST_TAG_MERGE_KEEP);
	    gst_tag_list_free (newtag);
	  } else {
	    tag->parsed_tags = newtag;
	  }
	} else {
	  GST_WARNING_OBJECT (tag, "detected ID3v1 tag, but couldn't parse it");
	  tag->v2tag_size = 0;
	}
      } else if (tag->v1tag_size != 0) {
	GST_WARNING_OBJECT (tag, "bad non-ID3v1 tag at end of file");
	tag->v1tag_size = 0;
      }
      gst_data_unref (GST_DATA (tag->buffer));
      tag->buffer = NULL;
      if (!tag->parse_mode) {
	/* seek to beginning */
	GST_LOG_OBJECT (tag, "seeking back to beginning");
	if (gst_pad_send_event (GST_PAD_PEER (tag->sinkpad),
				gst_event_new_seek (GST_FORMAT_BYTES | GST_SEEK_METHOD_SET | 
						    GST_SEEK_FLAG_FLUSH, tag->v2tag_size))) {
	  tag->state = GST_ID3_TAG_STATE_SEEKING_TO_NORMAL;
	} else {
	  gst_element_error (GST_ELEMENT (tag), "can't seek back to beginning from reading ID3v1 tag");
	}
      } else {
	if (tag->parsed_tags)
	  gst_element_found_tags_for_pad (GST_ELEMENT (tag), tag->srcpad, 0, 
				          gst_tag_list_copy (tag->parsed_tags));
	/* set eos, we're done parsing tags */
	GST_LOG_OBJECT (tag, "setting EOS after reading ID3v1 tag");
	tag->state = GST_ID3_TAG_STATE_NORMAL;
        gst_element_set_eos (GST_ELEMENT (tag));
	gst_pad_push (tag->srcpad, GST_DATA (gst_event_new (GST_EVENT_EOS)));	 
      }
      return;
    case GST_ID3_TAG_STATE_NORMAL_START:
      if (tag->parse_mode) {
	if (tag->parsed_tags)
	  gst_element_found_tags_for_pad (GST_ELEMENT (tag), tag->srcpad, 0, 
				          gst_tag_list_copy (tag->parsed_tags));
      } else {
	struct id3_tag *id3;
	GstTagList *merged;
	GstBuffer *tag_buffer;
	
	/* send event */
	if (tag->parsed_tags)
	  gst_element_found_tags (GST_ELEMENT (tag), tag->parsed_tags);
	/* render tag */
	tag->v2tag_size_new = 0;
	if (tag->v2tag_render) {
	  merged = gst_tag_list_merge (gst_tag_setter_get_list (GST_TAG_SETTER (tag)), tag->parsed_tags, 
		  gst_tag_setter_get_merge_mode (GST_TAG_SETTER (tag)));
	  if (merged) {
	    id3 = gst_mad_tag_list_to_id3_tag (merged);
	    if (id3) {
	      glong estimated;
	      estimated = id3_tag_render (id3, NULL);
	      tag_buffer = gst_buffer_new_and_alloc (estimated);
	      tag->v2tag_size_new = id3_tag_render (id3, GST_BUFFER_DATA (tag_buffer));
	      g_assert (estimated >= tag->v2tag_size_new);
	      GST_BUFFER_SIZE (tag_buffer) = tag->v2tag_size_new;
	      gst_pad_push (tag->srcpad, GST_DATA (tag_buffer));
	      id3_tag_delete (id3);
	    }
	    gst_tag_list_free (merged);
	  }
	}
      }
      /* fall through */
      tag->state = GST_ID3_TAG_STATE_NORMAL;
      tag->v1tag_size_new = (tag->v1tag_render && (tag->parsed_tags != NULL || 
		gst_tag_setter_get_list (GST_TAG_SETTER (tag)) != NULL)) ? 128 : 0;
    case GST_ID3_TAG_STATE_NORMAL:
      if (tag->parse_mode) {
	gst_element_set_eos (GST_ELEMENT (tag));
	gst_pad_push (tag->srcpad, GST_DATA (gst_event_new (GST_EVENT_EOS)));
      } else {
	if (buffer->offset >= tag->v1tag_offset) {
	  gst_data_unref (GST_DATA (buffer));
	  return;
	} else if (buffer->offset + buffer->size > tag->v1tag_offset) {
	  GstBuffer *sub = gst_buffer_create_sub (buffer, 0,
	                                          buffer->size - 128);
	  gst_data_unref (GST_DATA (buffer));
	  buffer = sub;
	}
	gst_pad_push (tag->srcpad, GST_DATA (buffer));
      }
      return;
  }
  g_assert_not_reached ();
}

static GstElementStateReturn
gst_id3_tag_change_state (GstElement *element)
{
  GstID3Tag *tag;

  tag = GST_ID3_TAG (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      g_assert (tag->parsed_tags == NULL);
      g_assert (tag->buffer == NULL);
      tag->v1tag_size = 0;
      tag->v1tag_offset = G_MAXUINT64;
      tag->v2tag_size = 0;
      tag->layer = 0;
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      /* do something to get out of the chain function faster */
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      if (tag->parsed_tags) {
	gst_tag_list_free (tag->parsed_tags);
	tag->parsed_tags = NULL;
      }
      if (tag->buffer) {
        gst_data_unref (GST_DATA (tag->buffer));
	tag->buffer = NULL;
      }
      break;
    case GST_STATE_READY_TO_NULL:
      break;
  }

  return parent_class->change_state (element);
}
