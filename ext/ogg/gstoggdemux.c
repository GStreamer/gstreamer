/* GStreamer
 * Copyright (C) 2003, 2004 Benjamin Otte <otte@gnome.org>
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
#include <gst/bytestream/filepad.h>
#include <ogg/ogg.h>
#include <string.h>

/* tweak this to improve setup times */
/* PLEASE don't just tweak it because one file is faster with tweaked numbers, 
 * but use a good benchmark with both video and audio files */
/* number of bytes we seek in front of desired point so we can resync properly */
#define SETUP_EXPECTED_PAGE_SIZE (8500) /* this is out of vorbisfile */
/* number of bytes where we don't seek to middle anymore but just walk through
 * all packets */
#define SETUP_PASSTHROUGH_SIZE (SETUP_EXPECTED_PAGE_SIZE * 20)
/* if we have to repeat a seek backwards because we didn't seek back far enough, 
 * we multiply the amount we seek by this amount */
#define SETUP_SEEK_MULTIPLIER (5)


GST_DEBUG_CATEGORY_STATIC (gst_ogg_demux_debug);
GST_DEBUG_CATEGORY_STATIC (gst_ogg_demux_setup_debug);
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

  gint64 start_offset;          /* earliest offset in file where this stream has been found */
  gboolean start_found;         /* we have found the bos (first) page */
  gint64 end_offset;            /* last offset in file where this stream has been found */
  gboolean end_found;           /* we have fount the eos (last) page */

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
  gint64 starts_at;             /* starting offset of chain */
  gint64 ends_at;               /* end offset of stream (only valid when not last chain or not in setup) */

  GSList *pads;                 /* list of GstOggPad */
}
GstOggChain;

#define CURRENT_CHAIN(ogg) (&g_array_index ((ogg)->chains, GstOggChain, (ogg)->current_chain))
#define FOR_PAD_IN_CURRENT_CHAIN(ogg, __pad, ...) \
  FOR_PAD_IN_CHAIN(ogg, __pad, (ogg)->current_chain, __VA_ARGS__)
#define FOR_PAD_IN_CHAIN(ogg, _pad, i, ...) G_STMT_START{			\
  GSList *_walk;							      	\
  GstOggChain *_chain = &g_array_index ((ogg)->chains, GstOggChain, i);		\
  if (i != -1) {								\
    for (_walk = _chain->pads; _walk; _walk = g_slist_next (_walk)) {		\
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
  GstFilePad *sinkpad;

  /* state */
  GstOggState state;
  GArray *chains;               /* list of chains we know */
  gint current_chain;           /* id of chain that currently "plays" */
  gboolean bos;                 /* no-more-pads signal needs this */
  /* setup */
  GSList *unordered;            /* streams we haven't found chains for yet */
  guint setup_state;            /* seperate from global state */

  /* ogg stuff */
  ogg_sync_state sync;

  /* seeking */
  GstOggPad *seek_pad;
  gint64 seek_to;
  gint64 seek_skipped;
  GstFormat seek_format;
};

struct _GstOggDemuxClass
{
  GstElementClass parent_class;
};

/* signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0
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

/* different setup phases */
typedef enum
{
  SETUP_INVALID,
  SETUP_READ_FIRST_BOS,
  SETUP_READ_BOS,
  SETUP_FIND_LAST_CHAIN,
  SETUP_FIND_END_OF_CHAIN,
  SETUP_FIND_END_OF_STREAMS,
  SETUP_FIND_END_OF_LAST_STREAMS
}
GstOggSetupState;

typedef struct
{
  gboolean (*init) (GstOggDemux * ogg);
  gboolean (*process) (GstOggDemux * ogg, ogg_page * page);
}
SetupStateFunc;

static gboolean _read_bos_init (GstOggDemux * ogg);
static gboolean _read_bos_process (GstOggDemux * ogg, ogg_page * page);
static gboolean _find_chain_init (GstOggDemux * ogg);
static gboolean _find_chain_process (GstOggDemux * ogg, ogg_page * page);
static gboolean _find_last_chain_init (GstOggDemux * ogg);
static gboolean _find_last_chain_process (GstOggDemux * ogg, ogg_page * page);
static gboolean _find_streams_init (GstOggDemux * ogg);
static gboolean _find_streams_process (GstOggDemux * ogg, ogg_page * page);

static SetupStateFunc setup_funcs[] = {
  {NULL, NULL},
  {_read_bos_init, _read_bos_process},
  {_read_bos_init, _read_bos_process},
  {_find_last_chain_init, _find_last_chain_process},
  {_find_chain_init, _find_chain_process},
  {_find_streams_init, _find_streams_process},
  {_find_streams_init, _find_streams_process},
  {NULL, NULL}                  /* just because */
};

static gboolean gst_ogg_demux_set_setup_state (GstOggDemux * ogg,
    GstOggSetupState state);

static void gst_ogg_demux_finalize (GObject * object);

static gboolean gst_ogg_demux_src_event (GstPad * pad, GstEvent * event);
static const GstEventMask *gst_ogg_demux_get_event_masks (GstPad * pad);
static const GstQueryType *gst_ogg_demux_get_query_types (GstPad * pad);
static const GstFormat *gst_ogg_demux_get_formats (GstPad * pad);

static gboolean gst_ogg_demux_src_query (GstPad * pad,
    GstQueryType type, GstFormat * format, gint64 * value);

static void gst_ogg_demux_iterate (GstFilePad * pad);
static gboolean gst_ogg_demux_handle_event (GstPad * pad, GstEvent * event);

static GstElementStateReturn gst_ogg_demux_change_state (GstElement * element);

static GstOggPad *gst_ogg_pad_new (GstOggDemux * ogg, int serial_no);
static void gst_ogg_pad_remove (GstOggDemux * ogg, GstOggPad * ogg_pad);
static void gst_ogg_pad_reset (GstOggDemux * ogg, GstOggPad * pad);
static void gst_ogg_demux_push (GstOggDemux * ogg, ogg_page * page);
static void gst_ogg_pad_push (GstOggDemux * ogg, GstOggPad * ogg_pad);
static void gst_ogg_chains_clear (GstOggDemux * ogg);
static void gst_ogg_add_chain (GstOggDemux * ogg);

static GstCaps *gst_ogg_type_find (ogg_packet * packet);

static void gst_ogg_print (GstOggDemux * demux);

#define GST_OGG_SET_STATE(ogg, new_state) G_STMT_START{				\
  GST_DEBUG_OBJECT (ogg, "setting state to %s", G_STRINGIFY (new_state));	\
  ogg->state = new_state;							\
  ogg->setup_state = (new_state == GST_OGG_STATE_SETUP) ?			\
      SETUP_READ_FIRST_BOS : SETUP_INVALID;	        		\
}G_STMT_END

GST_BOILERPLATE (GstOggDemux, gst_ogg_demux, GstElement, GST_TYPE_ELEMENT)

     static void gst_ogg_demux_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  static GstElementDetails gst_ogg_demux_details =
      GST_ELEMENT_DETAILS ("ogg demuxer",
      "Codec/Demuxer",
      "demux ogg streams (info about ogg: http://xiph.org)",
      "Benjamin Otte <otte@gnome.org>");

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
  GST_FLAG_SET (ogg, GST_ELEMENT_EVENT_AWARE);

  /* create the sink pad */
  ogg->sinkpad =
      GST_FILE_PAD (gst_file_pad_new (gst_static_pad_template_get
          (&ogg_demux_sink_template_factory), "sink"));
  gst_file_pad_set_iterate_function (ogg->sinkpad, gst_ogg_demux_iterate);
  gst_file_pad_set_event_function (ogg->sinkpad, gst_ogg_demux_handle_event);
  gst_pad_set_formats_function (GST_PAD (ogg->sinkpad),
      gst_ogg_demux_get_formats);
  gst_element_add_pad (GST_ELEMENT (ogg), GST_PAD (ogg->sinkpad));

  /* initalize variables */
  GST_OGG_SET_STATE (ogg, GST_OGG_STATE_START);
  ogg->chains = g_array_new (TRUE, TRUE, sizeof (GstOggChain));
  ogg->current_chain = -1;
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

static const GstFormat *
gst_ogg_demux_get_formats (GstPad * pad)
{
  static GstFormat src_formats[] = {
    GST_FORMAT_BYTES,
    GST_FORMAT_DEFAULT,         /* granulepos */
    GST_FORMAT_TIME,
    0
  };
  static GstFormat sink_formats[] = {
    GST_FORMAT_BYTES,
    GST_FORMAT_DEFAULT,         /* bytes */
    0
  };

  return (GST_PAD_IS_SRC (pad) ? src_formats : sink_formats);
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

/* the query function on the src pad only knows about granulepos
 * values but we can use the peer plugins to convert the granulepos
 * (which is supposed to be the default format) to any other format 
 */
static gboolean
gst_ogg_demux_src_query (GstPad * pad, GstQueryType type,
    GstFormat * format, gint64 * value)
{
  gboolean res = FALSE;
  GstOggDemux *ogg;
  GstOggPad *cur;
  guint64 granulepos;

  ogg = GST_OGG_DEMUX (gst_pad_get_parent (pad));

  cur = gst_ogg_get_pad_by_pad (ogg, pad);
  if (!cur)
    return FALSE;

  switch (type) {
    case GST_QUERY_TOTAL:{
      granulepos = cur->length;
      res = TRUE;
      break;
    }
    case GST_QUERY_POSITION:
      if (cur->length != 0) {
        granulepos = cur->known_offset;
        res = TRUE;
      }
      break;
    default:
      break;
  }
  if (res) {
    /* still ok, got a granulepos then */
    switch (*format) {
      case GST_FORMAT_DEFAULT:
        /* fine, result should be granulepos */
        *value = granulepos;
        res = TRUE;
        break;
      default:
        /* something we have to ask our peer */
        res = gst_pad_convert (GST_PAD_PEER (pad),
            GST_FORMAT_DEFAULT, granulepos, format, value);
        break;
    }
  }
  return res;
}

/* The current seeking implementation is the most simple I could come up with:
 * - when seeking forwards, just discard data until desired position is reached
 * - when seeking backwards, seek to beginning and seek forward from there
 * Anyone is free to improve this algorithm as it is quite stupid and probably
 * really slow.
 *
 * The seeking position can be specified as the granulepos in case a decoder
 * plugin can give us a correct granulepos, or in timestamps.
 * In the case of a time seek, we repeadedly ask the peer element to 
 * convert the granulepos in the page to a timestamp. We go back to playing
 * when the timestamp is the requested one (or close enough to it).
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
      gint64 offset, position;
      GstFormat format, my_format;
      gboolean res;

      format = GST_EVENT_SEEK_FORMAT (event);
      offset = GST_EVENT_SEEK_OFFSET (event);

      my_format = format;

      /* get position, we'll need it later to decide what direction
       * we need to seek in */
      res = gst_ogg_demux_src_query (pad,
          GST_QUERY_POSITION, &my_format, &position);
      if (!res)
        goto error;

      switch (GST_EVENT_SEEK_METHOD (event)) {
        case GST_SEEK_METHOD_END:
        {
          gint64 value;

          /* invalid offset */
          if (offset > 0)
            goto error;

          /* calculate total length first */
          res = gst_ogg_demux_src_query (pad,
              GST_QUERY_TOTAL, &my_format, &value);
          if (!res)
            goto error;

          /* requested position is end + offset */
          offset = value + offset;
          break;
        }
        case GST_SEEK_METHOD_CUR:
        {
          /* add current position to offset */
          offset = position + offset;
          break;
        }
        case GST_SEEK_METHOD_SET:
          /* offset and format are fine here */
          break;
        default:
          g_warning ("invalid seek method in seek event");
          goto error;
      }

      if (offset < position) {
        /* seek backwards, move to beginning of file */
        if (gst_file_pad_seek (ogg->sinkpad, 0, GST_SEEK_METHOD_SET) != 0)
          goto error;
        ogg_sync_clear (&ogg->sync);
      } else {
        /* seek forwards flush and skip */
        FOR_PAD_IN_CURRENT_CHAIN (ogg, pad, if (GST_PAD_IS_USABLE (pad->pad))
            gst_pad_push (pad->pad,
                GST_DATA (gst_event_new (GST_EVENT_FLUSH))););
      }

      GST_OGG_SET_STATE (ogg, GST_OGG_STATE_SEEK);
      FOR_PAD_IN_CURRENT_CHAIN (ogg, pad,
          pad->flags |= GST_OGG_PAD_NEEDS_DISCONT;);
      GST_DEBUG_OBJECT (ogg,
          "initiating seeking to format %d, offset %" G_GUINT64_FORMAT, format,
          offset);

      /* store format and position we seek to */
      ogg->seek_pad = cur;
      ogg->seek_to = offset;
      ogg->seek_format = format;

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

static gboolean
gst_ogg_demux_src_convert (GstPad * pad,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = FALSE;
  GstOggDemux *ogg;
  GstOggPad *cur;

  ogg = GST_OGG_DEMUX (gst_pad_get_parent (pad));
  cur = gst_ogg_get_pad_by_pad (ogg, pad);

  /* fill me, not sure with what... */

  return res;
}

static void
gst_ogg_start_playing (GstOggDemux * ogg)
{
  GST_DEBUG_OBJECT (ogg, "done with setup, changing to playback now");
  if (gst_file_pad_seek (ogg->sinkpad, 0, GST_SEEK_METHOD_SET) != 0) {
    GST_ELEMENT_ERROR (ogg, CORE, SEEK, (NULL),
        ("cannot seek to start after EOS"));
  }
  ogg_sync_clear (&ogg->sync);
  if (ogg->current_chain >= 0) {
    ogg->current_chain = 0;
  } else {
    gst_ogg_add_chain (ogg);
  }
  GST_FLAG_UNSET (ogg, GST_OGG_FLAG_EOS);
  GST_FLAG_SET (ogg, GST_OGG_FLAG_WAIT_FOR_DISCONT);
  GST_OGG_SET_STATE (ogg, GST_OGG_STATE_PLAY);
  gst_ogg_print (ogg);
}

static gboolean
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
          pad->flags |= GST_OGG_PAD_NEEDS_DISCONT;);
      break;
    default:
      gst_pad_event_default (pad, event);
      break;
  }
  return TRUE;
}

static void
gst_ogg_demux_eos (GstOggDemux * ogg)
{
  guint i;
  GSList *walk;
  GstEvent *event;

  GST_DEBUG_OBJECT (ogg, "got EOS");
  ogg->current_chain = -1;
  if (ogg->state == GST_OGG_STATE_SETUP) {
    gst_ogg_start_playing (ogg);
    return;
  }
  event = gst_event_new (GST_EVENT_EOS);
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
  gst_event_unref (event);
}

static GstOggPad *
gst_ogg_pad_get_in_chain (GstOggDemux * ogg, guint chain, int serial)
{
  FOR_PAD_IN_CHAIN (ogg, pad, chain, if (pad->serial == serial)
      return pad;);
  return NULL;
}

/* get the pad with the given serial in the current stream or NULL if none */
static GstOggPad *
gst_ogg_pad_get_in_current_chain (GstOggDemux * ogg, int serial)
{
  if (ogg->current_chain == -1)
    return NULL;
  g_return_val_if_fail (ogg->current_chain < ogg->chains->len, NULL);
  return gst_ogg_pad_get_in_chain (ogg, ogg->current_chain, serial);
}

/* FIXME: HACK - i dunno if this is supported ogg API */
static guint
gst_ogg_page_get_length (ogg_page * page)
{
  return page->header_len + page->body_len;
}

static gint64
gst_ogg_demux_position (GstOggDemux * ogg)
{
  gint64 pos = gst_file_pad_tell (ogg->sinkpad);

  if (pos < 0)
    return pos;

  return pos - ogg->sync.fill + ogg->sync.returned;
}

/* END HACK */

/* fill in values from this page */
#include <signal.h>
static void
gst_ogg_pad_populate (GstOggDemux * ogg, GstOggPad * pad, ogg_page * page)
{
  gint64 start, end;

  if (pad->length < ogg_page_granulepos (page))
    pad->length = ogg_page_granulepos (page);
  if (pad->pages < ogg_page_pageno (page))
    pad->pages = ogg_page_pageno (page);
  end = gst_ogg_demux_position (ogg);
  if (end >= 0) {
    /* we need to know the offsets into the stream for the current page */
    start = end - gst_ogg_page_get_length (page);
    //g_print ("really setting start from %lld to %lld\n", pad->start_offset, start);
    //g_print ("really setting end from %lld to %lld\n", pad->end_offset, end);
    if (start < pad->start_offset || pad->start_offset < 0)
      pad->start_offset = start;
    if (ogg_page_bos (page))
      pad->start_found = TRUE;
    if (end > pad->end_offset)
      pad->end_offset = end;
    if (ogg_page_eos (page))
      pad->end_found = TRUE;
  }
}

/* get the ogg pad with the given serial in the unordered list or create and add it */
static GstOggPad *
gst_ogg_pad_get_unordered (GstOggDemux * ogg, ogg_page * page)
{
  GSList *walk;
  GstOggPad *pad;
  int serial = ogg_page_serialno (page);

  for (walk = ogg->unordered; walk; walk = g_slist_next (walk)) {
    pad = (GstOggPad *) walk->data;

    if (pad->serial == serial)
      goto out;
  }
  pad = gst_ogg_pad_new (ogg, serial);
  ogg->unordered = g_slist_prepend (ogg->unordered, pad);

out:
  /* update start and end pointer if applicable */
  gst_ogg_pad_populate (ogg, pad, page);

  return pad;
}

static GstOggPad *
gst_ogg_pad_get (GstOggDemux * ogg, ogg_page * page)
{
  GstOggPad *pad =
      gst_ogg_pad_get_in_current_chain (ogg, ogg_page_serialno (page));
  if (pad) {
    gst_ogg_pad_populate (ogg, pad, page);
  } else {
    pad = gst_ogg_pad_get_unordered (ogg, page);
  }
  return pad;
}

static void
gst_ogg_add_chain (GstOggDemux * ogg)
{
  GST_LOG_OBJECT (ogg, "adding chain %u", ogg->chains->len);
  ogg->current_chain = ogg->chains->len;
  g_array_set_size (ogg->chains, ogg->chains->len + 1);
}

/* abort setup phase and just start playing */
static void
abort_setup (GstOggDemux * ogg)
{
  gst_ogg_print (ogg);
  gst_ogg_chains_clear (ogg);
  gst_ogg_start_playing (ogg);
}

#undef GST_CAT_DEFAULT
#define GST_CAT_DEFAULT gst_ogg_demux_setup_debug
static gboolean
gst_ogg_demux_set_setup_state (GstOggDemux * ogg, GstOggSetupState state)
{
  g_assert (ogg->state == GST_OGG_STATE_SETUP);
  g_assert (state > 0);
  g_assert (state < G_N_ELEMENTS (setup_funcs));
  g_assert (state != ogg->setup_state);

  GST_DEBUG_OBJECT (ogg, "setting setup state from %d to %d", ogg->setup_state,
      state);
  ogg->setup_state = state;
  if (!setup_funcs[state].init (ogg)) {
    abort_setup (ogg);
    return FALSE;
  }

  return TRUE;
}

/* seeks to the given position if TRUE is returned. Seeks a bit before this
 * offset for syncing. You can call this function multiple times, if sync 
 * failed, it will then seek further back. It will never seek further back as
 * min_offset though.
 */
static gboolean
gst_ogg_demux_seek_before (GstOggDemux * ogg, gint64 offset, gint64 min_offset)
{
  gint64 before;
  GstOggChain *chain;
  gint streams;

  /* figure out how many streams are in this chain */
  chain = CURRENT_CHAIN (ogg);
  if (chain) {
    streams = g_slist_length (chain->pads);
  } else {
    streams = 1;
  }

  /* need to multiply the expected page size with the numer of streams we
   * detected to have a good chance of finding all pages */
  before = ogg->seek_skipped ? ogg->seek_skipped * SETUP_SEEK_MULTIPLIER :
      SETUP_EXPECTED_PAGE_SIZE * streams;

  GST_DEBUG_OBJECT (ogg,
      "seeking to %" G_GINT64_FORMAT " bytes before %" G_GINT64_FORMAT,
      before, offset);
  /* tried to seek to start once, don't try again */
  if (min_offset + ogg->seek_skipped > offset)
    return FALSE;
  if (gst_file_pad_seek (ogg->sinkpad, MAX (min_offset, offset - before),
          GST_SEEK_METHOD_SET) != 0)
    return FALSE;
  ogg_sync_clear (&ogg->sync);
  ogg->seek_skipped = before;
  ogg->seek_to = offset;

  return TRUE;
}

static gboolean
_read_bos_init (GstOggDemux * ogg)
{
  gst_ogg_add_chain (ogg);

  return TRUE;
}

static gboolean
_read_bos_process (GstOggDemux * ogg, ogg_page * page)
{
  /* here we're reading in the bos pages of the current chain */
  if (ogg_page_bos (page)) {
    GstOggPad *pad;

    GST_LOG_OBJECT (ogg,
        "SETUP_READ_BOS: bos found with serial %d, adding to current chain",
        ogg_page_serialno (page));
    pad = gst_ogg_pad_get_unordered (ogg, page);
    ogg->unordered = g_slist_remove (ogg->unordered, pad);
    g_assert (CURRENT_CHAIN (ogg));
    CURRENT_CHAIN (ogg)->pads =
        g_slist_prepend (CURRENT_CHAIN (ogg)->pads, pad);
  } else {
    if (CURRENT_CHAIN (ogg)->pads == NULL) {
      GST_ERROR_OBJECT (ogg, "broken ogg stream, chain has no BOS pages");
      return FALSE;
    }
    GST_DEBUG_OBJECT (ogg,
        "SETUP_READ_BOS: no more bos pages, going to find end of stream");
    if (ogg->setup_state == SETUP_READ_FIRST_BOS) {
      return gst_ogg_demux_set_setup_state (ogg, SETUP_FIND_LAST_CHAIN);
    } else if (ogg->unordered) {
      return gst_ogg_demux_set_setup_state (ogg,
          SETUP_FIND_END_OF_LAST_STREAMS);
    } else {
      return gst_ogg_demux_set_setup_state (ogg, SETUP_FIND_END_OF_STREAMS);
    }
  }
  return TRUE;
}

static gboolean
_find_chain_get_unknown_part (GstOggDemux * ogg, gint64 * start, gint64 * end)
{
  *start = 0;
  *end = G_MAXINT64;

  g_assert (ogg->current_chain >= 0);
  FOR_PAD_IN_CURRENT_CHAIN (ogg, pad, *start = MAX (*start, pad->end_offset););

  if (ogg->setup_state == SETUP_FIND_LAST_CHAIN) {
    *end = gst_file_pad_get_length (ogg->sinkpad);
    if (*end < 0)
      return FALSE;
  } else {
    GSList *walk;

    g_assert (ogg->unordered != NULL);
    for (walk = ogg->unordered; walk; walk = g_slist_next (walk)) {
      GstOggPad *temp = walk->data;

      *end = MIN (*end, temp->start_offset);
    }
  }
  GST_DEBUG_OBJECT (ogg, "we're looking for a new chain in the range [%"
      G_GINT64_FORMAT ", %" G_GINT64_FORMAT "]", *start, *end);

  /* overlapping chains?! */
  if (*end < *start) {
    GST_ERROR_OBJECT (ogg, "chained streams overlap, bailing out");
    return FALSE;
  }

  return TRUE;
}

static gboolean
_find_last_chain_init (GstOggDemux * ogg)
{
  gint64 end = gst_file_pad_get_length (ogg->sinkpad);

  ogg->seek_skipped = 0;
  if (end < 0)
    return FALSE;
  if (!gst_ogg_demux_seek_before (ogg, end, 0))
    return FALSE;
  return TRUE;
}

static gboolean
_find_last_chain_process (GstOggDemux * ogg, ogg_page * page)
{
  GstOggPad *pad = gst_ogg_pad_get (ogg, page);

  /* optimization: set eos as found - we're investigating last pages here anyway */
  pad->end_found = TRUE;
  /* set to 0 to indicate we found a page */
  ogg->seek_skipped = 0;
  return TRUE;
}

static gboolean
_find_chain_seek (GstOggDemux * ogg, gint64 start, gint64 end)
{
  if (end - start < SETUP_PASSTHROUGH_SIZE) {
    GST_LOG_OBJECT (ogg,
        "iterating through remaining window, because it's smaller than %u bytes",
        SETUP_PASSTHROUGH_SIZE);
    if (ogg->seek_to >= start) {
      ogg->seek_skipped = 0;
      if (!gst_ogg_demux_seek_before (ogg, start, start))
        return FALSE;
    }
  } else {
    ogg->seek_skipped = 0;
    if (!gst_ogg_demux_seek_before (ogg, (start + end) / 2, start))
      return FALSE;
  }
  return TRUE;
}

static gboolean
_find_chain_init (GstOggDemux * ogg)
{
  gint64 start, end;

  ogg->seek_skipped = 0;
  ogg->seek_to = -1;
  if (!_find_chain_get_unknown_part (ogg, &start, &end))
    return FALSE;
  if (!_find_chain_seek (ogg, start, end))
    return FALSE;
  return TRUE;
}

static gboolean
_find_chain_process (GstOggDemux * ogg, ogg_page * page)
{
  GstOggPad *pad = gst_ogg_pad_get (ogg, page);
  gint64 start, end;

  if (!_find_chain_get_unknown_part (ogg, &start, &end))
    return FALSE;
  if (ogg->seek_to <= start && gst_ogg_demux_position (ogg) > end) {
    /* we now should have the first bos page, because
     * - we seeked to a point in the known chain
     * - we're now in a part that belongs to the unordered streams
     */
    g_assert (g_slist_find (ogg->unordered, pad));
    if (!ogg_page_bos (page)) {
      /* broken stream */
      return FALSE;
    }
    if (!gst_ogg_demux_set_setup_state (ogg, SETUP_READ_BOS))
      return FALSE;
    return _read_bos_process (ogg, page);
  } else {
    if (!_find_chain_seek (ogg, start, end))
      return FALSE;
  }

  return TRUE;
}

static gboolean
_find_streams_check (GstOggDemux * ogg)
{
  gint chain_nr = ogg->setup_state == SETUP_FIND_END_OF_LAST_STREAMS ?
      ogg->chains->len - 1 : ogg->chains->len - 2;
  gint64 endpos;

  /* figure out positions */
  if (ogg->setup_state == SETUP_FIND_END_OF_LAST_STREAMS) {
    if ((endpos = gst_file_pad_get_length (ogg->sinkpad)) < 0)
      return FALSE;
  } else {
    endpos = G_MAXINT64;
    FOR_PAD_IN_CHAIN (ogg, pad, ogg->chains->len - 1,
        endpos = MIN (endpos, pad->start_offset););
  }
  if (!ogg->seek_skipped || gst_ogg_demux_position (ogg) >= endpos) {
    /* have we found the endposition for all streams yet? */
    FOR_PAD_IN_CHAIN (ogg, pad, chain_nr, if (!pad->end_offset)
        goto go_on;);
    /* get out, we're done */
    ogg->seek_skipped = 0;
    ogg->seek_to = -1;
    if (ogg->unordered) {
      ogg->setup_state = SETUP_FIND_END_OF_CHAIN;
    } else {
      gst_ogg_start_playing (ogg);
    }
    return TRUE;
  go_on:
    if (!gst_ogg_demux_seek_before (ogg, endpos, 0))
      return FALSE;
  }

  return TRUE;
}

static gboolean
_find_streams_init (GstOggDemux * ogg)
{
  ogg->seek_skipped = 0;
  ogg->seek_to = -1;
  return _find_streams_check (ogg);
}

static gboolean
_find_streams_process (GstOggDemux * ogg, ogg_page * page)
{
  gint chain_nr = ogg->setup_state == SETUP_FIND_END_OF_LAST_STREAMS ?
      ogg->chains->len - 1 : ogg->chains->len - 2;

  g_assert (ogg->setup_state == SETUP_FIND_END_OF_LAST_STREAMS ||
      ogg->setup_state == SETUP_FIND_END_OF_STREAMS);
  g_assert (chain_nr >= 0);
  /* mark current pad as having an endframe */
  if (ogg->seek_skipped) {
    GstOggPad *pad =
        gst_ogg_pad_get_in_chain (ogg, chain_nr, ogg_page_serialno (page));
    if (pad) {
      pad->end_offset = TRUE;
      g_print ("marking pad %d as having an end\n", pad->serial);
    }
  }
  return _find_streams_check (ogg);
}

#undef GST_CAT_DEFAULT
#define GST_CAT_DEFAULT gst_ogg_demux_debug

static void
gst_ogg_demux_iterate (GstFilePad * pad)
{
  GstOggDemux *ogg;
  guint8 *data;
  guint available;
  int pageout_ret = 1;
  gint64 offset_end;

  ogg = GST_OGG_DEMUX (gst_pad_get_parent (GST_PAD (pad)));

  available = gst_file_pad_available (ogg->sinkpad);
  if (available == 0) {
    if (gst_file_pad_eof (ogg->sinkpad)) {
      gst_ogg_demux_eos (ogg);
    } else {
      GST_DEBUG_OBJECT (ogg, "no data available, doing nothing");
    }
  }
  GST_LOG_OBJECT (ogg, "queueing next %u bytes of data", available);
  data = (guint8 *) ogg_sync_buffer (&ogg->sync, available);
  if ((available = gst_file_pad_read (ogg->sinkpad, data, available)) < 0) {
    GST_ERROR_OBJECT (ogg, "error %u reading data from pad",
        gst_file_pad_error (ogg->sinkpad));
    return;
  }
  if (ogg_sync_wrote (&ogg->sync, available) != 0) {
    GST_ELEMENT_ERROR (ogg, LIBRARY, TOO_LAZY, (NULL),
        ("ogg_sync_wrote failed"));
    return;
  }
  offset_end = gst_file_pad_tell (ogg->sinkpad);
  g_assert (offset_end >= 0);   /* FIXME: do sth reasonable if no length available */
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
          if (gst_file_pad_get_length (ogg->sinkpad) <= offset_end) {
            if (ogg->seek_skipped) {
              if (!gst_ogg_demux_seek_before (ogg,
                      gst_file_pad_get_length (ogg->sinkpad), 0))
                abort_setup (ogg);
            } else if (ogg->setup_state == SETUP_FIND_LAST_CHAIN) {
              if (ogg->unordered) {
                if (!gst_ogg_demux_seek_before (ogg, offset_end / 2, 0))
                  abort_setup (ogg);
                if (!gst_ogg_demux_set_setup_state (ogg,
                        SETUP_FIND_END_OF_CHAIN))
                  goto out;
              } else {
                if (!gst_ogg_demux_set_setup_state (ogg,
                        SETUP_FIND_END_OF_LAST_STREAMS))
                  goto out;
              }
            } else {
              abort_setup (ogg);
            }
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
            if (!setup_funcs[ogg->setup_state].process (ogg, &page)) {
              abort_setup (ogg);
              goto out;
            }
            break;
          case GST_OGG_STATE_START:
            if (gst_file_pad_seek (ogg->sinkpad, 0, GST_SEEK_METHOD_END) == 0) {
              GST_OGG_SET_STATE (ogg, GST_OGG_STATE_SETUP);
              GST_DEBUG_OBJECT (ogg, "stream can seek, try setup now");
              if (gst_file_pad_seek (ogg->sinkpad, 0, GST_SEEK_METHOD_SET) != 0) {
                GST_ELEMENT_ERROR (ogg, CORE, SEEK, (NULL),
                    ("stream can seek to end, but not to start. Can't handle that."));
              }
              ogg_sync_clear (&ogg->sync);
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
  ret->start_offset = ret->end_offset = -1;
  ret->start_found = ret->end_found = FALSE;

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
  GstOggPad *cur =
      gst_ogg_pad_get_in_current_chain (ogg, ogg_page_serialno (page));

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
    ogg->bos = TRUE;
  } else if (ogg->bos) {
    gst_element_no_more_pads (GST_ELEMENT (ogg));
    ogg->bos = FALSE;
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
        gint64 position;

        position = ogg_page_granulepos (page);

        /* see if we reached the destination position when seeking */
        if (ogg->seek_format != GST_FORMAT_DEFAULT) {
          if (!gst_pad_convert (GST_PAD_PEER (cur->pad),
                  GST_FORMAT_DEFAULT, position, &ogg->seek_format, &position)) {
            /* let's just stop then */
            position = G_MAXINT64;
          }
        }

        if (position >= ogg->seek_to) {
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
          gst_pad_set_formats_function (pad->pad,
              GST_DEBUG_FUNCPTR (gst_ogg_demux_get_formats));
          gst_pad_set_convert_function (pad->pad,
              GST_DEBUG_FUNCPTR (gst_ogg_demux_src_convert));

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
  GST_DEBUG_CATEGORY_INIT (gst_ogg_demux_setup_debug, "oggdemux_setup", 0,
      "ogg demuxer setup stage when parsing pipeline");

  return gst_element_register (plugin, "oggdemux", GST_RANK_PRIMARY,
      GST_TYPE_OGG_DEMUX);
}

/* prints all info about the element */
#undef GST_CAT_DEFAULT
#define GST_CAT_DEFAULT gst_ogg_demux_setup_debug
#if 0
/* use a define here so the function name in debugging output stays the same */
static void
gst_ogg_print_pad (GstOggDemux * ogg, GstOggPad * pad)
#endif
#define gst_ogg_print_pad(ogg, _pad) \
G_STMT_START{\
  GstOggPad *pad = (_pad); \
  GST_INFO_OBJECT (ogg, "  stream %d:", pad->serial); \
  GST_INFO_OBJECT (ogg, "    length %" G_GUINT64_FORMAT, pad->length); \
  GST_INFO_OBJECT (ogg, "    pages %ld", pad->pages); \
  GST_INFO_OBJECT (ogg, "    offset: %"G_GINT64_FORMAT"%s - %"G_GINT64_FORMAT"%s", \
      pad->start_offset, pad->start_found ? "" : " (?)", \
      pad->end_offset, pad->end_found ? "" : " (?)"); \
}G_STMT_END
     static void gst_ogg_print (GstOggDemux * ogg)
{
  guint i;
  GSList *walk;

  for (i = 0; i < ogg->chains->len; i++) {
    GstOggChain *chain = &g_array_index (ogg->chains, GstOggChain, i);

    GST_INFO_OBJECT (ogg, "chain %d (%u streams):", i,
        g_slist_length (chain->pads));
    for (walk = chain->pads; walk; walk = g_slist_next (walk)) {
      gst_ogg_print_pad (ogg, walk->data);
    }
  }
  if (ogg->unordered) {
    GST_INFO_OBJECT (ogg, "unordered (%u streams):", i,
        g_slist_length (ogg->unordered));
    for (walk = ogg->unordered; walk; walk = g_slist_next (walk)) {
      gst_ogg_print_pad (ogg, walk->data);
    }
  }
}
