/* GStreamer
 * Copyright (C) 2004 Benjamin Otte <in7y118@public.uni-hamburg.de>
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

#include <gst/gst.h>
#include "gstxine.h"
#include <xine/audio_out.h>
#include <xine/xine_internal.h>
#include <xine/plugin_catalog.h>

#define GST_TYPE_XINE_AUDIO_SINK \
  (gst_xine_audio_sink_get_type())
#define GST_XINE_AUDIO_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_XINE_AUDIO_SINK,GstXineAudioSink))
#define GST_XINE_AUDIO_SINK_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_XINE_AUDIO_SINK, GstXineAudioSinkClass))
#define GST_XINE_AUDIO_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_XINE_AUDIO_SINK,GstXineAudioSinkClass))
#define GST_IS_XINE_AUDIO_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_XINE_AUDIO_SINK))
#define GST_IS_XINE_AUDIO_SINK_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_XINE_AUDIO_SINK))

GType gst_xine_audio_sink_get_type (void);

typedef struct _GstXineAudioSink GstXineAudioSink;
typedef struct _GstXineAudioSinkClass GstXineAudioSinkClass;

struct _GstXineAudioSink
{
  GstXine parent;

  GstPad *sinkpad;

  ao_driver_t *driver;
  guint open;			/* number of bytes per sample or 0 if driver not open */
};

struct _GstXineAudioSinkClass
{
  GstXineClass parent_class;

  plugin_node_t *plugin_node;
};

/** GstXineAudioSink ***********************************************************/

GST_BOILERPLATE (GstXineAudioSink, gst_xine_audio_sink, GstXine, GST_TYPE_XINE)

     static GstElementStateReturn
	 gst_xine_audio_sink_change_state (GstElement * element);

     static void gst_xine_audio_sink_base_init (gpointer g_class)
{
}

static void
gst_xine_audio_sink_class_init (GstXineAudioSinkClass * klass)
{
  GstElementClass *element = GST_ELEMENT_CLASS (klass);

  element->change_state = gst_xine_audio_sink_change_state;
}

static void
gst_xine_audio_sink_init (GstXineAudioSink * xine)
{
}

static void
gst_xine_audio_sink_chain (GstPad * pad, GstData * data)
{
  GstXineAudioSink *xine =
      GST_XINE_AUDIO_SINK (gst_object_get_parent (GST_OBJECT (pad)));

  while (xine->driver->write (xine->driver, (guint16 *) GST_BUFFER_DATA (data),
	  GST_BUFFER_SIZE (data) / xine->open) == 0);
  gst_data_unref (GST_DATA (data));
}

static GstElementStateReturn
gst_xine_audio_sink_change_state (GstElement * element)
{
  GstXineAudioSink *xine = GST_XINE_AUDIO_SINK (element);
  audio_driver_class_t *driver =
      (audio_driver_class_t *) GST_XINE_AUDIO_SINK_GET_CLASS (xine)->
      plugin_node->plugin_class;

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      if (driver == NULL) {
	xine_audio_port_t *port =
	    xine_open_audio_driver (GST_XINE_GET_CLASS (xine)->xine,
	    GST_XINE_AUDIO_SINK_GET_CLASS (xine)->plugin_node->info->id, NULL);

	if (!port)
	  return GST_STATE_FAILURE;
	port->exit (port);
	driver =
	    (audio_driver_class_t *) GST_XINE_AUDIO_SINK_GET_CLASS (xine)->
	    plugin_node->plugin_class;
	if (driver == NULL)
	  return GST_STATE_FAILURE;
      }
      xine->driver = driver->open_plugin (driver, NULL);
      if (!xine->driver)
	return GST_STATE_FAILURE;
      break;
    case GST_STATE_READY_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      if (xine->open != 0)
	xine->driver->close (xine->driver);
      xine->open = 0;
      break;
    case GST_STATE_READY_TO_NULL:
      xine->driver->exit (xine->driver);
      xine->driver = NULL;
      break;
    default:
      GST_ERROR_OBJECT (element, "invalid state change");
      break;
  }

  return GST_CALL_PARENT_WITH_DEFAULT (GST_ELEMENT_CLASS, change_state,
      (element), GST_STATE_SUCCESS);
}

static GstCaps *
_xine_audio_sink_get_caps (GstPad * pad)
{
  GstXineAudioSink *xine =
      GST_XINE_AUDIO_SINK (gst_object_get_parent (GST_OBJECT (pad)));
  GstCaps *caps, *ret = gst_caps_new_empty ();
  guint32 capa, channels;

  if (!xine->driver)
    return gst_caps_copy (gst_pad_get_pad_template_caps (pad));

  capa = xine->driver->get_capabilities (xine->driver);
  channels = capa & (AO_CAP_MODE_MONO | AO_CAP_MODE_STEREO);

  if (channels == 0) {
    /* neither mono nor stereo supported, die */
    return ret;
  }

  /* this loop is messy */
  capa &= AO_CAP_8BITS;
  do {
    if (capa & AO_CAP_8BITS) {
      caps = gst_caps_from_string ("audio/x-raw-int, "
	  "signed = (boolean) FALSE, "
	  "width = (int) 8, "
	  "depth = (int) 8, " "rate = (int) [ 8000, 192000 ]");
      capa &= ~AO_CAP_8BITS;
    } else {
      caps = gst_caps_from_string ("audio/x-raw-int, "
	  "endianness = (int) BYTE_ORDER, "
	  "signed = (boolean) TRUE, "
	  "width = (int) 16, "
	  "depth = (int) 16, " "rate = (int) [ 8000, 192000 ]");
      capa = -1;
    }
    switch (channels) {
      case AO_CAP_MODE_MONO | AO_CAP_MODE_STEREO:
	gst_caps_set_simple (caps, "channels", GST_TYPE_INT_RANGE, 1, 2, NULL);
	break;
      case AO_CAP_MODE_MONO:
	gst_caps_set_simple (caps, "channels", G_TYPE_INT, 1, NULL);
	break;
      case AO_CAP_MODE_STEREO:
	gst_caps_set_simple (caps, "channels", G_TYPE_INT, 2, NULL);
	break;
      default:
	g_assert_not_reached ();
	break;
    }
    gst_caps_append (ret, caps);
  } while (capa != -1);

  return ret;
}

static GstPadLinkReturn
_xine_audio_sink_link (GstPad * pad, const GstCaps * caps)
{
  GstStructure *structure = gst_caps_get_structure (caps, 0);
  GstXineAudioSink *xine =
      GST_XINE_AUDIO_SINK (gst_object_get_parent (GST_OBJECT (pad)));
  guint channels, temp, rate, width;
  int mode;

  if (!gst_structure_get_int (structure, "channels", &channels))
    return GST_PAD_LINK_REFUSED;
  mode = (channels == 1) ? AO_CAP_MODE_MONO : AO_CAP_MODE_STEREO;
  if (!gst_structure_get_int (structure, "rate", &rate))
    return GST_PAD_LINK_REFUSED;
  if (!gst_structure_get_int (structure, "width", &width))
    return GST_PAD_LINK_REFUSED;

  if (xine->open != 0)
    xine->driver->close (xine->driver);
  xine->open = 0;
  temp = xine->driver->open (xine->driver, width, rate, mode);
  if (temp == 0)
    return GST_PAD_LINK_REFUSED;

  xine->open = channels * width / 8;
  if (temp != rate) {
    /* FIXME? */
    GST_WARNING_OBJECT (xine, "rates don't match (requested: %u, got %u)", rate,
	temp);
  }

  return GST_PAD_LINK_OK;
}

/** GstXineAudioSink subclasses ************************************************/

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
	"signed = (boolean) FALSE, "
	"width = (int) 8, "
	"depth = (int) 8, "
	"rate = (int) [ 8000, 192000 ], "
	"channels = (int) [1, 2]; "
	"audio/x-raw-int, "
	"endianness = (int) BYTE_ORDER, "
	"signed = (boolean) TRUE, "
	"width = (int) 16, "
	"depth = (int) 16, "
	"rate = (int) [ 8000, 192000 ], " "channels = (int) [1, 2]")
    );

static void
gst_xine_audio_sink_subclass_init (gpointer g_class, gpointer class_data)
{
  GstXineAudioSinkClass *xine_class = GST_XINE_AUDIO_SINK_CLASS (g_class);
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstElementDetails details = GST_ELEMENT_DETAILS (NULL,
      "Source",
      NULL,
      "Benjamin Otte <otte@gnome.org>");

  xine_class->plugin_node = class_data;
  details.longname =
      g_strdup_printf ("%s xine audio sink", xine_class->plugin_node->info->id);
  details.description =
      g_strdup_printf ("%s audio output using Xine",
      xine_class->plugin_node->info->id);
  gst_element_class_set_details (element_class, &details);
  g_free (details.longname);
  g_free (details.description);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));
}

static void
gst_xine_audio_sink_sub_init (GTypeInstance * instance, gpointer g_class)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (instance);
  GstXineAudioSink *xine = GST_XINE_AUDIO_SINK (instance);

  xine->sinkpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
	  "sink"), "sink");
  gst_pad_set_chain_function (xine->sinkpad, gst_xine_audio_sink_chain);
  gst_pad_set_getcaps_function (xine->sinkpad, _xine_audio_sink_get_caps);
  gst_pad_set_link_function (xine->sinkpad, _xine_audio_sink_link);
  gst_element_add_pad (GST_ELEMENT (xine), xine->sinkpad);
}

gboolean
gst_xine_audio_sink_init_plugin (GstPlugin * plugin)
{
  GTypeInfo plugin_info = {
    sizeof (GstXineAudioSinkClass),
    NULL,
    NULL,
    gst_xine_audio_sink_subclass_init,
    NULL,
    NULL,
    sizeof (GstXineAudioSink),
    0,
    gst_xine_audio_sink_sub_init,
  };
  plugin_node_t *node;
  GstXineClass *klass;

  klass = g_type_class_ref (GST_TYPE_XINE);

  node = xine_list_first_content (klass->xine->plugin_catalog->aout);
  while (node) {
    gchar *plugin_name = g_strdup_printf ("xineaudiosink_%s", node->info->id);
    gchar *type_name = g_strdup_printf ("GstXineAudioSink%s", node->info->id);
    GType type;

    plugin_info.class_data = node;
    type =
	g_type_register_static (GST_TYPE_XINE_AUDIO_SINK, type_name,
	&plugin_info, 0);
    g_free (type_name);
    if (!gst_element_register (plugin, plugin_name, GST_RANK_MARGINAL, type)) {
      g_free (plugin_name);
      return FALSE;
    }
    g_free (plugin_name);

    node = xine_list_next_content (klass->xine->plugin_catalog->aout);
  }

  g_type_class_unref (klass);
  return TRUE;
}
