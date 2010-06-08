/* OGG muxer plugin for GStreamer
 * Copyright (C) 2004 Wim Taymans <wim@fluendo.com>
 * Copyright (C) 2006 Thomas Vander Stichele <thomas at apestaart dot org>
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
 * SECTION:element-oggmux
 * @see_also: <link linkend="gst-plugins-base-plugins-oggdemux">oggdemux</link>
 *
 * This element merges streams (audio and video) into ogg files.
 *
 * <refsect2>
 * <title>Example pipelines</title>
 * |[
 * gst-launch v4l2src num-buffers=500 ! video/x-raw-yuv,width=320,height=240 ! ffmpegcolorspace ! theoraenc ! oggmux ! filesink location=video.ogg
 * ]| Encodes a video stream captured from a v4l2-compatible camera to Ogg/Theora
 * (the encoding will stop automatically after 500 frames)
 * </refsect2>
 *
 * Last reviewed on 2008-02-06 (0.10.17)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/gstcollectpads.h>
#include <gst/tag/tag.h>

#include "gstoggmux.h"

/* memcpy - if someone knows a way to get rid of it, please speak up
 * note: the ogg docs even say you need this... */
#include <string.h>
#include <time.h>
#include <stdlib.h>             /* rand, srand, atoi */

GST_DEBUG_CATEGORY_STATIC (gst_ogg_mux_debug);
#define GST_CAT_DEFAULT gst_ogg_mux_debug

/* This isn't generally what you'd want with an end-time macro, because
   technically the end time of a buffer with invalid duration is invalid. But
   for sorting ogg pages this is what we want. */
#define GST_BUFFER_END_TIME(buf) \
    (GST_BUFFER_DURATION_IS_VALID (buf) \
    ? GST_BUFFER_TIMESTAMP (buf) + GST_BUFFER_DURATION (buf) \
    : GST_BUFFER_TIMESTAMP (buf))

#define GST_GP_FORMAT "[gp %8" G_GINT64_FORMAT "]"
#define GST_GP_CAST(_gp) ((gint64) _gp)

typedef enum
{
  GST_OGG_FLAG_BOS = GST_ELEMENT_FLAG_LAST,
  GST_OGG_FLAG_EOS
}
GstOggFlag;

/* OggMux signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

/* set to 0.5 seconds by default */
#define DEFAULT_MAX_DELAY       G_GINT64_CONSTANT(500000000)
#define DEFAULT_MAX_PAGE_DELAY  G_GINT64_CONSTANT(500000000)
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
        "audio/x-vorbis; audio/x-flac; audio/x-speex; audio/x-celt; "
        "application/x-ogm-video; application/x-ogm-audio; video/x-dirac; "
        "video/x-smoke; video/x-vp8; text/x-cmml, encoded = (boolean) TRUE; "
        "subtitle/x-kate; application/x-kate")
    );

static void gst_ogg_mux_base_init (gpointer g_class);
static void gst_ogg_mux_class_init (GstOggMuxClass * klass);
static void gst_ogg_mux_init (GstOggMux * ogg_mux);
static void gst_ogg_mux_finalize (GObject * object);

static GstFlowReturn
gst_ogg_mux_collected (GstCollectPads * pads, GstOggMux * ogg_mux);
static gboolean gst_ogg_mux_handle_src_event (GstPad * pad, GstEvent * event);
static GstPad *gst_ogg_mux_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name);
static void gst_ogg_mux_release_pad (GstElement * element, GstPad * pad);

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

  if (G_UNLIKELY (ogg_mux_type == 0)) {
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
    static const GInterfaceInfo preset_info = {
      NULL,
      NULL,
      NULL
    };

    ogg_mux_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstOggMux", &ogg_mux_info,
        0);

    g_type_add_interface_static (ogg_mux_type, GST_TYPE_PRESET, &preset_info);
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

  gst_element_class_set_details_simple (element_class,
      "Ogg muxer", "Codec/Muxer",
      "mux ogg streams (info about ogg: http://xiph.org)",
      "Wim Taymans <wim@fluendo.com>");
}

static void
gst_ogg_mux_class_init (GstOggMuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->finalize = gst_ogg_mux_finalize;
  gobject_class->get_property = gst_ogg_mux_get_property;
  gobject_class->set_property = gst_ogg_mux_set_property;

  gstelement_class->request_new_pad = gst_ogg_mux_request_new_pad;
  gstelement_class->release_pad = gst_ogg_mux_release_pad;

  g_object_class_install_property (gobject_class, ARG_MAX_DELAY,
      g_param_spec_uint64 ("max-delay", "Max delay",
          "Maximum delay in multiplexing streams", 0, G_MAXUINT64,
          DEFAULT_MAX_DELAY,
          (GParamFlags) G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_MAX_PAGE_DELAY,
      g_param_spec_uint64 ("max-page-delay", "Max page delay",
          "Maximum delay for sending out a page", 0, G_MAXUINT64,
          DEFAULT_MAX_PAGE_DELAY,
          (GParamFlags) G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

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
gst_ogg_mux_clear (GstOggMux * ogg_mux)
{
  ogg_mux->pulling = NULL;
  ogg_mux->need_headers = TRUE;
  ogg_mux->delta_pad = NULL;
  ogg_mux->offset = 0;
  ogg_mux->next_ts = 0;
  ogg_mux->last_ts = GST_CLOCK_TIME_NONE;
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

  GST_OBJECT_FLAG_SET (GST_ELEMENT (ogg_mux), GST_OGG_FLAG_BOS);

  /* seed random number generator for creation of serial numbers */
  srand (time (NULL));

  ogg_mux->collect = gst_collect_pads_new ();
  gst_collect_pads_set_function (ogg_mux->collect,
      (GstCollectPadsFunction) GST_DEBUG_FUNCPTR (gst_ogg_mux_collected),
      ogg_mux);

  ogg_mux->max_delay = DEFAULT_MAX_DELAY;
  ogg_mux->max_page_delay = DEFAULT_MAX_PAGE_DELAY;

  gst_ogg_mux_clear (ogg_mux);
}

static void
gst_ogg_mux_finalize (GObject * object)
{
  GstOggMux *ogg_mux;

  ogg_mux = GST_OGG_MUX (object);

  if (ogg_mux->collect) {
    gst_object_unref (ogg_mux->collect);
    ogg_mux->collect = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_ogg_mux_ogg_pad_destroy_notify (GstCollectData * data)
{
  GstOggPadData *oggpad = (GstOggPadData *) data;
  GstBuffer *buf;

  ogg_stream_clear (&oggpad->stream);

  if (oggpad->pagebuffers) {
    while ((buf = g_queue_pop_head (oggpad->pagebuffers)) != NULL) {
      gst_buffer_unref (buf);
    }
    g_queue_free (oggpad->pagebuffers);
    oggpad->pagebuffers = NULL;
  }
}

static GstPadLinkReturn
gst_ogg_mux_sinkconnect (GstPad * pad, GstPad * peer)
{
  GstOggMux *ogg_mux;

  ogg_mux = GST_OGG_MUX (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (ogg_mux, "sinkconnect triggered on %s", GST_PAD_NAME (pad));

  gst_object_unref (ogg_mux);

  return GST_PAD_LINK_OK;
}

static gboolean
gst_ogg_mux_sink_event (GstPad * pad, GstEvent * event)
{
  GstOggMux *ogg_mux = GST_OGG_MUX (gst_pad_get_parent (pad));
  GstOggPadData *ogg_pad = (GstOggPadData *) gst_pad_get_element_private (pad);
  gboolean ret;

  GST_DEBUG ("Got %s event on pad %s:%s", GST_EVENT_TYPE_NAME (event),
      GST_DEBUG_PAD_NAME (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:
      /* We don't support NEWSEGMENT events */
      gst_event_unref (event);
      ret = FALSE;
      break;
    default:
      ret = TRUE;
      break;
  }

  /* now GstCollectPads can take care of the rest, e.g. EOS */
  if (ret)
    ret = ogg_pad->collect_event (pad, event);

  gst_object_unref (ogg_mux);
  return ret;
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
    GST_DEBUG_OBJECT (ogg_mux, "Creating new pad for serial %d", serial);
    name = g_strdup_printf ("sink_%d", serial);
    newpad = gst_pad_new_from_template (templ, name);
    g_free (name);

    /* construct our own wrapper data structure for the pad to
     * keep track of its status */
    {
      GstOggPadData *oggpad;

      oggpad = (GstOggPadData *)
          gst_collect_pads_add_pad_full (ogg_mux->collect, newpad,
          sizeof (GstOggPadData), gst_ogg_mux_ogg_pad_destroy_notify);
      ogg_mux->active_pads++;

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
      oggpad->pagebuffers = g_queue_new ();

      oggpad->collect_event = (GstPadEventFunction) GST_PAD_EVENTFUNC (newpad);
      gst_pad_set_event_function (newpad,
          GST_DEBUG_FUNCPTR (gst_ogg_mux_sink_event));
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

static void
gst_ogg_mux_release_pad (GstElement * element, GstPad * pad)
{
  GstOggMux *ogg_mux;

  ogg_mux = GST_OGG_MUX (gst_pad_get_parent (pad));

  gst_collect_pads_remove_pad (ogg_mux->collect, pad);
  gst_element_remove_pad (element, pad);

  gst_object_unref (ogg_mux);
}

/* handle events */
static gboolean
gst_ogg_mux_handle_src_event (GstPad * pad, GstEvent * event)
{
  GstEventType type;

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
gst_ogg_mux_buffer_from_page (GstOggMux * mux, ogg_page * page, gboolean delta)
{
  GstBuffer *buffer;

  /* allocate space for header and body */
  buffer = gst_buffer_new_and_alloc (page->header_len + page->body_len);
  memcpy (GST_BUFFER_DATA (buffer), page->header, page->header_len);
  memcpy (GST_BUFFER_DATA (buffer) + page->header_len,
      page->body, page->body_len);

  /* Here we set granulepos as our OFFSET_END to give easy direct access to
   * this value later. Before we push it, we reset this to OFFSET + SIZE
   * (see gst_ogg_mux_push_buffer). */
  GST_BUFFER_OFFSET_END (buffer) = ogg_page_granulepos (page);
  if (delta)
    GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DELTA_UNIT);

  GST_LOG_OBJECT (mux, GST_GP_FORMAT
      " created buffer %p from ogg page",
      GST_GP_CAST (ogg_page_granulepos (page)), buffer);

  return buffer;
}

static GstFlowReturn
gst_ogg_mux_push_buffer (GstOggMux * mux, GstBuffer * buffer)
{
  GstCaps *caps;

  /* fix up OFFSET and OFFSET_END again */
  GST_BUFFER_OFFSET (buffer) = mux->offset;
  mux->offset += GST_BUFFER_SIZE (buffer);
  GST_BUFFER_OFFSET_END (buffer) = mux->offset;

  /* Ensure we have monotonically increasing timestamps in the output. */
  if (GST_BUFFER_TIMESTAMP_IS_VALID (buffer)) {
    if (mux->last_ts != GST_CLOCK_TIME_NONE &&
        GST_BUFFER_TIMESTAMP (buffer) < mux->last_ts)
      GST_BUFFER_TIMESTAMP (buffer) = mux->last_ts;
    else
      mux->last_ts = GST_BUFFER_TIMESTAMP (buffer);
  }

  caps = gst_pad_get_negotiated_caps (mux->srcpad);
  gst_buffer_set_caps (buffer, caps);
  if (caps)
    gst_caps_unref (caps);

  return gst_pad_push (mux->srcpad, buffer);
}

/* if all queues have at least one page, dequeue the page with the lowest
 * timestamp */
static gboolean
gst_ogg_mux_dequeue_page (GstOggMux * mux, GstFlowReturn * flowret)
{
  GSList *walk;
  GstOggPadData *opad = NULL;   /* "oldest" pad */
  GstClockTime oldest = GST_CLOCK_TIME_NONE;
  GstBuffer *buf = NULL;
  gboolean ret = FALSE;

  *flowret = GST_FLOW_OK;

  walk = mux->collect->data;
  while (walk) {
    GstOggPadData *pad = (GstOggPadData *) walk->data;

    /* We need each queue to either be at EOS, or have one or more pages
     * available with a set granulepos (i.e. not -1), otherwise we don't have
     * enough data yet to determine which stream needs to go next for correct
     * time ordering. */
    if (pad->pagebuffers->length == 0) {
      if (pad->eos) {
        GST_LOG_OBJECT (pad->collect.pad,
            "pad is EOS, skipping for dequeue decision");
      } else {
        GST_LOG_OBJECT (pad->collect.pad,
            "no pages in this queue, can't dequeue");
        return FALSE;
      }
    } else {
      /* We then need to check for a non-negative granulepos */
      int i;
      gboolean valid = FALSE;

      for (i = 0; i < pad->pagebuffers->length; i++) {
        buf = g_queue_peek_nth (pad->pagebuffers, i);
        /* Here we check the OFFSET_END, which is actually temporarily the
         * granulepos value for this buffer */
        if (GST_BUFFER_OFFSET_END (buf) != -1) {
          valid = TRUE;
          break;
        }
      }
      if (!valid) {
        GST_LOG_OBJECT (pad->collect.pad,
            "No page timestamps in queue, can't dequeue");
        return FALSE;
      }
    }

    walk = g_slist_next (walk);
  }

  walk = mux->collect->data;
  while (walk) {
    GstOggPadData *pad = (GstOggPadData *) walk->data;

    /* any page with a granulepos of -1 can be pushed immediately.
     * TODO: it CAN be, but it seems silly to do so? */
    buf = g_queue_peek_head (pad->pagebuffers);
    while (buf && GST_BUFFER_OFFSET_END (buf) == -1) {
      GST_LOG_OBJECT (pad->collect.pad, "[gp        -1] pushing page");
      g_queue_pop_head (pad->pagebuffers);
      *flowret = gst_ogg_mux_push_buffer (mux, buf);
      buf = g_queue_peek_head (pad->pagebuffers);
      ret = TRUE;
    }

    if (buf) {
      /* if no oldest buffer yet, take this one */
      if (oldest == GST_CLOCK_TIME_NONE) {
        GST_LOG_OBJECT (mux, "no oldest yet, taking buffer %p from pad %"
            GST_PTR_FORMAT " with gp time %" GST_TIME_FORMAT,
            buf, pad->collect.pad, GST_TIME_ARGS (GST_BUFFER_OFFSET (buf)));
        oldest = GST_BUFFER_OFFSET (buf);
        opad = pad;
      } else {
        /* if we have an oldest, compare with this one */
        if (GST_BUFFER_OFFSET (buf) < oldest) {
          GST_LOG_OBJECT (mux, "older buffer %p, taking from pad %"
              GST_PTR_FORMAT " with gp time %" GST_TIME_FORMAT,
              buf, pad->collect.pad, GST_TIME_ARGS (GST_BUFFER_OFFSET (buf)));
          oldest = GST_BUFFER_OFFSET (buf);
          opad = pad;
        }
      }
    }
    walk = g_slist_next (walk);
  }

  if (oldest != GST_CLOCK_TIME_NONE) {
    g_assert (opad);
    buf = g_queue_pop_head (opad->pagebuffers);
    GST_LOG_OBJECT (opad->collect.pad,
        GST_GP_FORMAT " pushing oldest page buffer %p (granulepos time %"
        GST_TIME_FORMAT ")", GST_BUFFER_OFFSET_END (buf), buf,
        GST_TIME_ARGS (GST_BUFFER_OFFSET (buf)));
    *flowret = gst_ogg_mux_push_buffer (mux, buf);
    ret = TRUE;
  }

  return ret;
}

/* put the given ogg page on a per-pad queue, timestamping it correctly.
 * after that, dequeue and push as many pages as possible.
 * Caller should make sure:
 * pad->timestamp     was set with the timestamp of the first packet put
 *                    on the page
 * pad->timestamp_end was set with the timestamp + duration of the last packet
 *                    put on the page
 * pad->gp_time       was set with the time matching the gp of the last
 *                    packet put on the page
 *
 * will also reset timestamp and timestamp_end, so caller func can restart
 * counting.
 */
static GstFlowReturn
gst_ogg_mux_pad_queue_page (GstOggMux * mux, GstOggPadData * pad,
    ogg_page * page, gboolean delta)
{
  GstFlowReturn ret;
  GstBuffer *buffer = gst_ogg_mux_buffer_from_page (mux, page, delta);

  /* take the timestamp of the first packet on this page */
  GST_BUFFER_TIMESTAMP (buffer) = pad->timestamp;
  GST_BUFFER_DURATION (buffer) = pad->timestamp_end - pad->timestamp;
  /* take the gp time of the last completed packet on this page */
  GST_BUFFER_OFFSET (buffer) = pad->gp_time;

  /* the next page will start where the current page's end time leaves off */
  pad->timestamp = pad->timestamp_end;

  g_queue_push_tail (pad->pagebuffers, buffer);
  GST_LOG_OBJECT (pad->collect.pad, GST_GP_FORMAT
      " queued buffer page %p (gp time %"
      GST_TIME_FORMAT ", timestamp %" GST_TIME_FORMAT
      "), %d page buffers queued", GST_GP_CAST (ogg_page_granulepos (page)),
      buffer, GST_TIME_ARGS (GST_BUFFER_OFFSET (buffer)),
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)),
      g_queue_get_length (pad->pagebuffers));

  while (gst_ogg_mux_dequeue_page (mux, &ret)) {
    if (ret != GST_FLOW_OK)
      break;
  }

  return ret;
}

/*
 * Given two pads, compare the buffers queued on it.
 * Returns:
 *  0 if they have an equal priority
 * -1 if the first is better
 *  1 if the second is better
 * Priority decided by: a) validity, b) older timestamp, c) smaller number
 * of muxed pages
 */
static gint
gst_ogg_mux_compare_pads (GstOggMux * ogg_mux, GstOggPadData * first,
    GstOggPadData * second)
{
  guint64 firsttime, secondtime;

  /* if the first pad doesn't contain anything or is even NULL, return
   * the second pad as best candidate and vice versa */
  if (first == NULL || (first->buffer == NULL && first->next_buffer == NULL))
    return 1;
  if (second == NULL || (second->buffer == NULL && second->next_buffer == NULL))
    return -1;

  /* no timestamp on first buffer, it must go first */
  if (first->buffer)
    firsttime = GST_BUFFER_TIMESTAMP (first->buffer);
  else
    firsttime = GST_BUFFER_TIMESTAMP (first->next_buffer);
  if (firsttime == GST_CLOCK_TIME_NONE)
    return -1;

  /* no timestamp on second buffer, it must go first */
  if (second->buffer)
    secondtime = GST_BUFFER_TIMESTAMP (second->buffer);
  else
    secondtime = GST_BUFFER_TIMESTAMP (second->next_buffer);
  if (secondtime == GST_CLOCK_TIME_NONE)
    return 1;

  /* first buffer has higher timestamp, second one should go first */
  if (secondtime < firsttime)
    return 1;
  /* second buffer has higher timestamp, first one should go first */
  else if (secondtime > firsttime)
    return -1;
  else {
    /* buffers with equal timestamps, prefer the pad that has the
     * least number of pages muxed */
    if (second->pageno < first->pageno)
      return 1;
    else if (second->pageno > first->pageno)
      return -1;
  }

  /* same priority if all of the above failed */
  return 0;
}

/* make sure at least one buffer is queued on all pads, two if possible
 * 
 * if pad->buffer == NULL, pad->next_buffer !=  NULL, then
 *   we do not know if the buffer is the last or not
 * if pad->buffer != NULL, pad->next_buffer != NULL, then
 *   pad->buffer is not the last buffer for the pad
 * if pad->buffer != NULL, pad->next_buffer == NULL, then
 *   pad->buffer if the last buffer for the pad
 * 
 * returns a pointer to an oggpad that holds the best buffer, or
 * NULL when no pad was usable. "best" means the buffer marked
 * with the lowest timestamp. If best->buffer == NULL then nothing
 * should be done until more data arrives */
static GstOggPadData *
gst_ogg_mux_queue_pads (GstOggMux * ogg_mux)
{
  GstOggPadData *bestpad = NULL, *still_hungry = NULL;
  GSList *walk;

  /* try to make sure we have a buffer from each usable pad first */
  walk = ogg_mux->collect->data;
  while (walk) {
    GstOggPadData *pad;
    GstCollectData *data;

    data = (GstCollectData *) walk->data;
    pad = (GstOggPadData *) data;

    walk = g_slist_next (walk);

    GST_LOG_OBJECT (data->pad, "looking at pad for buffer");

    /* try to get a new buffer for this pad if needed and possible */
    if (pad->buffer == NULL) {
      GstBuffer *buf;
      gboolean incaps;

      /* shift the buffer along if needed (it's okay if next_buffer is NULL) */
      if (pad->buffer == NULL) {
        GST_LOG_OBJECT (data->pad, "shifting buffer %" GST_PTR_FORMAT,
            pad->next_buffer);
        pad->buffer = pad->next_buffer;
        pad->next_buffer = NULL;
      }

      buf = gst_collect_pads_pop (ogg_mux->collect, data);
      GST_LOG_OBJECT (data->pad, "popped buffer %" GST_PTR_FORMAT, buf);

      /* On EOS we get a NULL buffer */
      if (buf != NULL) {
        if (ogg_mux->delta_pad == NULL &&
            GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_DELTA_UNIT))
          ogg_mux->delta_pad = pad;

        incaps = GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_IN_CAPS);
        /* if we need headers */
        if (pad->state == GST_OGG_PAD_STATE_CONTROL) {
          /* and we have one */
          if (incaps) {
            GST_DEBUG_OBJECT (ogg_mux,
                "got incaps buffer in control state, ignoring");
            /* just ignore */
            gst_buffer_unref (buf);
            buf = NULL;
          } else {
            GST_DEBUG_OBJECT (ogg_mux,
                "got data buffer in control state, switching " "to data mode");
            /* this is a data buffer so switch to data state */
            pad->state = GST_OGG_PAD_STATE_DATA;
          }
        }
      } else {
        GST_DEBUG_OBJECT (data->pad, "EOS on pad");
        if (!pad->eos) {
          ogg_page page;
          GstFlowReturn ret;

          /* it's no longer active */
          ogg_mux->active_pads--;

          /* Just gone to EOS. Flush existing page(s) */
          pad->eos = TRUE;

          while (ogg_stream_flush (&pad->stream, &page)) {
            /* Place page into the per-pad queue */
            ret = gst_ogg_mux_pad_queue_page (ogg_mux, pad, &page,
                pad->first_delta);
            /* increment the page number counter */
            pad->pageno++;
            /* mark other pages as delta */
            pad->first_delta = TRUE;
          }
        }
      }

      pad->next_buffer = buf;
    }

    /* we should have a buffer now, see if it is the best pad to
     * pull on */
    if (pad->buffer || pad->next_buffer) {
      if (gst_ogg_mux_compare_pads (ogg_mux, bestpad, pad) > 0) {
        GST_LOG_OBJECT (data->pad,
            "new best pad, with buffers %" GST_PTR_FORMAT
            " and %" GST_PTR_FORMAT, pad->buffer, pad->next_buffer);

        bestpad = pad;
      }
    } else if (!pad->eos) {
      GST_LOG_OBJECT (data->pad, "hungry pad");
      still_hungry = pad;
    }
  }

  if (still_hungry)
    /* drop back into collectpads... */
    return still_hungry;
  else
    return bestpad;
}

static GList *
gst_ogg_mux_get_headers (GstOggPadData * pad)
{
  GList *res = NULL;
  GstStructure *structure;
  GstCaps *caps;
  GstPad *thepad;

  thepad = pad->collect.pad;

  GST_LOG_OBJECT (thepad, "getting headers");

  caps = gst_pad_get_negotiated_caps (thepad);
  if (caps != NULL) {
    const GValue *streamheader;

    structure = gst_caps_get_structure (caps, 0);
    streamheader = gst_structure_get_value (structure, "streamheader");
    if (streamheader != NULL) {
      GST_LOG_OBJECT (thepad, "got header");
      if (G_VALUE_TYPE (streamheader) == GST_TYPE_ARRAY) {
        GArray *bufarr = g_value_peek_pointer (streamheader);
        gint i;

        GST_LOG_OBJECT (thepad, "got fixed list");

        for (i = 0; i < bufarr->len; i++) {
          GValue *bufval = &g_array_index (bufarr, GValue, i);

          GST_LOG_OBJECT (thepad, "item %d", i);
          if (G_VALUE_TYPE (bufval) == GST_TYPE_BUFFER) {
            GstBuffer *buf = g_value_peek_pointer (bufval);

            GST_LOG_OBJECT (thepad, "adding item %d to header list", i);

            gst_buffer_ref (buf);
            res = g_list_append (res, buf);
          }
        }
      } else {
        GST_LOG_OBJECT (thepad, "streamheader is not fixed list");
      }

      /* Start a new page for every CMML buffer */
      if (gst_structure_has_name (structure, "text/x-cmml"))
        pad->always_flush_page = TRUE;
    } else if (gst_structure_has_name (structure, "video/x-dirac")) {
      res = g_list_append (res, pad->buffer);
      pad->buffer = pad->next_buffer;
      pad->next_buffer = NULL;
      pad->always_flush_page = TRUE;
    } else {
      GST_LOG_OBJECT (thepad, "caps don't have streamheader");
    }
    gst_caps_unref (caps);
  } else {
    GST_LOG_OBJECT (thepad, "got empty caps as negotiated format");
  }
  return res;
}

static GstCaps *
gst_ogg_mux_set_header_on_caps (GstCaps * caps, GList * buffers)
{
  GstStructure *structure;
  GValue array = { 0 };
  GList *walk = buffers;

  caps = gst_caps_make_writable (caps);

  structure = gst_caps_get_structure (caps, 0);

  /* put buffers in a fixed list */
  g_value_init (&array, GST_TYPE_ARRAY);

  while (walk) {
    GstBuffer *buf = GST_BUFFER (walk->data);
    GstBuffer *copy;
    GValue value = { 0 };

    walk = walk->next;

    /* mark buffer */
    GST_LOG ("Setting IN_CAPS on buffer of length %d", GST_BUFFER_SIZE (buf));
    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_IN_CAPS);

    g_value_init (&value, GST_TYPE_BUFFER);
    copy = gst_buffer_copy (buf);
    gst_value_set_buffer (&value, copy);
    gst_buffer_unref (copy);
    gst_value_array_append_value (&array, &value);
    g_value_unset (&value);
  }
  gst_structure_set_value (structure, "streamheader", &array);
  g_value_unset (&array);

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

  GST_LOG_OBJECT (mux, "collecting headers");

  walk = mux->collect->data;
  while (walk) {
    GstOggPadData *pad;
    GstPad *thepad;

    pad = (GstOggPadData *) walk->data;
    thepad = pad->collect.pad;

    walk = g_slist_next (walk);

    GST_LOG_OBJECT (mux, "looking at pad %s:%s", GST_DEBUG_PAD_NAME (thepad));

    /* if the pad has no buffer, we don't care */
    if (pad->buffer == NULL && pad->next_buffer == NULL)
      continue;

    /* now figure out the headers */
    pad->headers = gst_ogg_mux_get_headers (pad);
  }

  GST_LOG_OBJECT (mux, "creating BOS pages");
  walk = mux->collect->data;
  while (walk) {
    GstOggPadData *pad;
    GstBuffer *buf;
    ogg_packet packet;
    ogg_page page;
    GstPad *thepad;
    GstCaps *caps;
    GstStructure *structure;
    GstBuffer *hbuf;

    pad = (GstOggPadData *) walk->data;
    thepad = pad->collect.pad;
    caps = gst_pad_get_negotiated_caps (thepad);
    structure = gst_caps_get_structure (caps, 0);

    walk = walk->next;

    pad->packetno = 0;

    GST_LOG_OBJECT (thepad, "looping over headers");

    if (pad->headers) {
      buf = GST_BUFFER (pad->headers->data);
      pad->headers = g_list_remove (pad->headers, buf);
    } else if (pad->buffer) {
      buf = pad->buffer;
      gst_buffer_ref (buf);
    } else if (pad->next_buffer) {
      buf = pad->next_buffer;
      gst_buffer_ref (buf);
    } else {
      /* fixme -- should be caught in the previous list traversal. */
      GST_OBJECT_LOCK (pad);
      g_critical ("No headers or buffers on pad %s:%s",
          GST_DEBUG_PAD_NAME (pad));
      GST_OBJECT_UNLOCK (pad);
      continue;
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

    GST_LOG_OBJECT (thepad, "flushing out BOS page");
    if (!ogg_stream_flush (&pad->stream, &page))
      g_critical ("Could not flush BOS page");

    hbuf = gst_ogg_mux_buffer_from_page (mux, &page, FALSE);

    GST_LOG_OBJECT (mux, "swapped out page with mime type %s",
        gst_structure_get_name (structure));

    /* quick hack: put Theora, VP8 and Dirac video pages at the front.
     * Ideally, we would have a settable enum for which Ogg
     * profile we work with, and order based on that.
     * (FIXME: if there is more than one video stream, shouldn't we only put
     * one's BOS into the first page, followed by an audio stream's BOS, and
     * only then followed by the remaining video and audio streams?) */
    if (gst_structure_has_name (structure, "video/x-theora")) {
      GST_DEBUG_OBJECT (thepad, "putting %s page at the front", "Theora");
      hbufs = g_list_prepend (hbufs, hbuf);
    } else if (gst_structure_has_name (structure, "video/x-dirac")) {
      GST_DEBUG_OBJECT (thepad, "putting %s page at the front", "Dirac");
      hbufs = g_list_prepend (hbufs, hbuf);
      pad->always_flush_page = TRUE;
    } else if (gst_structure_has_name (structure, "video/x-vp8")) {
      GST_DEBUG_OBJECT (thepad, "putting %s page at the front", "VP8");
      hbufs = g_list_prepend (hbufs, hbuf);
    } else {
      hbufs = g_list_append (hbufs, hbuf);
    }
    gst_caps_unref (caps);
  }

  GST_LOG_OBJECT (mux, "creating next headers");
  walk = mux->collect->data;
  while (walk) {
    GstOggPadData *pad;
    GstPad *thepad;

    pad = (GstOggPadData *) walk->data;
    thepad = pad->collect.pad;

    walk = walk->next;

    GST_LOG_OBJECT (mux, "looping over headers for pad %s:%s",
        GST_DEBUG_PAD_NAME (thepad));

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
        GST_LOG_OBJECT (mux,
            "flushing page as packet %" G_GUINT64_FORMAT " is first or "
            "last packet", pad->packetno);
        while (ogg_stream_flush (&pad->stream, &page)) {
          GstBuffer *hbuf = gst_ogg_mux_buffer_from_page (mux, &page, FALSE);

          GST_LOG_OBJECT (mux, "swapped out page");
          hbufs = g_list_append (hbufs, hbuf);
        }
      } else {
        GST_LOG_OBJECT (mux, "try to swap out page");
        /* just try to swap out a page then */
        while (ogg_stream_pageout (&pad->stream, &page) > 0) {
          GstBuffer *hbuf = gst_ogg_mux_buffer_from_page (mux, &page, FALSE);

          GST_LOG_OBJECT (mux, "swapped out page");
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
    gst_caps_unref (caps);
  }
  /* and send the buffers */
  while (hbufs != NULL) {
    GstBuffer *buf = GST_BUFFER (hbufs->data);

    hbufs = g_list_delete_link (hbufs, hbufs);

    if ((ret = gst_ogg_mux_push_buffer (mux, buf)) != GST_FLOW_OK)
      break;
  }
  /* free any remaining nodes/buffers in case we couldn't push them */
  g_list_foreach (hbufs, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (hbufs);

  return ret;
}

/* this function is called to process data on the best pending pad.
 *
 * basic idea:
 *
 * 1) store the selected pad and keep on pulling until we fill a
 *    complete ogg page or the ogg page is filled above the max-delay
 *    threshold. This is needed because the ogg spec says that
 *    you should fill a complete page with data from the same logical
 *    stream. When the page is filled, go back to 1).
 * 2) before filling a page, read ahead one more buffer to see if this
 *    packet is the last of the stream. We need to do this because the ogg
 *    spec mandates that the last packet should have the EOS flag set before
 *    sending it to ogg. if pad->buffer is NULL we need to wait to find out
 *    whether there are any more buffers.
 * 3) pages get queued on a per-pad queue. Every time a page is queued, a
 *    dequeue is called, which will dequeue the oldest page on any pad, provided
 *    that ALL pads have at least one marked page in the queue (or remaining
 *    pads are at EOS)
 */
static GstFlowReturn
gst_ogg_mux_process_best_pad (GstOggMux * ogg_mux, GstOggPadData * best)
{
  GstFlowReturn ret = GST_FLOW_OK;
  gboolean delta_unit;
  gint64 granulepos = 0;
  GstClockTime timestamp, gp_time;

  GST_LOG_OBJECT (ogg_mux, "best pad %" GST_PTR_FORMAT
      ", currently pulling from %" GST_PTR_FORMAT, best->collect.pad,
      ogg_mux->pulling);

  /* best->buffer is non-NULL, either the pad is EOS's or there is a next 
   * buffer */
  if (best->next_buffer == NULL && !best->eos) {
    GST_WARNING_OBJECT (ogg_mux, "no subsequent buffer and EOS not reached");
    return GST_FLOW_WRONG_STATE;
  }

  /* if we were already pulling from one pad, but the new "best" buffer is
   * from another pad, we need to check if we have reason to flush a page
   * for the pad we were pulling from before */
  if (ogg_mux->pulling && best &&
      ogg_mux->pulling != best && ogg_mux->pulling->buffer) {
    GstOggPadData *pad = ogg_mux->pulling;
    GstClockTime last_ts = GST_BUFFER_END_TIME (pad->buffer);

    /* if the next packet in the current page is going to make the page
     * too long, we need to flush */
    if (last_ts > ogg_mux->next_ts + ogg_mux->max_delay) {
      ogg_page page;

      GST_LOG_OBJECT (pad->collect.pad,
          GST_GP_FORMAT " stored packet %" G_GINT64_FORMAT
          " will make page too long, flushing",
          GST_BUFFER_OFFSET_END (pad->buffer), (gint64) pad->stream.packetno);

      while (ogg_stream_flush (&pad->stream, &page)) {
        /* end time of this page is the timestamp of the next buffer */
        ogg_mux->pulling->timestamp_end = GST_BUFFER_TIMESTAMP (pad->buffer);
        /* Place page into the per-pad queue */
        ret = gst_ogg_mux_pad_queue_page (ogg_mux, pad, &page,
            pad->first_delta);
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
    GST_LOG_OBJECT (ogg_mux->pulling->collect.pad, "pulling from best pad");

    /* remember timestamp and gp time of first buffer for this new pad */
    if (ogg_mux->pulling != NULL) {
      ogg_mux->next_ts = GST_BUFFER_TIMESTAMP (ogg_mux->pulling->buffer);
      GST_LOG_OBJECT (ogg_mux->pulling->collect.pad, "updated times, next ts %"
          GST_TIME_FORMAT, GST_TIME_ARGS (ogg_mux->next_ts));
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
   * has been filled and queued */
  if (ogg_mux->pulling != NULL) {
    ogg_packet packet;
    ogg_page page;
    GstBuffer *buf, *tmpbuf;
    GstOggPadData *pad = ogg_mux->pulling;
    gint64 duration;
    gboolean force_flush;

    GST_LOG_OBJECT (ogg_mux->pulling->collect.pad, "pulling from pad");

    /* now see if we have a buffer */
    buf = pad->buffer;
    if (buf == NULL) {
      GST_DEBUG_OBJECT (ogg_mux, "pad was EOS");
      ogg_mux->pulling = NULL;
      return GST_FLOW_OK;
    }

    delta_unit = GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_DELTA_UNIT);
    duration = GST_BUFFER_DURATION (buf);

    /* if the current "next timestamp" on the pad is unset, then this is the
     * first packet on the new page.  Update our pad's page timestamp */
    if (ogg_mux->pulling->timestamp == GST_CLOCK_TIME_NONE) {
      ogg_mux->pulling->timestamp = GST_BUFFER_TIMESTAMP (buf);
      GST_LOG_OBJECT (ogg_mux->pulling->collect.pad,
          "updated pad timestamp to %" GST_TIME_FORMAT,
          GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)));
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
    GST_LOG_OBJECT (pad->collect.pad, GST_GP_FORMAT
        " packet %" G_GINT64_FORMAT " (%ld bytes) created from buffer",
        GST_GP_CAST (packet.granulepos), (gint64) packet.packetno,
        packet.bytes);

    packet.e_o_s = (pad->eos ? 1 : 0);
    tmpbuf = NULL;

    /* we flush when we see a new keyframe */
    force_flush = (pad->prev_delta && !delta_unit) || pad->always_flush_page;
    if (duration != -1) {
      pad->duration += duration;
      /* if page duration exceeds max, flush page */
      if (pad->duration > ogg_mux->max_page_delay) {
        force_flush = TRUE;
        pad->duration = 0;
      }
    }

    if (GST_BUFFER_IS_DISCONT (buf)) {
      GST_LOG_OBJECT (pad->collect.pad, "got discont");
      packet.packetno++;
      /* No public API for this; hack things in */
      pad->stream.pageno++;
      force_flush = TRUE;
    }

    /* flush the currently built page if necessary */
    if (force_flush) {
      GST_LOG_OBJECT (pad->collect.pad,
          GST_GP_FORMAT " forced flush of page before this packet",
          GST_BUFFER_OFFSET_END (pad->buffer));
      while (ogg_stream_flush (&pad->stream, &page)) {
        /* end time of this page is the timestamp of the next buffer */
        ogg_mux->pulling->timestamp_end = GST_BUFFER_TIMESTAMP (pad->buffer);
        ret = gst_ogg_mux_pad_queue_page (ogg_mux, pad, &page,
            pad->first_delta);

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
      GST_DEBUG_OBJECT (pad->collect.pad, "swapping in EOS packet");
    if (packet.b_o_s == 1)
      GST_DEBUG_OBJECT (pad->collect.pad, "swapping in BOS packet");

    ogg_stream_packetin (&pad->stream, &packet);

    gp_time = GST_BUFFER_OFFSET (pad->buffer);
    granulepos = GST_BUFFER_OFFSET_END (pad->buffer);
    timestamp = GST_BUFFER_TIMESTAMP (pad->buffer);

    GST_LOG_OBJECT (pad->collect.pad,
        GST_GP_FORMAT " packet %" G_GINT64_FORMAT ", gp time %"
        GST_TIME_FORMAT ", timestamp %" GST_TIME_FORMAT " packetin'd",
        granulepos, (gint64) packet.packetno, GST_TIME_ARGS (gp_time),
        GST_TIME_ARGS (timestamp));
    /* don't need the old buffer anymore */
    gst_buffer_unref (pad->buffer);
    /* store new readahead buffer */
    pad->buffer = tmpbuf;

    /* let ogg write out the pages now. The packet we got could end
     * up in more than one page so we need to write them all */
    if (ogg_stream_pageout (&pad->stream, &page) > 0) {
      /* we have a new page, so we need to timestamp it correctly.
       * if this fresh packet ends on this page, then the page's granulepos
       * comes from that packet, and we should set this buffer's timestamp */

      GST_LOG_OBJECT (pad->collect.pad,
          GST_GP_FORMAT " packet %" G_GINT64_FORMAT ", time %"
          GST_TIME_FORMAT ") caused new page",
          granulepos, (gint64) packet.packetno, GST_TIME_ARGS (timestamp));
      GST_LOG_OBJECT (pad->collect.pad,
          GST_GP_FORMAT " new page %ld",
          GST_GP_CAST (ogg_page_granulepos (&page)), pad->stream.pageno);

      if (ogg_page_granulepos (&page) == granulepos) {
        /* the packet we streamed in finishes on the current page,
         * because the page's granulepos is the granulepos of the last
         * packet completed on that page,
         * so update the timestamp that we will give to the page */
        GST_LOG_OBJECT (pad->collect.pad,
            GST_GP_FORMAT
            " packet finishes on current page, updating gp time to %"
            GST_TIME_FORMAT, granulepos, GST_TIME_ARGS (gp_time));
        pad->gp_time = gp_time;
      } else {
        GST_LOG_OBJECT (pad->collect.pad,
            GST_GP_FORMAT
            " packet spans beyond current page, keeping old gp time %"
            GST_TIME_FORMAT, granulepos, GST_TIME_ARGS (pad->gp_time));
      }

      /* push the page */
      /* end time of this page is the timestamp of the next buffer */
      pad->timestamp_end = timestamp;
      ret = gst_ogg_mux_pad_queue_page (ogg_mux, pad, &page, pad->first_delta);
      pad->pageno++;
      /* mark next pages as delta */
      pad->first_delta = TRUE;

      /* use an inner loop here to flush the remaining pages and
       * mark them as delta frames as well */
      while (ogg_stream_pageout (&pad->stream, &page) > 0) {
        if (ogg_page_granulepos (&page) == granulepos) {
          /* the page has taken up the new packet completely, which means
           * the packet ends the page and we can update the gp time
           * before pushing out */
          pad->gp_time = gp_time;
        }

        /* we have a complete page now, we can push the page
         * and make sure to pull on a new pad the next time around */
        ret = gst_ogg_mux_pad_queue_page (ogg_mux, pad, &page,
            pad->first_delta);
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

    /* Update the gp time, if necessary, since any future page will have at
     * least this gp time.
     */
    if (pad->gp_time < gp_time) {
      pad->gp_time = gp_time;
      GST_LOG_OBJECT (pad->collect.pad,
          "Updated running gp time of pad %" GST_PTR_FORMAT
          " to %" GST_TIME_FORMAT, pad->collect.pad, GST_TIME_ARGS (gp_time));
    }
  }

  return ret;
}

/* all_pads_eos:
 *
 * Checks if all pads are EOS'd by peeking.
 *
 * Returns TRUE if all pads are EOS.
 */
static gboolean
all_pads_eos (GstCollectPads * pads)
{
  GSList *walk;
  gboolean alleos = TRUE;

  walk = pads->data;
  while (walk) {
    GstBuffer *buf;
    GstCollectData *data = (GstCollectData *) walk->data;

    buf = gst_collect_pads_peek (pads, data);
    if (buf) {
      alleos = FALSE;
      gst_buffer_unref (buf);
      goto beach;
    }
    walk = walk->next;
  }
beach:
  return alleos;
}

/* This function is called when there is data on all pads.
 * 
 * It finds a pad to pull on, this is done by looking at the buffers
 * to decide which one to use, and using the 'oldest' one first. It then calls
 * gst_ogg_mux_process_best_pad() to process as much data as possible.
 * 
 * If all the pads have received EOS, it flushes out all data by continually
 * getting the best pad and calling gst_ogg_mux_process_best_pad() until they
 * are all empty, and then sends EOS.
 */
static GstFlowReturn
gst_ogg_mux_collected (GstCollectPads * pads, GstOggMux * ogg_mux)
{
  GstOggPadData *best;
  GstFlowReturn ret;
  gint activebefore;

  GST_LOG_OBJECT (ogg_mux, "collected");

  activebefore = ogg_mux->active_pads;

  /* queue buffers on all pads; find a buffer with the lowest timestamp */
  best = gst_ogg_mux_queue_pads (ogg_mux);
  if (best && !best->buffer) {
    GST_DEBUG_OBJECT (ogg_mux, "No buffer available on best pad");
    return GST_FLOW_OK;
  }

  if (!best) {
    return GST_FLOW_WRONG_STATE;
  }

  ret = gst_ogg_mux_process_best_pad (ogg_mux, best);

  if (ogg_mux->active_pads < activebefore) {
    /* If the active pad count went down, this mean at least one pad has gone
     * EOS. Since CollectPads only calls _collected() once when all pads are
     * EOS, and our code doesn't _pop() from all pads we need to check that by
     * peeking on all pads, else we won't be called again and the muxing will
     * not terminate (push out EOS). */

    /* if all the pads have been removed, flush all pending data */
    if ((ret == GST_FLOW_OK) && all_pads_eos (pads)) {
      GST_LOG_OBJECT (ogg_mux, "no pads remaining, flushing data");

      do {
        best = gst_ogg_mux_queue_pads (ogg_mux);
        if (best)
          ret = gst_ogg_mux_process_best_pad (ogg_mux, best);
      } while ((ret == GST_FLOW_OK) && (best != NULL));

      GST_DEBUG_OBJECT (ogg_mux, "Pushing EOS");
      gst_pad_push_event (ogg_mux->srcpad, gst_event_new_eos ());
    }
  }

  return ret;
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

/* reset all variables in the ogg pads. */
static void
gst_ogg_mux_init_collectpads (GstCollectPads * collect)
{
  GSList *walk;

  walk = collect->data;
  while (walk) {
    GstOggPadData *oggpad = (GstOggPadData *) walk->data;

    ogg_stream_init (&oggpad->stream, oggpad->serial);
    oggpad->packetno = 0;
    oggpad->pageno = 0;
    oggpad->eos = FALSE;
    /* we assume there will be some control data first for this pad */
    oggpad->state = GST_OGG_PAD_STATE_CONTROL;
    oggpad->new_page = TRUE;
    oggpad->first_delta = FALSE;
    oggpad->prev_delta = FALSE;
    oggpad->pagebuffers = g_queue_new ();

    walk = g_slist_next (walk);
  }
}

/* Clear all buffers from the collectpads object */
static void
gst_ogg_mux_clear_collectpads (GstCollectPads * collect)
{
  GSList *walk;

  for (walk = collect->data; walk; walk = g_slist_next (walk)) {
    GstOggPadData *oggpad = (GstOggPadData *) walk->data;
    GstBuffer *buf;

    ogg_stream_clear (&oggpad->stream);

    while ((buf = g_queue_pop_head (oggpad->pagebuffers)) != NULL) {
      gst_buffer_unref (buf);
    }
    g_queue_free (oggpad->pagebuffers);
    oggpad->pagebuffers = NULL;

    if (oggpad->buffer) {
      gst_buffer_unref (oggpad->buffer);
      oggpad->buffer = NULL;
    }
    if (oggpad->next_buffer) {
      gst_buffer_unref (oggpad->next_buffer);
      oggpad->next_buffer = NULL;
    }
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
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_ogg_mux_clear (ogg_mux);
      gst_ogg_mux_init_collectpads (ogg_mux->collect);
      gst_collect_pads_start (ogg_mux->collect);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_collect_pads_stop (ogg_mux->collect);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_ogg_mux_clear_collectpads (ogg_mux->collect);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}

gboolean
gst_ogg_mux_plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_ogg_mux_debug, "oggmux", 0, "ogg muxer");

  return gst_element_register (plugin, "oggmux", GST_RANK_PRIMARY,
      GST_TYPE_OGG_MUX);
}
