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
#include <gst/bytestream/adapter.h>
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

struct _GstVisual
{
  GstElement element;

  /* pads */
  GstPad *sinkpad;
  GstPad *srcpad;

  /* libvisual stuff */
  VisAudio audio;
  VisVideo *video;
  VisActor *actor;

  /* audio/video state */
  gint rate;
  gdouble fps;
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

static GstElementStateReturn gst_visual_change_state (GstElement * element);
static void gst_visual_chain (GstPad * pad, GstData * _data);

static GstPadLinkReturn gst_visual_sinklink (GstPad * pad,
    const GstCaps * caps);
static GstPadLinkReturn gst_visual_srclink (GstPad * pad, const GstCaps * caps);
static GstCaps *gst_visual_getcaps (GstPad * pad);

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
  visual->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&sink_template),
      "sink");
  gst_pad_set_link_function (visual->sinkpad, gst_visual_sinklink);
  gst_pad_set_chain_function (visual->sinkpad, gst_visual_chain);
  gst_element_add_pad (GST_ELEMENT (visual), visual->sinkpad);

  visual->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&src_template),
      "src");
  gst_pad_set_link_function (visual->srcpad, gst_visual_srclink);
  gst_pad_set_getcaps_function (visual->srcpad, gst_visual_getcaps);
  gst_element_add_pad (GST_ELEMENT (visual), visual->srcpad);

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

  GST_CALL_PARENT (G_OBJECT_CLASS, dispose, (object));
}

static GstCaps *
gst_visual_getcaps (GstPad * pad)
{
  GstCaps *ret;
  GstVisual *visual = GST_VISUAL (gst_pad_get_parent (pad));

  if (!visual->actor)
    return gst_caps_copy (gst_pad_get_pad_template_caps (visual->srcpad));

  ret = gst_caps_new_empty ();
  if (visual_actor_depth_is_supported (visual->actor,
          VISUAL_VIDEO_CONTEXT_32BIT) == 1) {
    gst_caps_append (ret,
        gst_caps_from_string (GST_VIDEO_CAPS_xRGB_HOST_ENDIAN));
  }
  if (visual_actor_depth_is_supported (visual->actor,
          VISUAL_VIDEO_CONTEXT_24BIT) == 1) {
#if G_BYTE_ORDER == G_BIG_ENDIAN
    gst_caps_append (ret, gst_caps_from_string (GST_VIDEO_CAPS_RGB));
#else
    gst_caps_append (ret, gst_caps_from_string (GST_VIDEO_CAPS_BGR));
#endif
  }
  if (visual_actor_depth_is_supported (visual->actor,
          VISUAL_VIDEO_CONTEXT_16BIT) == 1) {
    gst_caps_append (ret, gst_caps_from_string (GST_VIDEO_CAPS_RGB_16));
  }
  return ret;
}

static GstPadLinkReturn
gst_visual_srclink (GstPad * pad, const GstCaps * caps)
{
  GstVisual *visual = GST_VISUAL (gst_pad_get_parent (pad));
  GstStructure *structure;
  gint depth;

  structure = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (structure, "width", &visual->width))
    return GST_PAD_LINK_REFUSED;
  if (!gst_structure_get_int (structure, "height", &visual->height))
    return GST_PAD_LINK_REFUSED;
  if (!gst_structure_get_double (structure, "framerate", &visual->fps))
    return GST_PAD_LINK_REFUSED;
  if (!gst_structure_get_int (structure, "bpp", &depth))
    return GST_PAD_LINK_REFUSED;

  if (visual->video)
    visual_video_free (visual->video);
  visual->video = visual_video_new ();
  visual_actor_set_video (visual->actor, visual->video);
  visual_video_set_opts (visual->video, "depth",
      visual_video_depth_enum_from_value (depth));
  visual_video_set_dimension (visual->video, visual->width, visual->height);
  visual_actor_video_negotiate (visual->actor);

  return GST_PAD_LINK_OK;
}

static GstPadLinkReturn
gst_visual_sinklink (GstPad * pad, const GstCaps * caps)
{
  GstVisual *visual = GST_VISUAL (gst_pad_get_parent (pad));
  GstStructure *structure;

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_get_int (structure, "rate", &visual->rate);

  return GST_PAD_LINK_OK;
}

static void
gst_visual_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *ret;
  guint i;
  GstVisual *visual = GST_VISUAL (gst_pad_get_parent (pad));

  /* spf = samples per frame */
  guint spf = visual->rate / visual->fps;

  gst_adapter_push (visual->adapter, GST_BUFFER (_data));
  while (gst_adapter_available (visual->adapter) > MAX (512, spf) * 4) {
    const guint16 *data =
        (const guint16 *) gst_adapter_peek (visual->adapter, 512);
    for (i = 0; i < 512; i++) {
      visual->audio.plugpcm[0][i] = *data++;
      visual->audio.plugpcm[1][i] = *data++;
    }
    ret = gst_pad_alloc_buffer (visual->srcpad, GST_BUFFER_OFFSET_NONE,
        visual->video->width * visual->video->width * visual->video->bpp);
    visual_video_set_buffer (visual->video, GST_BUFFER_DATA (ret));
    visual_actor_run (visual->actor, &visual->audio);
    GST_BUFFER_TIMESTAMP (ret) = GST_SECOND * visual->count++ / visual->fps;
    GST_BUFFER_DURATION (ret) = GST_SECOND / visual->fps;
    gst_pad_push (visual->srcpad, GST_DATA (ret));
    gst_adapter_flush (visual->adapter, spf * 4);
  }
  /* so we're on the safe side */
  visual_video_set_buffer (visual->video, NULL);
}

static GstElementStateReturn
gst_visual_change_state (GstElement * element)
{
  GstVisual *visual = GST_VISUAL (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      visual->actor =
          visual_actor_new (GST_VISUAL_GET_CLASS (visual)->plugin->name);
      if (!visual->actor)
        return GST_STATE_FAILURE;
      if (visual_actor_realize (visual->actor) != 0) {
        visual_actor_free (visual->actor);
        visual->actor = NULL;
        return GST_STATE_FAILURE;
      }
      break;
    case GST_STATE_READY_TO_PAUSED:
      gst_adapter_clear (visual->adapter);
      visual->count = 0;
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      break;
    case GST_STATE_READY_TO_NULL:
      visual_actor_destroy (visual->actor);
      visual->actor = NULL;
      break;
    default:
      g_assert_not_reached ();
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  guint i;
  VisList *list;

  if (!gst_library_load ("gstbytestream"))
    return FALSE;

  if (visual_init (NULL, NULL) != 0)
    return FALSE;

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

    if (ref->name == NULL)
      continue;
    name = g_strdup_printf ("GstVisual%s", ref->name);
    type = g_type_register_static (GST_TYPE_VISUAL, name, &info, 0);
    g_free (name);
    name = g_strdup_printf ("libvisual_%s", ref->name);
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
    plugin_init, VERSION, "LGPL", GST_PACKAGE, GST_ORIGIN)
