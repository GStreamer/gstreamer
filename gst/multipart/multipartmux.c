/* multipart muxer plugin for GStreamer
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
#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_multipart_mux_debug);
#define GST_CAT_DEFAULT gst_multipart_mux_debug

#define GST_TYPE_MULTIPART_MUX (gst_multipart_mux_get_type())
#define GST_MULTIPART_MUX(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MULTIPART_MUX, GstMultipartMux))
#define GST_MULTIPART_MUX_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MULTIPART_MUX, GstMultipartMux))
#define GST_IS_MULTIPART_MUX(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MULTIPART_MUX))
#define GST_IS_MULTIPART_MUX_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MULTIPART_MUX))

typedef struct _GstMultipartMux GstMultipartMux;
typedef struct _GstMultipartMuxClass GstMultipartMuxClass;

/* all information needed for one multipart stream */
typedef struct
{
  GstPad *pad;                  /* reference for this pad is held by element we belong to */

  GstBuffer *buffer;            /* the queued buffer for this pad */

  gboolean eos;
  const gchar *mimetype;

  guint state;                  /* state of the pad */
}
GstMultipartPad;

struct _GstMultipartMux
{
  GstElement element;

  /* pad */
  GstPad *srcpad;

  /* sinkpads, a GSList of GstMultipartPads */
  GSList *sinkpads;
  gint numpads;

  /* offset in stream */
  guint64 offset;

  /* boundary string */
  gchar *boundary;

  gboolean negotiated;
};

struct _GstMultipartMuxClass
{
  GstElementClass parent_class;
};

/* elementfactory information */
static GstElementDetails gst_multipart_mux_details =
GST_ELEMENT_DETAILS ("multipart muxer",
    "Codec/Muxer",
    "mux multipart streams",
    "Wim Taymans <wim@fluendo.com>");

/* MultipartMux signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

#define DEFAULT_BOUNDARY	"ThisRandomString"
enum
{
  ARG_0,
  ARG_BOUNDARY,
};

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("multipart/x-mixed-replace")
    );

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink_%d",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS_ANY         /* we can take anything, really */
    );

static void gst_multipart_mux_base_init (gpointer g_class);
static void gst_multipart_mux_class_init (GstMultipartMuxClass * klass);
static void gst_multipart_mux_init (GstMultipartMux * multipart_mux);

static void gst_multipart_mux_loop (GstElement * element);
static gboolean gst_multipart_mux_handle_src_event (GstPad * pad,
    GstEvent * event);
static GstPad *gst_multipart_mux_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name);
static void gst_multipart_mux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_multipart_mux_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static GstElementStateReturn gst_multipart_mux_change_state (GstElement *
    element);

static GstElementClass *parent_class = NULL;

/*static guint gst_multipart_mux_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_multipart_mux_get_type (void)
{
  static GType multipart_mux_type = 0;

  if (!multipart_mux_type) {
    static const GTypeInfo multipart_mux_info = {
      sizeof (GstMultipartMuxClass),
      gst_multipart_mux_base_init,
      NULL,
      (GClassInitFunc) gst_multipart_mux_class_init,
      NULL,
      NULL,
      sizeof (GstMultipartMux),
      0,
      (GInstanceInitFunc) gst_multipart_mux_init,
    };

    multipart_mux_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstMultipartMux",
        &multipart_mux_info, 0);
  }
  return multipart_mux_type;
}

static void
gst_multipart_mux_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));

  gst_element_class_set_details (element_class, &gst_multipart_mux_details);
}

static void
gst_multipart_mux_class_init (GstMultipartMuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gstelement_class->request_new_pad = gst_multipart_mux_request_new_pad;

  gstelement_class->change_state = gst_multipart_mux_change_state;

  gstelement_class->get_property = gst_multipart_mux_get_property;
  gstelement_class->set_property = gst_multipart_mux_set_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BOUNDARY,
      g_param_spec_string ("boundary", "Boundary", "Boundary string",
          DEFAULT_BOUNDARY, G_PARAM_READWRITE));

}

static const GstEventMask *
gst_multipart_mux_get_sink_event_masks (GstPad * pad)
{
  static const GstEventMask gst_multipart_mux_sink_event_masks[] = {
    {GST_EVENT_EOS, 0},
    {0,}
  };

  return gst_multipart_mux_sink_event_masks;
}

static void
gst_multipart_mux_init (GstMultipartMux * multipart_mux)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (multipart_mux);

  multipart_mux->srcpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "src"), "src");
  gst_pad_set_event_function (multipart_mux->srcpad,
      gst_multipart_mux_handle_src_event);
  gst_element_add_pad (GST_ELEMENT (multipart_mux), multipart_mux->srcpad);

  GST_FLAG_SET (GST_ELEMENT (multipart_mux), GST_ELEMENT_EVENT_AWARE);

  multipart_mux->sinkpads = NULL;
  multipart_mux->boundary = g_strdup (DEFAULT_BOUNDARY);
  multipart_mux->negotiated = FALSE;

  gst_element_set_loop_function (GST_ELEMENT (multipart_mux),
      gst_multipart_mux_loop);
}

static GstPadLinkReturn
gst_multipart_mux_sinkconnect (GstPad * pad, const GstCaps * vscaps)
{
  GstMultipartMux *multipart_mux;
  GstMultipartPad *mppad;
  GstStructure *structure;

  multipart_mux = GST_MULTIPART_MUX (gst_pad_get_parent (pad));

  mppad = (GstMultipartPad *) gst_pad_get_element_private (pad);

  GST_DEBUG ("multipart_mux: sinkconnect triggered on %s",
      gst_pad_get_name (pad));

  structure = gst_caps_get_structure (vscaps, 0);
  mppad->mimetype = gst_structure_get_name (structure);

  return GST_PAD_LINK_OK;
}

static void
gst_multipart_mux_pad_link (GstPad * pad, GstPad * peer, gpointer data)
{
  //GstMultipartMux *multipart_mux = GST_MULTIPART_MUX (data);

  GST_DEBUG ("pad '%s' connected", gst_pad_get_name (padname));
}

static void
gst_multipart_mux_pad_unlink (GstPad * pad, GstPad * peer, gpointer data)
{
  //GstMultipartMux *multipart_mux = GST_MULTIPART_MUX (data);

  GST_DEBUG ("pad '%s' unlinked", gst_pad_get_name (pad));
}

static GstPad *
gst_multipart_mux_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * req_name)
{
  GstMultipartMux *multipart_mux;
  GstPad *newpad;
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (element);

  g_return_val_if_fail (templ != NULL, NULL);

  if (templ->direction != GST_PAD_SINK) {
    g_warning ("multipart_mux: request pad that is not a SINK pad\n");
    return NULL;
  }

  g_return_val_if_fail (GST_IS_MULTIPART_MUX (element), NULL);

  multipart_mux = GST_MULTIPART_MUX (element);

  if (templ == gst_element_class_get_pad_template (klass, "sink_%d")) {
    gchar *name;

    /* create new pad with the name */
    name = g_strdup_printf ("sink_%02d", multipart_mux->numpads);
    newpad = gst_pad_new_from_template (templ, name);
    g_free (name);

    /* construct our own wrapper data structure for the pad to
     * keep track of its status */
    {
      GstMultipartPad *multipartpad = g_new0 (GstMultipartPad, 1);

      multipartpad->pad = newpad;
      multipartpad->eos = FALSE;

      /* save a pointer to our data in the pad */
      gst_pad_set_element_private (newpad, multipartpad);
      /* store our data for the pad */
      multipart_mux->sinkpads =
          g_slist_prepend (multipart_mux->sinkpads, multipartpad);
      multipart_mux->numpads++;
    }
  } else {
    g_warning ("multipart_mux: this is not our template!\n");
    return NULL;
  }

  g_signal_connect (newpad, "linked",
      G_CALLBACK (gst_multipart_mux_pad_link), (gpointer) multipart_mux);
  g_signal_connect (newpad, "unlinked",
      G_CALLBACK (gst_multipart_mux_pad_unlink), (gpointer) multipart_mux);

  /* setup some pad functions */
  gst_pad_set_link_function (newpad, gst_multipart_mux_sinkconnect);
  gst_pad_set_event_mask_function (newpad,
      gst_multipart_mux_get_sink_event_masks);
  /* dd the pad to the element */
  gst_element_add_pad (element, newpad);

  return newpad;
}

/* handle events */
static gboolean
gst_multipart_mux_handle_src_event (GstPad * pad, GstEvent * event)
{
  GstMultipartMux *multipart_mux;
  GstEventType type;

  multipart_mux = GST_MULTIPART_MUX (gst_pad_get_parent (pad));

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
gst_multipart_mux_next_buffer (GstMultipartPad * pad)
{
  GstData *data = NULL;

  while (data == NULL) {
    GST_LOG ("muxer: pulling %s:%s", GST_DEBUG_PAD_NAME (pad->pad));
    data = gst_pad_pull (pad->pad);
    /* if it's an event, handle it */
    if (GST_IS_EVENT (data)) {
      GstEventType type;
      GstMultipartMux *multipart_mux;
      GstEvent *event = GST_EVENT (data);

      multipart_mux = GST_MULTIPART_MUX (gst_pad_get_parent (pad->pad));
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

/*
 * Given two pads, compare the buffers queued on it and return 0 if they have
 * an equal priority, 1 if the new pad is better, -1 if the old pad is better 
 */
static gint
gst_multipart_mux_compare_pads (GstMultipartMux * multipart_mux,
    GstMultipartPad * old, GstMultipartPad * new)
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

  /* same priority if all of the above failed */
  return 0;
}

/* make sure a buffer is queued on all pads, returns a pointer to an multipartpad
 * that holds the best buffer or NULL when no pad was usable */
static GstMultipartPad *
gst_multipart_mux_queue_pads (GstMultipartMux * multipart_mux)
{
  GstMultipartPad *bestpad = NULL;
  GSList *walk;

  /* try to make sure we have a buffer from each usable pad first */
  walk = multipart_mux->sinkpads;
  while (walk) {
    GstMultipartPad *pad = (GstMultipartPad *) walk->data;

    walk = walk->next;

    /* try to get a new buffer for this pad if needed and possible */
    if (pad->buffer == NULL && GST_PAD_IS_USABLE (pad->pad)) {
      pad->buffer = gst_multipart_mux_next_buffer (pad);
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
      if (gst_multipart_mux_compare_pads (multipart_mux, bestpad, pad) > 0) {
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
 * 2) push buffer on best pad, go to 1
 */
static void
gst_multipart_mux_loop (GstElement * element)
{
  GstMultipartMux *mux;
  GstMultipartPad *pad;
  GstBuffer *newbuf, *buf;
  gchar *header;
  gint headerlen;
  gint newlen;

  mux = GST_MULTIPART_MUX (element);

  /* we don't know which pad to pull on, find one */
  pad = gst_multipart_mux_queue_pads (mux);
  if (pad == NULL) {
    /* no pad to pull on, send EOS */
    if (GST_PAD_IS_USABLE (mux->srcpad))
      gst_pad_push (mux->srcpad, GST_DATA (gst_event_new (GST_EVENT_EOS)));
    gst_element_set_eos (element);
    return;
  }

  /* now see if we have a buffer */
  buf = pad->buffer;
  if (buf == NULL) {
    /* no buffer, get one */
    buf = gst_multipart_mux_next_buffer (pad);
    if (buf == NULL) {
      /* data exhausted on this pad (EOS) */
      return;
    }
  }

  /* FIXME, negotiated is not set to FALSE properly after
   * reconnect */
  if (!mux->negotiated) {
    GstCaps *newcaps;

    newcaps = gst_caps_new_simple ("multipart/x-mixed-replace",
        "boundary", G_TYPE_STRING, mux->boundary, NULL);

    if (GST_PAD_LINK_FAILED (gst_pad_try_set_caps (mux->srcpad, newcaps))) {
      GST_ELEMENT_ERROR (mux, CORE, NEGOTIATION, (NULL), (NULL));
      return;
    }
    mux->negotiated = TRUE;
  }

  header = g_strdup_printf ("\n--%s\nContent-type: %s\n\n",
      mux->boundary, pad->mimetype);
  headerlen = strlen (header);
  newlen = headerlen + GST_BUFFER_SIZE (buf);
  newbuf = gst_pad_alloc_buffer (mux->srcpad, GST_BUFFER_OFFSET_NONE, newlen);

  memcpy (GST_BUFFER_DATA (newbuf), header, headerlen);
  memcpy (GST_BUFFER_DATA (newbuf) + headerlen,
      GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));
  GST_BUFFER_TIMESTAMP (newbuf) = GST_BUFFER_TIMESTAMP (buf);
  GST_BUFFER_DURATION (newbuf) = GST_BUFFER_DURATION (buf);
  GST_BUFFER_OFFSET (newbuf) = mux->offset;

  g_free (header);

  mux->offset += newlen;

  gst_pad_push (mux->srcpad, GST_DATA (newbuf));

  gst_buffer_unref (buf);
  pad->buffer = NULL;
}

static void
gst_multipart_mux_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstMultipartMux *mux;

  mux = GST_MULTIPART_MUX (object);

  switch (prop_id) {
    case ARG_BOUNDARY:
      g_value_set_string (value, mux->boundary);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_multipart_mux_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstMultipartMux *mux;

  mux = GST_MULTIPART_MUX (object);

  switch (prop_id) {
    case ARG_BOUNDARY:
      g_free (mux->boundary);
      mux->boundary = g_strdup (g_value_get_string (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstElementStateReturn
gst_multipart_mux_change_state (GstElement * element)
{
  GstMultipartMux *multipart_mux;
  gint transition = GST_STATE_TRANSITION (element);

  g_return_val_if_fail (GST_IS_MULTIPART_MUX (element), GST_STATE_FAILURE);

  multipart_mux = GST_MULTIPART_MUX (element);

  switch (transition) {
    case GST_STATE_NULL_TO_READY:
    case GST_STATE_READY_TO_PAUSED:
      multipart_mux->offset = 0;
      multipart_mux->negotiated = FALSE;
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
gst_multipart_mux_plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_multipart_mux_debug, "multipartmux", 0,
      "multipart muxer");

  return gst_element_register (plugin, "multipartmux", GST_RANK_NONE,
      GST_TYPE_MULTIPART_MUX);
}
