/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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

GST_DEBUG_CATEGORY (videorate_debug);
#define GST_CAT_DEFAULT videorate_debug

#define GST_TYPE_VIDEORATE \
  (gst_videorate_get_type())
#define GST_VIDEORATE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VIDEORATE,GstVideorate))
#define GST_VIDEORATE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VIDEORATE,GstVideorate))
#define GST_IS_VIDEORATE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VIDEORATE))
#define GST_IS_VIDEORATE_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VIDEORATE))

typedef struct _GstVideorate GstVideorate;
typedef struct _GstVideorateClass GstVideorateClass;

struct _GstVideorate
{
  GstElement element;

  GstPad *sinkpad, *srcpad;

  /* video state */
  gdouble from_fps, to_fps;
  guint64 next_ts;              /* Timestamp of next buffer to output */
  guint64 first_ts;             /* Timestamp of first buffer */
  GstBuffer *prevbuf;
  guint64 prev_ts;              /* Previous buffer timestamp */
  guint64 in, out, dup, drop;

  /* segment handling */
  gint64 segment_start;
  gint64 segment_stop;
  gint64 segment_accum;

  gboolean silent;
  gdouble new_pref;
};

struct _GstVideorateClass
{
  GstElementClass parent_class;
};

/* elementfactory information */
static GstElementDetails videorate_details =
GST_ELEMENT_DETAILS ("Video rate adjuster",
    "Filter/Effect/Video",
    "Drops/duplicates/adjusts timestamps on video frames to make a perfect stream",
    "Wim Taymans <wim@fluendo.com>");

/* GstVideorate signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

#define DEFAULT_SILENT		TRUE
#define DEFAULT_NEW_PREF	1.0

enum
{
  ARG_0,
  ARG_IN,
  ARG_OUT,
  ARG_DUP,
  ARG_DROP,
  ARG_SILENT,
  ARG_NEW_PREF,
  /* FILL ME */
};

static GstStaticPadTemplate gst_videorate_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-yuv; video/x-raw-rgb")
    );

static GstStaticPadTemplate gst_videorate_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-yuv; video/x-raw-rgb")
    );

static void gst_videorate_base_init (gpointer g_class);
static void gst_videorate_class_init (GstVideorateClass * klass);
static void gst_videorate_init (GstVideorate * videorate);
static gboolean gst_videorate_event (GstPad * pad, GstEvent * event);
static GstFlowReturn gst_videorate_chain (GstPad * pad, GstBuffer * buffer);

static void gst_videorate_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_videorate_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static GstStateChangeReturn gst_videorate_change_state (GstElement * element,
    GstStateChange transition);

static GstElementClass *parent_class = NULL;

/*static guint gst_videorate_signals[LAST_SIGNAL] = { 0 }; */

static GType
gst_videorate_get_type (void)
{
  static GType videorate_type = 0;

  if (!videorate_type) {
    static const GTypeInfo videorate_info = {
      sizeof (GstVideorateClass),
      gst_videorate_base_init,
      NULL,
      (GClassInitFunc) gst_videorate_class_init,
      NULL,
      NULL,
      sizeof (GstVideorate),
      0,
      (GInstanceInitFunc) gst_videorate_init,
    };

    videorate_type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstVideorate", &videorate_info, 0);
  }

  return videorate_type;
}

static void
gst_videorate_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &videorate_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_videorate_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_videorate_src_template));
}
static void
gst_videorate_class_init (GstVideorateClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  object_class->set_property = gst_videorate_set_property;
  object_class->get_property = gst_videorate_get_property;

  g_object_class_install_property (object_class, ARG_IN,
      g_param_spec_uint64 ("in", "In",
          "Number of input frames", 0, G_MAXUINT64, 0, G_PARAM_READABLE));
  g_object_class_install_property (object_class, ARG_OUT,
      g_param_spec_uint64 ("out", "Out",
          "Number of output frames", 0, G_MAXUINT64, 0, G_PARAM_READABLE));
  g_object_class_install_property (object_class, ARG_DUP,
      g_param_spec_uint64 ("duplicate", "Duplicate",
          "Number of duplicated frames", 0, G_MAXUINT64, 0, G_PARAM_READABLE));
  g_object_class_install_property (object_class, ARG_DROP,
      g_param_spec_uint64 ("drop", "Drop",
          "Number of dropped frames", 0, G_MAXUINT64, 0, G_PARAM_READABLE));
  g_object_class_install_property (object_class, ARG_SILENT,
      g_param_spec_boolean ("silent", "silent",
          "Don't emit notify for dropped and duplicated frames",
          DEFAULT_SILENT, G_PARAM_READWRITE));
  g_object_class_install_property (object_class, ARG_NEW_PREF,
      g_param_spec_double ("new_pref", "New Pref",
          "Value indicating how much to prefer new frames",
          0.0, 1.0, DEFAULT_NEW_PREF, G_PARAM_READWRITE));

  element_class->change_state = gst_videorate_change_state;
}

/* return the caps that can be used on out_pad given in_caps on in_pad */
static gboolean
gst_videorate_transformcaps (GstPad * in_pad, GstCaps * in_caps,
    GstPad * out_pad, GstCaps ** out_caps)
{
  GstCaps *intersect;
  const GstCaps *in_templ;
  gint i;

  in_templ = gst_pad_get_pad_template_caps (in_pad);
  intersect = gst_caps_intersect (in_caps, in_templ);

  /* all possible framerates are allowed */
  for (i = 0; i < gst_caps_get_size (intersect); i++) {
    GstStructure *structure;

    structure = gst_caps_get_structure (intersect, i);

    gst_structure_set (structure,
        "framerate", GST_TYPE_DOUBLE_RANGE, 0.0, G_MAXDOUBLE, NULL);
  }
  *out_caps = intersect;

  return TRUE;
}

static GstCaps *
gst_videorate_getcaps (GstPad * pad)
{
  GstVideorate *videorate;
  GstPad *otherpad;
  GstCaps *caps;

  videorate = GST_VIDEORATE (GST_PAD_PARENT (pad));

  otherpad = (pad == videorate->srcpad) ? videorate->sinkpad :
      videorate->srcpad;

  /* we can do what the peer can */
  caps = gst_pad_peer_get_caps (otherpad);
  if (caps) {
    GstCaps *transform;

    gst_videorate_transformcaps (otherpad, caps, pad, &transform);
    gst_caps_unref (caps);
    caps = transform;
  } else {
    /* no peer, our padtemplate is enough then */
    caps = gst_caps_copy (gst_pad_get_pad_template_caps (pad));
  }

  return caps;
}

static gboolean
gst_videorate_setcaps (GstPad * pad, GstCaps * caps)
{
  GstVideorate *videorate;
  GstStructure *structure;
  gboolean ret = TRUE;
  double fps;
  GstPad *otherpad, *opeer;

  videorate = GST_VIDEORATE (GST_PAD_PARENT (pad));

  structure = gst_caps_get_structure (caps, 0);
  if (!(ret = gst_structure_get_double (structure, "framerate", &fps)))
    goto done;

  if (pad == videorate->srcpad) {
    videorate->to_fps = fps;
    otherpad = videorate->sinkpad;
  } else {
    videorate->from_fps = fps;
    otherpad = videorate->srcpad;
  }
  /* now try to find something for the peer */
  opeer = gst_pad_get_peer (otherpad);
  if (opeer) {
    if (gst_pad_accept_caps (opeer, caps)) {
      /* the peer accepts the caps as they are */
      gst_pad_set_caps (otherpad, caps);

      ret = TRUE;
    } else {
      GstCaps *peercaps;
      GstCaps *intersect;
      GstCaps *transform = NULL;

      ret = FALSE;

      /* see how we can transform the input caps */
      if (!gst_videorate_transformcaps (pad, caps, otherpad, &transform))
        goto done;

      /* see what the peer can do */
      peercaps = gst_pad_get_caps (opeer);

      GST_DEBUG ("icaps %" GST_PTR_FORMAT, peercaps);
      GST_DEBUG ("transform %" GST_PTR_FORMAT, transform);

      /* filter against our possibilities */
      intersect = gst_caps_intersect (peercaps, transform);
      gst_caps_unref (peercaps);
      gst_caps_unref (transform);

      GST_DEBUG ("intersect %" GST_PTR_FORMAT, intersect);

      /* take first possibility */
      caps = gst_caps_copy_nth (intersect, 0);
      gst_caps_unref (intersect);
      structure = gst_caps_get_structure (caps, 0);

      /* and fixate */
      gst_structure_fixate_field_nearest_int (structure, "framerate", fps);

      gst_structure_get_double (structure, "framerate", &fps);

      if (otherpad == videorate->srcpad) {
        videorate->to_fps = fps;
      } else {
        videorate->from_fps = fps;
      }
      gst_pad_set_caps (otherpad, caps);
      ret = TRUE;
    }
    gst_object_unref (opeer);
  }
done:
  return ret;
}

static void
gst_videorate_blank_data (GstVideorate * videorate)
{
  GST_DEBUG ("resetting data");
  if (videorate->prevbuf)
    gst_buffer_unref (videorate->prevbuf);
  videorate->prevbuf = NULL;

  videorate->from_fps = 0;
  videorate->to_fps = 0;
  videorate->in = 0;
  videorate->out = 0;
  videorate->drop = 0;
  videorate->dup = 0;
  videorate->next_ts = 0LL;
  videorate->first_ts = 0LL;
  videorate->prev_ts = 0LL;

  videorate->segment_start = 0;
  videorate->segment_stop = 0;
  videorate->segment_accum = 0;
}

static void
gst_videorate_init (GstVideorate * videorate)
{
  GST_DEBUG ("gst_videorate_init");
  videorate->sinkpad =
      gst_pad_new_from_static_template (&gst_videorate_sink_template, "sink");
  gst_element_add_pad (GST_ELEMENT (videorate), videorate->sinkpad);
  gst_pad_set_event_function (videorate->sinkpad, gst_videorate_event);
  gst_pad_set_chain_function (videorate->sinkpad, gst_videorate_chain);
  gst_pad_set_getcaps_function (videorate->sinkpad, gst_videorate_getcaps);
  gst_pad_set_setcaps_function (videorate->sinkpad, gst_videorate_setcaps);

  videorate->srcpad =
      gst_pad_new_from_static_template (&gst_videorate_src_template, "src");
  gst_element_add_pad (GST_ELEMENT (videorate), videorate->srcpad);
  gst_pad_set_getcaps_function (videorate->srcpad, gst_videorate_getcaps);
  gst_pad_set_setcaps_function (videorate->srcpad, gst_videorate_setcaps);

  gst_videorate_blank_data (videorate);
  videorate->silent = DEFAULT_SILENT;
  videorate->new_pref = DEFAULT_NEW_PREF;
}

static GstFlowReturn
gst_videorate_event (GstPad * pad, GstEvent * event)
{
  GstVideorate *videorate;

  videorate = GST_VIDEORATE (GST_PAD_PARENT (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:
    {
      gint64 start, stop, base;
      gdouble rate;
      gboolean update;
      GstFormat format;

      gst_event_parse_new_segment (event, &update, &rate, &format, &start,
          &stop, &base);

      if (format != GST_FORMAT_TIME) {
        GST_WARNING ("Got discont but doesn't have GST_FORMAT_TIME value");
      } else {
        /* 
           We just want to update the accumulated stream_time.
         */
        videorate->segment_accum +=
            videorate->segment_stop - videorate->segment_start;
        videorate->segment_start = start;
        videorate->segment_stop = stop;
        GST_DEBUG_OBJECT (videorate, "Updated segment_accum:%" GST_TIME_FORMAT
            " segment_start:%" GST_TIME_FORMAT " segment_stop:%"
            GST_TIME_FORMAT, GST_TIME_ARGS (videorate->segment_accum),
            GST_TIME_ARGS (videorate->segment_start),
            GST_TIME_ARGS (videorate->segment_stop));
      }

      break;
    }
    case GST_EVENT_FLUSH_STOP:
    {
      gst_videorate_blank_data (videorate);
    }
    default:
      break;
  }

  return gst_pad_event_default (pad, event);
}

static GstFlowReturn
gst_videorate_chain (GstPad * pad, GstBuffer * buffer)
{
  GstVideorate *videorate;
  GstFlowReturn res = GST_FLOW_OK;

  videorate = GST_VIDEORATE (GST_PAD_PARENT (pad));

  if (videorate->from_fps == 0)
    return GST_FLOW_NOT_NEGOTIATED;

  if (videorate->to_fps == 0)
    return GST_FLOW_NOT_NEGOTIATED;

  /* pull in 2 buffers */
  if (videorate->prevbuf == NULL) {
    /* We're sure it's a GstBuffer here */
    videorate->prevbuf = buffer;
    videorate->next_ts = videorate->first_ts = videorate->prev_ts =
        GST_BUFFER_TIMESTAMP (buffer) - videorate->segment_start +
        videorate->segment_accum;
  } else {
    GstClockTime prevtime, intime;
    gint count = 0;
    gint64 diff1, diff2;

    prevtime = videorate->prev_ts;
    intime =
        GST_BUFFER_TIMESTAMP (buffer) - videorate->segment_start +
        videorate->segment_accum;

    GST_LOG_OBJECT (videorate,
        "BEGINNING prev buf %" GST_TIME_FORMAT " new buf %" GST_TIME_FORMAT
        " outgoing ts %" GST_TIME_FORMAT, GST_TIME_ARGS (prevtime),
        GST_TIME_ARGS (intime), GST_TIME_ARGS (videorate->next_ts));

    videorate->in++;

    /* got 2 buffers, see which one is the best */
    do {
      diff1 = prevtime - videorate->next_ts;
      diff2 = intime - videorate->next_ts;

      /* take absolute values, beware: abs and ABS don't work for gint64 */
      if (diff1 < 0)
        diff1 = -diff1;
      if (diff2 < 0)
        diff2 = -diff2;

      GST_LOG_OBJECT (videorate,
          "diff with prev %" GST_TIME_FORMAT " diff with new %"
          GST_TIME_FORMAT " outgoing ts %" GST_TIME_FORMAT,
          GST_TIME_ARGS (diff1), GST_TIME_ARGS (diff2),
          GST_TIME_ARGS (videorate->next_ts));

      /* output first one when its the best */
      if (diff1 < diff2) {
        GstBuffer *outbuf;

        count++;
        outbuf =
            gst_buffer_create_sub (videorate->prevbuf, 0,
            GST_BUFFER_SIZE (videorate->prevbuf));
        GST_BUFFER_TIMESTAMP (outbuf) = videorate->next_ts;
        videorate->out++;
        videorate->next_ts =
            videorate->first_ts +
            (videorate->out / videorate->to_fps * GST_SECOND);
        GST_BUFFER_DURATION (outbuf) =
            videorate->next_ts - GST_BUFFER_TIMESTAMP (outbuf);
        /* adapt for looping */
        GST_BUFFER_TIMESTAMP (outbuf) -= videorate->segment_accum;
        gst_buffer_set_caps (outbuf, GST_PAD_CAPS (videorate->srcpad));

        GST_LOG_OBJECT (videorate,
            "old is best, dup, pushing buffer outgoing ts %" GST_TIME_FORMAT,
            GST_TIME_ARGS (videorate->next_ts));

        if ((res = gst_pad_push (videorate->srcpad, outbuf)) != GST_FLOW_OK) {
          GST_WARNING_OBJECT (videorate, "couldn't push buffer on srcpad:%d",
              res);
          goto done;
        }

        GST_LOG_OBJECT (videorate,
            "old is best, dup, pushed buffer outgoing ts %" GST_TIME_FORMAT,
            GST_TIME_ARGS (videorate->next_ts));
      }
      /* continue while the first one was the best */
    }
    while (diff1 < diff2);

    /* if we outputed the first buffer more then once, we have dups */
    if (count > 1) {
      videorate->dup += count - 1;
      if (!videorate->silent)
        g_object_notify (G_OBJECT (videorate), "duplicate");
    }
    /* if we didn't output the first buffer, we have a drop */
    else if (count == 0) {
      videorate->drop++;
      if (!videorate->silent)
        g_object_notify (G_OBJECT (videorate), "drop");
      GST_LOG_OBJECT (videorate,
          "new is best, old never used, drop, outgoing ts %"
          GST_TIME_FORMAT, GST_TIME_ARGS (videorate->next_ts));
    }
    GST_LOG_OBJECT (videorate,
        "END, putting new in old, diff1 %" GST_TIME_FORMAT
        ", diff2 %" GST_TIME_FORMAT ", next_ts %" GST_TIME_FORMAT
        ", in %lld, out %lld, drop %lld, dup %lld", GST_TIME_ARGS (diff1),
        GST_TIME_ARGS (diff2), GST_TIME_ARGS (videorate->next_ts),
        videorate->in, videorate->out, videorate->drop, videorate->dup);

    /* swap in new one when it's the best */
    gst_buffer_unref (videorate->prevbuf);
    videorate->prevbuf = buffer;
    videorate->prev_ts =
        GST_BUFFER_TIMESTAMP (buffer) - videorate->segment_start +
        videorate->segment_accum;
  }
done:

  return res;
}

static void
gst_videorate_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstVideorate *videorate = GST_VIDEORATE (object);

  switch (prop_id) {
    case ARG_SILENT:
      videorate->silent = g_value_get_boolean (value);
      break;
    case ARG_NEW_PREF:
      videorate->new_pref = g_value_get_double (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_videorate_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstVideorate *videorate = GST_VIDEORATE (object);

  switch (prop_id) {
    case ARG_IN:
      g_value_set_uint64 (value, videorate->in);
      break;
    case ARG_OUT:
      g_value_set_uint64 (value, videorate->out);
      break;
    case ARG_DUP:
      g_value_set_uint64 (value, videorate->dup);
      break;
    case ARG_DROP:
      g_value_set_uint64 (value, videorate->drop);
      break;
    case ARG_SILENT:
      g_value_set_boolean (value, videorate->silent);
      break;
    case ARG_NEW_PREF:
      g_value_set_double (value, videorate->new_pref);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_videorate_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstVideorate *videorate;

  videorate = GST_VIDEORATE (element);

  switch (transition) {
    default:
      break;
  }

  ret = parent_class->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_videorate_blank_data (videorate);
      break;
    default:
      break;
  }

  return ret;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (videorate_debug, "videorate", 0,
      "Videorate stream fixer");

  return gst_element_register (plugin, "videorate", GST_RANK_NONE,
      GST_TYPE_VIDEORATE);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "videorate",
    "Adjusts video frames",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
