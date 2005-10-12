/* GStreamer APEv1/2 tag reader
 * Copyright (C) 2004 Ronald Bultje <rbultje@ronald.bitfreak.net>
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

#include <string.h>
#include <gst/gst.h>
#include <gst/bytestream/bytestream.h>

#include "apedemux.h"

GST_DEBUG_CATEGORY_STATIC (apedemux_debug);
#define GST_CAT_DEFAULT apedemux_debug

static GstStaticPadTemplate sink_templ = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-apetag")
    );

static GstStaticPadTemplate src_templ = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,          /* spider/decodebin hack */
    GST_STATIC_CAPS_ANY);

static void gst_ape_demux_base_init (GstApeDemuxClass * klass);
static void gst_ape_demux_class_init (GstApeDemuxClass * klass);
static void gst_ape_demux_init (GstApeDemux * ape);

static void gst_ape_demux_loop (GstElement * element);

static const GstEventMask *gst_ape_demux_get_event_mask (GstPad * pad);
static gboolean gst_ape_demux_handle_src_event (GstPad * pad, GstEvent * event);
static const GstFormat *gst_ape_demux_get_src_formats (GstPad * pad);
static const GstQueryType *gst_ape_demux_get_src_query_types (GstPad * pad);
static gboolean gst_ape_demux_handle_src_query (GstPad * pad,
    GstQueryType type, GstFormat * format, gint64 * value);

static GstStateChangeReturn gst_ape_demux_change_state (GstElement * element,
    GstStateChange transition);

static GstElementClass *parent_class = NULL;

GType
gst_ape_demux_get_type (void)
{
  static GType ape_demux_type = 0;

  if (!ape_demux_type) {
    static const GTypeInfo ape_demux_info = {
      sizeof (GstApeDemuxClass),
      (GBaseInitFunc) gst_ape_demux_base_init,
      NULL,
      (GClassInitFunc) gst_ape_demux_class_init,
      NULL,
      NULL,
      sizeof (GstApeDemux),
      0,
      (GInstanceInitFunc) gst_ape_demux_init,
    };

    ape_demux_type =
        g_type_register_static (GST_TYPE_ELEMENT,
        "GstApeDemux", &ape_demux_info, 0);
  }

  return ape_demux_type;
}

static void
gst_ape_demux_base_init (GstApeDemuxClass * klass)
{
  static GstElementDetails gst_ape_demux_details =
      GST_ELEMENT_DETAILS ("Ape tag reader",
      "Codec/Demuxer/Audio",
      "Reads APEv1/2 tags",
      "Ronald Bultje <rbultje@ronald.bitfreak.net>");
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_templ));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_templ));
  gst_element_class_set_details (element_class, &gst_ape_demux_details);
}

static void
gst_ape_demux_class_init (GstApeDemuxClass * klass)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (apedemux_debug, "apedemux",
      0, "Demuxer for APE tag reader");

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gstelement_class->change_state = gst_ape_demux_change_state;
}

static void
gst_ape_demux_init (GstApeDemux * ape)
{
  GST_OBJECT_FLAG_SET (ape, GST_ELEMENT_EVENT_AWARE);

  ape->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&sink_templ),
      "sink");
  gst_element_add_pad (GST_ELEMENT (ape), ape->sinkpad);

#if 0
  ape->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&src_templ),
      "src");
  gst_pad_set_formats_function (ape->srcpad, gst_ape_demux_get_src_formats);
  gst_pad_set_event_mask_function (ape->srcpad, gst_ape_demux_get_event_mask);
  gst_pad_set_event_function (ape->srcpad, gst_ape_demux_handle_src_event);
  gst_pad_set_query_type_function (ape->srcpad,
      gst_ape_demux_get_src_query_types);
  gst_pad_set_query_function (ape->srcpad, gst_ape_demux_handle_src_query);
  gst_pad_use_explicit_caps (ape->srcpad);
  gst_element_add_pad (GST_ELEMENT (ape), ape->srcpad);
#endif
  ape->srcpad = NULL;

  gst_element_set_loop_function (GST_ELEMENT (ape), gst_ape_demux_loop);

  ape->state = GST_APE_DEMUX_TAGREAD;
  ape->start_off = ape->end_off = 0;
}

static const GstFormat *
gst_ape_demux_get_src_formats (GstPad * pad)
{
  static const GstFormat formats[] = {
    GST_FORMAT_BYTES,
    0
  };

  return formats;
}

static const GstQueryType *
gst_ape_demux_get_src_query_types (GstPad * pad)
{
  static const GstQueryType types[] = {
    GST_QUERY_TOTAL,
    GST_QUERY_POSITION,
    0
  };

  return types;
}

static gboolean
gst_ape_demux_handle_src_query (GstPad * pad,
    GstQueryType type, GstFormat * format, gint64 * value)
{
  GstApeDemux *ape = GST_APE_DEMUX (gst_pad_get_parent (pad));
  gboolean res;

  res = gst_pad_query (GST_PAD_PEER (ape->sinkpad), type, format, value);
  if (!res)
    return FALSE;

  switch (type) {
    case GST_QUERY_TOTAL:
      switch (*format) {
        case GST_FORMAT_BYTES:
          *value -= (ape->start_off + ape->end_off);
          break;
        default:
          break;
      }
      break;
    case GST_QUERY_POSITION:
      switch (*format) {
        case GST_FORMAT_BYTES:
          *value -= ape->start_off;
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

static const GstEventMask *
gst_ape_demux_get_event_mask (GstPad * pad)
{
  static const GstEventMask masks[] = {
    {GST_EVENT_SEEK, GST_SEEK_METHOD_SET | GST_SEEK_FLAG_KEY_UNIT},
    {0,}
  };

  return masks;
}

static gboolean
gst_ape_demux_handle_src_event (GstPad * pad, GstEvent * event)
{
  GstApeDemux *ape = GST_APE_DEMUX (gst_pad_get_parent (pad));

  if (ape->state != GST_APE_DEMUX_IDENTITY)
    return FALSE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
      switch (GST_EVENT_SEEK_FORMAT (event)) {
        case GST_FORMAT_BYTES:{
          GstEvent *new;
          gint64 new_off;

          new_off = GST_EVENT_SEEK_OFFSET (event);
          new_off += ape->start_off;
          new = gst_event_new_seek (GST_EVENT_SEEK_TYPE (event), new_off);
          gst_event_unref (event);
          event = new;
          break;
        }
        default:
          break;
      }
      break;
    default:
      break;
  }

  return gst_pad_send_event (GST_PAD_PEER (ape->sinkpad), event);
}

/*
 * Handle an event during 'open' stage.
 */

static gboolean
gst_ape_demux_handle_event (GstApeDemux * ape, GstByteStream * bs)
{
  GstEvent *event;
  guint32 remaining;
  gboolean res = FALSE;

  gst_bytestream_get_status (bs, &remaining, &event);
  if (!event) {
    GST_ELEMENT_ERROR (ape, RESOURCE, READ, (NULL), (NULL));
    return FALSE;
  }

  switch (GST_EVENT_TYPE (event)) {
      /* this shouldn't happen. We definately can't deal with it. */
    case GST_EVENT_EOS:
    case GST_EVENT_INTERRUPT:
      GST_ELEMENT_ERROR (ape, RESOURCE, READ, (NULL),
          ("Cannot deal with EOS/interrupt events during init stage"));
      break;
    case GST_EVENT_DISCONTINUOUS:
    case GST_EVENT_FLUSH:
      /* we disregard those during init stage */
      res = TRUE;
      break;
    default:
      gst_pad_event_default (ape->sinkpad, event);
      return TRUE;
  }

  gst_event_unref (event);

  return res;
}

/*
 * Find media type. Simple for now.
 */

typedef struct _GstApeDemuxTypeFind
{
  GstApeDemux *ape;
  GstByteStream *bs;
  gboolean seekable;
  guint64 len;
  GstCaps *caps;
  guint probability;
  gboolean stop;
} GstApeDemuxTypeFind;

static guint8 *
gst_ape_demux_typefind_peek (gpointer ptr, gint64 offset, guint size)
{
  GstApeDemuxTypeFind *apetf = ptr;
  guint8 *data;

  /* non-seekable first - easy */
  if (!apetf->seekable || offset == 0) {
    /* don't seek outside reach */
    if (offset != 0 || size > apetf->len)
      return NULL;

    /* try to get data, fatal event *is* fatal for typefinding */
    while (gst_bytestream_peek_bytes (apetf->bs, &data, size) != size) {
      if (!gst_ape_demux_handle_event (apetf->ape, apetf->bs)) {
        apetf->stop = TRUE;
        return NULL;
      }
    }

    return data;
  }

  /* FIXME: theoretically we could embed mp3 and we'd like to seek
   * beyond just the beginnings then. */
  return NULL;
}

static guint64
gst_ape_demux_typefind_get_length (gpointer ptr)
{
  GstApeDemuxTypeFind *apetf = ptr;

  return apetf->len;
}

static void
gst_ape_demux_typefind_suggest (gpointer ptr,
    guint probability, const GstCaps * caps)
{
  GstApeDemuxTypeFind *apetf = ptr;

  GST_LOG ("Found type of mime %s, probability %u",
      gst_structure_get_name (gst_caps_get_structure (caps, 0)), probability);

  if (probability > apetf->probability) {
    if (apetf->caps)
      gst_caps_free (apetf->caps);
    apetf->caps = gst_caps_copy (caps);
    apetf->probability = probability;
  }
}

static gboolean
gst_ape_demux_typefind (GstApeDemux * ape,
    GstByteStream * bs, gboolean seekable)
{
  GstApeDemuxTypeFind apetf;
  GstTypeFind tf;
  GList *factories;

  GST_LOG ("Doing typefinding now");

  /* prepare */
  memset (&apetf, 0, sizeof (apetf));
  memset (&tf, 0, sizeof (tf));
  tf.peek = gst_ape_demux_typefind_peek;
  tf.suggest = gst_ape_demux_typefind_suggest;
  tf.data = &apetf;
  apetf.bs = bs;
  apetf.ape = ape;
  apetf.len = gst_bytestream_length (bs);
  if (apetf.len != (guint64) - 1) {
    apetf.len -= ape->start_off + ape->end_off;
    tf.get_length = gst_ape_demux_typefind_get_length;
  }
  apetf.seekable = seekable;

  /* run */
  for (factories = gst_type_find_factory_get_list ();
      factories != NULL && !apetf.stop &&
      apetf.probability < GST_TYPE_FIND_MAXIMUM; factories = factories->next) {
    gst_type_find_factory_call_function (factories->data, &tf);
  }

  /* fatal error */
  if (apetf.stop)
    return FALSE;

  /* type found? */
  if (!apetf.caps || apetf.probability < GST_TYPE_FIND_MINIMUM) {
    GST_ELEMENT_ERROR (ape, STREAM, TYPE_NOT_FOUND, (NULL), (NULL));
    return FALSE;
  }

  GST_LOG ("Done typefinding, found mime %s",
      gst_structure_get_name (gst_caps_get_structure (apetf.caps, 0)));

  ape->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&src_templ),
      "src");
  gst_pad_set_formats_function (ape->srcpad, gst_ape_demux_get_src_formats);
  gst_pad_set_event_mask_function (ape->srcpad, gst_ape_demux_get_event_mask);
  gst_pad_set_event_function (ape->srcpad, gst_ape_demux_handle_src_event);
  gst_pad_set_query_type_function (ape->srcpad,
      gst_ape_demux_get_src_query_types);
  gst_pad_set_query_function (ape->srcpad, gst_ape_demux_handle_src_query);
  gst_pad_use_explicit_caps (ape->srcpad);
  gst_pad_set_explicit_caps (ape->srcpad, apetf.caps);
  gst_element_add_pad (GST_ELEMENT (ape), ape->srcpad);

  return TRUE;
}

/*
 * Parse tags from a buffer.
 */

static GstTagList *
gst_ape_demux_parse_tags (GstApeDemux * ape, guint8 * data, gint size)
{
  GstTagList *taglist = gst_tag_list_new ();
  gboolean have_tag = FALSE;

  GST_LOG ("Reading tags from chunk of size %u bytes", size);

  /* get rid of header/footer */
  if (!memcmp (data, "APETAGEX", 8)) {
    data += 32;
    size -= 32;
  }
  if (!memcmp (data + size - 32, "APETAGEX", 8)) {
    size -= 32;
  }

  /* read actual tags - at least 10 bytes for tag header */
  while (size >= 10) {
    guint len, n = 8;
    gchar *tag, *val;
    const gchar *type = NULL;
    gboolean i = FALSE;

    /* find tag type and size */
    len = GST_READ_UINT32_LE (data);
    while (n < size && data[n] != 0x0)
      n++;
    if (n == size)
      break;
    g_assert (data[n] == 0x0);
    n++;
    if (size - n < len)
      break;

    /* read */
    tag = g_strndup (&data[8], n - 9);
    val = g_strndup (&data[n], len);
    if (!strcasecmp (tag, "title")) {
      type = GST_TAG_TITLE;
    } else if (!strcasecmp (tag, "artist")) {
      type = GST_TAG_ARTIST;
    } else if (!strcasecmp (tag, "album")) {
      type = GST_TAG_ALBUM;
    } else if (!strcasecmp (tag, "comment")) {
      type = GST_TAG_COMMENT;
    } else if (!strcasecmp (tag, "copyright")) {
      type = GST_TAG_COPYRIGHT;
    } else if (!strcasecmp (tag, "genre")) {
      type = GST_TAG_GENRE;
    } else if (!strcasecmp (tag, "isrc")) {
      type = GST_TAG_ISRC;
    } else if (!strcasecmp (tag, "track")) {
      type = GST_TAG_TRACK_NUMBER;
      i = TRUE;
    }
    if (type) {
      GValue v = { 0 };

      if (i) {
        g_value_init (&v, G_TYPE_INT);
        g_value_set_int (&v, atoi (val));
      } else {
        g_value_init (&v, G_TYPE_STRING);
        g_value_set_string (&v, val);
      }
      gst_tag_list_add_values (taglist, GST_TAG_MERGE_APPEND, type, &v, NULL);
      g_value_unset (&v);
      have_tag = TRUE;
    }
    GST_DEBUG ("Read tag %s: %s", tag, val);
    g_free (tag);
    g_free (val);

    /* move data pointer */
    size -= len + n;
    data += len + n;
  }

  /* let people know */
  if (have_tag) {
    gst_element_found_tags (GST_ELEMENT (ape), taglist);
    /* we'll push it over the srcpad later */
  } else {
    gst_tag_list_free (taglist);
    taglist = NULL;
  }

  return taglist;
}

/*
 * "Open" a APEv1/2 file.
 */

static gboolean
gst_ape_demux_stream_init (GstApeDemux * ape)
{
  GstByteStream *bs;
  gboolean seekable = TRUE, res = TRUE;
  guint8 *data;
  guint32 size = 0;
  GstTagList *taglist1 = NULL, *taglist2 = NULL, *taglist = NULL;

  GST_LOG ("Initializing stream, stripping tags");

  /* start off, we'll want byte-reading here */
  bs = gst_bytestream_new (ape->sinkpad);

  /* peek one byte to not confuse the typefinder */
  while (gst_bytestream_peek_bytes (bs, &data, 1) != 1) {
    if (!gst_ape_demux_handle_event (ape, bs)) {
      res = FALSE;
      goto the_city;
    }
  }

  /* can we seek? */
  if (!gst_bytestream_seek (bs, 0, GST_SEEK_METHOD_END)) {
    seekable = FALSE;
  } else {
    if (!gst_bytestream_seek (bs, 0, GST_SEEK_METHOD_SET)) {
      GST_ELEMENT_ERROR (ape, RESOURCE, SEEK, (NULL),
          ("Couldn't seek back to start - cannot handle that"));
      res = FALSE;
      goto the_city;
    }
  }

  /* ape tags at start? */
  while (gst_bytestream_peek_bytes (bs, &data, 32) != 32) {
    if (!gst_ape_demux_handle_event (ape, bs)) {
      res = FALSE;
      goto the_city;
    }
  }
  if (!memcmp (data, "APETAGEX", 8)) {
    GST_LOG ("Found tags at start");

    /* APEv2 at start of file - note that the flags are useless because
     * I have yet to see the first writer that writes correct HAS_HEADER
     * and HAS_FOOTER flags... So we detect it ourselves. */
    size = GST_READ_UINT32_LE (data + 12);

    /* Size is without the header and with the footer. So add 32 because
     * we're still at position 0 here (peek != read). */
    size += 32;
    while (gst_bytestream_peek_bytes (bs, &data, size) != size) {
      if (!gst_ape_demux_handle_event (ape, bs)) {
        res = FALSE;
        goto the_city;
      }
    }
    taglist1 = gst_ape_demux_parse_tags (ape, data, size);
    ape->start_off = size;
  }

  /* if we're not seekable, then this is it already. Flush the tags,
   * and forward the rest of the data to the next element. */
  if (!seekable) {
    if (size != 0)
      gst_bytestream_flush_fast (bs, size);

    if (!gst_ape_demux_typefind (ape, bs, FALSE)) {
      res = FALSE;
      goto the_city;
    }

    gst_bytestream_get_status (bs, &size, NULL);
    if (size) {
      GstBuffer *buf = NULL;

      gst_bytestream_read (bs, &buf, size);
      g_assert (buf);
      gst_pad_push (ape->srcpad, GST_DATA (buf));
    }

    goto the_city;
  }

  /* now look for tags at the end */
  if (!gst_bytestream_seek (bs, -32, GST_SEEK_METHOD_END)) {
    GST_ELEMENT_ERROR (ape, RESOURCE, SEEK, (NULL), (NULL));
    res = FALSE;
    goto the_city;
  }
  while (gst_bytestream_peek_bytes (bs, &data, 32) != 32) {
    if (!gst_ape_demux_handle_event (ape, bs)) {
      res = FALSE;
      goto the_city;
    }
  }

  if (!memcmp (data, "APETAGEX", 8)) {
    GST_LOG ("Found tags at end");

    /* APEv1/2 at start of file - note that the flags are useless because
     * I have yet to see the first writer that writes correct HAS_HEADER
     * and HAS_FOOTER flags... So we detect it ourselves. */
    size = GST_READ_UINT32_LE (data + 12);

    /* size is without header, so add 32 to detect that. */
    size += 32;
    if (!gst_bytestream_seek (bs, -(gint64) size, GST_SEEK_METHOD_END)) {
      GST_ELEMENT_ERROR (ape, RESOURCE, SEEK, (NULL), (NULL));
      res = FALSE;
      goto the_city;
    }
    while (gst_bytestream_peek_bytes (bs, &data, size) != size) {
      if (!gst_ape_demux_handle_event (ape, bs)) {
        res = FALSE;
        goto the_city;
      }
    }
    if (memcmp (data, "APETAGEX", 8) != 0) {
      data += 32;
      size -= 32;
    }
    taglist2 = gst_ape_demux_parse_tags (ape, data, size);
    ape->end_off = size;
  }

  /* seek back to beginning */
  if (!gst_bytestream_seek (bs, ape->start_off, GST_SEEK_METHOD_SET)) {
    GST_ELEMENT_ERROR (ape, RESOURCE, SEEK, (NULL), (NULL));
    res = FALSE;
    goto the_city;
  }

  /* get any events */
  while (gst_bytestream_peek_bytes (bs, &data, 1) != 1) {
    if (!gst_ape_demux_handle_event (ape, bs)) {
      res = FALSE;
      goto the_city;
    }
  }

  /* typefind */
  if (!gst_ape_demux_typefind (ape, bs, TRUE)) {
    res = FALSE;
    goto the_city;
  }

  /* push any leftover data */
  gst_bytestream_get_status (bs, &size, NULL);
  if (size) {
    GstBuffer *buf = NULL;

    gst_bytestream_read (bs, &buf, size);
    g_assert (buf);
    gst_pad_push (ape->srcpad, GST_DATA (buf));
  }

the_city:
  /* become rich & famous */
  gst_bytestream_destroy (bs);
  if (taglist1 || taglist2) {
    if (res) {
      /* merge */
      if (taglist1 && taglist2) {
        taglist = gst_tag_list_merge (taglist1, taglist2,
            GST_TAG_MERGE_REPLACE);
        gst_tag_list_free (taglist1);
        gst_tag_list_free (taglist2);
      } else {
        taglist = taglist1 ? taglist1 : taglist2;
      }
      gst_pad_push (ape->srcpad, GST_DATA (gst_event_new_tag (taglist)));
    } else {
      if (taglist1)
        gst_tag_list_free (taglist1);
      if (taglist2)
        gst_tag_list_free (taglist2);
    }
  }

  return res;
}

/*
 * Forward one buffer (we're an identity here).
 */

static void
gst_ape_demux_stream_data (GstApeDemux * ape)
{
  GstData *data;

  data = gst_pad_pull (ape->sinkpad);

  if (GST_IS_EVENT (data)) {
    GstEvent *event = GST_EVENT (data);

    switch (GST_EVENT_TYPE (event)) {
      case GST_EVENT_DISCONTINUOUS:{
        GstEvent *new;
        gint64 new_off = ape->start_off;

        gst_event_discont_get_value (event, GST_FORMAT_BYTES, &new_off);
        new_off -= ape->start_off;
        new = gst_event_new_discontinuous (GST_EVENT_DISCONT_NEW_MEDIA (event),
            GST_FORMAT_BYTES, new_off, GST_FORMAT_UNDEFINED);
        gst_event_unref (event);
        event = new;
        data = GST_DATA (new);
        break;
      }
      default:
        break;
    }

    gst_pad_event_default (ape->sinkpad, event);

    return;
  } else {
    GstBuffer *buf = GST_BUFFER (data), *kid;
    gint64 pos, len;
    GstFormat fmt = GST_FORMAT_BYTES;

    kid = gst_buffer_create_sub (buf, 0, GST_BUFFER_SIZE (buf));
    GST_BUFFER_OFFSET (kid) -= ape->start_off;
    gst_buffer_unref (buf);
    data = GST_DATA (kid);

    /* if the plugin allows us to, see if we're close to eos */
    if (gst_pad_query (GST_PAD_PEER (ape->sinkpad),
            GST_QUERY_POSITION, &fmt, &pos) &&
        gst_pad_query (GST_PAD_PEER (ape->sinkpad),
            GST_QUERY_TOTAL, &fmt, &len)) {
      if (pos > len - ape->end_off) {
        if (pos - GST_BUFFER_SIZE (buf) >= len - ape->end_off) {
          gst_buffer_unref (kid);
          data = NULL;
        } else {
          GST_BUFFER_SIZE (kid) -= ape->end_off - (len - pos);
        }
      }
    }
  }

  if (data)
    gst_pad_push (ape->srcpad, data);
}

static void
gst_ape_demux_loop (GstElement * element)
{
  GstApeDemux *ape = GST_APE_DEMUX (element);

  switch (ape->state) {
    case GST_APE_DEMUX_TAGREAD:
      if (!gst_ape_demux_stream_init (ape))
        return;
      GST_LOG ("From now on, we're in identity mode");
      ape->state = GST_APE_DEMUX_IDENTITY;
      break;

    case GST_APE_DEMUX_IDENTITY:
      gst_ape_demux_stream_data (ape);
      break;

    default:
      g_assert (0);
  }
}

static GstStateChangeReturn
gst_ape_demux_change_state (GstElement * element, GstStateChange transition)
{
  GstApeDemux *ape = GST_APE_DEMUX (element);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (ape->srcpad) {
        gst_element_remove_pad (element, ape->srcpad);
        ape->srcpad = NULL;
      }
      ape->state = GST_APE_DEMUX_TAGREAD;
      ape->start_off = ape->end_off = 0;
      break;
    default:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  return GST_STATE_CHANGE_SUCCESS;
}
