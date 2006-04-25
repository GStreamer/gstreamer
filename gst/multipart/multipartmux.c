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

/**
 * SECTION:element-multipartmux
 * @short_description: Muxer that takes one or several digital streams
 * and muxes them to a single multipart stream.
 *
 * <refsect2>
 * <para>
 * MultipartMux uses the #GstCaps of the sink pad as the Content-type field for
 * incoming buffers when muxing them to a multipart stream. Most of the time 
 * multipart streams are sequential JPEG frames.
 * </para>
 * <title>Sample pipelines</title>
 * <para>
 * Here is a simple pipeline to mux 5 JPEG frames per second into a multipart
 * stream stored to a file :
 * <programlisting>
 * gst-launch videotestsrc ! video/x-raw-yuv, framerate=(fraction)5/1 ! jpegenc ! multipartmux ! filesink location=/tmp/test.multipart
 * </programlisting>
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/gstcollectpads.h>

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
  GstCollectData collect;       /* we extend the CollectData */

  GstBuffer *buffer;            /* the queued buffer for this pad */
}
GstMultipartPad;

/**
 * GstMultipartMux:
 *
 * The opaque #GstMultipartMux structure.
 */
struct _GstMultipartMux
{
  GstElement element;

  /* pad */
  GstPad *srcpad;

  /* sinkpads */
  GstCollectPads *collect;

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
static const GstElementDetails gst_multipart_mux_details =
GST_ELEMENT_DETAILS ("Multipart muxer",
    "Codec/Muxer",
    "mux multipart streams",
    "Wim Taymans <wim@fluendo.com>");

#define DEFAULT_BOUNDARY        "ThisRandomString"

enum
{
  ARG_0,
  ARG_BOUNDARY
      /* FILL ME */
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

static void gst_multipart_mux_finalize (GObject * object);

static gboolean gst_multipart_mux_handle_src_event (GstPad * pad,
    GstEvent * event);
static GstPad *gst_multipart_mux_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name);
static GstStateChangeReturn gst_multipart_mux_change_state (GstElement *
    element, GstStateChange transition);

static GstFlowReturn gst_multipart_mux_collected (GstCollectPads * pads,
    GstMultipartMux * mux);

static void gst_multipart_mux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_multipart_mux_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstElementClass *parent_class = NULL;

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

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->finalize = gst_multipart_mux_finalize;
  gobject_class->get_property = gst_multipart_mux_get_property;
  gobject_class->set_property = gst_multipart_mux_set_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BOUNDARY,
      g_param_spec_string ("boundary", "Boundary", "Boundary string",
          DEFAULT_BOUNDARY, G_PARAM_READWRITE));

  gstelement_class->request_new_pad = gst_multipart_mux_request_new_pad;
  gstelement_class->change_state = gst_multipart_mux_change_state;
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

  multipart_mux->boundary = g_strdup (DEFAULT_BOUNDARY);
  multipart_mux->negotiated = FALSE;

  multipart_mux->collect = gst_collect_pads_new ();
  gst_collect_pads_set_function (multipart_mux->collect,
      (GstCollectPadsFunction) GST_DEBUG_FUNCPTR (gst_multipart_mux_collected),
      multipart_mux);
}

static void
gst_multipart_mux_finalize (GObject * object)
{
  GstMultipartMux *multipart_mux;

  multipart_mux = GST_MULTIPART_MUX (object);

  if (multipart_mux->collect) {
    gst_object_unref (multipart_mux->collect);
    multipart_mux->collect = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstPadLinkReturn
gst_multipart_mux_sinkconnect (GstPad * pad, GstPad * peer)
{
  GstMultipartMux *multipart_mux;
  GstMultipartPad *mppad;
  gchar *pad_name = NULL;

  multipart_mux = GST_MULTIPART_MUX (gst_pad_get_parent (pad));

  mppad = (GstMultipartPad *) gst_pad_get_element_private (pad);

  pad_name = gst_pad_get_name (pad);

  GST_DEBUG_OBJECT (multipart_mux, "sinkconnect triggered on %s", pad_name);

  g_free (pad_name);

  gst_object_unref (multipart_mux);

  return GST_PAD_LINK_OK;
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
      GstMultipartPad *multipartpad;

      multipartpad = (GstMultipartPad *)
          gst_collect_pads_add_pad (multipart_mux->collect, newpad,
          sizeof (GstMultipartPad));

      /* save a pointer to our data in the pad */
      gst_pad_set_element_private (newpad, multipartpad);
      multipart_mux->numpads++;
    }
  } else {
    g_warning ("multipart_mux: this is not our template!\n");
    return NULL;
  }

  /* setup some pad functions */
  gst_pad_set_link_function (newpad, gst_multipart_mux_sinkconnect);

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

  gst_object_unref (multipart_mux);

  return gst_pad_event_default (pad, event);
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
gst_multipart_mux_queue_pads (GstMultipartMux * mux)
{
  GSList *walk = NULL;
  GstMultipartPad *bestpad = NULL;

  g_return_val_if_fail (GST_IS_MULTIPART_MUX (mux), NULL);

  /* try to make sure we have a buffer from each usable pad first */
  walk = mux->collect->data;
  while (walk) {
    GstCollectData *data = (GstCollectData *) walk->data;
    GstMultipartPad *pad = (GstMultipartPad *) data;

    walk = g_slist_next (walk);

    /* try to get a new buffer for this pad if needed and possible */
    if (pad->buffer == NULL) {
      GstBuffer *buf = NULL;

      buf = gst_collect_pads_pop (mux->collect, data);

      /* Adjust timestamp with segment_start and preroll */
      if (buf) {
        GST_BUFFER_TIMESTAMP (buf) -= data->segment.start;
      }

      pad->buffer = buf;
    }

    /* we should have a buffer now, see if it is the best stream to
     * pull on */
    if (pad->buffer != NULL) {
      if (gst_multipart_mux_compare_pads (mux, bestpad, pad) > 0) {
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
static GstFlowReturn
gst_multipart_mux_collected (GstCollectPads * pads, GstMultipartMux * mux)
{
  GstMultipartPad *best;
  GstFlowReturn ret = GST_FLOW_OK;
  gchar *header = NULL;
  size_t newlen, headerlen;
  GstBuffer *newbuf = NULL;
  GstStructure *structure = NULL;

  GST_DEBUG_OBJECT (mux, "all pads are collected");

  /* queue buffers on all pads; find a buffer with the lowest timestamp */
  best = gst_multipart_mux_queue_pads (mux);
  if (best && !best->buffer)
    goto beach;

  /* EOS */
  if (!best) {
    GST_DEBUG_OBJECT (mux, "Pushing EOS");
    gst_pad_push_event (mux->srcpad, gst_event_new_eos ());
    ret = GST_FLOW_WRONG_STATE;
    goto beach;
  }

  /* If not negotiated yet set caps on src pad */
  if (!mux->negotiated) {
    GstCaps *newcaps;

    newcaps = gst_caps_new_simple ("multipart/x-mixed-replace",
        "boundary", G_TYPE_STRING, mux->boundary, NULL);

    if (gst_pad_set_caps (mux->srcpad, newcaps)) {
      mux->negotiated = TRUE;
    } else {
      GST_ELEMENT_ERROR (mux, CORE, NEGOTIATION, (NULL), (NULL));
      ret = GST_FLOW_UNEXPECTED;
      goto beach;
    }
  }

  structure = gst_caps_get_structure (GST_BUFFER_CAPS (best->buffer), 0);
  if (!structure) {
    GST_WARNING_OBJECT (mux, "no caps on the incoming buffer %p", best->buffer);
    goto beach;
  }

  header = g_strdup_printf ("\n--%s\nContent-type: %s\n\n",
      mux->boundary, gst_structure_get_name (structure));

  headerlen = strlen (header);
  newlen = headerlen + GST_BUFFER_SIZE (best->buffer);

  ret =
      gst_pad_alloc_buffer_and_set_caps (mux->srcpad, GST_BUFFER_OFFSET_NONE,
      newlen, GST_PAD_CAPS (mux->srcpad), &newbuf);
  if (ret != GST_FLOW_OK) {
    GST_WARNING_OBJECT (mux, "failed allocating a %d bytes buffer", newlen);
    g_free (header);
    goto beach;
  }

  memcpy (GST_BUFFER_DATA (newbuf), header, headerlen);
  memcpy (GST_BUFFER_DATA (newbuf) + headerlen,
      GST_BUFFER_DATA (best->buffer), GST_BUFFER_SIZE (best->buffer));

  gst_buffer_stamp (newbuf, best->buffer);
  GST_BUFFER_OFFSET (newbuf) = mux->offset;

  g_free (header);

  mux->offset += newlen;

  gst_pad_push (mux->srcpad, newbuf);

  gst_buffer_unref (best->buffer);
  best->buffer = NULL;

beach:
  return ret;
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

static GstStateChangeReturn
gst_multipart_mux_change_state (GstElement * element, GstStateChange transition)
{
  GstMultipartMux *multipart_mux;
  GstStateChangeReturn ret;

  multipart_mux = GST_MULTIPART_MUX (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      multipart_mux->negotiated = FALSE;
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      multipart_mux->offset = 0;
      GST_DEBUG_OBJECT (multipart_mux, "starting collect pads");
      gst_collect_pads_start (multipart_mux->collect);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      GST_DEBUG_OBJECT (multipart_mux, "stopping collect pads");
      gst_collect_pads_stop (multipart_mux->collect);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    default:
      break;
  }

  return ret;
}

gboolean
gst_multipart_mux_plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_multipart_mux_debug, "multipartmux", 0,
      "multipart muxer");

  return gst_element_register (plugin, "multipartmux", GST_RANK_NONE,
      GST_TYPE_MULTIPART_MUX);
}
