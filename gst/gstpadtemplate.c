/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstpadtemplate.c: Templates for pad creation
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
 * SECTION:gstpadtemplate
 * @short_description: Describe the media type of a pad.
 * @see_also: #GstPad, #GstElementFactory
 *
 * Padtemplates describe the possible media types a pad or an elementfactory can
 * handle.
 *
 * Pad and PadTemplates have #GstCaps attached to it to describe the media type
 * they are capable of dealing with. gst_pad_template_get_caps() is used to get
 * the caps of a padtemplate. It's not possible to modify the caps of a
 * padtemplate after creation.
 *
 * Padtemplates can be created with gst_pad_template_new() or with the
 * convenient GST_PAD_TEMPLATE_FACTORY() macro. A padtemplate can be used to
 * create a pad or to add to an elementfactory.
 *
 * The following code example shows the code to create a pad from a padtemplate.
 * <example>
 * <title>Create a pad from a padtemplate</title>
 *   <programlisting>
 *   GstStaticPadTemplate my_template =
 *   GST_STATIC_PAD_TEMPLATE (
 *     "sink",		// the name of the pad
 *     GST_PAD_SINK,	// the direction of the pad
 *     GST_PAD_ALWAYS,	// when this pad will be present
 *     GST_STATIC_CAPS (	// the capabilities of the padtemplate
 *       "audio/x-raw-int, "
 *         "channels = (int) [ 1, 6 ]"
 *     )
 *   )
 *   void
 *   my_method (void)
 *   {
 *     GstPad *pad;
 *     pad = gst_pad_new_from_template (GST_PAD_TEMPLATE_GET (my_template_factory), "sink");
 *     ...
 *   }
 *   </programlisting>
 * </example>
 *
 * The following example shows you how to add the padtemplate to an
 * elementfactory:
 * <informalexample>
 *   <programlisting>
 *   gboolean
 *   my_factory_init (GstPlugin *plugin)
 *   {
 *     GstElementFactory *factory;
 *     factory = gst_element_factory_new ("my_factory", GST_TYPE_MYFACTORY, &amp;gst_myfactory_details);
 *     g_return_val_if_fail (factory != NULL, FALSE);
 *     gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (my_template_factory));
 *     gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));
 *     return TRUE;
 *   }
 *   </programlisting>
 * </informalexample>
 */

#include "gst_private.h"

#include "gstpad.h"
#include "gstpadtemplate.h"
#include "gstenumtypes.h"
#include "gstmarshal.h"
#include "gstutils.h"
#include "gstinfo.h"
#include "gsterror.h"
#include "gstvalue.h"

#define GST_CAT_DEFAULT GST_CAT_PADS

enum
{
  TEMPL_PAD_CREATED,
  /* FILL ME */
  LAST_SIGNAL
};

static GstObject *parent_class = NULL;
static guint gst_pad_template_signals[LAST_SIGNAL] = { 0 };

static void gst_pad_template_class_init (GstPadTemplateClass * klass);
static void gst_pad_template_init (GstPadTemplate * templ);
static void gst_pad_template_dispose (GObject * object);

GType
gst_pad_template_get_type (void)
{
  static GType padtemplate_type = 0;

  if (!padtemplate_type) {
    static const GTypeInfo padtemplate_info = {
      sizeof (GstPadTemplateClass), NULL, NULL,
      (GClassInitFunc) gst_pad_template_class_init, NULL, NULL,
      sizeof (GstPadTemplate),
      0,
      (GInstanceInitFunc) gst_pad_template_init, NULL
    };

    padtemplate_type =
        g_type_register_static (GST_TYPE_OBJECT, "GstPadTemplate",
        &padtemplate_info, 0);
  }
  return padtemplate_type;
}

static void
gst_pad_template_class_init (GstPadTemplateClass * klass)
{
  GObjectClass *gobject_class;
  GstObjectClass *gstobject_class;

  gobject_class = (GObjectClass *) klass;
  gstobject_class = (GstObjectClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_OBJECT);

  /**
   * GstPadTemplate::pad-created:
   * @pad_template: the object which received the signal.
   * @pad: the pad that was created.
   *
   * This signal is fired when an element creates a pad from this template.
   */
  gst_pad_template_signals[TEMPL_PAD_CREATED] =
      g_signal_new ("pad-created", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstPadTemplateClass, pad_created),
      NULL, NULL, gst_marshal_VOID__OBJECT, G_TYPE_NONE, 1, GST_TYPE_PAD);

  gobject_class->dispose = gst_pad_template_dispose;

  gstobject_class->path_string_separator = "*";
}

static void
gst_pad_template_init (GstPadTemplate * templ)
{
}

static void
gst_pad_template_dispose (GObject * object)
{
  GstPadTemplate *templ = GST_PAD_TEMPLATE (object);

  g_free (GST_PAD_TEMPLATE_NAME_TEMPLATE (templ));
  if (GST_PAD_TEMPLATE_CAPS (templ)) {
    gst_caps_unref (GST_PAD_TEMPLATE_CAPS (templ));
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

/* ALWAYS padtemplates cannot have conversion specifications (like src_%d),
 * since it doesn't make sense.
 * SOMETIMES padtemplates can do whatever they want, they are provided by the
 * element.
 * REQUEST padtemplates can be reverse-parsed (the user asks for 'sink1', the
 * 'sink%d' template is automatically selected), so we need to restrict their
 * naming.
 */
static gboolean
name_is_valid (const gchar * name, GstPadPresence presence)
{
  const gchar *str;

  if (presence == GST_PAD_ALWAYS) {
    if (strchr (name, '%')) {
      g_warning ("invalid name template %s: conversion specifications are not"
          " allowed for GST_PAD_ALWAYS padtemplates", name);
      return FALSE;
    }
  } else if (presence == GST_PAD_REQUEST) {
    if ((str = strchr (name, '%')) && strchr (str + 1, '%')) {
      g_warning ("invalid name template %s: only one conversion specification"
          " allowed in GST_PAD_REQUEST padtemplate", name);
      return FALSE;
    }
    if (str && (*(str + 1) != 's' && *(str + 1) != 'd')) {
      g_warning ("invalid name template %s: conversion specification must be of"
          " type '%%d' or '%%s' for GST_PAD_REQUEST padtemplate", name);
      return FALSE;
    }
    if (str && (*(str + 2) != '\0')) {
      g_warning ("invalid name template %s: conversion specification must"
          " appear at the end of the GST_PAD_REQUEST padtemplate name", name);
      return FALSE;
    }
  }

  return TRUE;
}

/**
 * gst_static_pad_template_get:
 * @pad_template: the static pad template
 *
 * Converts a #GstStaticPadTemplate into a #GstPadTemplate.
 *
 * Returns: a new #GstPadTemplate.
 */
GstPadTemplate *
gst_static_pad_template_get (GstStaticPadTemplate * pad_template)
{
  GstPadTemplate *new;

  if (!name_is_valid (pad_template->name_template, pad_template->presence))
    return NULL;

  new = g_object_new (gst_pad_template_get_type (),
      "name", pad_template->name_template, NULL);

  GST_PAD_TEMPLATE_NAME_TEMPLATE (new) = g_strdup (pad_template->name_template);
  GST_PAD_TEMPLATE_DIRECTION (new) = pad_template->direction;
  GST_PAD_TEMPLATE_PRESENCE (new) = pad_template->presence;

  GST_PAD_TEMPLATE_CAPS (new) =
      gst_caps_copy (gst_static_caps_get (&pad_template->static_caps));

  return new;
}

/**
 * gst_pad_template_new:
 * @name_template: the name template.
 * @direction: the #GstPadDirection of the template.
 * @presence: the #GstPadPresence of the pad.
 * @caps: a #GstCaps set for the template. The caps are taken ownership of.
 *
 * Creates a new pad template with a name according to the given template
 * and with the given arguments. This functions takes ownership of the provided
 * caps, so be sure to not use them afterwards.
 *
 * Returns: a new #GstPadTemplate.
 */
GstPadTemplate *
gst_pad_template_new (const gchar * name_template,
    GstPadDirection direction, GstPadPresence presence, GstCaps * caps)
{
  GstPadTemplate *new;

  g_return_val_if_fail (name_template != NULL, NULL);
  g_return_val_if_fail (caps != NULL, NULL);
  g_return_val_if_fail (direction == GST_PAD_SRC
      || direction == GST_PAD_SINK, NULL);
  g_return_val_if_fail (presence == GST_PAD_ALWAYS
      || presence == GST_PAD_SOMETIMES || presence == GST_PAD_REQUEST, NULL);

  if (!name_is_valid (name_template, presence)) {
    gst_caps_unref (caps);
    return NULL;
  }

  new = g_object_new (gst_pad_template_get_type (),
      "name", name_template, NULL);

  GST_PAD_TEMPLATE_NAME_TEMPLATE (new) = g_strdup (name_template);
  GST_PAD_TEMPLATE_DIRECTION (new) = direction;
  GST_PAD_TEMPLATE_PRESENCE (new) = presence;
  GST_PAD_TEMPLATE_CAPS (new) = caps;

  return new;
}

/**
 * gst_static_pad_template_get_caps:
 * @templ: a #GstStaticPadTemplate to get capabilities of.
 *
 * Gets the capabilities of the static pad template.
 *
 * Returns: the #GstCaps of the static pad template. If you need to keep a
 * reference to the caps, take a ref (see gst_caps_ref ()).
 */
GstCaps *
gst_static_pad_template_get_caps (GstStaticPadTemplate * templ)
{
  g_return_val_if_fail (templ, NULL);

  return (GstCaps *) gst_static_caps_get (&templ->static_caps);
}

/**
 * gst_pad_template_get_caps:
 * @templ: a #GstPadTemplate to get capabilities of.
 *
 * Gets the capabilities of the pad template.
 *
 * Returns: the #GstCaps of the pad template. If you need to keep a reference to
 * the caps, take a ref (see gst_caps_ref ()).
 */
GstCaps *
gst_pad_template_get_caps (GstPadTemplate * templ)
{
  g_return_val_if_fail (GST_IS_PAD_TEMPLATE (templ), NULL);

  return GST_PAD_TEMPLATE_CAPS (templ);
}

void
gst_pad_template_pad_created (GstPadTemplate * templ, GstPad * pad)
{
  gst_object_sink (GST_OBJECT (templ));
  g_signal_emit (G_OBJECT (templ),
      gst_pad_template_signals[TEMPL_PAD_CREATED], 0, pad);
}
