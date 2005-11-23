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

  /* state stuff */
  GstAdapter *adapter;
  guint count;
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
  gst_element_add_pad (GST_ELEMENT (visual), visual->sinkpad);

  visual->srcpad = gst_pad_new_from_static_template (&src_template, "src");
  gst_pad_set_setcaps_function (visual->srcpad, gst_visual_src_setcaps);
  gst_pad_set_getcaps_function (visual->srcpad, gst_visual_getcaps);
  gst_element_add_pad (GST_ELEMENT (visual), visual->srcpad);

  visual->next_ts = 0;
  visual->adapter = gst_adapter_new ();
}

static void
gst_visual_dispose (GObject * object)
{
  GstVisual *visual = GST_VISUAL (object);

  if (visual->adapter) {
    g_object_unref (visual->adapter);
    visual->adapter = NULL;
  }

  if (visual->actor) {
    visual_object_unref (VISUAL_OBJECT (visual->actor));
    visual->actor = NULL;
  }

  if (visual->video) {
    visual_object_unref (VISUAL_OBJECT (visual->video));
    visual->video = NULL;
  }
  GST_CALL_PARENT (G_OBJECT_CLASS, dispose, (object));
}

static GstCaps *
gst_visual_getcaps (GstPad * pad)
{
  GstCaps *ret;
  GstVisual *visual = GST_VISUAL (gst_pad_get_parent (pad));
  int depths;

  if (!visual->actor)
    return gst_caps_copy (gst_pad_get_pad_template_caps (visual->srcpad));

  ret = gst_caps_new_empty ();
  depths = visual_actor_get_supported_depth (visual->actor);
  if (depths < 0) {
    /* FIXME: set an error */
    return ret;
  }
  if (depths == VISUAL_VIDEO_DEPTH_GL) {
    /* We can't handle GL only plugins */
    return ret;
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

  GST_DEBUG_OBJECT (visual, "returning caps %" GST_PTR_FORMAT, ret);
  return ret;
}

static gboolean
gst_visual_src_setcaps (GstPad * pad, GstCaps * caps)
{
  GstVisual *visual = GST_VISUAL (gst_pad_get_parent (pad));
  GstStructure *structure;
  gint depth;

  structure = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (structure, "width", &visual->width))
    return FALSE;
  if (!gst_structure_get_int (structure, "height", &visual->height))
    return FALSE;
  if (!gst_structure_get_int (structure, "bpp", &depth))
    return FALSE;
  if (!gst_structure_get_fraction (structure, "framerate", &visual->fps_n,
          &visual->fps_d))
    return FALSE;

  visual_video_set_depth (visual->video,
      visual_video_depth_enum_from_value (depth));
  visual_video_set_dimension (visual->video, visual->width, visual->height);
  visual_actor_video_negotiate (visual->actor, 0, FALSE, FALSE);

  return TRUE;
}

static gboolean
gst_visual_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstVisual *visual = GST_VISUAL (gst_pad_get_parent (pad));
  GstStructure *structure;

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_get_int (structure, "rate", &visual->rate);

  return TRUE;
}

static GstFlowReturn
get_buffer (GstVisual * visual, GstBuffer ** outbuf)
{
  GstFlowReturn ret;

  if (GST_PAD_CAPS (visual->srcpad) == NULL) {
    gint width, height, bpp;
    GstStructure *s;
    GstCaps *caps;

    /* No output caps current set up. Try and pick some */
    caps = gst_pad_get_allowed_caps (visual->srcpad);

    if (gst_caps_is_empty (caps)) {
      gst_caps_unref (caps);
      return GST_FLOW_NOT_NEGOTIATED;
    }

    if (!gst_caps_is_fixed (caps)) {
      /* OK, not fixed, fixate the width and height */
      caps = gst_caps_make_writable (caps);
      gst_caps_truncate (caps);

      s = gst_caps_get_structure (caps, 0);

      gst_structure_fixate_field_nearest_int (s, "width", 320);
      gst_structure_fixate_field_nearest_int (s, "height", 240);
      gst_structure_fixate_field_nearest_fraction (s, "framerate", 25, 1);

      gst_pad_fixate_caps (visual->srcpad, caps);
    } else
      s = gst_caps_get_structure (caps, 0);

    GST_DEBUG_OBJECT (visual,
        "Trying to alloc buffer with caps: %" GST_PTR_FORMAT, caps);

    if (!gst_structure_get_int (s, "width", &width) ||
        !gst_structure_get_int (s, "height", &height) ||
        !gst_structure_get_int (s, "bpp", &bpp)) {
      ret = FALSE;
    } else {
      ret = gst_pad_alloc_buffer (visual->srcpad, GST_BUFFER_OFFSET_NONE,
          height * GST_ROUND_UP_4 (width) * bpp, caps, outbuf);
    }

    if (GST_PAD_CAPS (visual->srcpad) == NULL)
      gst_pad_set_caps (visual->srcpad, caps);
    gst_caps_unref (caps);
  } else {
    ret = gst_pad_alloc_buffer (visual->srcpad, GST_BUFFER_OFFSET_NONE,
        visual->video->height * GST_ROUND_UP_4 (visual->video->width) *
        visual->video->bpp, GST_PAD_CAPS (visual->srcpad), outbuf);
  }

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
  guint spf;

  /* If we don't have an output format yet, preallocate a buffer to try and
   * set one */
  if (GST_PAD_CAPS (visual->srcpad) == NULL) {
    ret = get_buffer (visual, &outbuf);
    if (ret != GST_FLOW_OK) {
      gst_buffer_unref (buffer);
      return ret;
    }
  }

  /* Match timestamps from the incoming audio */
  if (GST_BUFFER_TIMESTAMP (buffer) != GST_CLOCK_TIME_NONE)
    visual->next_ts = GST_BUFFER_TIMESTAMP (buffer);

  /* spf = samples per frame */
  spf = ((guint64) (visual->rate) * visual->fps_d) / visual->fps_n;
  gst_adapter_push (visual->adapter, buffer);

  while (gst_adapter_available (visual->adapter) > MAX (512, spf) * 4 &&
      (ret == GST_FLOW_OK)) {
    /* Read 512 samples per channel */
    const guint16 *data =
        (const guint16 *) gst_adapter_peek (visual->adapter, 512 * 4);

    for (i = 0; i < 512; i++) {
      visual->audio.plugpcm[0][i] = *data++;
      visual->audio.plugpcm[1][i] = *data++;
    }

    if (outbuf == NULL) {
      ret = get_buffer (visual, &outbuf);
      if (ret != GST_FLOW_OK) {
        return ret;
      }
    }

    if (visual->video != NULL) {
      visual_video_set_buffer (visual->video, GST_BUFFER_DATA (outbuf));
      visual_audio_analyze (&visual->audio);
      visual_actor_run (visual->actor, &visual->audio);

      GST_BUFFER_TIMESTAMP (outbuf) = visual->next_ts;
      GST_BUFFER_DURATION (outbuf) = gst_util_uint64_scale_int (GST_SECOND,
          visual->fps_d, visual->fps_n);
      visual->next_ts += GST_BUFFER_DURATION (outbuf);
      ret = gst_pad_push (visual->srcpad, outbuf);
      outbuf = NULL;
    }

    /* Flush out the number of samples per frame * channels * sizeof (gint16) */
    /* Recompute spf in case caps changed */
    spf = ((guint64) (visual->rate) * visual->fps_d) / visual->fps_n;
    GST_DEBUG_OBJECT (visual, "finished frame, flushing %u samples from input",
        spf);
    gst_adapter_flush (visual->adapter,
        MIN (gst_adapter_available (visual->adapter), spf * 4));
  }

  /* so we're on the safe side */
  if (visual->video)
    visual_video_set_buffer (visual->video, NULL);

  if (outbuf != NULL)
    gst_buffer_unref (outbuf);

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

      if (!visual->actor || !visual->video)
        return GST_STATE_CHANGE_FAILURE;

      if (visual_actor_realize (visual->actor) != 0) {
        visual_object_unref (VISUAL_OBJECT (visual->actor));
        visual->actor = NULL;
        return GST_STATE_CHANGE_FAILURE;
      }
      visual_actor_set_video (visual->actor, visual->video);
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_adapter_clear (visual->adapter);
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
      visual->next_ts = 0;
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      if (visual->actor)
        visual_object_unref (VISUAL_OBJECT (visual->actor));
      if (visual->video)
        visual_object_unref (VISUAL_OBJECT (visual->video));
      visual->actor = NULL;
      visual->video = NULL;
      break;
    default:
      break;
  }

  return ret;
}

static void
make_valid_name (char *name)
{
  /*
   * Replace invalid chars with _ in the type name
   */
  static const gchar *extra_chars = "-_+";
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

  if (!visual_is_initialized ())
    if (visual_init (NULL, NULL) != 0)
      return FALSE;

  visual_log_set_verboseness (VISUAL_LOG_VERBOSENESS_LOW);
  visual_log_set_info_handler (libvisual_log_handler, (void *) GST_LEVEL_INFO);
  visual_log_set_warning_handler (libvisual_log_handler,
      (void *) GST_LEVEL_WARNING);
  visual_log_set_critical_handler (libvisual_log_handler,
      (void *) GST_LEVEL_ERROR);
  visual_log_set_error_handler (libvisual_log_handler,
      (void *) GST_LEVEL_ERROR);

  list = visual_actor_get_list ();
  for (i = 0; i < visual_list_count (list); i++) {
    VisPluginRef *ref = visual_list_get (list, i);
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

    if (ref->info->plugname == NULL)
      continue;

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

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "libvisual",
    "libvisual visualization plugins",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
