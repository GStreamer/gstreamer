/* GStreamer
 * Copyright (C) 2004 Benjamin Otte <otte@gnome.org>
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
#include <gst/base/gstadapter.h>
#include <gst/video/video.h>
#include <gst/audio/audio.h>
#include <libvisual/libvisual.h>

#define GST_TYPE_VISUAL (gst_visual_get_type())
#define GST_IS_VISUAL(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VISUAL))
#define GST_VISUAL(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VISUAL,GstVisual))
#define GST_IS_VISUAL_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VISUAL))
#define GST_VISUAL_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VISUAL,GstVisualClass))
#define GST_VISUAL_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_VISUAL, GstVisualClass))

typedef struct _GstVisual GstVisual;
typedef struct _GstVisualClass GstVisualClass;

GST_DEBUG_CATEGORY_STATIC (libvisual_debug);
#define GST_CAT_DEFAULT (libvisual_debug)

struct _GstVisual
{
  GstElement element;

  /* pads */
  GstPad *sinkpad;
  GstPad *srcpad;
  GstClockTime next_ts;

  /* libvisual stuff */
  VisAudio audio;
  VisVideo *video;
  VisActor *actor;

  /* audio/video state */
  gint rate;                    /* Input samplerate */

  /* framerate numerator & denominator */
  gint fps_n;
  gint fps_d;
  gint width;
  gint height;
  GstClockTime duration;
  guint outsize;

  /* samples per frame based on caps */
  guint spf;

  /* state stuff */
  GstAdapter *adapter;
  guint count;

  /* QoS stuff *//* with LOCK */
  gdouble proportion;
  GstClockTime earliest_time;
};

struct _GstVisualClass
{
  GstElementClass parent_class;

  VisPluginRef *plugin;
};

GType gst_visual_get_type (void);


static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_xRGB_HOST_ENDIAN "; "
#if G_BYTE_ORDER == G_BIG_ENDIAN
        GST_VIDEO_CAPS_RGB "; "
#else
        GST_VIDEO_CAPS_BGR "; "
#endif
        GST_VIDEO_CAPS_RGB_16)
    );

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "width = (int) 16, "
        "depth = (int) 16, "
        "endianness = (int) BYTE_ORDER, "
        "signed = (boolean) TRUE, "
        "channels = (int) 2, " "rate = (int) [ 1000, MAX ]")
    );


static void gst_visual_class_init (gpointer g_class, gpointer class_data);
static void gst_visual_init (GstVisual * visual);
static void gst_visual_dispose (GObject * object);

static GstStateChangeReturn gst_visual_change_state (GstElement * element,
    GstStateChange transition);
static GstFlowReturn gst_visual_chain (GstPad * pad, GstBuffer * buffer);
static gboolean gst_visual_sink_event (GstPad * pad, GstEvent * event);
static gboolean gst_visual_src_event (GstPad * pad, GstEvent * event);

static gboolean gst_visual_sink_setcaps (GstPad * pad, GstCaps * caps);
static gboolean gst_visual_src_setcaps (GstPad * pad, GstCaps * caps);
static GstCaps *gst_visual_getcaps (GstPad * pad);
static void libvisual_log_handler (const char *message, const char *funcname,
    void *priv);

static GstElementClass *parent_class = NULL;

GType
gst_visual_get_type (void)
{
  static GType type = 0;

  if (!type) {
    static const GTypeInfo info = {
      sizeof (GstVisualClass),
      NULL,
      NULL,
      gst_visual_class_init,
      NULL,
      NULL,
      sizeof (GstVisual),
      0,
      (GInstanceInitFunc) gst_visual_init,
    };

    type = g_type_register_static (GST_TYPE_ELEMENT, "GstVisual", &info, 0);
  }
  return type;
}

static void
libvisual_log_handler (const char *message, const char *funcname, void *priv)
{
  GST_CAT_LEVEL_LOG (libvisual_debug, (GstDebugLevel) (priv), NULL, "%s - %s",
      funcname, message);
}

static void
gst_visual_class_init (gpointer g_class, gpointer class_data)
{
  GstVisualClass *klass = GST_VISUAL_CLASS (g_class);
  GstElementClass *element = GST_ELEMENT_CLASS (g_class);
  GObjectClass *object = G_OBJECT_CLASS (g_class);

  klass->plugin = class_data;

  element->change_state = gst_visual_change_state;

  if (class_data == NULL) {
    parent_class = g_type_class_peek_parent (g_class);
  } else {
    GstElementDetails details = {
      NULL,
      "Visualization",
      klass->plugin->info->about,
      "Benjamin Otte <otte@gnome.org>"
    };

    details.longname = g_strdup_printf ("libvisual %s plugin v.%s",
        klass->plugin->info->name, klass->plugin->info->version);

    /* FIXME: improve to only register what plugin supports? */
    gst_element_class_add_pad_template (element,
        gst_static_pad_template_get (&src_template));
    gst_element_class_add_pad_template (element,
        gst_static_pad_template_get (&sink_template));
    gst_element_class_set_details (element, &details);
    g_free (details.longname);
  }

  object->dispose = gst_visual_dispose;
}

static void
gst_visual_init (GstVisual * visual)
{
  /* create the sink and src pads */
  visual->sinkpad = gst_pad_new_from_static_template (&sink_template, "sink");
  gst_pad_set_setcaps_function (visual->sinkpad, gst_visual_sink_setcaps);
  gst_pad_set_chain_function (visual->sinkpad, gst_visual_chain);
  gst_pad_set_event_function (visual->sinkpad, gst_visual_sink_event);
  gst_element_add_pad (GST_ELEMENT (visual), visual->sinkpad);

  visual->srcpad = gst_pad_new_from_static_template (&src_template, "src");
  gst_pad_set_setcaps_function (visual->srcpad, gst_visual_src_setcaps);
  gst_pad_set_getcaps_function (visual->srcpad, gst_visual_getcaps);
  gst_pad_set_event_function (visual->srcpad, gst_visual_src_event);
  gst_element_add_pad (GST_ELEMENT (visual), visual->srcpad);

  visual->adapter = gst_adapter_new ();
}

static void
gst_visual_clear_actors (GstVisual * visual)
{
  if (visual->actor) {
    visual_object_unref (VISUAL_OBJECT (visual->actor));
    visual->actor = NULL;
  }
  if (visual->video) {
    visual_object_unref (VISUAL_OBJECT (visual->video));
    visual->video = NULL;
  }
}

static void
gst_visual_dispose (GObject * object)
{
  GstVisual *visual = GST_VISUAL (object);

  if (visual->adapter) {
    g_object_unref (visual->adapter);
    visual->adapter = NULL;
  }
  gst_visual_clear_actors (visual);

  GST_CALL_PARENT (G_OBJECT_CLASS, dispose, (object));
}

static void
gst_visual_reset (GstVisual * visual)
{
  visual->next_ts = -1;
  gst_adapter_clear (visual->adapter);

  GST_OBJECT_LOCK (visual);
  visual->proportion = 1.0;
  visual->earliest_time = -1;
  GST_OBJECT_UNLOCK (visual);
}

static GstCaps *
gst_visual_getcaps (GstPad * pad)
{
  GstCaps *ret;
  GstVisual *visual = GST_VISUAL (gst_pad_get_parent (pad));
  int depths;

  if (!visual->actor) {
    ret = gst_caps_copy (gst_pad_get_pad_template_caps (visual->srcpad));
    goto beach;
  }

  ret = gst_caps_new_empty ();
  depths = visual_actor_get_supported_depth (visual->actor);
  if (depths < 0) {
    /* FIXME: set an error */
    goto beach;
  }
  if (depths == VISUAL_VIDEO_DEPTH_GL) {
    /* We can't handle GL only plugins */
    goto beach;
  }

  GST_DEBUG_OBJECT (visual, "libvisual plugin supports depths %u (0x%04x)",
      depths, depths);
  /* if (depths & VISUAL_VIDEO_DEPTH_32BIT) Always supports 32bit output */
  gst_caps_append (ret, gst_caps_from_string (GST_VIDEO_CAPS_xRGB_HOST_ENDIAN));

  if (depths & VISUAL_VIDEO_DEPTH_24BIT) {
#if G_BYTE_ORDER == G_BIG_ENDIAN
    gst_caps_append (ret, gst_caps_from_string (GST_VIDEO_CAPS_RGB));
#else
    gst_caps_append (ret, gst_caps_from_string (GST_VIDEO_CAPS_BGR));
#endif
  }
  if (depths & VISUAL_VIDEO_DEPTH_16BIT) {
    gst_caps_append (ret, gst_caps_from_string (GST_VIDEO_CAPS_RGB_16));
  }

beach:

  GST_DEBUG_OBJECT (visual, "returning caps %" GST_PTR_FORMAT, ret);
  gst_object_unref (visual);
  return ret;
}

static gboolean
gst_visual_src_setcaps (GstPad * pad, GstCaps * caps)
{
  GstVisual *visual = GST_VISUAL (gst_pad_get_parent (pad));
  GstStructure *structure;
  gint depth;

  structure = gst_caps_get_structure (caps, 0);

  GST_DEBUG_OBJECT (visual, "src pad got caps %" GST_PTR_FORMAT, caps);

  if (!gst_structure_get_int (structure, "width", &visual->width))
    goto error;
  if (!gst_structure_get_int (structure, "height", &visual->height))
    goto error;
  if (!gst_structure_get_int (structure, "bpp", &depth))
    goto error;
  if (!gst_structure_get_fraction (structure, "framerate", &visual->fps_n,
          &visual->fps_d))
    goto error;

  visual_video_set_depth (visual->video,
      visual_video_depth_enum_from_value (depth));
  visual_video_set_dimension (visual->video, visual->width, visual->height);
  visual_actor_video_negotiate (visual->actor, 0, FALSE, FALSE);

  /* precalc some values */
  visual->outsize =
      visual->video->height * GST_ROUND_UP_4 (visual->video->width) *
      visual->video->bpp;
  visual->spf =
      gst_util_uint64_scale_int (visual->rate, visual->fps_d, visual->fps_n);
  visual->duration =
      gst_util_uint64_scale_int (GST_SECOND, visual->fps_d, visual->fps_n);

  gst_object_unref (visual);
  return TRUE;

error:
  {
    gst_object_unref (visual);
    return FALSE;
  }
}

static gboolean
gst_visual_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstVisual *visual = GST_VISUAL (gst_pad_get_parent (pad));
  GstStructure *structure;

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_get_int (structure, "rate", &visual->rate);

  if (visual->fps_n != 0) {
    visual->spf =
        gst_util_uint64_scale_int (visual->rate, visual->fps_d, visual->fps_n);
  }

  gst_object_unref (visual);
  return TRUE;
}

static gboolean
gst_vis_src_negotiate (GstVisual * visual)
{
  GstCaps *othercaps, *target, *intersect;
  GstStructure *structure;
  const GstCaps *templ;

  templ = gst_pad_get_pad_template_caps (visual->srcpad);

  /* see what the peer can do */
  othercaps = gst_pad_peer_get_caps (visual->srcpad);
  if (othercaps) {
    intersect = gst_caps_intersect (othercaps, templ);
    gst_caps_unref (othercaps);

    if (gst_caps_is_empty (intersect))
      goto no_format;

    target = gst_caps_copy_nth (intersect, 0);
    gst_caps_unref (intersect);
  } else {
    target = gst_caps_ref ((GstCaps *) templ);
  }

  structure = gst_caps_get_structure (target, 0);
  gst_structure_fixate_field_nearest_int (structure, "width", 320);
  gst_structure_fixate_field_nearest_int (structure, "height", 240);
  gst_structure_fixate_field_nearest_fraction (structure, "framerate", 30, 1);

  gst_pad_set_caps (visual->srcpad, target);
  gst_caps_unref (target);

  return TRUE;

no_format:
  {
    GST_ELEMENT_ERROR (visual, STREAM, FORMAT, (NULL),
        ("could not negotiate output format"));
    gst_caps_unref (intersect);
    gst_caps_unref (othercaps);
    return FALSE;
  }
}

static gboolean
gst_visual_sink_event (GstPad * pad, GstEvent * event)
{
  GstVisual *visual;
  gboolean res;

  visual = GST_VISUAL (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      res = gst_pad_push_event (visual->srcpad, event);
      break;
    case GST_EVENT_FLUSH_STOP:
      /* reset QoS and adapter. */
      gst_visual_reset (visual);
      res = gst_pad_push_event (visual->srcpad, event);
      break;
    default:
      res = gst_pad_push_event (visual->srcpad, event);
      break;
  }

  gst_object_unref (visual);
  return res;
}

static gboolean
gst_visual_src_event (GstPad * pad, GstEvent * event)
{
  GstVisual *visual;
  gboolean res;

  visual = GST_VISUAL (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_QOS:
    {
      gdouble proportion;
      GstClockTimeDiff diff;
      GstClockTime timestamp;

      gst_event_parse_qos (event, &proportion, &diff, &timestamp);

      GST_OBJECT_LOCK (visual);
      visual->proportion = proportion;
      visual->earliest_time = timestamp + diff;
      GST_OBJECT_UNLOCK (visual);

      res = gst_pad_push_event (visual->sinkpad, event);
      break;
    }
    default:
      res = gst_pad_push_event (visual->sinkpad, event);
      break;
  }

  gst_object_unref (visual);
  return res;
}

static GstFlowReturn
get_buffer (GstVisual * visual, GstBuffer ** outbuf)
{
  GstFlowReturn ret;

  if (GST_PAD_CAPS (visual->srcpad) == NULL) {
    if (!gst_vis_src_negotiate (visual))
      return GST_FLOW_NOT_NEGOTIATED;
  }

  GST_DEBUG_OBJECT (visual, "allocating output buffer with caps %"
      GST_PTR_FORMAT, GST_PAD_CAPS (visual->srcpad));

  ret =
      gst_pad_alloc_buffer_and_set_caps (visual->srcpad,
      GST_BUFFER_OFFSET_NONE, visual->outsize,
      GST_PAD_CAPS (visual->srcpad), outbuf);

  if (ret != GST_FLOW_OK)
    return ret;

  if (*outbuf == NULL)
    return GST_FLOW_ERROR;

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_visual_chain (GstPad * pad, GstBuffer * buffer)
{
  GstBuffer *outbuf = NULL;
  guint i;
  GstVisual *visual = GST_VISUAL (gst_pad_get_parent (pad));
  GstFlowReturn ret = GST_FLOW_OK;

  GST_DEBUG_OBJECT (visual, "chain function called");

  /* If we don't have an output format yet, preallocate a buffer to try and
   * set one */
  if (GST_PAD_CAPS (visual->srcpad) == NULL) {
    ret = get_buffer (visual, &outbuf);
    if (ret != GST_FLOW_OK) {
      gst_buffer_unref (buffer);
      goto beach;
    }
  }

  /* Match timestamps from the incoming audio */
  if (GST_BUFFER_TIMESTAMP (buffer) != GST_CLOCK_TIME_NONE)
    visual->next_ts = GST_BUFFER_TIMESTAMP (buffer);

  gst_adapter_push (visual->adapter, buffer);

  while (gst_adapter_available (visual->adapter) > MAX (512, visual->spf) * 4 &&
      (ret == GST_FLOW_OK)) {
    gboolean need_skip;

    /* Read 512 samples per channel */
    const guint16 *data;

    data = (const guint16 *) gst_adapter_peek (visual->adapter, 512 * 4);

    GST_DEBUG_OBJECT (visual, "QoS: in: %" G_GUINT64_FORMAT
        ", earliest: %" G_GUINT64_FORMAT, visual->next_ts,
        visual->earliest_time);

    if (visual->next_ts != -1) {
      GST_OBJECT_LOCK (visual);
      /* check for QoS, don't compute buffers that are known to be latea */
      need_skip = visual->earliest_time != -1 &&
          visual->next_ts <= visual->earliest_time;
      GST_OBJECT_UNLOCK (visual);

      if (need_skip) {
        GST_WARNING_OBJECT (visual, "skipping frame because of QoS");
        goto skip;
      }
    }

    for (i = 0; i < 512; i++) {
      visual->audio.plugpcm[0][i] = *data++;
      visual->audio.plugpcm[1][i] = *data++;
    }

    if (outbuf == NULL) {
      ret = get_buffer (visual, &outbuf);
      if (ret != GST_FLOW_OK) {
        goto beach;
      }
    }
    visual_video_set_buffer (visual->video, GST_BUFFER_DATA (outbuf));
    visual_audio_analyze (&visual->audio);
    visual_actor_run (visual->actor, &visual->audio);
    visual_video_set_buffer (visual->video, NULL);

    GST_BUFFER_TIMESTAMP (outbuf) = visual->next_ts;
    GST_BUFFER_DURATION (outbuf) = visual->duration;

    ret = gst_pad_push (visual->srcpad, outbuf);
    outbuf = NULL;

    GST_DEBUG_OBJECT (visual, "finished frame, flushing %u samples from input",
        visual->spf);
  skip:
    /* interpollate next timestamp */
    if (visual->next_ts != -1)
      visual->next_ts += visual->duration;

    /* Flush out the number of samples per frame * channels * sizeof (gint16) */
    gst_adapter_flush (visual->adapter,
        MIN (gst_adapter_available (visual->adapter), visual->spf * 4));
  }

  if (outbuf != NULL)
    gst_buffer_unref (outbuf);

beach:
  gst_object_unref (visual);

  return ret;
}

static GstStateChangeReturn
gst_visual_change_state (GstElement * element, GstStateChange transition)
{
  GstVisual *visual = GST_VISUAL (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      visual->actor =
          visual_actor_new (GST_VISUAL_GET_CLASS (visual)->plugin->info->
          plugname);
      visual->video = visual_video_new ();

      /* can't have a play without actors */
      if (!visual->actor || !visual->video)
        goto no_actors;

      if (visual_actor_realize (visual->actor) != 0)
        goto no_realize;

      visual_actor_set_video (visual->actor, visual->video);
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_visual_reset (visual);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_visual_clear_actors (visual);
      break;
    default:
      break;
  }

  return ret;

  /* ERRORS */
no_actors:
  {
    GST_ELEMENT_ERROR (visual, LIBRARY, INIT, (NULL),
        ("could not create actors"));
    gst_visual_clear_actors (visual);
    return GST_STATE_CHANGE_FAILURE;
  }
no_realize:
  {
    GST_ELEMENT_ERROR (visual, LIBRARY, INIT, (NULL),
        ("could not realize actor"));
    gst_visual_clear_actors (visual);
    return GST_STATE_CHANGE_FAILURE;
  }
}

static void
make_valid_name (char *name)
{
  /*
   * Replace invalid chars with _ in the type name
   */
  static const gchar extra_chars[] = "-_+";
  gchar *p = name;

  for (; *p; p++) {
    int valid = ((p[0] >= 'A' && p[0] <= 'Z') ||
        (p[0] >= 'a' && p[0] <= 'z') ||
        (p[0] >= '0' && p[0] <= '9') || strchr (extra_chars, p[0]));
    if (!valid)
      *p = '_';
  }
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  guint i;
  VisList *list;

  GST_DEBUG_CATEGORY_INIT (libvisual_debug, "libvisual", 0,
      "libvisual audio visualisations");

  visual_log_set_verboseness (VISUAL_LOG_VERBOSENESS_LOW);
  visual_log_set_info_handler (libvisual_log_handler, (void *) GST_LEVEL_INFO);
  visual_log_set_warning_handler (libvisual_log_handler,
      (void *) GST_LEVEL_WARNING);
  visual_log_set_critical_handler (libvisual_log_handler,
      (void *) GST_LEVEL_ERROR);
  visual_log_set_error_handler (libvisual_log_handler,
      (void *) GST_LEVEL_ERROR);

  if (!visual_is_initialized ())
    if (visual_init (NULL, NULL) != 0)
      return FALSE;

  list = visual_actor_get_list ();
  for (i = 0; i < visual_list_count (list); i++) {
    VisPluginRef *ref = visual_list_get (list, i);
    VisActorPlugin *actplugin = NULL;
    VisPluginData *visplugin = NULL;
    gboolean is_gl = FALSE;
    GType type;
    gchar *name;
    GTypeInfo info = {
      sizeof (GstVisualClass),
      NULL,
      NULL,
      gst_visual_class_init,
      NULL,
      ref,
      sizeof (GstVisual),
      0,
      NULL
    };

    visplugin = visual_plugin_load (ref);

    if (ref->info->plugname == NULL)
      continue;

    actplugin = VISUAL_PLUGIN_ACTOR (visplugin->info->plugin);

    /* Ignore plugins that only support GL output for now */
    if (actplugin->depth == VISUAL_VIDEO_DEPTH_GL) {
      GST_DEBUG ("plugin %s is a GL plugin (%d), ignoring",
          ref->info->plugname, actplugin->depth);
      is_gl = TRUE;
    } else {
      GST_DEBUG ("plugin %s is not a GL plugin (%d), registering",
          ref->info->plugname, actplugin->depth);
    }

    visual_plugin_unload (visplugin);

    if (!is_gl) {
      name = g_strdup_printf ("GstVisual%s", ref->info->plugname);
      make_valid_name (name);
      type = g_type_register_static (GST_TYPE_VISUAL, name, &info, 0);
      g_free (name);

      name = g_strdup_printf ("libvisual_%s", ref->info->plugname);
      make_valid_name (name);
      if (!gst_element_register (plugin, name, GST_RANK_NONE, type)) {
        g_free (name);
        return FALSE;
      }
      g_free (name);
    }
  }

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "libvisual",
    "libvisual visualization plugins",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
