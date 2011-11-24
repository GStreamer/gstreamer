/* Generic video mixer plugin
 *
 * GStreamer
 * Copyright (C) 2009 Julien Isorce <julien.isorce@gmail.com>
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
#include <gst/controller/gstcontroller.h>
#include <gst/video/video.h>

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include "gstglmixer.h"

#define GST_CAT_DEFAULT gst_gl_mixer_debug
GST_DEBUG_CATEGORY (gst_gl_mixer_debug);

#define GST_GL_MIXER_GET_STATE_LOCK(mix) \
  (GST_GL_MIXER(mix)->state_lock)
#define GST_GL_MIXER_STATE_LOCK(mix) \
  (g_mutex_lock(GST_GL_MIXER_GET_STATE_LOCK (mix)))
#define GST_GL_MIXER_STATE_UNLOCK(mix) \
  (g_mutex_unlock(GST_GL_MIXER_GET_STATE_LOCK (mix)))

static void gst_gl_mixer_pad_class_init (GstGLMixerPadClass * klass);
static void gst_gl_mixer_pad_init (GstGLMixerPad * mixerpad);

static void gst_gl_mixer_pad_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_gl_mixer_pad_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);

static gboolean gst_gl_mixer_src_event (GstPad * pad, GstEvent * event);
static gboolean gst_gl_mixer_sink_event (GstPad * pad, GstEvent * event);


enum
{
  PROP_PAD_0
};

G_DEFINE_TYPE (GstGLMixerPad, gst_gl_mixer_pad, GST_TYPE_PAD);

static void
gst_gl_mixer_pad_class_init (GstGLMixerPadClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->set_property = gst_gl_mixer_pad_set_property;
  gobject_class->get_property = gst_gl_mixer_pad_get_property;
}

static void
gst_gl_mixer_pad_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  //GstGLMixerPad *pad = GST_GL_MIXER_PAD (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gl_mixer_pad_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  //GstGLMixerPad *pad = GST_GL_MIXER_PAD (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gl_mixer_set_master_geometry (GstGLMixer * mix)
{
  GSList *walk = mix->sinkpads;
  gint fps_n = 0;
  gint fps_d = 0;
  GstGLMixerPad *master = NULL;

  while (walk) {
    GstGLMixerPad *mixpad = GST_GL_MIXER_PAD (walk->data);

    walk = g_slist_next (walk);

    /* If mix framerate < mixpad framerate, using fractions */
    GST_DEBUG_OBJECT (mix, "comparing framerate %d/%d to mixpad's %d/%d",
        fps_n, fps_d, mixpad->fps_n, mixpad->fps_d);
    if ((!fps_n && !fps_d) ||
        ((gint64) fps_n * mixpad->fps_d < (gint64) mixpad->fps_n * fps_d)) {
      fps_n = mixpad->fps_n;
      fps_d = mixpad->fps_d;
      GST_DEBUG_OBJECT (mix, "becomes the master pad");
      master = mixpad;
    }
  }

  /* set results */
  if (mix->master != master || mix->fps_n != fps_n || mix->fps_d != fps_d) {
    mix->setcaps = TRUE;
    mix->sendseg = TRUE;
    mix->master = master;
    mix->fps_n = fps_n;
    mix->fps_d = fps_d;
  }
}

static gboolean
gst_gl_mixer_pad_sink_setcaps (GstPad * pad, GstCaps * vscaps)
{
  GstGLMixer *mix = GST_GL_MIXER (gst_pad_get_parent (pad));
  GstGLMixerPad *mixpad = GST_GL_MIXER_PAD (pad);
  GstStructure *structure = gst_caps_get_structure (vscaps, 0);
  gint width = 0;
  gint height = 0;
  gboolean ret = FALSE;
  const GValue *framerate = gst_structure_get_value (structure, "framerate");

  GST_INFO_OBJECT (mix, "Setting caps %" GST_PTR_FORMAT, vscaps);

  if (!gst_structure_get_int (structure, "width", &width) ||
      !gst_structure_get_int (structure, "height", &height) || !framerate)
    goto beach;

  GST_GL_MIXER_STATE_LOCK (mix);
  mixpad->fps_n = gst_value_get_fraction_numerator (framerate);
  mixpad->fps_d = gst_value_get_fraction_denominator (framerate);

  mixpad->width = width;
  mixpad->height = height;

  gst_gl_mixer_set_master_geometry (mix);
  GST_GL_MIXER_STATE_UNLOCK (mix);

  ret = TRUE;

beach:
  gst_object_unref (mix);

  return ret;
}

static void
gst_gl_mixer_pad_init (GstGLMixerPad * mixerpad)
{
  gst_pad_set_setcaps_function (GST_PAD (mixerpad),
      gst_gl_mixer_pad_sink_setcaps);

  mixerpad->display = NULL;
}

/* GLMixer signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0
};

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_GL_VIDEO_CAPS)
    );

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink_%d",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS (GST_GL_VIDEO_CAPS)
    );

static void gst_gl_mixer_finalize (GObject * object);

static GstCaps *gst_gl_mixer_getcaps (GstPad * pad);
static gboolean gst_gl_mixer_setcaps (GstPad * pad, GstCaps * caps);
static gboolean gst_gl_mixer_query (GstPad * pad, GstQuery * query);

static GstFlowReturn gst_gl_mixer_collected (GstCollectPads * pads,
    GstGLMixer * mix);
static GstPad *gst_gl_mixer_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name);
static void gst_gl_mixer_release_pad (GstElement * element, GstPad * pad);

static void gst_gl_mixer_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gl_mixer_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static GstStateChangeReturn gst_gl_mixer_change_state (GstElement * element,
    GstStateChange transition);

static void gst_gl_mixer_child_proxy_init (gpointer g_iface,
    gpointer iface_data);
static void _do_init (GType object_type);

GST_BOILERPLATE_FULL (GstGLMixer, gst_gl_mixer, GstElement,
    GST_TYPE_ELEMENT, _do_init);

static void
_do_init (GType object_type)
{
  static const GInterfaceInfo child_proxy_info = {
    (GInterfaceInitFunc) gst_gl_mixer_child_proxy_init,
    NULL,
    NULL
  };

  GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "glmixer", 0, "opengl mixer");
  g_type_add_interface_static (object_type, GST_TYPE_CHILD_PROXY,
      &child_proxy_info);
  GST_INFO ("GstChildProxy interface registered");
}

static GstObject *
gst_gl_mixer_child_proxy_get_child_by_index (GstChildProxy * child_proxy,
    guint index)
{
  GstGLMixer *mix = GST_GL_MIXER (child_proxy);
  GstObject *obj;

  GST_GL_MIXER_STATE_LOCK (mix);
  if ((obj = g_slist_nth_data (mix->sinkpads, index)))
    gst_object_ref (obj);
  GST_GL_MIXER_STATE_UNLOCK (mix);
  return obj;
}

static guint
gst_gl_mixer_child_proxy_get_children_count (GstChildProxy * child_proxy)
{
  guint count = 0;
  GstGLMixer *mix = GST_GL_MIXER (child_proxy);

  GST_GL_MIXER_STATE_LOCK (mix);
  count = mix->numpads;
  GST_GL_MIXER_STATE_UNLOCK (mix);
  GST_INFO_OBJECT (mix, "Children Count: %d", count);
  return count;
}

static void
gst_gl_mixer_child_proxy_init (gpointer g_iface, gpointer iface_data)
{
  GstChildProxyInterface *iface = g_iface;

  GST_INFO ("intializing child proxy interface");
  iface->get_child_by_index = gst_gl_mixer_child_proxy_get_child_by_index;
  iface->get_children_count = gst_gl_mixer_child_proxy_get_children_count;
}

static void
gst_gl_mixer_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
}

static void
gst_gl_mixer_class_init (GstGLMixerClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_gl_mixer_finalize);

  gobject_class->get_property = gst_gl_mixer_get_property;
  gobject_class->set_property = gst_gl_mixer_set_property;

  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_gl_mixer_request_new_pad);
  gstelement_class->release_pad = GST_DEBUG_FUNCPTR (gst_gl_mixer_release_pad);
  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_gl_mixer_change_state);

  /* Register the pad class */
  (void) (GST_TYPE_GL_MIXER_PAD);

  klass->set_caps = NULL;
}

static void
gst_gl_mixer_collect_free (GstGLMixerCollect * mixcol)
{
  if (mixcol->buffer) {
    gst_buffer_unref (mixcol->buffer);
    mixcol->buffer = NULL;
  }
}

static void
gst_gl_mixer_reset (GstGLMixer * mix)
{
  GSList *walk;

  mix->width = 0;
  mix->height = 0;
  mix->fps_n = 0;
  mix->fps_d = 0;
  mix->setcaps = FALSE;
  mix->sendseg = FALSE;
  mix->segment_position = 0;
  mix->segment_rate = 1.0;

  mix->last_ts = 0;

  /* clean up collect data */
  walk = mix->collect->data;
  while (walk) {
    GstGLMixerCollect *data = (GstGLMixerCollect *) walk->data;

    gst_gl_mixer_collect_free (data);
    walk = g_slist_next (walk);
  }

  mix->next_sinkpad = 0;
}

static void
gst_gl_mixer_init (GstGLMixer * mix, GstGLMixerClass * g_class)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (mix);

  mix->srcpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "src"), "src");
  gst_pad_set_getcaps_function (GST_PAD (mix->srcpad),
      GST_DEBUG_FUNCPTR (gst_gl_mixer_getcaps));
  gst_pad_set_setcaps_function (GST_PAD (mix->srcpad),
      GST_DEBUG_FUNCPTR (gst_gl_mixer_setcaps));
  gst_pad_set_query_function (GST_PAD (mix->srcpad),
      GST_DEBUG_FUNCPTR (gst_gl_mixer_query));
  gst_pad_set_event_function (GST_PAD (mix->srcpad),
      GST_DEBUG_FUNCPTR (gst_gl_mixer_src_event));
  gst_element_add_pad (GST_ELEMENT (mix), mix->srcpad);

  mix->collect = gst_collect_pads_new ();

  gst_collect_pads_set_function (mix->collect,
      (GstCollectPadsFunction) GST_DEBUG_FUNCPTR (gst_gl_mixer_collected), mix);

  mix->state_lock = g_mutex_new ();

  mix->array_buffers = 0;
  mix->display = NULL;
  mix->fbo = 0;
  mix->depthbuffer = 0;

  /* initialize variables */
  gst_gl_mixer_reset (mix);
}

static void
gst_gl_mixer_finalize (GObject * object)
{
  GstGLMixer *mix = GST_GL_MIXER (object);

  gst_object_unref (mix->collect);
  g_mutex_free (mix->state_lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_gl_mixer_query_duration (GstGLMixer * mix, GstQuery * query)
{
  gint64 max;
  gboolean res;
  GstFormat format;
  GstIterator *it;
  gboolean done;

  /* parse format */
  gst_query_parse_duration (query, &format, NULL);

  max = -1;
  res = TRUE;
  done = FALSE;

  /* Take maximum of all durations */
  it = gst_element_iterate_sink_pads (GST_ELEMENT_CAST (mix));
  while (!done) {
    GstIteratorResult ires;
    gpointer item;

    ires = gst_iterator_next (it, &item);
    switch (ires) {
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
      case GST_ITERATOR_OK:
      {
        GstPad *pad = GST_PAD_CAST (item);
        gint64 duration;

        /* ask sink peer for duration */
        res &= gst_pad_query_peer_duration (pad, &format, &duration);
        /* take max from all valid return values */
        if (res) {
          /* valid unknown length, stop searching */
          if (duration == -1) {
            max = duration;
            done = TRUE;
          }
          /* else see if bigger than current max */
          else if (duration > max)
            max = duration;
        }
        gst_object_unref (pad);
        break;
      }
      case GST_ITERATOR_RESYNC:
        max = -1;
        res = TRUE;
        gst_iterator_resync (it);
        break;
      default:
        res = FALSE;
        done = TRUE;
        break;
    }
  }
  gst_iterator_free (it);

  if (res) {
    /* and store the max */
    GST_DEBUG_OBJECT (mix, "Total duration in format %s: %"
        GST_TIME_FORMAT, gst_format_get_name (format), GST_TIME_ARGS (max));
    gst_query_set_duration (query, format, max);
  }

  return res;
}

static gboolean
gst_gl_mixer_query_latency (GstGLMixer * mix, GstQuery * query)
{
  GstClockTime min, max;
  gboolean live;
  gboolean res;
  GstIterator *it;
  gboolean done;

  res = TRUE;
  done = FALSE;
  live = FALSE;
  min = 0;
  max = GST_CLOCK_TIME_NONE;

  /* Take maximum of all latency values */
  it = gst_element_iterate_sink_pads (GST_ELEMENT_CAST (mix));
  while (!done) {
    GstIteratorResult ires;
    gpointer item;

    ires = gst_iterator_next (it, &item);
    switch (ires) {
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
      case GST_ITERATOR_OK:
      {
        GstPad *pad = GST_PAD_CAST (item);

        GstQuery *peerquery;

        GstClockTime min_cur, max_cur;

        gboolean live_cur;

        peerquery = gst_query_new_latency ();

        /* Ask peer for latency */
        res &= gst_pad_peer_query (pad, peerquery);

        /* take max from all valid return values */
        if (res) {
          gst_query_parse_latency (peerquery, &live_cur, &min_cur, &max_cur);

          if (min_cur > min)
            min = min_cur;

          if (max_cur != GST_CLOCK_TIME_NONE &&
              ((max != GST_CLOCK_TIME_NONE && max_cur > max) ||
                  (max == GST_CLOCK_TIME_NONE)))
            max = max_cur;

          live = live || live_cur;
        }

        gst_query_unref (peerquery);
        gst_object_unref (pad);
        break;
      }
      case GST_ITERATOR_RESYNC:
        live = FALSE;
        min = 0;
        max = GST_CLOCK_TIME_NONE;
        res = TRUE;
        gst_iterator_resync (it);
        break;
      default:
        res = FALSE;
        done = TRUE;
        break;
    }
  }
  gst_iterator_free (it);

  if (res) {
    /* store the results */
    GST_DEBUG_OBJECT (mix, "Calculated total latency: live %s, min %"
        GST_TIME_FORMAT ", max %" GST_TIME_FORMAT,
        (live ? "yes" : "no"), GST_TIME_ARGS (min), GST_TIME_ARGS (max));
    gst_query_set_latency (query, live, min, max);
  }

  return res;
}

static gboolean
gst_gl_mixer_query (GstPad * pad, GstQuery * query)
{
  GstGLMixer *mix = GST_GL_MIXER (gst_pad_get_parent (pad));
  gboolean res = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstFormat format;

      gst_query_parse_position (query, &format, NULL);

      switch (format) {
        case GST_FORMAT_TIME:
          /* FIXME, bring to stream time, might be tricky */
          gst_query_set_position (query, format, mix->last_ts);
          res = TRUE;
          break;
        default:
          break;
      }
      break;
    }
    case GST_QUERY_DURATION:
      res = gst_gl_mixer_query_duration (mix, query);
      break;
    case GST_QUERY_LATENCY:
      res = gst_gl_mixer_query_latency (mix, query);
      break;

    case GST_QUERY_CUSTOM:
    {
      /* mix is a sink in terms of gl chain, so we are sharing the gldisplay that
       * comes from src pad with every display of the sink pads */
      GSList *walk = mix->sinkpads;
      GstStructure *structure = gst_query_get_structure (query);

      res =
          g_strcmp0 (gst_element_get_name (mix),
          gst_structure_get_name (structure)) == 0;

      if (!res) {
        GstGLDisplay *foreign_display = NULL;
        gulong foreign_gl_context = 0;

        if (mix->display) {
          /* this gl filter is a sink in terms of the gl chain */
          foreign_display = mix->display;
        } else {
          /* at least one gl element is after in our gl chain */
          /* id_value is set by upstream element of itself when going
           * to paused state */
          const GValue *id_value =
              gst_structure_get_value (structure, "gstgldisplay");
          foreign_display = GST_GL_DISPLAY (g_value_get_pointer (id_value));
        }

        foreign_gl_context =
            gst_gl_display_get_internal_gl_context (foreign_display);

        /* iterate on each sink pad until reaching the gl element
         * that requested the query */
        while (!res && walk) {
          GstGLMixerPad *sink_pad = GST_GL_MIXER_PAD (walk->data);
          GstPad *peer = gst_pad_get_peer (GST_PAD_CAST (sink_pad));
          walk = g_slist_next (walk);

          g_assert (sink_pad->display != NULL);

          gst_gl_display_activate_gl_context (foreign_display, FALSE);

          res =
              gst_gl_display_create_context (sink_pad->display,
              foreign_gl_context);

          gst_gl_display_activate_gl_context (foreign_display, TRUE);

          if (res)
            gst_structure_set (structure, "gstgldisplay", G_TYPE_POINTER,
                sink_pad->display, NULL);
          else
            GST_ELEMENT_ERROR (mix, RESOURCE, NOT_FOUND,
                (GST_GL_DISPLAY_ERR_MSG (sink_pad->display)), (NULL));

          /* does not work:
           * res = gst_pad_query_default (GST_PAD_CAST (sink_pad), query);*/
          res = gst_pad_query (peer, query);
          gst_object_unref (peer);
        }
      }
      break;
    }
    default:
      /* FIXME, needs a custom query handler because we have multiple
       * sinkpads, send to the master pad until then */
      res = gst_pad_query (GST_PAD_CAST (mix->master), query);
      break;
  }

  gst_object_unref (mix);
  return res;
}

static GstCaps *
gst_gl_mixer_getcaps (GstPad * pad)
{
  GstGLMixer *mix = GST_GL_MIXER (gst_pad_get_parent (pad));
  GstCaps *caps = gst_caps_copy (gst_pad_get_pad_template_caps (mix->srcpad));
  GstStructure *structure = gst_caps_get_structure (caps, 0);

  gst_structure_set (structure, "width", G_TYPE_INT, 8000, NULL);
  gst_structure_set (structure, "height", G_TYPE_INT, /*G_MAXINT */ 6000, NULL);
  if (mix->fps_d != 0)
    gst_structure_set (structure, "framerate", GST_TYPE_FRACTION, mix->fps_n,
        mix->fps_d, NULL);

  gst_object_unref (mix);

  return caps;
}

static gboolean
gst_gl_mixer_setcaps (GstPad * pad, GstCaps * caps)
{
  GstGLMixer *mix = GST_GL_MIXER (gst_pad_get_parent_element (pad));
  GstGLMixerClass *mixer_class = GST_GL_MIXER_GET_CLASS (mix);
  GstStructure *structure = gst_caps_get_structure (caps, 0);
  gint width = 0;
  gint height = 0;

  GST_INFO_OBJECT (mix, "set src caps: %" GST_PTR_FORMAT, caps);

  if (!gst_structure_get_int (structure, "width", &width) ||
      !gst_structure_get_int (structure, "height", &height)) {
    gst_object_unref (mix);
    return FALSE;
  }

  GST_GL_MIXER_STATE_LOCK (mix);

  mix->width = width;
  mix->height = height;

  GST_GL_MIXER_STATE_UNLOCK (mix);

  if (!gst_gl_display_gen_fbo (mix->display, mix->width, mix->height,
          &mix->fbo, &mix->depthbuffer)) {
    GST_ELEMENT_ERROR (mix, RESOURCE, NOT_FOUND,
        (GST_GL_DISPLAY_ERR_MSG (mix->display)), (NULL));
    gst_object_unref (mix);
    return FALSE;
  }

  if (mixer_class->set_caps)
    mixer_class->set_caps (mix, caps);

  gst_object_unref (mix);

  return TRUE;
}

static GstPad *
gst_gl_mixer_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * req_name)
{
  GstGLMixer *mix = GST_GL_MIXER (element);
  GstGLMixerPad *mixpad = NULL;
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (element);

  if (templ == gst_element_class_get_pad_template (klass, "sink_%d")) {
    gint serial = 0;
    gchar *name = NULL;
    GstGLMixerCollect *mixcol = NULL;

    if (req_name == NULL || strlen (req_name) < 6) {
      /* no name given when requesting the pad, use next available int */
      serial = mix->next_sinkpad++;
    } else {
      /* parse serial number from requested padname */
      serial = atoi (&req_name[5]);
      if (serial >= mix->next_sinkpad)
        mix->next_sinkpad = serial + 1;
    }
    /* create new pad with the name */
    name = g_strdup_printf ("sink_%d", serial);
    mixpad = g_object_new (GST_TYPE_GL_MIXER_PAD, "name", name, "direction",
        templ->direction, "template", templ, NULL);
    g_free (name);

    GST_GL_MIXER_STATE_LOCK (mix);

    mixcol = (GstGLMixerCollect *)
        gst_collect_pads_add_pad (mix->collect, GST_PAD (mixpad),
        sizeof (GstGLMixerCollect));

    /* FIXME: hacked way to override/extend the event function of
     * GstCollectPads; because it sets its own event function giving the
     * element no access to events */
    mix->collect_event =
        (GstPadEventFunction) GST_PAD_EVENTFUNC (GST_PAD (mixpad));
    gst_pad_set_event_function (GST_PAD (mixpad),
        GST_DEBUG_FUNCPTR (gst_gl_mixer_sink_event));

    /* Keep track of each other */
    mixcol->mixpad = mixpad;
    mixpad->mixcol = mixcol;

    /* Keep an internal list of mixpads for zordering */
    mix->sinkpads = g_slist_append (mix->sinkpads, mixpad);
    mix->numpads++;
    GST_GL_MIXER_STATE_UNLOCK (mix);
  } else {
    g_warning ("glmixer: this is not our template!");
    return NULL;
  }

  /* add the pad to the element */
  gst_element_add_pad (element, GST_PAD (mixpad));
  gst_child_proxy_child_added (GST_OBJECT (mix), GST_OBJECT (mixpad));

  return GST_PAD (mixpad);
}

static void
gst_gl_mixer_release_pad (GstElement * element, GstPad * pad)
{
  GstGLMixer *mix = GST_GL_MIXER (element);
  GstGLMixerPad *mixpad = NULL;

  GST_GL_MIXER_STATE_LOCK (mix);
  if (G_UNLIKELY (g_slist_find (mix->sinkpads, pad) == NULL)) {
    g_warning ("Unknown pad %s", GST_PAD_NAME (pad));
    goto error;
  }

  mixpad = GST_GL_MIXER_PAD (pad);

  mix->sinkpads = g_slist_remove (mix->sinkpads, pad);
  gst_gl_mixer_collect_free (mixpad->mixcol);
  gst_collect_pads_remove_pad (mix->collect, pad);
  gst_child_proxy_child_removed (GST_OBJECT (mix), GST_OBJECT (mixpad));
  /* determine possibly new geometry and master */
  gst_gl_mixer_set_master_geometry (mix);
  mix->numpads--;
  GST_GL_MIXER_STATE_UNLOCK (mix);

  gst_element_remove_pad (element, pad);
  return;
error:
  GST_GL_MIXER_STATE_UNLOCK (mix);
}

/* try to get a buffer on all pads. As long as the queued value is
 * negative, we skip buffers */
static gboolean
gst_gl_mixer_fill_queues (GstGLMixer * mix)
{
  GSList *walk = NULL;
  gboolean eos = TRUE;

  g_return_val_if_fail (GST_IS_GL_MIXER (mix), FALSE);

  /* try to make sure we have a buffer from each usable pad first */
  walk = mix->collect->data;
  while (walk) {
    GstCollectData *data = (GstCollectData *) walk->data;
    GstGLMixerCollect *mixcol = (GstGLMixerCollect *) data;
    GstGLMixerPad *mixpad = mixcol->mixpad;

    walk = g_slist_next (walk);

    if (mixcol->buffer == NULL) {
      GstBuffer *buf = NULL;

      GST_LOG_OBJECT (mix, "we need a new buffer");

      buf = gst_collect_pads_peek (mix->collect, data);

      if (buf) {
        guint64 duration;

        GST_LOG_OBJECT (mix, "we have a buffer !");

        mixcol->buffer = buf;

        duration = GST_BUFFER_DURATION (mixcol->buffer);
        /* no duration on the buffer, use the framerate */
        if (!GST_CLOCK_TIME_IS_VALID (duration)) {
          if (mixpad->fps_n == 0) {
            duration = GST_CLOCK_TIME_NONE;
          } else {
            duration =
                gst_util_uint64_scale_int (GST_SECOND, mixpad->fps_d,
                mixpad->fps_n);
          }
        }
        if (GST_CLOCK_TIME_IS_VALID (duration))
          mixpad->queued += duration;
        else if (!mixpad->queued)
          mixpad->queued = GST_CLOCK_TIME_NONE;
      } else {
        GST_LOG_OBJECT (mix, "pop returned a NULL buffer");
      }
    }
    if (mix->sendseg && (mixpad == mix->master)) {
      GstEvent *event;
      gint64 stop, start;
      GstSegment *segment = &data->segment;

      /* FIXME, use rate/applied_rate as set on all sinkpads.
       * - currently we just set rate as received from last seek-event
       * We could potentially figure out the duration as well using
       * the current segment positions and the stated stop positions.
       * Also we just start from stream time 0 which is rather
       * weird. For non-synchronized mixing, the time should be
       * the min of the stream times of all received segments,
       * rationale being that the duration is at least going to
       * be as long as the earliest stream we start mixing. This
       * would also be correct for synchronized mixing but then
       * the later streams would be delayed until the stream times
       * match.
       */
      GST_INFO_OBJECT (mix, "_sending play segment");

      start = segment->accum;

      /* get the duration of the segment if we can and add it to the accumulated
       * time on the segment. */
      if (segment->stop != -1 && segment->start != -1)
        stop = start + (segment->stop - segment->start);
      else
        stop = -1;

      event = gst_event_new_new_segment_full (FALSE, segment->rate, 1.0,
          segment->format, start, stop, start + mix->segment_position);
      gst_pad_push_event (mix->srcpad, event);
      mix->sendseg = FALSE;
    }

    if (mixcol->buffer != NULL && GST_CLOCK_TIME_IS_VALID (mixpad->queued)) {
      /* got a buffer somewhere so we're not eos */
      eos = FALSE;
    }
  }

  return eos;
}

static void
gst_gl_mixer_process_buffers (GstGLMixer * mix, GstBuffer * outbuf)
{
  GstGLMixerClass *mix_class = GST_GL_MIXER_GET_CLASS (mix);
  GSList *walk = mix->sinkpads;
  gint array_index = 0;

  while (walk) {                /* We walk with this list because it's ordered */
    GstGLMixerPad *pad = GST_GL_MIXER_PAD (walk->data);
    GstGLMixerCollect *mixcol = pad->mixcol;

    walk = g_slist_next (walk);

    if (mixcol->buffer != NULL) {
      GstClockTime timestamp;
      gint64 stream_time;
      GstSegment *seg;

      seg = &mixcol->collect.segment;

      timestamp = GST_BUFFER_TIMESTAMP (mixcol->buffer);

      stream_time =
          gst_segment_to_stream_time (seg, GST_FORMAT_TIME, timestamp);

      /* sync object properties on stream time */
      if (GST_CLOCK_TIME_IS_VALID (stream_time))
        gst_object_sync_values (G_OBJECT (pad), stream_time);

      /* put buffer into array */
      mix->array_buffers->pdata[array_index] = mixcol->buffer;

      if (pad == mix->master) {
        gint64 running_time;

        running_time =
            gst_segment_to_running_time (seg, GST_FORMAT_TIME, timestamp);

        /* outgoing buffers need the running_time */
        GST_BUFFER_TIMESTAMP (outbuf) = running_time;
        GST_BUFFER_DURATION (outbuf) = GST_BUFFER_DURATION (mixcol->buffer);

        mix->last_ts = running_time;
        if (GST_BUFFER_DURATION_IS_VALID (outbuf))
          mix->last_ts += GST_BUFFER_DURATION (outbuf);
      }
    }
    ++array_index;
  }

  mix_class->process_buffers (mix, mix->array_buffers, outbuf);
}

/* remove buffers from the queue that were expired in the
 * interval of the master, we also prepare the queued value
 * in the pad so that we can skip and fill buffers later on */
static void
gst_gl_mixer_update_queues (GstGLMixer * mix)
{
  GSList *walk;
  gint64 interval;

  interval = mix->master->queued;
  if (interval <= 0) {
    if (mix->fps_n == 0) {
      interval = G_MAXINT64;
    } else {
      interval = GST_SECOND * mix->fps_d / mix->fps_n;
    }
    GST_LOG_OBJECT (mix, "set interval to %" G_GINT64_FORMAT " nanoseconds",
        interval);
  }

  walk = mix->sinkpads;
  while (walk) {
    GstGLMixerPad *pad = GST_GL_MIXER_PAD (walk->data);
    GstGLMixerCollect *mixcol = pad->mixcol;

    walk = g_slist_next (walk);

    if (mixcol->buffer != NULL) {
      pad->queued -= interval;
      GST_LOG_OBJECT (pad, "queued now %" G_GINT64_FORMAT, pad->queued);
      if (pad->queued <= 0) {
        GstBuffer *buffer =
            gst_collect_pads_pop (mix->collect, &mixcol->collect);
        GST_LOG_OBJECT (pad, "unreffing buffer");
        if (buffer)
          gst_buffer_unref (buffer);
        else
          GST_WARNING_OBJECT (pad,
              "Buffer was removed by GstCollectPads in the meantime");
        gst_buffer_unref (mixcol->buffer);
        mixcol->buffer = NULL;
      }
    }
  }
}

static GstFlowReturn
gst_gl_mixer_collected (GstCollectPads * pads, GstGLMixer * mix)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer *outbuf = NULL;
  GstGLBuffer *gl_outbuf = NULL;
  gboolean eos = FALSE;
  gint width = 0;
  gint height = 0;

  g_return_val_if_fail (GST_IS_GL_MIXER (mix), GST_FLOW_ERROR);

  if (mix->width == 0) {
    GstCaps *newcaps = gst_caps_make_writable
        (gst_pad_get_negotiated_caps (GST_PAD (mix->master)));

    gst_pad_set_caps (mix->srcpad, newcaps);
  }

  GST_LOG_OBJECT (mix, "all pads are collected");
  GST_GL_MIXER_STATE_LOCK (mix);

  eos = gst_gl_mixer_fill_queues (mix);

  if (eos) {
    /* Push EOS downstream */
    GST_LOG_OBJECT (mix, "all our sinkpads are EOS, pushing downstream");
    gst_pad_push_event (mix->srcpad, gst_event_new_eos ());
    ret = GST_FLOW_WRONG_STATE;
    goto error;
  }

  /* Calculating out buffer size from input size */
  ret = gst_gl_buffer_parse_caps (GST_PAD_CAPS (mix->srcpad), &width, &height);

  if (!ret)
    goto error;

  gl_outbuf = gst_gl_buffer_new (mix->display, mix->width, mix->height);

  outbuf = GST_BUFFER (gl_outbuf);
  gst_buffer_set_caps (outbuf, GST_PAD_CAPS (mix->srcpad));

  gst_gl_mixer_process_buffers (mix, outbuf);

  gst_gl_mixer_update_queues (mix);
  GST_GL_MIXER_STATE_UNLOCK (mix);

  ret = gst_pad_push (mix->srcpad, outbuf);

beach:
  return ret;

  /* ERRORS */
error:
  {
    if (outbuf)
      gst_buffer_unref (outbuf);

    GST_GL_MIXER_STATE_UNLOCK (mix);
    goto beach;
  }
}

static gboolean
forward_event_func (GstPad * pad, GValue * ret, GstEvent * event)
{
  gst_event_ref (event);
  GST_LOG_OBJECT (pad, "About to send event %s", GST_EVENT_TYPE_NAME (event));
  if (!gst_pad_push_event (pad, event)) {
    g_value_set_boolean (ret, FALSE);
    GST_WARNING_OBJECT (pad, "Sending event  %p (%s) failed.",
        event, GST_EVENT_TYPE_NAME (event));
  } else {
    GST_LOG_OBJECT (pad, "Sent event  %p (%s).",
        event, GST_EVENT_TYPE_NAME (event));
  }
  gst_object_unref (pad);
  return TRUE;
}

/* forwards the event to all sinkpads, takes ownership of the
 * event
 *
 * Returns: TRUE if the event could be forwarded on all
 * sinkpads.
 */
static gboolean
forward_event (GstGLMixer * mix, GstEvent * event)
{
  GstIterator *it;
  GValue vret = { 0 };

  GST_LOG_OBJECT (mix, "Forwarding event %p (%s)", event,
      GST_EVENT_TYPE_NAME (event));

  g_value_init (&vret, G_TYPE_BOOLEAN);
  g_value_set_boolean (&vret, TRUE);
  it = gst_element_iterate_sink_pads (GST_ELEMENT_CAST (mix));
  gst_iterator_fold (it, (GstIteratorFoldFunction) forward_event_func, &vret,
      event);
  gst_iterator_free (it);
  gst_event_unref (event);

  return g_value_get_boolean (&vret);
}

static gboolean
gst_gl_mixer_src_event (GstPad * pad, GstEvent * event)
{
  GstGLMixer *mix = GST_GL_MIXER (gst_pad_get_parent (pad));
  gboolean result;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_QOS:
      /* QoS might be tricky */
      result = FALSE;
      break;
    case GST_EVENT_SEEK:
    {
      GstSeekFlags flags;
      GstSeekType curtype;
      gint64 cur;

      /* parse the seek parameters */
      gst_event_parse_seek (event, NULL, NULL, &flags, &curtype,
          &cur, NULL, NULL);

      /* check if we are flushing */
      if (flags & GST_SEEK_FLAG_FLUSH) {
        /* make sure we accept nothing anymore and return WRONG_STATE */
        gst_collect_pads_set_flushing (mix->collect, TRUE);

        /* flushing seek, start flush downstream, the flush will be done
         * when all pads received a FLUSH_STOP. */
        gst_pad_push_event (mix->srcpad, gst_event_new_flush_start ());
      }

      /* now wait for the collected to be finished and mark a new
       * segment */
      GST_OBJECT_LOCK (mix->collect);
      if (curtype == GST_SEEK_TYPE_SET)
        mix->segment_position = cur;
      else
        mix->segment_position = 0;
      mix->sendseg = TRUE;
      GST_OBJECT_UNLOCK (mix->collect);

      result = forward_event (mix, event);
      break;
    }
    case GST_EVENT_NAVIGATION:
      /* navigation is rather pointless. */
      result = FALSE;
      break;
    default:
      /* just forward the rest for now */
      result = forward_event (mix, event);
      break;
  }
  gst_object_unref (mix);

  return result;
}

static gboolean
gst_gl_mixer_sink_event (GstPad * pad, GstEvent * event)
{
  GstGLMixerPad *vpad = GST_GL_MIXER_PAD (pad);
  GstGLMixer *videomixer = GST_GL_MIXER (gst_pad_get_parent (pad));
  gboolean ret;

  GST_DEBUG_OBJECT (pad, "Got %s event on pad %s:%s",
      GST_EVENT_TYPE_NAME (event), GST_DEBUG_PAD_NAME (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
      /* mark a pending new segment. This event is synchronized
       * with the streaming thread so we can safely update the
       * variable without races. It's somewhat weird because we
       * assume the collectpads forwarded the FLUSH_STOP past us
       * and downstream (using our source pad, the bastard!).
       */
      videomixer->sendseg = TRUE;

      /* Reset pad state after FLUSH_STOP */
      if (vpad->mixcol->buffer)
        gst_buffer_unref (vpad->mixcol->buffer);
      vpad->mixcol->buffer = NULL;
      vpad->queued = 0;
      break;
    case GST_EVENT_NEWSEGMENT:
      videomixer->sendseg = TRUE;
      break;
    default:
      break;
  }

  /* now GstCollectPads can take care of the rest, e.g. EOS */
  ret = videomixer->collect_event (pad, event);

  gst_object_unref (videomixer);
  return ret;
}


static void
gst_gl_mixer_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  //GstGLMixer *mix = GST_GL_MIXER (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gl_mixer_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  //GstGLMixer *mix = GST_GL_MIXER (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_gl_mixer_change_state (GstElement * element, GstStateChange transition)
{
  GstGLMixer *mix;
  GstStateChangeReturn ret;
  GstGLMixerClass *mixer_class;

  g_return_val_if_fail (GST_IS_GL_MIXER (element), GST_STATE_CHANGE_FAILURE);

  mix = GST_GL_MIXER (element);
  mixer_class = GST_GL_MIXER_GET_CLASS (mix);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
    {
      GSList *walk = mix->sinkpads;
      gint i = 0;

      GstElement *parent = GST_ELEMENT (gst_element_get_parent (mix));
      GstStructure *structure = NULL;
      GstQuery *query = NULL;
      gboolean isPerformed = FALSE;

      if (!parent) {
        GST_ELEMENT_ERROR (mix, CORE, STATE_CHANGE, (NULL),
            ("A parent bin is required"));
        return FALSE;
      }

      structure = gst_structure_new (gst_element_get_name (mix), NULL);
      query = gst_query_new_application (GST_QUERY_CUSTOM, structure);

      /* retrieve the gldisplay that is owned by gl elements after the gl mixer */
      isPerformed = gst_element_query (parent, query);

      if (isPerformed) {
        const GValue *id_value =
            gst_structure_get_value (structure, "gstgldisplay");
        if (G_VALUE_HOLDS_POINTER (id_value))
          /* at least one gl element is after in our gl chain */
          mix->display =
              g_object_ref (GST_GL_DISPLAY (g_value_get_pointer (id_value)));
        else {
          /* this gl filter is a sink in terms of the gl chain */
          mix->display = gst_gl_display_new ();
          gst_gl_display_create_context (mix->display, 0);
        }
      }

      gst_query_unref (query);
      gst_object_unref (GST_OBJECT (parent));

      /* instanciate a gldisplay for each sink pad */
      while (walk) {
        GstGLMixerPad *sink_pad = GST_GL_MIXER_PAD (walk->data);
        walk = g_slist_next (walk);
        sink_pad->display = gst_gl_display_new ();
      }
      mix->array_buffers = g_ptr_array_sized_new (mix->next_sinkpad);
      for (i = 0; i < mix->next_sinkpad; ++i) {
        g_ptr_array_add (mix->array_buffers, NULL);
      }
      GST_LOG_OBJECT (mix, "starting collectpads");
      gst_collect_pads_start (mix->collect);
      break;
    }
    case GST_STATE_CHANGE_PAUSED_TO_READY:
    {
      GSList *walk = mix->sinkpads;
      GST_LOG_OBJECT (mix, "stopping collectpads");
      gst_collect_pads_stop (mix->collect);
      g_ptr_array_free (mix->array_buffers, TRUE);
      while (walk) {
        GstGLMixerPad *sink_pad = GST_GL_MIXER_PAD (walk->data);
        walk = g_slist_next (walk);
        if (sink_pad->display)
          gst_gl_display_activate_gl_context (sink_pad->display, FALSE);
      }
      if (mixer_class->reset)
        mixer_class->reset (mix);
      if (mix->fbo) {
        gst_gl_display_del_fbo (mix->display, mix->fbo, mix->depthbuffer);
        mix->fbo = 0;
        mix->depthbuffer = 0;
      }
      if (mix->display) {
        g_object_unref (mix->display);
        mix->display = NULL;
      }
      while (walk) {
        GstGLMixerPad *sink_pad = GST_GL_MIXER_PAD (walk->data);
        walk = g_slist_next (walk);
        if (sink_pad->display) {
          gst_gl_display_activate_gl_context (sink_pad->display, TRUE);
          g_object_unref (sink_pad->display);
          sink_pad->display = NULL;
        }
      }
      break;
    }
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_gl_mixer_reset (mix);
      break;
    default:
      break;
  }

  return ret;
}
