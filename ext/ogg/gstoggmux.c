/* OGG muxer plugin for GStreamer
 * Copyright (C) 2004 Wim Taymans <wim@fluendo.com>
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
#include <time.h>

GST_DEBUG_CATEGORY_STATIC (gst_ogg_mux_debug);
#define GST_CAT_DEFAULT gst_ogg_mux_debug

#define GST_TYPE_OGG_MUX (gst_ogg_mux_get_type())
#define GST_OGG_MUX(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_OGG_MUX, GstOggMux))
#define GST_OGG_MUX_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_OGG_MUX, GstOggMux))
#define GST_IS_OGG_MUX(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_OGG_MUX))
#define GST_IS_OGG_MUX_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_OGG_MUX))

typedef struct _GstOggMux GstOggMux;
typedef struct _GstOggMuxClass GstOggMuxClass;

/* all information needed for one ogg stream */
typedef struct
{
  GstPad *pad;                  /* reference for this pad is held by element we belong to */

  GstBuffer *buffer;            /* the queued buffer for this pad */

  gint serial;
  ogg_stream_state stream;
  gint64 packetno;              /* number of next packet */
  gint64 pageno;                /* number of next page */
  gboolean eos;

  guint state;                  /* state of the pad */
}
GstOggPad;

typedef enum
{
  GST_OGG_PAD_STATE_CONTROL = 0,
  GST_OGG_PAD_STATE_DATA = 1
}
GstOggPadState;

struct _GstOggMux
{
  GstElement element;

  /* pad */
  GstPad *srcpad;

  /* sinkpads, a GSList of GstOggPads */
  GSList *sinkpads;

  /* the pad we are currently pulling from to fill a page */
  GstOggPad *pulling;

  /* next timestamp for the page */
  GstClockTime next_ts;

  /* offset in stream */
  guint64 offset;
};

typedef enum
{
  GST_OGG_FLAG_BOS = GST_ELEMENT_FLAG_LAST,
  GST_OGG_FLAG_EOS
}
GstOggFlag;

struct _GstOggMuxClass
{
  GstElementClass parent_class;
};

/* elementfactory information */
static GstElementDetails gst_ogg_mux_details = GST_ELEMENT_DETAILS ("ogg muxer",
    "Codec/Muxer",
    "mux ogg streams (info about ogg: http://xiph.org)",
    "Wim Taymans <wim@fluendo.com>");

/* OggMux signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
};

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/ogg")
    );

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink_%d",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS_ANY         /* we can take anything, really */
    );

static void gst_ogg_mux_base_init (gpointer g_class);
static void gst_ogg_mux_class_init (GstOggMuxClass * klass);
static void gst_ogg_mux_init (GstOggMux * ogg_mux);

static void gst_ogg_mux_loop (GstElement * element);
static gboolean gst_ogg_mux_handle_src_event (GstPad * pad, GstEvent * event);
static GstPad *gst_ogg_mux_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name);
static void gst_ogg_mux_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_ogg_mux_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static GstElementStateReturn gst_ogg_mux_change_state (GstElement * element);

static GstElementClass *parent_class = NULL;

/*static guint gst_ogg_mux_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_ogg_mux_get_type (void)
{
  static GType ogg_mux_type = 0;

  if (!ogg_mux_type) {
    static const GTypeInfo ogg_mux_info = {
      sizeof (GstOggMuxClass),
      gst_ogg_mux_base_init,
      NULL,
      (GClassInitFunc) gst_ogg_mux_class_init,
      NULL,
      NULL,
      sizeof (GstOggMux),
      0,
      (GInstanceInitFunc) gst_ogg_mux_init,
    };

    ogg_mux_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstOggMux", &ogg_mux_info,
        0);
  }
  return ogg_mux_type;
}

static void
gst_ogg_mux_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));

  gst_element_class_set_details (element_class, &gst_ogg_mux_details);
}

static void
gst_ogg_mux_class_init (GstOggMuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gstelement_class->request_new_pad = gst_ogg_mux_request_new_pad;

  gstelement_class->change_state = gst_ogg_mux_change_state;

  gstelement_class->get_property = gst_ogg_mux_get_property;
  gstelement_class->set_property = gst_ogg_mux_set_property;
}

static const GstEventMask *
gst_ogg_mux_get_sink_event_masks (GstPad * pad)
{
  static const GstEventMask gst_ogg_mux_sink_event_masks[] = {
    {GST_EVENT_EOS, 0},
    {0,}
  };

  return gst_ogg_mux_sink_event_masks;
}

static void
gst_ogg_mux_init (GstOggMux * ogg_mux)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (ogg_mux);

  ogg_mux->srcpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "src"), "src");
  gst_pad_set_event_function (ogg_mux->srcpad, gst_ogg_mux_handle_src_event);
  gst_element_add_pad (GST_ELEMENT (ogg_mux), ogg_mux->srcpad);

  GST_FLAG_SET (GST_ELEMENT (ogg_mux), GST_ELEMENT_EVENT_AWARE);
  GST_FLAG_SET (GST_ELEMENT (ogg_mux), GST_OGG_FLAG_BOS);

  /* seed random number generator for creation of serial numbers */
  srand (time (NULL));

  ogg_mux->sinkpads = NULL;
  ogg_mux->pulling = NULL;

  gst_element_set_loop_function (GST_ELEMENT (ogg_mux), gst_ogg_mux_loop);
}

static GstPadLinkReturn
gst_ogg_mux_sinkconnect (GstPad * pad, const GstCaps * vscaps)
{
  GstOggMux *ogg_mux;
  GstStructure *structure;
  const gchar *mimetype;

  ogg_mux = GST_OGG_MUX (gst_pad_get_parent (pad));

  GST_DEBUG ("ogg_mux: sinkconnect triggered on %s", gst_pad_get_name (pad));

  structure = gst_caps_get_structure (vscaps, 0);
  mimetype = gst_structure_get_name (structure);

  return GST_PAD_LINK_OK;
}

static void
gst_ogg_mux_pad_link (GstPad * pad, GstPad * peer, gpointer data)
{
  //GstOggMux *ogg_mux = GST_OGG_MUX (data);
  const gchar *padname = gst_pad_get_name (pad);

  GST_DEBUG ("pad '%s' connected", padname);
}

static void
gst_ogg_mux_pad_unlink (GstPad * pad, GstPad * peer, gpointer data)
{
  //GstOggMux *ogg_mux = GST_OGG_MUX (data);
  const gchar *padname = gst_pad_get_name (pad);

  GST_DEBUG ("pad '%s' unlinked", padname);
}

static GstPad *
gst_ogg_mux_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * req_name)
{
  GstOggMux *ogg_mux;
  GstPad *newpad;
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (element);

  g_return_val_if_fail (templ != NULL, NULL);

  if (templ->direction != GST_PAD_SINK) {
    g_warning ("ogg_mux: request pad that is not a SINK pad\n");
    return NULL;
  }

  g_return_val_if_fail (GST_IS_OGG_MUX (element), NULL);

  ogg_mux = GST_OGG_MUX (element);

  if (templ == gst_element_class_get_pad_template (klass, "sink_%d")) {
    gint serial;
    gchar *name;

    if (req_name == NULL || strlen (req_name) < 6) {
      /* no name given when requesting the pad, use random serial number */
      serial = rand ();
    } else {
      /* parse serial number from requested padname */
      serial = atoi (&req_name[5]);
    }
    /* create new pad with the name */
    name = g_strdup_printf ("sink_%d", serial);
    newpad = gst_pad_new_from_template (templ, name);
    g_free (name);

    /* construct our own wrapper data structure for the pad to
     * keep track of its status */
    {
      GstOggPad *oggpad = g_new0 (GstOggPad, 1);

      oggpad->pad = newpad;
      oggpad->serial = serial;
      ogg_stream_init (&oggpad->stream, serial);
      oggpad->packetno = 0;
      oggpad->pageno = 0;
      oggpad->eos = FALSE;
      /* we assume there will be some control data first for this pad */
      oggpad->state = GST_OGG_PAD_STATE_CONTROL;

      /* save a pointer to our data in the pad */
      gst_pad_set_element_private (newpad, oggpad);
      /* store our data for the pad */
      ogg_mux->sinkpads = g_slist_prepend (ogg_mux->sinkpads, oggpad);
    }
  } else {
    g_warning ("ogg_mux: this is not our template!\n");
    return NULL;
  }

  g_signal_connect (newpad, "linked",
      G_CALLBACK (gst_ogg_mux_pad_link), (gpointer) ogg_mux);
  g_signal_connect (newpad, "unlinked",
      G_CALLBACK (gst_ogg_mux_pad_unlink), (gpointer) ogg_mux);

  /* setup some pad functions */
  gst_pad_set_link_function (newpad, gst_ogg_mux_sinkconnect);
  gst_pad_set_event_mask_function (newpad, gst_ogg_mux_get_sink_event_masks);
  /* dd the pad to the element */
  gst_element_add_pad (element, newpad);

  return newpad;
}

/* handle events */
static gboolean
gst_ogg_mux_handle_src_event (GstPad * pad, GstEvent * event)
{
  GstOggMux *ogg_mux;
  GstEventType type;

  ogg_mux = GST_OGG_MUX (gst_pad_get_parent (pad));

  type = event ? GST_EVENT_TYPE (event) : GST_EVENT_UNKNOWN;

  switch (type) {
    case GST_EVENT_SEEK:
      /* disable seeking for now */
      return FALSE;
    default:
      break;
  }

  return gst_pad_event_default (pad, event);
}

static GstBuffer *
gst_ogg_mux_next_buffer (GstOggPad * pad)
{
  GstData *data = NULL;

  while (data == NULL) {
    GST_LOG ("muxer: pulling %s:%s\n", GST_DEBUG_PAD_NAME (pad->pad));
    data = gst_pad_pull (pad->pad);
    /* if it's an event, handle it */
    if (GST_IS_EVENT (data)) {
      GstEventType type;
      GstOggMux *ogg_mux;
      GstEvent *event = GST_EVENT (data);

      ogg_mux = GST_OGG_MUX (gst_pad_get_parent (pad->pad));
      type = event ? GST_EVENT_TYPE (event) : GST_EVENT_UNKNOWN;

      switch (type) {
        case GST_EVENT_EOS:
          return NULL;
        default:
          gst_pad_event_default (pad->pad, event);
          break;
      }
      data = NULL;
    }
  }
  return GST_BUFFER (data);
}

static void
gst_ogg_mux_push_page (GstOggMux * mux, ogg_page * page)
{
  GstBuffer *buffer;

  /* allocate space for header and body */
  buffer = gst_pad_alloc_buffer (mux->srcpad, GST_BUFFER_OFFSET_NONE,
      page->header_len + page->body_len);
  memcpy (GST_BUFFER_DATA (buffer), page->header, page->header_len);
  memcpy (GST_BUFFER_DATA (buffer) + page->header_len,
      page->body, page->body_len);

  /* next_ts was the timestamp of the first buffer put in this page */
  GST_BUFFER_TIMESTAMP (buffer) = mux->next_ts;
  GST_BUFFER_OFFSET (buffer) = mux->offset;
  mux->offset += GST_BUFFER_SIZE (buffer);
  GST_BUFFER_OFFSET_END (buffer) = mux->offset;

  if (GST_PAD_IS_USABLE (mux->srcpad))
    gst_pad_push (mux->srcpad, GST_DATA (buffer));
  else
    gst_buffer_unref (buffer);
}

/*
 * Given two pads, compare the buffers queued on it and return 0 if they have
 * an equal priority, 1 if the new pad is better, -1 if the old pad is better 
 */
static gint
gst_ogg_mux_compare_pads (GstOggMux * ogg_mux, GstOggPad * old, GstOggPad * new)
{
  guint64 oldtime, newtime;

  /* if the old pad doesn't contain anything or is even NULL, return 
   * the new pad as best candidate and vice versa */
  if (old == NULL || old->buffer == NULL)
    return 1;
  if (new == NULL || new->buffer == NULL)
    return -1;

  /* no timestamp on old buffer, it must go first */
  oldtime = GST_BUFFER_TIMESTAMP (old->buffer);
  if (oldtime == GST_CLOCK_TIME_NONE)
    return -1;

  /* no timestamp on new buffer, it must go first */
  newtime = GST_BUFFER_TIMESTAMP (new->buffer);
  if (newtime == GST_CLOCK_TIME_NONE)
    return 1;

  /* old buffer has higher timestamp, new one should go first */
  if (newtime < oldtime)
    return 1;
  /* new buffer has higher timestamp, old one should go first */
  else if (newtime > oldtime)
    return -1;
  else {
    /* buffers with equal timestamps, prefer the pad that has the
     * least number of pages muxed */
    if (new->pageno < old->pageno)
      return 1;
    else if (new->pageno > old->pageno)
      return -1;
  }

  /* same priority if all of the above failed */
  return 0;
}

/* make sure a buffer is queued on all pads, returns a pointer to an oggpad
 * that holds the best buffer or NULL when no pad was usable */
static GstOggPad *
gst_ogg_mux_queue_pads (GstOggMux * ogg_mux)
{
  GstOggPad *bestpad = NULL;
  GSList *walk;

  /* try to make sure we have a buffer from each usable pad first */
  walk = ogg_mux->sinkpads;
  while (walk) {
    GstOggPad *pad = (GstOggPad *) walk->data;

    walk = walk->next;

    /* try to get a new buffer for this pad if needed and possible */
    if (pad->buffer == NULL && GST_PAD_IS_USABLE (pad->pad)) {
      pad->buffer = gst_ogg_mux_next_buffer (pad);
      /* no next buffer, try another pad */
      if (pad->buffer == NULL)
        continue;
    }

    /* skip unusable pads */
    if (!GST_PAD_IS_USABLE (pad->pad))
      continue;

    /* we should have a buffer now, see if it is the best pad to
     * pull on */
    if (pad->buffer != NULL) {
      if (gst_ogg_mux_compare_pads (ogg_mux, bestpad, pad) > 0) {
        bestpad = pad;
      }
    }
  }
  return bestpad;
}

/* basic idea:
 *
 * 1) find a pad to pull on, this is done by pulling on all pads and
 *    looking at the buffers to decide which one should be muxed first.
 * 2) store the selected pad and keep on pulling until we fill a 
 *    complete ogg page. This is needed because the ogg spec says that
 *    you should fill a complete page with data from the same logical
 *    stream. When the page is filled, go back to 1).
 * 3) before filling a packet, read ahead one more buffer to see if this
 *    packet is the last of the stream.
 */
static void
gst_ogg_mux_loop (GstElement * element)
{
  GstOggMux *ogg_mux;

  ogg_mux = GST_OGG_MUX (element);

  /* if we don't know which pad to pull on, find one */
  if (ogg_mux->pulling == NULL) {
    ogg_mux->pulling = gst_ogg_mux_queue_pads (ogg_mux);
    /* remember timestamp of first buffer for this new pad */
    if (ogg_mux->pulling != NULL) {
      ogg_mux->next_ts = GST_BUFFER_TIMESTAMP (ogg_mux->pulling->buffer);
    } else {
      /* no pad to pull on, send EOS */
      if (GST_PAD_IS_USABLE (ogg_mux->srcpad))
        gst_pad_push (ogg_mux->srcpad,
            GST_DATA (gst_event_new (GST_EVENT_EOS)));
      gst_element_set_eos (element);
      return;
    }
  }

  /* we are pulling from a pad, continue to do so until a page
   * has been filled and pushed */
  if (ogg_mux->pulling != NULL) {
    ogg_packet packet;
    ogg_page page;
    GstBuffer *buf, *tmpbuf;
    GstOggPad *pad = ogg_mux->pulling;
    gint ret;
    GstOggPadState newstate;

    /* now see if we have a buffer */
    buf = pad->buffer;
    if (buf == NULL) {
      /* no buffer, get one */
      buf = gst_ogg_mux_next_buffer (pad);
      /* data exhausted on this pad (EOS) */
      if (buf == NULL) {
        /* stop pulling from the pad */
        ogg_mux->pulling = NULL;
        return;
      }
    }

    /* ogg expects headers and data to be in a different page, we
     * have no way of knowing headers from data here so we use the
     * buffer control flag as a hint */
    newstate = (pad->packetno < 3 ?
        GST_OGG_PAD_STATE_CONTROL : GST_OGG_PAD_STATE_DATA);

    if (newstate != pad->state) {
      /* state switch, flush page */
      while (ogg_stream_flush (&pad->stream, &page)) {
        gst_ogg_mux_push_page (ogg_mux, &page);
        pad->pageno++;
      }
      /* switching state */
      pad->state = newstate;
    }

    /* create a packet from the buffer */
    packet.packet = GST_BUFFER_DATA (buf);
    packet.bytes = GST_BUFFER_SIZE (buf);
    packet.granulepos = GST_BUFFER_OFFSET_END (buf);
    /* mark BOS and packet number */
    packet.b_o_s = (pad->packetno == 0);
    packet.packetno = pad->packetno++;

    /* read ahead one more buffer to find EOS */
    tmpbuf = gst_ogg_mux_next_buffer (pad);
    /* data exhausted on this pad */
    if (tmpbuf == NULL) {
      /* stop pulling from the pad */
      ogg_mux->pulling = NULL;
    }
    /* mark EOS */
    packet.e_o_s = (tmpbuf == NULL ? 1 : 0);

    /* swap the packet in */
    ogg_stream_packetin (&pad->stream, &packet);

    /* don't need the old buffer anymore */
    gst_buffer_unref (pad->buffer);
    /* store new readahead buffer */
    pad->buffer = tmpbuf;

    /* create a page */
    ret = ogg_stream_pageout (&pad->stream, &page);
    if (ret > 0) {
      /* we have a complete page now, we can push the page 
       * and make sure to pull on a new pad the next time around */
      gst_ogg_mux_push_page (ogg_mux, &page);
      /* increment the page number counter */
      pad->pageno++;
      /* we're done pulling on this pad, make sure to choose a new 
       * pad for pulling in the next iteration */
      ogg_mux->pulling = NULL;
    }
  }
}

static void
gst_ogg_mux_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ogg_mux_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstElementStateReturn
gst_ogg_mux_change_state (GstElement * element)
{
  GstOggMux *ogg_mux;
  gint transition = GST_STATE_TRANSITION (element);

  g_return_val_if_fail (GST_IS_OGG_MUX (element), GST_STATE_FAILURE);

  ogg_mux = GST_OGG_MUX (element);

  switch (transition) {
    case GST_STATE_NULL_TO_READY:
    case GST_STATE_READY_TO_PAUSED:
      ogg_mux->next_ts = 0;
      ogg_mux->offset = 0;
      ogg_mux->pulling = NULL;
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
    case GST_STATE_PLAYING_TO_PAUSED:
    case GST_STATE_PAUSED_TO_READY:
    case GST_STATE_READY_TO_NULL:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

gboolean
gst_ogg_mux_plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_ogg_mux_debug, "oggmux", 0, "ogg muxer");

  return gst_element_register (plugin, "oggmux", GST_RANK_PRIMARY,
      GST_TYPE_OGG_MUX);
}
