/* GStreamer
 * Copyright (C) 2003 Julien Moutte <julien@moutte.net>
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

/* Object header */
#include "gstswitch.h"

enum {
  ARG_0,
  ARG_NB_SOURCES,
  ARG_ACTIVE_SOURCE
};

/* ElementFactory information */
static GstElementDetails gst_switch_details = GST_ELEMENT_DETAILS (
  "Switch",
  "Generic",
  "N-to-1 input switching",
  "Julien Moutte <julien@moutte.net>"
);

static GstStaticPadTemplate gst_switch_sink_factory =
GST_STATIC_PAD_TEMPLATE (
  "sink%d",
  GST_PAD_SINK,
  GST_PAD_REQUEST,
  GST_STATIC_CAPS_ANY
);

static GstElementClass *parent_class = NULL;

/* ============================================================= */
/*                                                               */
/*                       Private Methods                         */
/*                                                               */
/* ============================================================= */

static GstPad*
gst_switch_request_new_pad (GstElement *element,
                            GstPadTemplate *templ,
                            const gchar *unused) 
{
  char *name = NULL;
  GstPad *sinkpad = NULL;
  GstSwitch *gstswitch = NULL;
  GstSwitchPad *switchpad = NULL;
  
  g_return_val_if_fail (GST_IS_SWITCH (element), NULL);
  
  /* We only provide requested sink pads */
  if (templ->direction != GST_PAD_SINK) {
    g_warning ("gstswitch: requested a non sink pad\n");
    return NULL;
  }
  
  gstswitch = GST_SWITCH (element);
  
  name = g_strdup_printf ("sink%d", gstswitch->nb_sinkpads);
  
  sinkpad = gst_pad_new_from_template (templ, name);
  
  if (name)
    g_free (name);
  
  /* That pad will proxy caps and link */
  gst_pad_set_link_function (sinkpad,
                             GST_DEBUG_FUNCPTR (gst_pad_proxy_pad_link));
  gst_pad_set_getcaps_function (sinkpad,
                                GST_DEBUG_FUNCPTR (gst_pad_proxy_getcaps));
  
  gst_element_add_pad (GST_ELEMENT (gstswitch), sinkpad);
  
  switchpad = g_new0 (GstSwitchPad, 1);
  if (!switchpad)
    return NULL;
  
  switchpad->sinkpad = sinkpad;
  switchpad->data = NULL;
  switchpad->forwarded = FALSE;
  
  gstswitch->sinkpads = g_list_insert (gstswitch->sinkpads, switchpad,
                                       gstswitch->nb_sinkpads);
  gstswitch->nb_sinkpads++;
  
  if (GST_PAD_CAPS (gstswitch->srcpad)) {
    gst_pad_try_set_caps (sinkpad, GST_PAD_CAPS (gstswitch->srcpad));
  }
  
  return sinkpad;
}

static gboolean
gst_switch_poll_sinkpads (GstSwitch *gstswitch)
{
  GList *pads;
  
  g_return_val_if_fail (gstswitch != NULL, FALSE);
  g_return_val_if_fail (GST_IS_SWITCH (gstswitch), FALSE);
  
  pads = gstswitch->sinkpads;
  
  while (pads) {
    GstSwitchPad *switchpad = pads->data;
    GstData *data = gst_pad_pull (switchpad->sinkpad);
    if (GST_IS_EVENT (data) &&
        (GST_EVENT_TYPE (GST_EVENT (data)) == GST_EVENT_EOS)) {
      /* If that data was not forwarded we unref it */
      if (!switchpad->forwarded && switchpad->data) {
        gst_data_unref (switchpad->data);
        switchpad->data = NULL;
      }
      gst_event_unref (GST_EVENT (data));
    }
    else  {
      /* If that data was not forwarded we unref it */
      if (!switchpad->forwarded && switchpad->data) {
        gst_data_unref (switchpad->data);
        switchpad->data = NULL;
      }
      switchpad->data = data;
      switchpad->forwarded = FALSE;
    }
    pads = g_list_next (pads);
  }
    
  return TRUE;
}

static void 
gst_switch_loop (GstElement *element) 
{
  GstSwitch *gstswitch = NULL;
  GstSwitchPad *switchpad = NULL;
  
  g_return_if_fail (element != NULL);
  g_return_if_fail (GST_IS_SWITCH (element));
  
  gstswitch = GST_SWITCH (element);
  
  /* We poll all our sinkpads */
  gst_switch_poll_sinkpads (gstswitch);
  
  /* We get the active sinkpad */
  switchpad = g_list_nth_data (gstswitch->sinkpads, gstswitch->active_sinkpad);
  
  if (switchpad) {
    /* Pushing active sinkpad data to srcpad */
    gst_pad_push (gstswitch->srcpad, switchpad->data);
    /* Mark this data as forwarded so that it won't get unrefed on next poll */
    switchpad->forwarded = TRUE;
  }
}

/* =========================================== */
/*                                             */
/*                 Properties                  */
/*                                             */
/* =========================================== */

static void
gst_switch_set_property (GObject *object, guint prop_id,
                         const GValue *value, GParamSpec *pspec)
{
  GstSwitch *gstswitch = NULL;
  
  g_return_if_fail (GST_IS_SWITCH (object));
  
  gstswitch = GST_SWITCH (object);
  
  switch (prop_id) {
    case ARG_ACTIVE_SOURCE:
      gstswitch->active_sinkpad = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_switch_get_property (GObject *object, guint prop_id,
                         GValue *value, GParamSpec *pspec)
{
  GstSwitch *gstswitch = NULL;
  
  g_return_if_fail (GST_IS_SWITCH (object));
  
  gstswitch = GST_SWITCH (object);
  
  switch (prop_id) {
    case ARG_ACTIVE_SOURCE:
      g_value_set_int (value, gstswitch->active_sinkpad);
      break;
    case ARG_NB_SOURCES:
      g_value_set_int (value, gstswitch->nb_sinkpads);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* =========================================== */
/*                                             */
/*              Init & Class init              */
/*                                             */
/* =========================================== */

static void
gst_switch_dispose (GObject *object)
{
  GstSwitch *gstswitch = NULL;
  
  gstswitch = GST_SWITCH (object);

  if (gstswitch->sinkpads) {  
    g_list_free (gstswitch->sinkpads);
    gstswitch->sinkpads = NULL;
  }
  
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_switch_init (GstSwitch *gstswitch)
{
  gstswitch->srcpad = gst_pad_new ("src", GST_PAD_SRC);
  gst_element_add_pad (GST_ELEMENT (gstswitch), gstswitch->srcpad);
  gst_pad_set_link_function (gstswitch->srcpad,
                             GST_DEBUG_FUNCPTR (gst_pad_proxy_pad_link));
  gst_pad_set_getcaps_function (gstswitch->srcpad,
                                GST_DEBUG_FUNCPTR (gst_pad_proxy_getcaps));
  gst_element_set_loop_function (GST_ELEMENT (gstswitch), gst_switch_loop);
  
  gstswitch->sinkpads = NULL;
  gstswitch->active_sinkpad = 0;
  gstswitch->nb_sinkpads = 0;
}

static void
gst_switch_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  
  gst_element_class_set_details (element_class, &gst_switch_details);

  gst_element_class_add_pad_template (element_class, 
    gst_static_pad_template_get (&gst_switch_sink_factory));
}

static void
gst_switch_class_init (GstSwitchClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);
  
  g_object_class_install_property (gobject_class,
                                   ARG_NB_SOURCES,
                                   g_param_spec_int ("nb_sources",
                                                     "number of sources",
                                                     "number of sources",
                                                     G_MININT, G_MAXINT, 0,
                                                     G_PARAM_READABLE));
  g_object_class_install_property (gobject_class,
                                   ARG_ACTIVE_SOURCE,
                                   g_param_spec_int ("active_source",
                                                     "active source",
                                                     "active source",
                                                     G_MININT, G_MAXINT, 0,
                                                     G_PARAM_READWRITE));
                                                     
  gobject_class->dispose = gst_switch_dispose;
  gobject_class->set_property = gst_switch_set_property;
  gobject_class->get_property = gst_switch_get_property;
  
  gstelement_class->request_new_pad = gst_switch_request_new_pad;
}

/* ============================================================= */
/*                                                               */
/*                       Public Methods                          */
/*                                                               */
/* ============================================================= */

GType
gst_switch_get_type (void)
{
  static GType switch_type = 0;

  if (!switch_type) {
      static const GTypeInfo switch_info = {
        sizeof(GstSwitchClass),
        gst_switch_base_init,
        NULL,
        (GClassInitFunc) gst_switch_class_init,
        NULL,
        NULL,
        sizeof(GstSwitch),
        0,
        (GInstanceInitFunc) gst_switch_init,
      };
      
      switch_type = g_type_register_static (GST_TYPE_ELEMENT,
                                            "GstSwitch", &switch_info, 0);
  }
    
  return switch_type;
}

static gboolean
plugin_init (GstPlugin *plugin)
{
  return gst_element_register (plugin, "switch", GST_RANK_NONE,
                               GST_TYPE_SWITCH);
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "switch",
  "N-to-1 input switching",
  plugin_init,
  VERSION,
  GST_LICENSE,
  GST_PACKAGE,
  GST_ORIGIN
)
