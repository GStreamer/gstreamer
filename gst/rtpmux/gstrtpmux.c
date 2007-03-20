/* RTP muxer element for GStreamer
 *
 * gstrtpmux.c:
 *
 * Copyright (C) <2007> Nokia Corporation.
 *   Contact: Zeeshan Ali <zeeshan.ali@nokia.com>
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *               2000,2005 Wim Taymans <wim@fluendo.com>
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
 * SECTION:element-rtpmux
 * @short_description: Muxer that takes one or several RTP streams
 * and muxes them to a single rtp stream.
 *
 * <refsect2>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/rtp/gstrtpbuffer.h>

#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_rtp_mux_debug);
#define GST_CAT_DEFAULT gst_rtp_mux_debug

#define GST_TYPE_RTP_MUX (gst_rtp_mux_get_type())
#define GST_RTP_MUX(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_RTP_MUX, GstRTPMux))
#define GST_RTP_MUX_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_RTP_MUX, GstRTPMux))
#define GST_IS_RTP_MUX(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_RTP_MUX))
#define GST_IS_RTP_MUX_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_RTP_MUX))

typedef struct _GstRTPMux GstRTPMux;
typedef struct _GstRTPMuxClass GstRTPMuxClass;

/**
 * GstRTPMux:
 *
 * The opaque #GstRTPMux structure.
 */
struct _GstRTPMux
{
  GstElement element;

  /* pad */
  GstPad *srcpad;

  /* sinkpads */
  gint numpads;
  GstPad *special_pad;
  
  guint16  seqnum_base;
  gint16   seqnum_offset;
  guint16  seqnum;
};

struct _GstRTPMuxClass
{
  GstElementClass parent_class;
};

/* elementfactory information */
static const GstElementDetails gst_rtp_mux_details =
GST_ELEMENT_DETAILS ("RTP muxer",
    "Codec/Muxer",
    "multiplex N rtp streams into one",
    "Zeeshan Ali <first.last@nokia.com>");

enum
{
  ARG_0,
  PROP_SEQNUM_OFFSET,
  PROP_SEQNUM
  /* FILL ME */
};

#define DEFAULT_SEQNUM_OFFSET           -1

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp")
    );

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink_%d",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("application/x-rtp")
    );

static void gst_rtp_mux_base_init (gpointer g_class);
static void gst_rtp_mux_class_init (GstRTPMuxClass * klass);
static void gst_rtp_mux_init (GstRTPMux * rtp_mux);

static void gst_rtp_mux_finalize (GObject * object);

static gboolean gst_rtp_mux_handle_sink_event (GstPad * pad,
    GstEvent * event);
static GstPad *gst_rtp_mux_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name);
static GstFlowReturn gst_rtp_mux_chain (GstPad * pad,
    GstBuffer * buffer);
static gboolean gst_rtp_mux_setcaps (GstPad *pad, GstCaps *caps);

static GstStateChangeReturn gst_rtp_mux_change_state (GstElement *
    element, GstStateChange transition);

static void gst_rtp_mux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rtp_mux_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstElementClass *parent_class = NULL;

GType
gst_rtp_mux_get_type (void)
{
  static GType rtp_mux_type = 0;

  if (!rtp_mux_type) {
    static const GTypeInfo rtp_mux_info = {
      sizeof (GstRTPMuxClass),
      gst_rtp_mux_base_init,
      NULL,
      (GClassInitFunc) gst_rtp_mux_class_init,
      NULL,
      NULL,
      sizeof (GstRTPMux),
      0,
      (GInstanceInitFunc) gst_rtp_mux_init,
    };

    rtp_mux_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstRTPMux",
        &rtp_mux_info, 0);
  }
  return rtp_mux_type;
}

static void
gst_rtp_mux_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));

  gst_element_class_set_details (element_class, &gst_rtp_mux_details);
}

static void
gst_rtp_mux_class_init (GstRTPMuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->finalize = gst_rtp_mux_finalize;
  gobject_class->get_property = gst_rtp_mux_get_property;
  gobject_class->set_property = gst_rtp_mux_set_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SEQNUM_OFFSET,
      g_param_spec_int ("seqnum-offset", "Sequence number Offset",
          "Offset to add to all outgoing seqnum (-1 = random)", -1, G_MAXINT,
          DEFAULT_SEQNUM_OFFSET, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SEQNUM,
      g_param_spec_uint ("seqnum", "Sequence number",
          "The RTP sequence number of the last processed packet",
          0, G_MAXUINT, 0, G_PARAM_READABLE));

  gstelement_class->request_new_pad = gst_rtp_mux_request_new_pad;
  gstelement_class->change_state = gst_rtp_mux_change_state;
}

static void
gst_rtp_mux_init (GstRTPMux * rtp_mux)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (rtp_mux);

  rtp_mux->srcpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "src"), "src");
  gst_element_add_pad (GST_ELEMENT (rtp_mux), rtp_mux->srcpad);
  
  rtp_mux->seqnum_offset = DEFAULT_SEQNUM_OFFSET;
}

static void
gst_rtp_mux_finalize (GObject * object)
{
  GstRTPMux *rtp_mux;

  rtp_mux = GST_RTP_MUX (object);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstPad *
gst_rtp_mux_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * req_name)
{
  GstRTPMux *rtp_mux;
  GstPad *newpad;
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (element);

  g_return_val_if_fail (templ != NULL, NULL);

  rtp_mux = GST_RTP_MUX (element);

  if (templ->direction != GST_PAD_SINK) {
    GST_WARNING_OBJECT (rtp_mux, "request pad that is not a SINK pad");
    return NULL;
  }

  g_return_val_if_fail (GST_IS_RTP_MUX (element), NULL);

  if (templ == gst_element_class_get_pad_template (klass, "sink_%d")) {
    gchar *name;

    GST_OBJECT_LOCK (rtp_mux);
    /* create new pad with the name */
    name = g_strdup_printf ("sink_%02d", rtp_mux->numpads);
    newpad = gst_pad_new_from_template (templ, name);
    g_free (name);

    rtp_mux->numpads++;
    GST_OBJECT_UNLOCK (rtp_mux);
  } else {
    GST_WARNING_OBJECT (rtp_mux, "this is not our template!\n");
    return NULL;
  }

  /* setup some pad functions */
  gst_pad_set_chain_function (newpad, gst_rtp_mux_chain);
  gst_pad_set_setcaps_function (newpad, gst_rtp_mux_setcaps);
  gst_pad_set_event_function (newpad, gst_rtp_mux_handle_sink_event);

  /* dd the pad to the element */
  gst_element_add_pad (element, newpad);

  return newpad;
}

static GstFlowReturn
gst_rtp_mux_chain (GstPad * pad, GstBuffer * buffer)
{
  GstRTPMux *rtp_mux;
  gboolean drop = FALSE;
  GstFlowReturn ret;
  
  rtp_mux = GST_RTP_MUX (gst_pad_get_parent (pad));
  GST_OBJECT_LOCK (rtp_mux);
  
  if (rtp_mux->special_pad != NULL &&
      rtp_mux->special_pad != pad) {
    drop = TRUE;
  }

  if (drop) {
    gst_buffer_unref (buffer);
    ret = GST_FLOW_OK;
    GST_OBJECT_UNLOCK (rtp_mux);
  }

  else {
    rtp_mux->seqnum++;
    GST_LOG_OBJECT (rtp_mux, "setting RTP seqnum %d", rtp_mux->seqnum);
    gst_rtp_buffer_set_seq (buffer, rtp_mux->seqnum);
    GST_DEBUG_OBJECT (rtp_mux, "Pushing packet size %d, seq=%d, ts=%u",
            GST_BUFFER_SIZE (buffer), rtp_mux->seqnum - 1);

    GST_OBJECT_UNLOCK (rtp_mux);
    ret = gst_pad_push (rtp_mux->srcpad, buffer);
  }
    
  gst_object_unref (rtp_mux);
  return ret;
}

static gboolean
gst_rtp_mux_setcaps (GstPad *pad, GstCaps *caps)
{
  /*GstRTPMux *rtp_mux;
  GstCaps *old_caps;
  GstCaps *new_caps;
  gint i;
  gboolean ret;

  rtp_mux = GST_RTP_MUX (gst_pad_get_parent (pad));

  new_caps = gst_caps_copy (caps);

  / * We want our own seq base on the caps * /
  for (i=0; i< gst_caps_get_size (new_caps); i++) {
     GstStructure *structure = gst_caps_get_structure (new_caps, i);
     gst_structure_set (structure,
             "seqnum-base", G_TYPE_UINT, rtp_mux->seqnum_base, NULL);
  }

  old_caps = GST_PAD_CAPS (rtp_mux->srcpad);
  if (old_caps != NULL) {
    new_caps = gst_caps_union (old_caps, new_caps);
  }

  GST_DEBUG_OBJECT (rtp_mux,
          "seting caps %" GST_PTR_FORMAT " on src pad..", caps);
  ret = gst_pad_set_caps (rtp_mux->srcpad, new_caps);
  gst_caps_unref (new_caps);

  return ret;*/

  return TRUE;
}

static gboolean
gst_rtp_mux_handle_sink_event (GstPad * pad, GstEvent * event)
{
  GstRTPMux *rtp_mux;
  GstEventType type;

  rtp_mux = GST_RTP_MUX (gst_pad_get_parent (pad));

  type = event ? GST_EVENT_TYPE (event) : GST_EVENT_UNKNOWN;

  switch (type) {
    case GST_EVENT_CUSTOM_DOWNSTREAM_OOB:
    {
      const GstStructure *structure;

      structure = gst_event_get_structure (event);
      /* FIXME: is this event generic enough to be given a generic name? */
      if (structure && gst_structure_has_name (structure, "stream-lock")) {
        gboolean lock;

        if (!gst_structure_get_boolean (structure, "lock", &lock))
          break;

        GST_OBJECT_LOCK (rtp_mux);
        if (lock) {
          if (rtp_mux->special_pad != NULL) {
              GST_WARNING_OBJECT (rtp_mux,
                      "Stream lock already acquired by pad %s",
                      GST_ELEMENT_NAME (rtp_mux->special_pad));
          }

          else
            rtp_mux->special_pad = gst_object_ref (pad);
        }

        else {
          if (rtp_mux->special_pad == NULL) {
              GST_WARNING_OBJECT (rtp_mux,
                      "Stream lock not acquired, can't release it");
          }

          else if (pad != rtp_mux->special_pad) {
              GST_WARNING_OBJECT (rtp_mux,
                      "pad %s attempted to release Stream lock"
                      " which was acquired by pad %s", GST_ELEMENT_NAME (pad),
                      GST_ELEMENT_NAME (rtp_mux->special_pad));
          }

          else {
            gst_object_unref (rtp_mux->special_pad);
            rtp_mux->special_pad = NULL;
          }
        }
        
        GST_OBJECT_UNLOCK (rtp_mux);
      }

      break;
    }
    default:
      break;
  }

  gst_object_unref (rtp_mux);

  return gst_pad_event_default (pad, event);
}

static void
gst_rtp_mux_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstRTPMux *rtp_mux;

  rtp_mux = GST_RTP_MUX (object);

  switch (prop_id) {
    case PROP_SEQNUM_OFFSET:
      g_value_set_int (value, rtp_mux->seqnum_offset);
      break;
    case PROP_SEQNUM:
      g_value_set_uint (value, rtp_mux->seqnum);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtp_mux_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstRTPMux *rtp_mux;

  rtp_mux = GST_RTP_MUX (object);

  switch (prop_id) {
    case PROP_SEQNUM_OFFSET:
      rtp_mux->seqnum_offset = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_rtp_mux_change_state (GstElement * element, GstStateChange transition)
{
  GstRTPMux *rtp_mux;
  GstStateChangeReturn ret;

  rtp_mux = GST_RTP_MUX (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (rtp_mux->seqnum_offset == -1)
          rtp_mux->seqnum_base = g_random_int_range (0, G_MAXUINT16);
      else
          rtp_mux->seqnum_base = rtp_mux->seqnum_offset;
      rtp_mux->seqnum = rtp_mux->seqnum_base;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
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
gst_rtp_mux_plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_rtp_mux_debug, "rtpmux", 0,
      "rtp muxer");

  return gst_element_register (plugin, "rtpmux", GST_RANK_NONE,
      GST_TYPE_RTP_MUX);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "rtpmux",
    "RTP muxer",
    gst_rtp_mux_plugin_init,
    VERSION,
    "LGPL",
    "Farsight",
    "http://farsight.sf.net")
