/* GStreamer
 * Copyright (C) 2003 Benjamin Otte <in7y118@public.uni-hamburg.de>
 *
 * gstvorbistagsetter.c: plugin for reading / modifying vorbis tags
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
#include <gst/gsttaginterface.h>
#include "gsttageditingprivate.h"
#include <stdlib.h>
#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_vorbis_tag_debug);
#define GST_CAT_DEFAULT gst_vorbis_tag_debug

#define GST_TYPE_VORBIS_TAG (gst_vorbis_tag_get_type())
#define GST_VORBIS_TAG(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VORBIS_TAG, GstVorbisTag))
#define GST_VORBIS_TAG_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VORBIS_TAG, GstVorbisTag))
#define GST_IS_VORBIS_TAG(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VORBIS_TAG))
#define GST_IS_VORBIS_TAG_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VORBIS_TAG))

typedef struct _GstVorbisTag GstVorbisTag;
typedef struct _GstVorbisTagClass GstVorbisTagClass;

struct _GstVorbisTag
{
  GstElement element;

  /* pads */
  GstPad *sinkpad;
  GstPad *srcpad;

  /* output mode */
  guint output;
};

struct _GstVorbisTagClass
{
  GstElementClass parent_class;
};

/* elementfactory information */
static GstElementDetails gst_vorbis_tag_details =
GST_ELEMENT_DETAILS ("vorbis tag extractor",
    "Tag",
    "Extract tagging information from vorbis streams",
    "Benjamin Otte <in7y118@public.uni-hamburg.de>");

enum
{
  OUTPUT_UNKNOWN,
  OUTPUT_TAGS,
  OUTPUT_DATA
};

/* signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

static GstStaticPadTemplate gst_vorbis_tag_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-vorbis; application/x-gst-tags")
    );

static GstStaticPadTemplate gst_vorbis_tag_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-vorbis")
    );


static void gst_vorbis_tag_base_init (gpointer g_class);
static void gst_vorbis_tag_class_init (gpointer g_class, gpointer class_data);
static void gst_vorbis_tag_init (GTypeInstance * instance, gpointer g_class);

static void gst_vorbis_tag_chain (GstPad * pad, GstData * data);

static GstElementStateReturn gst_vorbis_tag_change_state (GstElement * element);

static GstElementClass *parent_class = NULL;

/* static guint gst_vorbis_tag_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_vorbis_tag_get_type (void)
{
  static GType vorbis_tag_type = 0;

  if (!vorbis_tag_type) {
    static const GTypeInfo vorbis_tag_info = {
      sizeof (GstVorbisTagClass),
      gst_vorbis_tag_base_init,
      NULL,
      gst_vorbis_tag_class_init,
      NULL,
      NULL,
      sizeof (GstVorbisTag),
      0,
      gst_vorbis_tag_init,
    };
    static const GInterfaceInfo tag_setter_info = {
      NULL,
      NULL,
      NULL
    };

    vorbis_tag_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstVorbisTag",
        &vorbis_tag_info, 0);

    g_type_add_interface_static (vorbis_tag_type, GST_TYPE_TAG_SETTER,
        &tag_setter_info);

    GST_DEBUG_CATEGORY_INIT (gst_vorbis_tag_debug, "vorbistag", 0,
        "vorbis tagging element");
  }
  return vorbis_tag_type;
}
static void
gst_vorbis_tag_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &gst_vorbis_tag_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_vorbis_tag_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_vorbis_tag_src_template));
}
static void
gst_vorbis_tag_class_init (gpointer g_class, gpointer class_data)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  parent_class = g_type_class_peek_parent (g_class);

  gstelement_class->change_state = gst_vorbis_tag_change_state;
}
static void
gst_vorbis_tag_init (GTypeInstance * instance, gpointer g_class)
{
  GstVorbisTag *tag = GST_VORBIS_TAG (instance);

  /* create the sink and src pads */
  tag->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_vorbis_tag_sink_template), "sink");
  gst_element_add_pad (GST_ELEMENT (tag), tag->sinkpad);
  gst_pad_set_chain_function (tag->sinkpad,
      GST_DEBUG_FUNCPTR (gst_vorbis_tag_chain));

  tag->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_vorbis_tag_src_template), "src");
  gst_element_add_pad (GST_ELEMENT (tag), tag->srcpad);
}
static GstTagEntryMatch tag_matches[] = {
  {GST_TAG_TITLE, "TITLE"},
  {GST_TAG_VERSION, "VERSION"},
  {GST_TAG_ALBUM, "ALBUM"},
  {GST_TAG_TRACK_NUMBER, "TRACKNUMBER"},
  {GST_TAG_ALBUM_VOLUME_NUMBER, "DISCNUMBER"},
  {GST_TAG_TRACK_COUNT, "TRACKTOTAL"},
  {GST_TAG_ALBUM_VOLUME_COUNT, "DISCTOTAL"},
  {GST_TAG_ARTIST, "ARTIST"},
  {GST_TAG_PERFORMER, "PERFORMER"},
  {GST_TAG_COPYRIGHT, "COPYRIGHT"},
  {GST_TAG_LICENSE, "LICENSE"},
  {GST_TAG_ORGANIZATION, "ORGANIZATION"},
  {GST_TAG_DESCRIPTION, "DESCRIPTION"},
  {GST_TAG_GENRE, "GENRE"},
  {GST_TAG_DATE, "DATE"},
  {GST_TAG_CONTACT, "CONTACT"},
  {GST_TAG_ISRC, "ISRC"},
  {GST_TAG_COMMENT, "COMMENT"},
  {GST_TAG_TRACK_GAIN, "REPLAYGAIN_TRACK_GAIN"},
  {GST_TAG_TRACK_PEAK, "REPLAYGAIN_TRACK_PEAK"},
  {GST_TAG_ALBUM_GAIN, "REPLAYGAIN_ALBUM_GAIN"},
  {GST_TAG_ALBUM_PEAK, "REPLAYGAIN_ALBUM_PEAK"},
  {NULL, NULL}
};

/**
 * gst_tag_from_vorbis_tag:
 * @vorbis_tag: vorbiscomment tag to convert to GStreamer tag
 *
 * Looks up the GStreamer tag for a vorbiscommment tag.
 *
 * Returns: The corresponding GStreamer tag or NULL if none exists.
 */
G_CONST_RETURN gchar *
gst_tag_from_vorbis_tag (const gchar * vorbis_tag)
{
  int i = 0;
  gchar *real_vorbis_tag;

  g_return_val_if_fail (vorbis_tag != NULL, NULL);

  real_vorbis_tag = g_ascii_strup (vorbis_tag, -1);
  while (tag_matches[i].gstreamer_tag != NULL) {
    if (strcmp (real_vorbis_tag, tag_matches[i].original_tag) == 0) {
      break;
    }
    i++;
  }
  g_free (real_vorbis_tag);
  return tag_matches[i].gstreamer_tag;
}

/**
 * gst_tag_to_vorbis_tag:
 * @gst_tag: GStreamer tag to convert to vorbiscomment tag
 *
 * Looks up the vorbiscommment tag for a GStreamer tag.
 *
 * Returns: The corresponding vorbiscommment tag or NULL if none exists.
 */
G_CONST_RETURN gchar *
gst_tag_to_vorbis_tag (const gchar * gst_tag)
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


void
gst_vorbis_tag_add (GstTagList * list, const gchar * tag, const gchar * value)
{
  const gchar *gst_tag = gst_tag_from_vorbis_tag (tag);

  if (gst_tag == NULL)
    return;

  switch (gst_tag_get_type (gst_tag)) {
    case G_TYPE_UINT:
      if (strcmp (gst_tag, GST_TAG_DATE) == 0) {
        GDate *date;
        guint y, d = 1, m = 1;
        gchar *check = (gchar *) value;

        y = strtoul (check, &check, 10);
        if (*check == '-') {
          check++;
          m = strtoul (check, &check, 10);
          if (*check == '-') {
            check++;
            d = strtoul (check, &check, 10);
          }
        }
        if (*check != '\0')
          break;
        if (y == 0)
          break;
        date = g_date_new_dmy (d, m, y);
        y = g_date_get_julian (date);
        g_date_free (date);
        gst_tag_list_add (list, GST_TAG_MERGE_APPEND, gst_tag, y, NULL);
        break;
      } else {
        guint tmp;
        gchar *check;
        gboolean is_track_number_tag;
        gboolean is_disc_number_tag;

        is_track_number_tag = (strcmp (gst_tag, GST_TAG_TRACK_NUMBER) == 0);
        is_disc_number_tag =
            (strcmp (gst_tag, GST_TAG_ALBUM_VOLUME_NUMBER) == 0);
        tmp = strtoul (value, &check, 10);
        if (*check == '/' && (is_track_number_tag || is_disc_number_tag)) {
          guint count;

          check++;
          count = strtoul (check, &check, 10);
          if (*check != '\0' || count == 0)
            break;
          if (is_track_number_tag) {
            gst_tag_list_add (list, GST_TAG_MERGE_APPEND, GST_TAG_TRACK_COUNT,
                count, NULL);
          } else {
            gst_tag_list_add (list, GST_TAG_MERGE_APPEND,
                GST_TAG_ALBUM_VOLUME_COUNT, count, NULL);
          }
        }
        if (*check != '\0')
          break;
        gst_tag_list_add (list, GST_TAG_MERGE_APPEND, gst_tag, tmp, NULL);
      }
      break;
    case G_TYPE_STRING:
      gst_tag_list_add (list, GST_TAG_MERGE_APPEND, gst_tag, value, NULL);
      break;
    case G_TYPE_DOUBLE:
      gst_tag_list_add (list, GST_TAG_MERGE_APPEND, gst_tag, g_strtod (value,
              NULL), NULL);
      break;
    default:
      break;
  }
}

/**
 * gst_tag_list_from_vorbiscomment_buffer:
 * @buffer: buffer to convert
 * @id_data: identification data at start of stream
 * @id_data_length: length of identification data
 * @ vendor_string: pointer to a string that should take the vendor string of this
 *		    vorbis comment or NULL if you don't need it.
 *
 * Creates a new tag list that contains the information parsed out of a 
 * vorbiscomment packet.
 *
 * Returns: A new #GstTagList with all tags that could be extracted from the 
 *	    given vorbiscomment buffer or NULL on error.
 */
GstTagList *
gst_tag_list_from_vorbiscomment_buffer (const GstBuffer * buffer,
    const guint8 * id_data, const guint id_data_length, gchar ** vendor_string)
{
#define ADVANCE(x) G_STMT_START{						\
  data += x;									\
  size -= x;									\
  if (size < 4) goto error;							\
  cur_size = GST_READ_UINT32_LE (data);						\
  data += 4;									\
  size -= 4;									\
  if (cur_size > size) goto error;						\
  cur = data;									\
}G_STMT_END
  gchar *cur, *value;
  guint cur_size;
  guint iterations;
  guint8 *data;
  guint size;
  GstTagList *list;

  g_return_val_if_fail (GST_IS_BUFFER (buffer), NULL);
  g_return_val_if_fail (id_data != NULL, NULL);
  g_return_val_if_fail (id_data_length > 0, NULL);

  data = GST_BUFFER_DATA (buffer);
  size = GST_BUFFER_SIZE (buffer);
  list = gst_tag_list_new ();

  if (size < 11)
    goto error;
  if (memcmp (data, id_data, id_data_length) != 0)
    goto error;
  ADVANCE (id_data_length);
  if (vendor_string)
    *vendor_string = g_strndup (cur, cur_size);
  ADVANCE (cur_size);
  iterations = cur_size;
  cur_size = 0;
  while (iterations) {
    ADVANCE (cur_size);
    iterations--;
    cur = g_strndup (cur, cur_size);
    value = strchr (cur, '=');
    if (value == NULL) {
      g_free (cur);
      continue;
    }
    *value = '\0';
    value++;
    if (!g_utf8_validate (value, -1, NULL)) {
      g_free (cur);
      continue;
    }
    gst_vorbis_tag_add (list, cur, value);
    g_free (cur);
  }

  return list;

error:
  gst_tag_list_free (list);
  return NULL;
#undef ADVANCE
}
typedef struct
{
  guint count;
  guint data_count;
  GList *entries;
}
MyForEach;

GList *
gst_tag_to_vorbis_comments (const GstTagList * list, const gchar * tag)
{
  gchar *result;
  GList *l = NULL;
  guint i;
  const gchar *vorbis_tag = gst_tag_to_vorbis_tag (tag);

  if (!vorbis_tag)
    return NULL;
  for (i = 0; i < gst_tag_list_get_tag_size (list, tag); i++) {
    switch (gst_tag_get_type (tag)) {
      case G_TYPE_UINT:
        if (strcmp (tag, GST_TAG_DATE) == 0) {
          GDate *date;
          guint u;

          g_assert (gst_tag_list_get_uint_index (list, tag, i, &u));
          date = g_date_new_julian (u);
          /* vorbis suggests using ISO date formats */
          result =
              g_strdup_printf ("%s=%04d-%02d-%02d", vorbis_tag,
              (gint) g_date_get_year (date), (gint) g_date_get_month (date),
              (gint) g_date_get_day (date));
          g_date_free (date);
        } else {
          guint u;

          g_assert (gst_tag_list_get_uint_index (list, tag, i, &u));
          result = g_strdup_printf ("%s=%u", vorbis_tag, u);
        }
        break;
      case G_TYPE_STRING:{
        gchar *str;

        g_assert (gst_tag_list_get_string_index (list, tag, i, &str));
        result = g_strdup_printf ("%s=%s", vorbis_tag, str);
        break;
      }
      case G_TYPE_DOUBLE:{
        gdouble value;

        g_assert (gst_tag_list_get_double_index (list, tag, i, &value));
        result = g_strdup_printf ("%s=%f", vorbis_tag, value);
      }
      default:
        GST_DEBUG ("Couldn't write tag %s", tag);
        continue;
    }
    l = g_list_prepend (l, result);
  }

  return g_list_reverse (l);
}

static void
write_one_tag (const GstTagList * list, const gchar * tag, gpointer user_data)
{
  MyForEach *data = (MyForEach *) user_data;
  GList *comments;
  GList *it;

  comments = gst_tag_to_vorbis_comments (list, tag);

  for (it = comments; it != NULL; it = it->next) {
    gchar *result = it->data;

    data->count++;
    data->data_count += strlen (result);
    data->entries = g_list_prepend (data->entries, result);
  }
}

/**
 * gst_tag_list_to_vorbiscomment_buffer:
 * @buffer: tag list to convert
 * @id_data: identification data at start of stream
 * @id_data_length: length of identification data
 * @ vendor_string: string that describes the vendor string or NULL
 *
 * Creates a new vorbiscomment buffer from a tag list.
 *
 * Returns: A new #GstBuffer containing a vorbiscomment buffer with all tags that 
 *	    could be converted from the given tag list.
 */
GstBuffer *
gst_tag_list_to_vorbiscomment_buffer (const GstTagList * list,
    const guint8 * id_data, const guint id_data_length,
    const gchar * vendor_string)
{
  GstBuffer *buffer;
  guint8 *data;
  guint i;
  GList *l;
  MyForEach my_data = { 0, 0, NULL };
  guint vendor_len;
  int required_size;

  g_return_val_if_fail (GST_IS_TAG_LIST (list), NULL);
  g_return_val_if_fail (id_data != NULL, NULL);
  g_return_val_if_fail (id_data_length > 0, NULL);

  if (vendor_string == NULL)
    vendor_string = "GStreamer encoded vorbiscomment";
  vendor_len = strlen (vendor_string);
  required_size = id_data_length + 4 + vendor_len + 4 + 1;
  gst_tag_list_foreach ((GstTagList *) list, write_one_tag, &my_data);
  required_size += 4 * my_data.count + my_data.data_count;
  buffer = gst_buffer_new_and_alloc (required_size);
  data = GST_BUFFER_DATA (buffer);
  memcpy (data, id_data, id_data_length);
  data += id_data_length;
  *((guint32 *) data) = GUINT32_TO_LE (vendor_len);
  data += 4;
  memcpy (data, vendor_string, vendor_len);
  data += vendor_len;
  l = my_data.entries = g_list_reverse (my_data.entries);
  *((guint32 *) data) = GUINT32_TO_LE (my_data.count);
  data += 4;
  for (i = 0; i < my_data.count; i++) {
    guint size;
    gchar *cur;

    g_assert (l != NULL);
    cur = l->data;
    l = g_list_next (l);
    size = strlen (cur);
    *((guint32 *) data) = GUINT32_TO_LE (size);
    data += 4;
    memcpy (data, cur, size);
    data += size;
  }
  g_list_free (my_data.entries);
  *data = 1;

  return buffer;
}
static void
gst_vorbis_tag_chain (GstPad * pad, GstData * data)
{
  GstVorbisTag *tag;
  GstBuffer *buffer;

  buffer = GST_BUFFER (data);
  tag = GST_VORBIS_TAG (gst_pad_get_parent (pad));

  if (tag->output == OUTPUT_UNKNOWN) {
    /* caps nego */
    do {
      if (gst_pad_try_set_caps (tag->srcpad,
              gst_caps_new_simple ("audio/x-vorbis", NULL)) >= 0) {
        tag->output = OUTPUT_DATA;
      } else if (gst_pad_try_set_caps (tag->srcpad,
              gst_caps_new_simple ("application/x-gst-tags", NULL)) >= 0) {
        tag->output = OUTPUT_TAGS;
      } else {
        const GstCaps *caps =
            gst_static_caps_get (&gst_vorbis_tag_src_template.static_caps);
        if (gst_pad_recover_caps_error (tag->srcpad, caps))
          continue;
        return;
      }
    } while (FALSE);
  }

  if (GST_BUFFER_SIZE (buffer) == 0)
    GST_ELEMENT_ERROR (tag, CORE, TAG, (NULL),
        ("empty buffers are not allowed in vorbis data"));

  if (GST_BUFFER_DATA (buffer)[0] == 3) {
    gchar *vendor;
    GstTagList *list =
        gst_tag_list_from_vorbiscomment_buffer (buffer, "\003vorbis", 7,
        &vendor);

    gst_data_unref (data);
    if (list == NULL) {
      GST_ELEMENT_ERROR (tag, CORE, TAG, (NULL),
          ("invalid data in vorbis comments"));
      return;
    }
    gst_element_found_tags_for_pad (GST_ELEMENT (tag), tag->srcpad, 0,
        gst_tag_list_copy (list));
    gst_tag_list_merge (list, gst_tag_setter_get_list (GST_TAG_SETTER (tag)),
        gst_tag_setter_get_merge_mode (GST_TAG_SETTER (tag)));
    data =
        GST_DATA (gst_tag_list_to_vorbiscomment_buffer (list, "\003vorbis", 7,
            vendor));
    gst_tag_list_free (list);
    g_free (vendor);
  }

  if (tag->output == OUTPUT_DATA) {
    gst_pad_push (tag->srcpad, data);
  } else {
    gst_data_unref (data);
  }
}
static GstElementStateReturn
gst_vorbis_tag_change_state (GstElement * element)
{
  GstVorbisTag *tag;

  tag = GST_VORBIS_TAG (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      /* do something to get out of the chain function faster */
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      tag->output = OUTPUT_UNKNOWN;
      break;
    case GST_STATE_READY_TO_NULL:
      break;
  }

  return parent_class->change_state (element);
}
