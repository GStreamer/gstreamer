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
#include <xine/input_plugin.h>
#include <xine/xine_internal.h>
#include <xine/plugin_catalog.h>

#define GST_TYPE_XINE_INPUT \
  (gst_xine_input_get_type())
#define GST_XINE_INPUT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_XINE_INPUT,GstXineInput))
#define GST_XINE_INPUT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_XINE_INPUT, GstXineInputClass))
#define GST_XINE_INPUT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_XINE_INPUT,GstXineInputClass))
#define GST_IS_XINE_INPUT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_XINE_INPUT))
#define GST_IS_XINE_INPUT_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_XINE_INPUT))

GType gst_xine_input_get_type (void);

typedef struct _GstXineInput      GstXineInput;
typedef struct _GstXineInputClass GstXineInputClass;

struct _GstXineInput
{
  GstXine		parent;

  GstPad *		srcpad;

  input_plugin_t *	input;
  gchar *		location;
  guint			blocksize;
};

struct _GstXineInputClass 
{
  GstXineClass		parent_class;

  plugin_node_t *	plugin_node;
};

/** GstXineInput ***********************************************************/

enum {
  ARG_0,
  ARG_LOCATION
};

GST_BOILERPLATE (GstXineInput, gst_xine_input, GstXine, GST_TYPE_XINE)

static void	gst_xine_input_dispose		(GObject *object);
static void	gst_xine_input_set_property 	(GObject *object, guint prop_id, 
						 const GValue *value, GParamSpec *pspec);
static void 	gst_xine_input_get_property 	(GObject *object, guint prop_id,
						 GValue *value, GParamSpec *pspec);
static GstElementStateReturn
		gst_xine_input_change_state	(GstElement *element);

static void
gst_xine_input_base_init (gpointer g_class)
{
}

static void
gst_xine_input_class_init (GstXineInputClass *klass)
{
  GstElementClass *element = GST_ELEMENT_CLASS (klass);
  GObjectClass *object = G_OBJECT_CLASS (klass);

  element->change_state = gst_xine_input_change_state;

  object->set_property = gst_xine_input_set_property;
  object->get_property = gst_xine_input_get_property;
  object->dispose = gst_xine_input_dispose;
  
  g_object_class_install_property (object, ARG_LOCATION, 
    g_param_spec_string ("location", "location", "location", 
	                 NULL, G_PARAM_READWRITE));	
}

static void
gst_xine_input_init (GstXineInput *xine)
{
}

static void
gst_xine_input_dispose (GObject *object)
{
  GstXineInput *xine = GST_XINE_INPUT (object);

  g_free (xine->location);
  xine->location = NULL;
}

static void
gst_xine_input_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstXineInput *xine = GST_XINE_INPUT (object);

  switch (prop_id) {
    case ARG_LOCATION:
      if (gst_element_get_state (GST_ELEMENT (xine)) != GST_STATE_NULL)
	return;
      if (xine->location)
	g_free (xine->location);
      xine->location = g_strdup (g_value_get_string (value));
      break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    return;
  }
}

static void
gst_xine_input_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstXineInput *xine = GST_XINE_INPUT (object);

  switch (prop_id) {
    case ARG_LOCATION:
      g_value_set_string (value, xine->location);
      break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    return;
  }
}

#define BUFFER_SIZE 4096 /* FIXME: what size? */
static GstData *
gst_xine_input_get (GstPad *pad)
{
  GstXineInput *xine = GST_XINE_INPUT (gst_object_get_parent (GST_OBJECT (pad)));
  GstBuffer *buf;
  gint real_size, position;
  
  /* FIXME: how does xine figure out EOS? */
  position = xine->input->get_current_pos (xine->input);
  if (position > 0 && position == xine->input->get_length (xine->input)) {
    gst_element_set_eos (GST_ELEMENT (xine));
    return GST_DATA (gst_event_new (GST_EVENT_EOS));
  }
  
  buf = gst_pad_alloc_buffer (xine->srcpad, GST_BUFFER_OFFSET_NONE, xine->blocksize);
  GST_BUFFER_OFFSET (buf) = position;
  real_size = xine->input->read (xine->input, GST_BUFFER_DATA (buf), GST_BUFFER_MAXSIZE (buf));
  GST_BUFFER_SIZE (buf) = real_size;
  if (real_size < 0) {
    GST_ELEMENT_ERROR (xine, RESOURCE, READ, (NULL), ("error %d reading data", real_size));
    gst_data_unref (GST_DATA (buf));
    return NULL;
  } else if (real_size == 0) {
    buf_element_t *element;
    if (xine->input->get_capabilities (xine->input) & INPUT_CAP_BLOCK)
      element = xine->input->read_block (xine->input, gst_xine_get_stream (GST_XINE (xine))->audio_fifo, xine->blocksize);
    if (element == NULL) {
      /* FIXME: is this EOS? */
      gst_element_set_eos (GST_ELEMENT (xine));
      return GST_DATA (gst_event_new (GST_EVENT_EOS));	
    } else {
      GST_BUFFER_SIZE (buf) = element->size;
      /* FIXME: put buf_element_t data in buffer */
      memcpy (GST_BUFFER_DATA (buf), element->mem, element->size);
      element->free_buffer (element);
    }
  }
  GST_BUFFER_OFFSET_END (buf) = xine->input->get_current_pos (xine->input);
  
  return GST_DATA (buf);
}

static GstElementStateReturn
gst_xine_input_change_state (GstElement *element)
{
  GstXineInput *xine = GST_XINE_INPUT (element);
  input_class_t *input = (input_class_t *) GST_XINE_INPUT_GET_CLASS (xine)->plugin_node->plugin_class;
  
  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      xine->input = input->get_instance (input, gst_xine_get_stream (GST_XINE (xine)), xine->location);
      if (!xine->input)
	return GST_STATE_FAILURE;
      if (xine->input->open (xine->input) == 0)
	return GST_STATE_FAILURE;
      xine->blocksize = xine->input->get_blocksize (xine->input);
      if (xine->blocksize == 0)
	xine->blocksize = BUFFER_SIZE;
      break;
    case GST_STATE_READY_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      /* FIXME: reset stream */
      break;
    case GST_STATE_READY_TO_NULL:
      xine->input->dispose (xine->input);
      xine->input = NULL;
      break;
    default:
      GST_ERROR_OBJECT (element, "invalid state change");
      break;
  }
  
  return GST_CALL_PARENT_WITH_DEFAULT (GST_ELEMENT_CLASS, change_state, (element), GST_STATE_SUCCESS);
}

/** GstXineInput subclasses ************************************************/

static GstStaticPadTemplate any_template = GST_STATIC_PAD_TEMPLATE (
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS_ANY
);

static GstStaticPadTemplate cdda_template = GST_STATIC_PAD_TEMPLATE (
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS (
    "audio/x-raw-int, "
      "endianness = (int) LITTLE_ENDIAN, "
      "signed = (boolean) true, "
      "width = (int) 16, "
      "depth = (int) 16, "
      "rate = (int) 44100, "
      "channels = (int) 2"
  )
);

static void
gst_xine_input_subclass_init (gpointer g_class, gpointer class_data)
{
  GstXineInputClass *xine_class = GST_XINE_INPUT_CLASS (g_class);
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstElementDetails details = GST_ELEMENT_DETAILS (
    NULL,
    "Source",
    NULL,
    "Benjamin Otte <otte@gnome.org>"
  );
  input_class_t *input;
  
  xine_class->plugin_node = class_data;
  input = (input_class_t *) xine_class->plugin_node->plugin_class;
  details.longname = g_strdup_printf ("%s xine input", input->get_identifier (input));
  details.description = g_strdup_printf ("%s", input->get_description (input));
  gst_element_class_set_details (element_class, &details);
  g_free (details.longname);
  g_free (details.description);
  
  /* FIXME: this is pretty hackish, anyone knows a better idea (xine doesn't) */
  if (strcmp (input->get_description (input), "CD") == 0) {
    gst_element_class_add_pad_template (element_class, 
        gst_static_pad_template_get (&cdda_template));
  } else {
    gst_element_class_add_pad_template (element_class, 
        gst_static_pad_template_get (&any_template));
  }
}

static void
gst_xine_input_sub_init (GTypeInstance *instance, gpointer g_class)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (instance);
  GstXineInput *xine = GST_XINE_INPUT (instance);

  xine->srcpad = gst_pad_new_from_template (
	gst_element_class_get_pad_template (klass, "src"), "src");
  gst_pad_set_get_function (xine->srcpad, gst_xine_input_get);
  gst_element_add_pad (GST_ELEMENT (xine), xine->srcpad);
}

gboolean
gst_xine_input_init_plugin (GstPlugin *plugin)
{
  GTypeInfo plugin_info =
  {
    sizeof (GstXineInputClass),
    NULL,
    NULL,
    gst_xine_input_subclass_init,
    NULL,
    NULL,
    sizeof (GstXineInput),
    0,
    gst_xine_input_sub_init,
  };
  plugin_node_t *node;
  GstXineClass *klass;
    
  klass = g_type_class_ref (GST_TYPE_XINE);
  
  node = xine_list_first_content (klass->xine->plugin_catalog->input);
  while (node) {
    gchar *plugin_name = g_strdup_printf ("xinesrc_%s", node->info->id);
    gchar *type_name = g_strdup_printf ("GstXineInput%s", node->info->id);
    GType type;
    plugin_info.class_data = node;
    type = g_type_register_static (GST_TYPE_XINE_INPUT, type_name, &plugin_info, 0);
    g_free (type_name);
    if (!gst_element_register (plugin, plugin_name, 
	GST_RANK_MARGINAL, type)) {
      g_free (plugin_name);
      return FALSE;
    }
    g_free (plugin_name);

    node = xine_list_next_content (klass->xine->plugin_catalog->input);
  }

  g_type_class_unref (klass); 
  return TRUE;
}

