/* GStreamer
 * Copyright (C) 2003 Benjamin Otte <in7y118@public.uni-hamburg.de>
 *
 * gstoggdemux.c: ogg stream demuxer
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
#include <gst/gst.h>
#include <ogg/ogg.h>
/* memcpy - if someone knows a way to get rid of it, please speak up 
 * note: the ogg docs even say you need this... */
#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_ogg_demux_debug);
#define GST_CAT_DEFAULT gst_ogg_demux_debug

#define GST_TYPE_OGG_DEMUX (gst_ogg_demux_get_type())
#define GST_OGG_DEMUX(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_OGG_DEMUX, GstOggDemux))
#define GST_OGG_DEMUX_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_OGG_DEMUX, GstOggDemux))
#define GST_IS_OGG_DEMUX(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_OGG_DEMUX))
#define GST_IS_OGG_DEMUX_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_OGG_DEMUX))

typedef struct _GstOggDemux GstOggDemux;
typedef struct _GstOggDemuxClass GstOggDemuxClass;

typedef enum
{
  /* just because you shouldn't make a valid enum value 0 */
  GST_OGG_STATE_INAVLID,
  /* just started, we need to decide if we should do setup */
  GST_OGG_STATE_START,
  /* setup is analyzing the stream, getting lengths and so on */
  GST_OGG_STATE_SETUP,
  /* after a seek, during resyncing */
  GST_OGG_STATE_SEEK,
  /* normal playback */
  GST_OGG_STATE_PLAY
}
GstOggState;

/* all information needed for one ogg stream */
typedef struct
{
  GstPad *pad;                  /* reference for this pad is held by element we belong to */

  gint serial;
  ogg_stream_state stream;
  guint64 offset;               /* end offset of last buffer */
  guint64 known_offset;         /* last known offset */
  gint64 packetno;              /* number of next expected packet */

  guint64 length;               /* length of stream or 0 */
  glong pages;                  /* number of pages in stream or 0 */

  guint flags;
}
GstOggPad;

typedef enum
{
  GST_OGG_PAD_NEEDS_DISCONT = (1 << 0),
  GST_OGG_PAD_NEEDS_FLUSH = (1 << 1)
}
GstOggPadFlags;

/* all information needed for one ogg chain (relevant for chained bitstreams) */
typedef struct
{
  GSList *pads;                 /* list of GstOggPad */
}
GstOggChain;

#define CURRENT_CHAIN(ogg) (&g_array_index ((ogg)->chains, GstOggChain, (ogg)->current_chain))
#define FOR_PAD_IN_CURRENT_CHAIN(ogg, _pad, ...) G_STMT_START{			\
  GSList *_walk;							      	\
  if ((ogg)->current_chain != -1) {						\
    for (_walk = CURRENT_CHAIN (ogg)->pads; _walk; _walk = g_slist_next (_walk)) { \
      GstOggPad *_pad = (GstOggPad *) _walk->data;				\
      __VA_ARGS__								\
    }										\
  }										\
}G_STMT_END

typedef enum
{
  GST_OGG_FLAG_BOS = GST_ELEMENT_FLAG_LAST,
  GST_OGG_FLAG_EOS,
  GST_OGG_FLAG_WAIT_FOR_DISCONT
}
GstOggFlag;

struct _GstOggDemux
{
  GstElement element;

  /* pad */
  GstPad *sinkpad;

  /* state */
  GstOggState state;
  GArray *chains;
  gint current_chain;
  guint flags;

  /* ogg stuff */
  ogg_sync_state sync;

  /* seeking */
  GstOggPad *seek_pad;
  guint64 seek_to;
};

struct _GstOggDemuxClass
{
  GstElementClass parent_class;
};

/* elementfactory information */
static GstElementDetails gst_ogg_demux_details =
GST_ELEMENT_DETAILS ("ogg demuxer",
    "Codec/Demuxer",
    "demux ogg streams (info about ogg: http://xiph.org)",
    "Benjamin Otte <in7y118@public.uni-hamburg.de>");


/* signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  /* FILL ME */
};

static GstStaticPadTemplate ogg_demux_src_template_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate ogg_demux_sink_template_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/ogg")
    );


static void gst_ogg_demux_finalize (GObject * object);

static gboolean gst_ogg_demux_src_event (GstPad * pad, GstEvent * event);
static const GstEventMask *gst_ogg_demux_get_event_masks (GstPad * pad);
static const GstQueryType *gst_ogg_demux_get_query_types (GstPad * pad);

static gboolean gst_ogg_demux_src_query (GstPad * pad,
    GstQueryType type, GstFormat * format, gint64 * value);

static void gst_ogg_demux_chain (GstPad * pad, GstData * buffer);

static GstElementStateReturn gst_ogg_demux_change_state (GstElement * element);

static GstOggPad *gst_ogg_pad_new (GstOggDemux * ogg, int serial_no);
static void gst_ogg_pad_remove (GstOggDemux * ogg, GstOggPad * ogg_pad);
static void gst_ogg_pad_reset (GstOggDemux * ogg, GstOggPad * pad);
static void gst_ogg_demux_push (GstOggDemux * ogg, ogg_page * page);
static void gst_ogg_pad_push (GstOggDemux * ogg, GstOggPad * ogg_pad);

static GstCaps *gst_ogg_type_find (ogg_packet * packet);

static void gst_ogg_print (GstOggDemux * demux);

#define GST_OGG_SET_STATE(ogg, new_state) G_STMT_START{				\
  GST_DEBUG_OBJECT (ogg, "setting state to %s", G_STRINGIFY (new_state));	\
  ogg->state = new_state;							\
}G_STMT_END

GST_BOILERPLATE (GstOggDemux, gst_ogg_demux, GstElement, GST_TYPE_ELEMENT)

     static void gst_ogg_demux_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &gst_ogg_demux_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&ogg_demux_sink_template_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&ogg_demux_src_template_factory));
}
static void
gst_ogg_demux_class_init (GstOggDemuxClass * klass)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gstelement_class->change_state = gst_ogg_demux_change_state;

  gobject_class->finalize = gst_ogg_demux_finalize;
}
static void
gst_ogg_demux_init (GstOggDemux * ogg)
{
  /* create the sink pad */
  ogg->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&ogg_demux_sink_template_factory), "sink");
  gst_element_add_pad (GST_ELEMENT (ogg), ogg->sinkpad);
  gst_pad_set_chain_function (ogg->sinkpad,
      GST_DEBUG_FUNCPTR (gst_ogg_demux_chain));

  /* initalize variables */
  GST_OGG_SET_STATE (ogg, GST_OGG_STATE_START);
  ogg->chains = g_array_new (TRUE, TRUE, sizeof (GstOggChain));
  ogg->current_chain = -1;

  GST_FLAG_SET (ogg, GST_ELEMENT_EVENT_AWARE);
}
static void
gst_ogg_demux_finalize (GObject * object)
{
  GstOggDemux *ogg;

  ogg = GST_OGG_DEMUX (object);

  /* chains are removed when going to READY */
  g_assert (ogg->current_chain == -1);
  g_assert (ogg->chains->len == 0);
  g_array_free (ogg->chains, TRUE);
}

static const GstEventMask *
gst_ogg_demux_get_event_masks (GstPad * pad)
{
  static const GstEventMask gst_ogg_demux_src_event_masks[] = {
    {GST_EVENT_SEEK, GST_SEEK_METHOD_SET | GST_SEEK_FLAG_FLUSH},
    {0,}
  };

  return gst_ogg_demux_src_event_masks;
}
static const GstQueryType *
gst_ogg_demux_get_query_types (GstPad * pad)
{
  static const GstQueryType gst_ogg_demux_src_query_types[] = {
    GST_QUERY_TOTAL,
    GST_QUERY_POSITION,
    0
  };

  return gst_ogg_demux_src_query_types;
}

static GstOggPad *
gst_ogg_get_pad_by_pad (GstOggDemux * ogg, GstPad * pad)
{
  GSList *walk;
  GstOggPad *cur;

  if (ogg->current_chain == -1) {
    GST_DEBUG_OBJECT (ogg, "no active chain, returning NULL");
    return NULL;
  }
  for (walk = CURRENT_CHAIN (ogg)->pads; walk; walk = g_slist_next (walk)) {
    cur = (GstOggPad *) walk->data;
    if (cur->pad == pad)
      return cur;
  }
  return NULL;
}

static gboolean
gst_ogg_demux_src_query (GstPad * pad, GstQueryType type,
    GstFormat * format, gint64 * value)
{
  gboolean res = FALSE;
  GstOggDemux *ogg = GST_OGG_DEMUX (gst_pad_get_parent (pad));
  GstOggPad *cur = gst_ogg_get_pad_by_pad (ogg, pad);

  if (!cur)
    return FALSE;

  switch (type) {
    case GST_QUERY_TOTAL:{
      if (*format == GST_FORMAT_DEFAULT) {
        *value = cur->length;
        res = TRUE;
      }
      break;
    }
    case GST_QUERY_POSITION:
      if (*format == GST_FORMAT_DEFAULT && cur->length != 0) {
        *value = cur->known_offset;
        res = TRUE;
      }
      break;
    default:
      break;
  }
  return res;
}

/* The current seeking implementation is the most simple I could come up with:
 * - when seeking forwards, just discard data until desired position is reached
 * - when seeking backwards, seek to beginning and seek forward from there
 * Anyone is free to improve this algorithm as it is quite stupid and probably
 * really slow.
 */
static gboolean
gst_ogg_demux_src_event (GstPad * pad, GstEvent * event)
{
  GstOggDemux *ogg;
  GstOggPad *cur;

  ogg = GST_OGG_DEMUX (gst_pad_get_parent (pad));
  cur = gst_ogg_get_pad_by_pad (ogg, pad);
  /* FIXME: optimize this so events from inactive chains work? 
   * in theory there shouldn't be an exisiting pad for inactive chains */
  if (cur == NULL)
    goto error;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      gint64 offset;

      if (GST_EVENT_SEEK_FORMAT (event) != GST_FORMAT_DEFAULT)
        goto error;
      offset = GST_EVENT_SEEK_OFFSET (event);
      switch (GST_EVENT_SEEK_METHOD (event)) {
        case GST_SEEK_METHOD_END:
          if (cur->length == 0 || offset > 0)
            goto error;
          offset = cur->length + offset;
          break;
        case GST_SEEK_METHOD_CUR:
          offset += cur->known_offset;
          break;
        case GST_SEEK_METHOD_SET:
          break;
        default:
          g_warning ("invalid seek method in seek event");
          goto error;
      }
      if (offset < cur->known_offset) {
        GstEvent *restart =
            gst_event_new_seek (GST_FORMAT_BYTES | GST_SEEK_METHOD_SET |
            GST_EVENT_SEEK_FLAGS (event), 0);
        if (!gst_pad_send_event (GST_PAD_PEER (ogg->sinkpad), restart))
          goto error;
      } else {
        FOR_PAD_IN_CURRENT_CHAIN (ogg, pad, if (GST_PAD_IS_USABLE (pad->pad))
            gst_pad_push (pad->pad,
                GST_DATA (gst_event_new (GST_EVENT_FLUSH))););
      }

      GST_OGG_SET_STATE (ogg, GST_OGG_STATE_SEEK);
      FOR_PAD_IN_CURRENT_CHAIN (ogg, pad,
          pad->flags |= GST_OGG_PAD_NEEDS_DISCONT;
          );
      GST_DEBUG_OBJECT (ogg, "initiating seeking to offset %" G_GUINT64_FORMAT,
          offset);
      ogg->seek_pad = cur;
      ogg->seek_to = offset;
      gst_event_unref (event);
      return TRUE;
    }
    default:
      return gst_pad_event_default (pad, event);
  }

  g_assert_not_reached ();

error:
  gst_event_unref (event);
  return FALSE;
}

static void
gst_ogg_start_playing (GstOggDemux * ogg)
{
  GST_DEBUG_OBJECT (ogg, "got EOS in setup, changing to playback now");
  if (!gst_pad_send_event (GST_PAD_PEER (ogg->sinkpad),
          gst_event_new_seek (GST_FORMAT_BYTES | GST_SEEK_METHOD_SET, 0))) {
    GST_ELEMENT_ERROR (ogg, CORE, SEEK, (NULL),
        ("cannot seek to start after EOS"));
  }
  ogg->current_chain = 0;
  GST_FLAG_UNSET (ogg, GST_OGG_FLAG_EOS);
  GST_FLAG_SET (ogg, GST_OGG_FLAG_WAIT_FOR_DISCONT);
  GST_OGG_SET_STATE (ogg, GST_OGG_STATE_PLAY);
  gst_ogg_print (ogg);
}

static void
gst_ogg_demux_handle_event (GstPad * pad, GstEvent * event)
{
  GstOggDemux *ogg = GST_OGG_DEMUX (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_DISCONTINUOUS:
      GST_DEBUG_OBJECT (ogg, "got a discont event");
      ogg_sync_reset (&ogg->sync);
      gst_event_unref (event);
      GST_FLAG_UNSET (ogg, GST_OGG_FLAG_WAIT_FOR_DISCONT);
      FOR_PAD_IN_CURRENT_CHAIN (ogg, pad,
          pad->flags |= GST_OGG_PAD_NEEDS_DISCONT;
          );
      break;
    case GST_EVENT_EOS:
      if (ogg->state == GST_OGG_STATE_SETUP) {
        gst_ogg_start_playing (ogg);
      } else {
        guint i;
        GSList *walk;

        GST_DEBUG_OBJECT (ogg, "got EOS");
        ogg->current_chain = -1;
        for (i = 0; i < ogg->chains->len; i++) {
          GstOggChain *chain = &g_array_index (ogg->chains, GstOggChain, i);

          for (walk = chain->pads; walk; walk = g_slist_next (walk)) {
            GstOggPad *pad = (GstOggPad *) walk->data;

            if (pad->pad && GST_PAD_IS_USABLE (pad->pad)) {
              gst_data_ref (GST_DATA (event));
              gst_pad_push (pad->pad, GST_DATA (event));
            }
          }
        }
        gst_element_set_eos (GST_ELEMENT (ogg));
      }
      gst_event_unref (event);
      break;
    default:
      gst_pad_event_default (pad, event);
      break;
  }
  return;
}

/* get the pad with the given serial in the current stream or NULL if none */
static GstOggPad *
gst_ogg_pad_get_in_current_chain (GstOggDemux * ogg, int serial)
{
  GSList *walk;

  if (ogg->current_chain == -1)
    return NULL;
  g_return_val_if_fail (ogg->current_chain < ogg->chains->len, NULL);

  for (walk = CURRENT_CHAIN (ogg)->pads; walk; walk = g_slist_next (walk)) {
    GstOggPad *pad = (GstOggPad *) walk->data;

    if (pad->serial == serial)
      return pad;
  }
  return NULL;
}

static void
gst_ogg_add_chain (GstOggDemux * ogg)
{
  GST_LOG_OBJECT (ogg, "adding chain %u", ogg->chains->len);
  ogg->current_chain = ogg->chains->len;
  g_array_set_size (ogg->chains, ogg->chains->len + 1);
}

static void
gst_ogg_demux_chain (GstPad * pad, GstData * buffer)
{
  GstOggDemux *ogg;
  guint8 *data;
  int pageout_ret = 1;
  guint64 offset_end;

  /* handle events */
  if (GST_IS_EVENT (buffer)) {
    gst_ogg_demux_handle_event (pad, GST_EVENT (buffer));
    return;
  }

  ogg = GST_OGG_DEMUX (gst_pad_get_parent (pad));

  if (GST_FLAG_IS_SET (ogg, GST_OGG_FLAG_WAIT_FOR_DISCONT)) {
    GST_LOG_OBJECT (ogg, "waiting for discont event, discarding buffer");
    gst_data_unref (buffer);
    return;
  }

  GST_LOG_OBJECT (ogg, "queueing buffer %p with offset %llu", buffer,
      GST_BUFFER_OFFSET (buffer));
  data = (guint8 *) ogg_sync_buffer (&ogg->sync, GST_BUFFER_SIZE (buffer));
  memcpy (data, GST_BUFFER_DATA (buffer), GST_BUFFER_SIZE (buffer));
  if (ogg_sync_wrote (&ogg->sync, GST_BUFFER_SIZE (buffer)) != 0) {
    gst_data_unref (buffer);
    GST_ELEMENT_ERROR (ogg, LIBRARY, TOO_LAZY, (NULL),
        ("ogg_sync_wrote failed"));
    return;
  }
  offset_end = GST_BUFFER_OFFSET_IS_VALID (buffer) ?
      GST_BUFFER_OFFSET (buffer) + GST_BUFFER_SIZE (buffer) : (guint64) - 1;
  gst_data_unref (buffer);
  while (pageout_ret != 0) {
    ogg_page page;

    pageout_ret = ogg_sync_pageout (&ogg->sync, &page);
    switch (pageout_ret) {
      case -1:
        /* FIXME: need some kind of discont here, we don't know any values to send though,
         * we only have the END_OFFSET */
        break;
      case 0:
        if (ogg->state == GST_OGG_STATE_SETUP) {
          guint64 length;
          GstFormat format = GST_FORMAT_BYTES;

          if (!gst_pad_query (GST_PAD_PEER (ogg->sinkpad), GST_QUERY_TOTAL,
                  &format, &length))
            length = 0;
          if (length <= offset_end) {
            gst_ogg_start_playing (ogg);
            goto out;
          }
        }
        break;
      case 1:
        GST_LOG_OBJECT (ogg,
            "processing ogg page (serial %d, packet %ld, granule pos %llu",
            ogg_page_serialno (&page), ogg_page_pageno (&page),
            ogg_page_granulepos (&page));
        switch (ogg->state) {
          case GST_OGG_STATE_SETUP:
            if (ogg_page_eos (&page)) {
              GstOggPad *cur = gst_ogg_pad_get_in_current_chain (ogg,
                  ogg_page_serialno (&page));

              GST_FLAG_SET (ogg, GST_OGG_FLAG_EOS);
              if (!cur) {
                GST_ERROR_OBJECT (ogg, "unknown serial %d",
                    ogg_page_serialno (&page));
              } else {
                cur->pages = ogg_page_pageno (&page);
                cur->length = ogg_page_granulepos (&page);
              }
            } else {
              if (GST_FLAG_IS_SET (ogg, GST_OGG_FLAG_EOS)
                  && ogg_page_bos (&page)) {
                gst_ogg_add_chain (ogg);
              }
              GST_FLAG_UNSET (ogg, GST_OGG_FLAG_EOS);
            }
            if (ogg_page_bos (&page)) {
              if (gst_ogg_pad_get_in_current_chain (ogg,
                      ogg_page_serialno (&page))) {
                GST_ERROR_OBJECT (ogg,
                    "multiple BOS page for serial %d (page %ld)",
                    ogg_page_serialno (&page), ogg_page_pageno (&page));
              } else {
                GstOggPad *pad =
                    gst_ogg_pad_new (ogg, ogg_page_serialno (&page));
                CURRENT_CHAIN (ogg)->pads =
                    g_slist_prepend (CURRENT_CHAIN (ogg)->pads, pad);
              }
              GST_FLAG_SET (ogg, GST_OGG_FLAG_BOS);
            } else {
              GST_FLAG_UNSET (ogg, GST_OGG_FLAG_BOS);
            }
            break;
          case GST_OGG_STATE_START:
            if (gst_pad_send_event (GST_PAD_PEER (ogg->sinkpad),
                    gst_event_new_seek (GST_FORMAT_BYTES | GST_SEEK_METHOD_END,
                        0))) {
              GST_OGG_SET_STATE (ogg, GST_OGG_STATE_SETUP);
              GST_DEBUG_OBJECT (ogg, "stream can seek, try setup now");
              if (!gst_pad_send_event (GST_PAD_PEER (ogg->sinkpad),
                      gst_event_new_seek (GST_FORMAT_BYTES |
                          GST_SEEK_METHOD_SET, 0))) {
                GST_ELEMENT_ERROR (ogg, CORE, SEEK, (NULL),
                    ("stream can seek to end, but not to start. Can't handle that."));
              }
              gst_ogg_add_chain (ogg);
              GST_FLAG_SET (ogg, GST_OGG_FLAG_WAIT_FOR_DISCONT);
              goto out;
            }
            gst_ogg_add_chain (ogg);
            GST_OGG_SET_STATE (ogg, GST_OGG_STATE_PLAY);
            /* fall through */
          case GST_OGG_STATE_SEEK:
          case GST_OGG_STATE_PLAY:
            gst_ogg_demux_push (ogg, &page);
            break;
          default:
            g_assert_not_reached ();
            break;
        }
        break;
      default:
        GST_WARNING_OBJECT (ogg,
            "unknown return value %d from ogg_sync_pageout", pageout_ret);
        pageout_ret = 0;
        break;
    }
  }
out:
  return;
}
static GstOggPad *
gst_ogg_pad_new (GstOggDemux * ogg, int serial)
{
  GstOggPad *ret = g_new0 (GstOggPad, 1);
  GstTagList *list = gst_tag_list_new ();

  ret->serial = serial;
  if (ogg_stream_init (&ret->stream, serial) != 0) {
    GST_ERROR_OBJECT (ogg,
        "Could not initialize ogg_stream struct for serial %d.", serial);
    g_free (ret);
    return NULL;
  }
  gst_tag_list_add (list, GST_TAG_MERGE_REPLACE, GST_TAG_SERIAL, serial, NULL);
  gst_element_found_tags (GST_ELEMENT (ogg), list);
  GST_LOG_OBJECT (ogg, "created new ogg src %p for stream with serial %d", ret,
      serial);

  return ret;
}
static void
gst_ogg_pad_remove (GstOggDemux * ogg, GstOggPad * pad)
{
  if (pad->pad) {
    /* FIXME:
     * we do it in the EOS signal already - EOS handling needs to be better thought out.
     * Correct way would be pushing EOS on eos page, but scheduler doesn't like that
     if (GST_PAD_IS_USEABLE (pad->pad))
     gst_pad_push (pad->pad, GST_DATA (gst_event_new (GST_EVENT_EOS)));
     */
    gst_element_remove_pad (GST_ELEMENT (ogg), pad->pad);
  }
  if (ogg_stream_clear (&pad->stream) != 0)
    GST_ERROR_OBJECT (ogg,
        "ogg_stream_clear (serial %d) did not return 0, ignoring this error",
        pad->serial);
  GST_LOG_OBJECT (ogg, "free ogg src %p for stream with serial %d", pad,
      pad->serial);
  g_free (pad);
}
static void
gst_ogg_demux_push (GstOggDemux * ogg, ogg_page * page)
{
  GSList *walk;
  GstOggPad *cur;

  /* find the stream */
  for (walk = CURRENT_CHAIN (ogg)->pads; walk; walk = g_slist_next (walk)) {
    cur = (GstOggPad *) walk->data;
    if (cur->serial == ogg_page_serialno (page)) {
      goto br;
    }
  }
  cur = NULL;
br:
  /* now we either have a stream (cur) or not */
  if (ogg_page_bos (page)) {
    if (cur) {
      GST_DEBUG_OBJECT (ogg,
          "ogg page declared as BOS while stream %d already existed."
          "Possibly a seek happened.", cur->serial);
    } else if (cur) {
      GST_DEBUG_OBJECT (ogg, "reactivating deactivated stream %d.",
          cur->serial);
    } else {
      /* FIXME: monitor if we are still in creation stage? */
      cur = gst_ogg_pad_new (ogg, ogg_page_serialno (page));
      if (!cur) {
        GST_ELEMENT_ERROR (ogg, LIBRARY, TOO_LAZY, (NULL),
            ("Creating ogg_stream struct failed."));
        return;
      }
      if (ogg->current_chain == -1) {
        /* add new one at the end */
        gst_ogg_add_chain (ogg);
      }
      CURRENT_CHAIN (ogg)->pads =
          g_slist_prepend (CURRENT_CHAIN (ogg)->pads, cur);
    }
  }
  if (cur == NULL) {
    GST_ELEMENT_ERROR (ogg, STREAM, DECODE, (NULL),
        ("invalid ogg stream serial no"));
    return;
  }
  if (ogg_stream_pagein (&cur->stream, page) != 0) {
    GST_WARNING_OBJECT (ogg,
        "ogg stream choked on page (serial %d), resetting stream", cur->serial);
    gst_ogg_pad_reset (ogg, cur);
    return;
  }
  switch (ogg->state) {
    case GST_OGG_STATE_SEEK:
      GST_LOG_OBJECT (ogg,
          "in seek - offset now: %" G_GUINT64_FORMAT
          " (pad %d) - desired offset %" G_GUINT64_FORMAT " (pad %d)",
          cur->known_offset, cur->serial, ogg->seek_to, ogg->seek_pad->serial);
      if (cur == ogg->seek_pad) {
        if (ogg_page_granulepos (page) > ogg->seek_to) {
          GST_OGG_SET_STATE (ogg, GST_OGG_STATE_PLAY);
          GST_DEBUG_OBJECT (ogg,
              "ended seek at offset %" G_GUINT64_FORMAT " (requested  %"
              G_GUINT64_FORMAT, cur->known_offset, ogg->seek_to);
          ogg->seek_pad = NULL;
          ogg->seek_to = 0;
        }
      }
      /* fallthrough */
    case GST_OGG_STATE_PLAY:
      cur->known_offset = ogg_page_granulepos (page);
      gst_ogg_pad_push (ogg, cur);
      break;
    default:
      g_assert_not_reached ();
      break;
  }
  if (ogg_page_eos (page)) {
    GST_DEBUG_OBJECT (ogg, "got EOS for stream with serial %d, sending EOS now",
        cur->serial);
#if 0
    /* Removing pads while PLAYING doesn't work with current schedulers */
    /* remove from list, as this will never be called again */
    gst_ogg_pad_remove (ogg, cur);
    /* this is also not possible because sending EOS this way confuses the scheduler */
    gst_pad_push (cur->pad, GST_DATA (gst_event_new (GST_EVENT_EOS)));
#else
#endif
  }
}
static void
gst_ogg_pad_push (GstOggDemux * ogg, GstOggPad * pad)
{
  ogg_packet packet;
  int ret;
  GstBuffer *buf;

  while (TRUE) {
    ret = ogg_stream_packetout (&pad->stream, &packet);
    switch (ret) {
      case 0:
        return;
      case -1:
        gst_ogg_pad_reset (ogg, pad);
        break;
      case 1:{
        /* only push data when playing, not during seek or similar */
        if (ogg->state != GST_OGG_STATE_PLAY)
          continue;
        if (!pad->pad) {
          GstCaps *caps = gst_ogg_type_find (&packet);
          gchar *name = g_strdup_printf ("serial_%d", pad->serial);

          if (caps == NULL) {
            GST_WARNING_OBJECT (ogg,
                "couldn't find caps for stream with serial %d", pad->serial);
            caps = gst_caps_new_simple ("application/octet-stream", NULL);
          }
          pad->pad =
              gst_pad_new_from_template (gst_static_pad_template_get
              (&ogg_demux_src_template_factory), name);
          g_free (name);
          gst_pad_set_event_function (pad->pad,
              GST_DEBUG_FUNCPTR (gst_ogg_demux_src_event));
          gst_pad_set_event_mask_function (pad->pad,
              GST_DEBUG_FUNCPTR (gst_ogg_demux_get_event_masks));
          gst_pad_set_query_function (pad->pad,
              GST_DEBUG_FUNCPTR (gst_ogg_demux_src_query));
          gst_pad_set_query_type_function (pad->pad,
              GST_DEBUG_FUNCPTR (gst_ogg_demux_get_query_types));
          gst_pad_use_explicit_caps (pad->pad);
          gst_pad_set_explicit_caps (pad->pad, caps);
          gst_pad_set_active (pad->pad, TRUE);
          gst_element_add_pad (GST_ELEMENT (ogg), pad->pad);
        }
        /* check for discont */
        if (packet.packetno != pad->packetno++) {
          pad->flags |= GST_OGG_PAD_NEEDS_DISCONT;
          pad->packetno = packet.packetno + 1;
        }
        /* send discont if needed */
        if ((pad->flags & GST_OGG_PAD_NEEDS_DISCONT)
            && GST_PAD_IS_USABLE (pad->pad)) {
          GstEvent *event = gst_event_new_discontinuous (FALSE,
              GST_FORMAT_DEFAULT, pad->known_offset, GST_FORMAT_UNDEFINED);     /* FIXME: this might be wrong because we can only use the last known offset */

          gst_pad_push (pad->pad, GST_DATA (event));
          pad->flags &= (~GST_OGG_PAD_NEEDS_DISCONT);
        };
        /* optimization: use a bufferpool containing the ogg packet? */
        buf =
            gst_pad_alloc_buffer (pad->pad, GST_BUFFER_OFFSET_NONE,
            packet.bytes);
        memcpy (buf->data, packet.packet, packet.bytes);
        if (pad->offset != -1)
          GST_BUFFER_OFFSET (buf) = pad->offset;
        if (packet.granulepos != -1)
          GST_BUFFER_OFFSET_END (buf) = packet.granulepos;
        pad->offset = packet.granulepos;
        if (GST_PAD_IS_USABLE (pad->pad))
          gst_pad_push (pad->pad, GST_DATA (buf));
        break;
      }
      default:
        GST_ERROR_OBJECT (ogg,
            "invalid return value %d for ogg_stream_packetout, resetting stream",
            ret);
        gst_ogg_pad_reset (ogg, pad);
        break;
    }
  }
}
static void
gst_ogg_pad_reset (GstOggDemux * ogg, GstOggPad * pad)
{
  ogg_stream_reset (&pad->stream);
  pad->offset = GST_BUFFER_OFFSET_NONE;
  /* FIXME: need a discont here */
}

static void
gst_ogg_chains_clear (GstOggDemux * ogg)
{
  gint i;
  GSList *walk;

  for (i = ogg->chains->len - 1; i >= 0; i--) {
    GstOggChain *cur = &g_array_index (ogg->chains, GstOggChain, i);

    for (walk = cur->pads; walk; walk = g_slist_next (walk)) {
      gst_ogg_pad_remove (ogg, (GstOggPad *) walk->data);
    }
    g_slist_free (cur->pads);
    g_array_remove_index (ogg->chains, i);
  }
  ogg->current_chain = -1;
}

static GstElementStateReturn
gst_ogg_demux_change_state (GstElement * element)
{
  GstOggDemux *ogg;

  ogg = GST_OGG_DEMUX (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      ogg_sync_init (&ogg->sync);
      break;
    case GST_STATE_READY_TO_PAUSED:
      ogg_sync_reset (&ogg->sync);
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      gst_ogg_chains_clear (ogg);
      GST_OGG_SET_STATE (ogg, GST_OGG_STATE_START);
      ogg->seek_pad = NULL;
      ogg->seek_to = 0;
      break;
    case GST_STATE_READY_TO_NULL:
      ogg_sync_clear (&ogg->sync);
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  return parent_class->change_state (element);
}

/*** typefinding **************************************************************/
/* ogg supports its own typefinding because the ogg spec defines that the first
 * packet of an ogg stream must identify the stream. Therefore ogg can use a
 * simplified approach at typefinding.
 */
typedef struct
{
  ogg_packet *packet;
  guint best_probability;
  GstCaps *caps;
}
OggTypeFind;
static guint8 *
ogg_find_peek (gpointer data, gint64 offset, guint size)
{
  OggTypeFind *find = (OggTypeFind *) data;

  if (offset + size <= find->packet->bytes) {
    return ((guint8 *) find->packet->packet) + offset;
  } else {
    return NULL;
  }
}
static void
ogg_find_suggest (gpointer data, guint probability, const GstCaps * caps)
{
  OggTypeFind *find = (OggTypeFind *) data;

  if (probability > find->best_probability) {
    gst_caps_replace (&find->caps, gst_caps_copy (caps));
    find->best_probability = probability;
  }
}
static GstCaps *
gst_ogg_type_find (ogg_packet * packet)
{
  GstTypeFind gst_find;
  OggTypeFind find;
  GList *walk, *type_list = NULL;

  walk = type_list = gst_type_find_factory_get_list ();

  find.packet = packet;
  find.best_probability = 0;
  find.caps = NULL;
  gst_find.data = &find;
  gst_find.peek = ogg_find_peek;
  gst_find.suggest = ogg_find_suggest;

  while (walk) {
    GstTypeFindFactory *factory = GST_TYPE_FIND_FACTORY (walk->data);

    gst_type_find_factory_call_function (factory, &gst_find);
    if (find.best_probability >= GST_TYPE_FIND_MAXIMUM)
      break;
    walk = g_list_next (walk);
  }

  if (find.best_probability > 0)
    return find.caps;

  return NULL;
}

gboolean
gst_ogg_demux_plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_ogg_demux_debug, "oggdemux", 0, "ogg demuxer");

  return gst_element_register (plugin, "oggdemux", GST_RANK_PRIMARY,
      GST_TYPE_OGG_DEMUX);
}

/* prints all info about the element */
static void
gst_ogg_print (GstOggDemux * ogg)
{
  guint i;
  GSList *walk;

  for (i = 0; i < ogg->chains->len; i++) {
    GstOggChain *chain = &g_array_index (ogg->chains, GstOggChain, i);

    GST_INFO_OBJECT (ogg, "chain %d (%u streams):", i,
        g_slist_length (chain->pads));
    for (walk = chain->pads; walk; walk = g_slist_next (walk)) {
      GstOggPad *pad = (GstOggPad *) walk->data;

      GST_INFO_OBJECT (ogg, "  stream %d:", pad->serial);
      GST_INFO_OBJECT (ogg, "    length %" G_GUINT64_FORMAT, pad->length);
      GST_INFO_OBJECT (ogg, "    pages %ld", pad->pages);
    }
  }
}
