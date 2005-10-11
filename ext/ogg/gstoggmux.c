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
#include <gst/base/gstcollectpads.h>

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

typedef enum
{
  GST_OGG_PAD_STATE_CONTROL = 0,
  GST_OGG_PAD_STATE_DATA = 1
}
GstOggPadState;

/* all information needed for one ogg stream */
typedef struct
{
  GstCollectData collect;       /* we extend the CollectData */

  GstBuffer *buffer;            /* the queued buffer for this pad */

  gint serial;
  ogg_stream_state stream;
  gint64 packetno;              /* number of next packet */
  gint64 pageno;                /* number of next page */
  guint64 duration;             /* duration of current page */
  gboolean eos;
  gint64 offset;

  GstOggPadState state;         /* state of the pad */

  GList *headers;

  gboolean new_page;            /* starting a new page */
  gboolean first_delta;         /* was the first packet in the page a delta */
  gboolean prev_delta;          /* was the previous buffer a delta frame */
}
GstOggPad;

struct _GstOggMux
{
  GstElement element;

  /* source pad */
  GstPad *srcpad;

  /* sinkpads */
  GstCollectPads *collect;

  /* the pad we are currently using to fill a page */
  GstOggPad *pulling;

  /* next timestamp for the page */
  GstClockTime next_ts;

  /* offset in stream */
  guint64 offset;

  /* need_headers */
  gboolean need_headers;

  guint64 max_delay;
  guint64 max_page_delay;

  GstOggPad *delta_pad;         /* when a delta frame is detected on a stream, we mark
                                   pages as delta frames up to the page that has the
                                   keyframe */

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

/* set to 0.5 seconds by default */
#define DEFAULT_MAX_DELAY	500000000LL
#define DEFAULT_MAX_PAGE_DELAY	500000000LL
enum
{
  ARG_0,
  ARG_MAX_DELAY,
  ARG_MAX_PAGE_DELAY,
};

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/ogg")
    );

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink_%d",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("video/x-theora; "
        "audio/x-vorbis; audio/x-flac; audio/x-speex; "
        "application/x-ogm-video; application/x-ogm-audio")
    );

static void gst_ogg_mux_base_init (gpointer g_class);
static void gst_ogg_mux_class_init (GstOggMuxClass * klass);
static void gst_ogg_mux_init (GstOggMux * ogg_mux);

static GstFlowReturn
gst_ogg_mux_collected (GstCollectPads * pads, GstOggMux * ogg_mux);
static gboolean gst_ogg_mux_handle_src_event (GstPad * pad, GstEvent * event);
static GstPad *gst_ogg_mux_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name);
static void gst_ogg_mux_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_ogg_mux_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static GstStateChangeReturn gst_ogg_mux_change_state (GstElement * element,
    GstStateChange transition);

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

  gobject_class->get_property = gst_ogg_mux_get_property;
  gobject_class->set_property = gst_ogg_mux_set_property;

  gstelement_class->request_new_pad = gst_ogg_mux_request_new_pad;

  g_object_class_install_property (gobject_class, ARG_MAX_DELAY,
      g_param_spec_uint64 ("max-delay", "Max delay",
          "Maximum delay in multiplexing streams", 0, G_MAXUINT64,
          DEFAULT_MAX_DELAY, (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_MAX_PAGE_DELAY,
      g_param_spec_uint64 ("max-page-delay", "Max page delay",
          "Maximum delay for sending out a page", 0, G_MAXUINT64,
          DEFAULT_MAX_PAGE_DELAY, (GParamFlags) G_PARAM_READWRITE));

  gstelement_class->change_state = gst_ogg_mux_change_state;

}

#if 0
static const GstEventMask *
gst_ogg_mux_get_sink_event_masks (GstPad * pad)
{
  static const GstEventMask gst_ogg_mux_sink_event_masks[] = {
    {GST_EVENT_EOS, 0},
    {GST_EVENT_DISCONTINUOUS, 0},
    {0,}
  };

  return gst_ogg_mux_sink_event_masks;
}
#endif

static void
gst_ogg_mux_init (GstOggMux * ogg_mux)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (ogg_mux);

  ogg_mux->srcpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "src"), "src");
  gst_pad_set_event_function (ogg_mux->srcpad, gst_ogg_mux_handle_src_event);
  gst_element_add_pad (GST_ELEMENT (ogg_mux), ogg_mux->srcpad);

  GST_FLAG_SET (GST_ELEMENT (ogg_mux), GST_OGG_FLAG_BOS);

  /* seed random number generator for creation of serial numbers */
  srand (time (NULL));

  ogg_mux->collect = gst_collectpads_new ();
  gst_collectpads_set_function (ogg_mux->collect,
      (GstCollectPadsFunction) gst_ogg_mux_collected, ogg_mux);
  ogg_mux->pulling = NULL;
  ogg_mux->need_headers = TRUE;
  ogg_mux->max_delay = DEFAULT_MAX_DELAY;
  ogg_mux->max_page_delay = DEFAULT_MAX_PAGE_DELAY;

  ogg_mux->delta_pad = NULL;
}

static GstPadLinkReturn
gst_ogg_mux_sinkconnect (GstPad * pad, GstPad * peer)
{
  GstOggMux *ogg_mux;

  ogg_mux = GST_OGG_MUX (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (ogg_mux, "sinkconnect triggered on %s",
      gst_pad_get_name (pad));

  return GST_PAD_LINK_OK;
}

static GstPad *
gst_ogg_mux_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * req_name)
{
  GstOggMux *ogg_mux;
  GstPad *newpad;
  GstElementClass *klass;

  g_return_val_if_fail (templ != NULL, NULL);

  if (templ->direction != GST_PAD_SINK)
    goto wrong_direction;

  g_return_val_if_fail (GST_IS_OGG_MUX (element), NULL);
  ogg_mux = GST_OGG_MUX (element);

  klass = GST_ELEMENT_GET_CLASS (element);

  if (templ != gst_element_class_get_pad_template (klass, "sink_%d"))
    goto wrong_template;

  {
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
      GstOggPad *oggpad;

      oggpad = (GstOggPad *)
          gst_collectpads_add_pad (ogg_mux->collect, newpad,
          sizeof (GstOggPad));

      oggpad->serial = serial;
      ogg_stream_init (&oggpad->stream, serial);
      oggpad->packetno = 0;
      oggpad->pageno = 0;
      oggpad->eos = FALSE;
      /* we assume there will be some control data first for this pad */
      oggpad->state = GST_OGG_PAD_STATE_CONTROL;
      oggpad->new_page = TRUE;
      oggpad->first_delta = FALSE;
      oggpad->prev_delta = FALSE;
    }
  }

  /* setup some pad functions */
  gst_pad_set_link_function (newpad, gst_ogg_mux_sinkconnect);
  /* dd the pad to the element */
  gst_element_add_pad (element, newpad);

  return newpad;

  /* ERRORS */
wrong_direction:
  {
    g_warning ("ogg_mux: request pad that is not a SINK pad\n");
    return NULL;
  }
wrong_template:
  {
    g_warning ("ogg_mux: this is not our template!\n");
    return NULL;
  }
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

#if 0
static GstBuffer *
gst_ogg_mux_next_buffer (GstOggPad * pad, gboolean * interrupt)
{
  GstData *data = NULL;
  GstBuffer *buffer = NULL;

  while (buffer == NULL) {
    //gst_pad_pull (pad->pad, &buffer);
    GST_DEBUG ("muxer: pulled %s:%s %p", GST_DEBUG_PAD_NAME (pad->pad), data);
    /* if it's an event, handle it */
    if (GST_IS_EVENT (data)) {
      GstEventType type;
      GstOggMux *ogg_mux;
      GstEvent *event = GST_EVENT (data);

      ogg_mux = GST_OGG_MUX (gst_pad_get_parent (pad->pad));
      type = event ? GST_EVENT_TYPE (event) : GST_EVENT_UNKNOWN;

      switch (type) {
        case GST_EVENT_EOS:
          pad->eos = TRUE;
          gst_event_unref (event);
          return NULL;
        case GST_EVENT_DISCONTINUOUS:
        {
          guint64 start_value, end_value;

          if (gst_event_discont_get_value (event, GST_FORMAT_TIME,
                  &start_value, &end_value)) {
            GST_DEBUG_OBJECT (ogg_mux,
                "got discont of %" G_GUINT64_FORMAT " and %" G_GUINT64_FORMAT
                " on pad %s:%s", start_value, end_value,
                GST_DEBUG_PAD_NAME (pad->pad));
          }
          pad->offset = start_value;
          gst_event_unref (event);

        }
          break;
        default:
          gst_pad_event_default (pad->pad, event);
          break;
      }
      data = NULL;
    } else {
      GstBuffer *buf = GST_BUFFER (data);
      gboolean incaps;

      incaps = GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_IN_CAPS);
      /* if we need headers */
      if (pad->state == GST_OGG_PAD_STATE_CONTROL) {
        /* and we have one */
        if (incaps) {
          GST_DEBUG ("muxer: got incaps buffer in control state, ignoring");
          /* just ignore */
          gst_buffer_unref (buf);
          /* pull next one in next iteration */
          data = NULL;
        } else {
          GST_DEBUG
              ("muxer: got data buffer in control state, switching to data mode");
          /* this is a data buffer so switch to data state */
          pad->state = GST_OGG_PAD_STATE_DATA;
        }
      }
    }
  }
  return GST_BUFFER (data);
}
#endif

static GstBuffer *
gst_ogg_mux_buffer_from_page (GstOggMux * mux, ogg_page * page, gboolean delta)
{
  GstBuffer *buffer;

  /* allocate space for header and body */
  buffer = gst_buffer_new_and_alloc (page->header_len + page->body_len);
  memcpy (GST_BUFFER_DATA (buffer), page->header, page->header_len);
  memcpy (GST_BUFFER_DATA (buffer) + page->header_len,
      page->body, page->body_len);

  /* next_ts was the timestamp of the first buffer put in this page */
  GST_BUFFER_TIMESTAMP (buffer) = mux->next_ts;
  GST_BUFFER_OFFSET (buffer) = mux->offset;
  mux->offset += GST_BUFFER_SIZE (buffer);
  GST_BUFFER_OFFSET_END (buffer) = mux->offset;
  if (delta)
    GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DELTA_UNIT);

  return buffer;
}

static GstFlowReturn
gst_ogg_mux_push_page (GstOggMux * mux, ogg_page * page, gboolean delta)
{
  GstBuffer *buffer;
  GstFlowReturn ret;

  buffer = gst_ogg_mux_buffer_from_page (mux, page, delta);

  ret = gst_pad_push (mux->srcpad, buffer);

  return ret;
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
  walk = ogg_mux->collect->data;
  while (walk) {
    GstOggPad *pad;
    GstCollectData *data;

    data = (GstCollectData *) walk->data;
    pad = (GstOggPad *) data;

    walk = g_slist_next (walk);

    /* try to get a new buffer for this pad if needed and possible */
    if (pad->buffer == NULL) {
      GstBuffer *buf;
      gboolean incaps;

      buf = gst_collectpads_pop (ogg_mux->collect, data);

      /* On EOS we get a NULL buffer */
      if (buf != NULL) {
        incaps = GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_IN_CAPS);
        /* if we need headers */
        if (pad->state == GST_OGG_PAD_STATE_CONTROL) {
          /* and we have one */
          if (incaps) {
            GST_DEBUG ("muxer: got incaps buffer in control state, ignoring");
            /* just ignore */
            gst_buffer_unref (buf);
            buf = NULL;
            /* We discarded the data of this pad, so it's not EOS. If no bestpad
               selected so far then use this one */
            if (!bestpad) {
              bestpad = pad;
            }
          } else {
            GST_DEBUG ("muxer: got data buffer in control state, switching "
                "to data mode");
            /* this is a data buffer so switch to data state */
            pad->state = GST_OGG_PAD_STATE_DATA;
          }
        }
      }
      pad->buffer = buf;
    }

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

static GList *
gst_ogg_mux_get_headers (GstOggPad * pad)
{
  GList *res = NULL;
  GstOggMux *ogg_mux;
  GstStructure *structure;
  const GstCaps *caps;
  GstPad *thepad;

  thepad = pad->collect.pad;

  ogg_mux = GST_OGG_MUX (GST_PAD_PARENT (thepad));

  GST_LOG ("getting headers from pad %s:%s", GST_DEBUG_PAD_NAME (thepad));

  caps = gst_pad_get_negotiated_caps (thepad);
  if (caps != NULL) {
    const GValue *streamheader;

    structure = gst_caps_get_structure (caps, 0);
    streamheader = gst_structure_get_value (structure, "streamheader");
    if (streamheader != NULL) {
      GST_LOG ("got header");
      if (G_VALUE_TYPE (streamheader) == GST_TYPE_ARRAY) {
        GArray *bufarr = g_value_peek_pointer (streamheader);
        gint i;

        GST_LOG ("got fixed list");

        for (i = 0; i < bufarr->len; i++) {
          GValue *bufval = &g_array_index (bufarr, GValue, i);

          GST_LOG ("item %d", i);
          if (G_VALUE_TYPE (bufval) == GST_TYPE_BUFFER) {
            GstBuffer *buf = g_value_peek_pointer (bufval);

            GST_LOG ("adding item %d to header list", i);

            gst_buffer_ref (buf);
            res = g_list_append (res, buf);
          }
        }
      } else {
        GST_LOG ("streamheader is not fixed list");
      }
    } else {
      GST_LOG ("caps done have streamheader");
    }
  } else {
    GST_LOG ("got empty caps as negotiated format");
  }
  return res;
}

static GstCaps *
gst_ogg_mux_set_header_on_caps (GstCaps * caps, GList * buffers)
{
  GstStructure *structure;
  GValue list = { 0 };
  GList *walk = buffers;

  caps = gst_caps_make_writable (caps);

  structure = gst_caps_get_structure (caps, 0);

  /* put buffers in a fixed list */
  g_value_init (&list, GST_TYPE_ARRAY);

  while (walk) {
    GstBuffer *buf = GST_BUFFER (walk->data);
    GValue value = { 0 };

    walk = walk->next;

    /* mark buffer */
    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_IN_CAPS);

    g_value_init (&value, GST_TYPE_BUFFER);
    gst_value_set_buffer (&value, buf);
    gst_value_list_append_value (&list, &value);
    g_value_unset (&value);
  }
  gst_structure_set_value (structure, "streamheader", &list);
  g_value_unset (&list);

  return caps;
}

/*
 * For each pad we need to write out one (small) header in one
 * page that allows decoders to identify the type of the stream.
 * After that we need to write out all extra info for the decoders.
 * In the case of a codec that also needs data as configuration, we can
 * find that info in the streamcaps. 
 * After writing the headers we must start a new page for the data.
 */
static GstFlowReturn
gst_ogg_mux_send_headers (GstOggMux * mux)
{
  GSList *walk;
  GList *hbufs, *hwalk;
  GstCaps *caps;
  GstFlowReturn ret;

  hbufs = NULL;
  ret = GST_FLOW_OK;

  GST_LOG ("collecting headers");

  walk = mux->collect->data;
  while (walk) {
    GstOggPad *pad;
    GstPad *thepad;

    pad = (GstOggPad *) walk->data;
    thepad = pad->collect.pad;

    walk = g_slist_next (walk);

    GST_LOG ("looking at pad %s:%s", GST_DEBUG_PAD_NAME (thepad));

    /* if the pad has no buffer, we don't care */
    if (pad->buffer == NULL)
      continue;

    /* now figure out the headers */
    pad->headers = gst_ogg_mux_get_headers (pad);
  }

  GST_LOG ("creating first headers");
  walk = mux->collect->data;
  while (walk) {
    GstOggPad *pad;
    GstBuffer *buf;
    ogg_packet packet;
    ogg_page page;
    GstPad *thepad;

    pad = (GstOggPad *) walk->data;
    thepad = pad->collect.pad;

    walk = walk->next;

    pad->packetno = 0;

    GST_LOG ("looping over headers for pad %s:%s", GST_DEBUG_PAD_NAME (thepad));

    if (pad->headers) {
      buf = GST_BUFFER (pad->headers->data);
      pad->headers = g_list_remove (pad->headers, buf);
    } else {
      buf = pad->buffer;
      gst_buffer_ref (buf);
    }

    /* create a packet from the buffer */
    packet.packet = GST_BUFFER_DATA (buf);
    packet.bytes = GST_BUFFER_SIZE (buf);
    packet.granulepos = GST_BUFFER_OFFSET_END (buf);
    if (packet.granulepos == -1)
      packet.granulepos = 0;
    /* mark BOS and packet number */
    packet.b_o_s = (pad->packetno == 0);
    packet.packetno = pad->packetno++;
    /* mark EOS */
    packet.e_o_s = 0;

    /* swap the packet in */
    ogg_stream_packetin (&pad->stream, &packet);
    gst_buffer_unref (buf);

    GST_LOG ("flushing page with first packet");
    while (ogg_stream_flush (&pad->stream, &page)) {
      GstBuffer *hbuf = gst_ogg_mux_buffer_from_page (mux, &page, FALSE);

      GST_LOG ("swapped out page");
      hbufs = g_list_append (hbufs, hbuf);
    }
  }

  GST_LOG ("creating next headers");
  walk = mux->collect->data;
  while (walk) {
    GstOggPad *pad;
    GstPad *thepad;

    pad = (GstOggPad *) walk->data;
    thepad = pad->collect.pad;

    walk = walk->next;

    GST_LOG ("looping over headers for pad %s:%s", GST_DEBUG_PAD_NAME (thepad));

    hwalk = pad->headers;
    while (hwalk) {
      GstBuffer *buf = GST_BUFFER (hwalk->data);
      ogg_packet packet;
      ogg_page page;

      hwalk = hwalk->next;

      /* create a packet from the buffer */
      packet.packet = GST_BUFFER_DATA (buf);
      packet.bytes = GST_BUFFER_SIZE (buf);
      packet.granulepos = GST_BUFFER_OFFSET_END (buf);
      if (packet.granulepos == -1)
        packet.granulepos = 0;
      /* mark BOS and packet number */
      packet.b_o_s = (pad->packetno == 0);
      packet.packetno = pad->packetno++;
      /* mark EOS */
      packet.e_o_s = 0;

      /* swap the packet in */
      ogg_stream_packetin (&pad->stream, &packet);
      gst_buffer_unref (buf);

      /* if last header, flush page */
      if (hwalk == NULL) {
        GST_LOG ("flushing page as packet %d is first or last packet",
            pad->packetno);
        while (ogg_stream_flush (&pad->stream, &page)) {
          GstBuffer *hbuf = gst_ogg_mux_buffer_from_page (mux, &page, FALSE);

          GST_LOG ("swapped out page");
          hbufs = g_list_append (hbufs, hbuf);
        }
      } else {
        GST_LOG ("try to swap out page");
        /* just try to swap out a page then */
        while (ogg_stream_pageout (&pad->stream, &page) > 0) {
          GstBuffer *hbuf = gst_ogg_mux_buffer_from_page (mux, &page, FALSE);

          GST_LOG ("swapped out page");
          hbufs = g_list_append (hbufs, hbuf);
        }
      }
    }
    g_list_free (pad->headers);
    pad->headers = NULL;
  }
  /* hbufs holds all buffers for the headers now */

  /* create caps with the buffers */
  caps = gst_pad_get_caps (mux->srcpad);
  if (caps) {
    caps = gst_ogg_mux_set_header_on_caps (caps, hbufs);
    gst_pad_set_caps (mux->srcpad, caps);
  }
  /* and send the buffers */
  hwalk = hbufs;
  while (hwalk) {
    GstBuffer *buf = GST_BUFFER (hwalk->data);

    hwalk = hwalk->next;

    if ((ret = gst_pad_push (mux->srcpad, buf)) != GST_FLOW_OK)
      break;
  }
  g_list_free (hbufs);

  return ret;
}

/* this function is called when there is data on all pads.
 *
 * basic idea:
 *
 * 1) find a pad to pull on, this is done by looking at the buffers 
 *    to decide which one should be muxed first.
 * 2) store the selected pad and keep on pulling until we fill a 
 *    complete ogg page or the ogg page is filled above the max-delay 
 *    threshold. This is needed because the ogg spec says that
 *    you should fill a complete page with data from the same logical
 *    stream. When the page is filled, go back to 1).
 * 3) before filling a packet, read ahead one more buffer to see if this
 *    packet is the last of the stream. We need to do this because the ogg
 *    spec mandates that the last packet should have the EOS flag set before
 *    sending it to ogg.
 */
static GstFlowReturn
gst_ogg_mux_collected (GstCollectPads * pads, GstOggMux * ogg_mux)
{
  GstOggPad *best;
  gboolean delta_unit;
  GstFlowReturn ret;

  GST_DEBUG ("collected");

  best = gst_ogg_mux_queue_pads (ogg_mux);
  if (best && !best->buffer)
    return GST_FLOW_OK;

  GST_DEBUG ("best pad %p", best);

  if (!best) {                  /* EOS : FIXME !! We need to handle EOS correctly */
    gst_pad_push_event (ogg_mux->srcpad, gst_event_new_eos ());
    return GST_FLOW_WRONG_STATE;
  }

  /* we're pulling a pad and there is a better one, see if we need
   * to flush the current page */
  if (ogg_mux->pulling && best &&
      ogg_mux->pulling != best && ogg_mux->pulling->buffer) {
    GstOggPad *pad = ogg_mux->pulling;

    GstClockTime last_ts =
        GST_BUFFER_TIMESTAMP (pad->buffer) + GST_BUFFER_DURATION (pad->buffer);

    /* if the next packet in the current page is going to make the page 
     * too long, we need to flush */
    if (last_ts > ogg_mux->next_ts + ogg_mux->max_delay) {
      ogg_page page;

      while (ogg_stream_flush (&pad->stream, &page)) {
        ret = gst_ogg_mux_push_page (ogg_mux, &page, pad->first_delta);
        /* increment the page number counter */
        pad->pageno++;
        /* mark other pages as delta */
        pad->first_delta = TRUE;
      }
      pad->new_page = TRUE;
      ogg_mux->pulling = NULL;
    }
  }

  /* if we don't know which pad to pull on, use the best one */
  if (ogg_mux->pulling == NULL) {
    ogg_mux->pulling = best;
    /* remember timestamp of first buffer for this new pad */
    if (ogg_mux->pulling != NULL) {
      ogg_mux->next_ts = GST_BUFFER_TIMESTAMP (ogg_mux->pulling->buffer);
    } else {
      /* no pad to pull on, send EOS */
      gst_pad_push_event (ogg_mux->srcpad, gst_event_new_eos ());
      return GST_FLOW_WRONG_STATE;
    }
  }

  if (ogg_mux->need_headers) {
    ret = gst_ogg_mux_send_headers (ogg_mux);
    ogg_mux->need_headers = FALSE;
  }

  /* we are pulling from a pad, continue to do so until a page
   * has been filled and pushed */
  if (ogg_mux->pulling != NULL) {
    ogg_packet packet;
    ogg_page page;
    GstBuffer *buf, *tmpbuf;
    GstOggPad *pad = ogg_mux->pulling;
    gint64 duration;
    gboolean force_flush;

    /* now see if we have a buffer */
    buf = pad->buffer;

    delta_unit = GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_DELTA_UNIT);
    duration = GST_BUFFER_DURATION (buf);

    /* create a packet from the buffer */
    packet.packet = GST_BUFFER_DATA (buf);
    packet.bytes = GST_BUFFER_SIZE (buf);
    packet.granulepos = GST_BUFFER_OFFSET_END (buf);
    if (packet.granulepos == -1)
      packet.granulepos = 0;
    /* mark BOS and packet number */
    packet.b_o_s = (pad->packetno == 0);
    packet.packetno = pad->packetno++;

#if 0
    /* read ahead one more buffer to find EOS */
    tmpbuf = gst_ogg_mux_next_buffer (pad, &interrupt);
    if (interrupt)
      return;
    /* data exhausted on this pad */
    if (tmpbuf == NULL) {
      /* stop pulling from the pad */
      ogg_mux->pulling = NULL;
    }
    /* mark EOS */
    packet.e_o_s = (tmpbuf == NULL ? 1 : 0);
#else
    packet.e_o_s = 0;
    tmpbuf = NULL;
#endif

    /* we flush when we see a new keyframe */
    force_flush = (pad->prev_delta && !delta_unit);
    if (duration != -1) {
      pad->duration += duration;
      /* if page duration exceeds max, flush page */
      if (pad->duration > ogg_mux->max_page_delay) {
        force_flush = TRUE;
        pad->duration = 0;
      }
    }

    /* force flush */
    if (force_flush) {
      while (ogg_stream_flush (&pad->stream, &page)) {
        ret = gst_ogg_mux_push_page (ogg_mux, &page, pad->first_delta);
        /* increment the page number counter */
        pad->pageno++;
        /* mark other pages as delta */
        pad->first_delta = TRUE;
      }
      pad->new_page = TRUE;
    }

    /* if this is the first packet of a new page figure out the delta flag */
    if (pad->new_page) {
      if (delta_unit) {
        /* This page is a delta frame */
        if (ogg_mux->delta_pad == NULL) {
          /* we got a delta unit on this pad */
          ogg_mux->delta_pad = pad;
        }
        /* mark the page as delta */
        pad->first_delta = TRUE;
      } else {
        /* got a keyframe */
        if (ogg_mux->delta_pad == pad) {
          /* if we get it on the pad with deltaunits,
           * we mark the page as non delta */
          pad->first_delta = FALSE;
        } else if (ogg_mux->delta_pad != NULL) {
          /* if there are pads with delta frames, we
           * must mark this one as delta */
          pad->first_delta = TRUE;
        } else {
          pad->first_delta = FALSE;
        }
      }
      pad->new_page = FALSE;
    }

    /* save key unit to track delta->key unit transitions */
    pad->prev_delta = delta_unit;

    /* swap the packet in */
    if (packet.e_o_s == 1)
      GST_DEBUG_OBJECT (pad, "swapping in EOS packet");
    if (packet.b_o_s == 1)
      GST_DEBUG_OBJECT (pad, "swapping in BOS packet");

    ogg_stream_packetin (&pad->stream, &packet);

    /* don't need the old buffer anymore */
    gst_buffer_unref (pad->buffer);
    /* store new readahead buffer */
    pad->buffer = tmpbuf;

    /* let ogg write out the pages now. The packet we got could end 
     * up in more than one page so we need to write them all */
    if (ogg_stream_pageout (&pad->stream, &page) > 0) {
      /* push the page */
      ret = gst_ogg_mux_push_page (ogg_mux, &page, pad->first_delta);
      pad->pageno++;
      /* mark next pages as delta */
      pad->first_delta = TRUE;

      /* use an inner loop her to flush the remaining pages and
       * mark them as delta frames as well */
      while (ogg_stream_pageout (&pad->stream, &page) > 0) {
        /* we have a complete page now, we can push the page 
         * and make sure to pull on a new pad the next time around */
        ret = gst_ogg_mux_push_page (ogg_mux, &page, pad->first_delta);
        /* increment the page number counter */
        pad->pageno++;
      }
      /* need a new page as well */
      pad->new_page = TRUE;
      pad->duration = 0;
      /* we're done pulling on this pad, make sure to choose a new 
       * pad for pulling in the next iteration */
      ogg_mux->pulling = NULL;
    }
  }

  return GST_FLOW_OK;
}

static void
gst_ogg_mux_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstOggMux *ogg_mux;

  ogg_mux = GST_OGG_MUX (object);

  switch (prop_id) {
    case ARG_MAX_DELAY:
      g_value_set_uint64 (value, ogg_mux->max_delay);
      break;
    case ARG_MAX_PAGE_DELAY:
      g_value_set_uint64 (value, ogg_mux->max_page_delay);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ogg_mux_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstOggMux *ogg_mux;

  ogg_mux = GST_OGG_MUX (object);

  switch (prop_id) {
    case ARG_MAX_DELAY:
      ogg_mux->max_delay = g_value_get_uint64 (value);
      break;
    case ARG_MAX_PAGE_DELAY:
      ogg_mux->max_page_delay = g_value_get_uint64 (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_ogg_mux_change_state (GstElement * element, GstStateChange transition)
{
  GstOggMux *ogg_mux;
  GstStateChangeReturn ret;

  ogg_mux = GST_OGG_MUX (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      ogg_mux->next_ts = 0;
      ogg_mux->offset = 0;
      ogg_mux->pulling = NULL;
      gst_collectpads_start (ogg_mux->collect);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_collectpads_stop (ogg_mux->collect);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  return ret;
}

gboolean
gst_ogg_mux_plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_ogg_mux_debug, "oggmux", 0, "ogg muxer");

  return gst_element_register (plugin, "oggmux", GST_RANK_NONE,
      GST_TYPE_OGG_MUX);
}
