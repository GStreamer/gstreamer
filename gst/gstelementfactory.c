/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstelementfactory.c: GstElementFactory object, support routines
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

/* #define DEBUG_ENABLED */
#include "gst_private.h"

#include "gstelement.h"
#include "gstregistrypool.h"
#include "gstlog.h"

static void 		gst_element_factory_class_init 		(GstElementFactoryClass *klass);
static void 		gst_element_factory_init 		(GstElementFactory *factory);

static void 		gst_element_factory_unload_thyself 	(GstPluginFeature *feature);

static GstPluginFeatureClass *parent_class = NULL;
/* static guint gst_element_factory_signals[LAST_SIGNAL] = { 0 }; */

GType 
gst_element_factory_get_type (void) 
{
  static GType elementfactory_type = 0;

  if (!elementfactory_type) {
    static const GTypeInfo elementfactory_info = {
      sizeof (GstElementFactoryClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_element_factory_class_init,
      NULL,
      NULL,
      sizeof(GstElementFactory),
      0,
      (GInstanceInitFunc) gst_element_factory_init,
      NULL
    };
    elementfactory_type = g_type_register_static (GST_TYPE_PLUGIN_FEATURE, 
		    				  "GstElementFactory", &elementfactory_info, 0);
  }
  return elementfactory_type;
}

static void
gst_element_factory_class_init (GstElementFactoryClass *klass)
{
  GObjectClass *gobject_class;
  GstObjectClass *gstobject_class;
  GstPluginFeatureClass *gstpluginfeature_class;

  gobject_class = (GObjectClass*)klass;
  gstobject_class = (GstObjectClass*)klass;
  gstpluginfeature_class = (GstPluginFeatureClass*) klass;

  parent_class = g_type_class_ref (GST_TYPE_PLUGIN_FEATURE);

  gstpluginfeature_class->unload_thyself = 	GST_DEBUG_FUNCPTR (gst_element_factory_unload_thyself);

}

static void
gst_element_factory_init (GstElementFactory *factory)
{
  factory->padtemplates = NULL;
  factory->numpadtemplates = 0;
}

/**
 * gst_element_factory_find:
 * @name: name of factory to find
 *
 * Search for an element factory of the given name.
 *
 * Returns: #GstElementFactory if found, NULL otherwise
 */
GstElementFactory*
gst_element_factory_find (const gchar *name)
{
  GstPluginFeature *feature;

  g_return_val_if_fail(name != NULL, NULL);

  feature = gst_registry_pool_find_feature (name, GST_TYPE_ELEMENT_FACTORY);
  if (feature)
    return GST_ELEMENT_FACTORY (feature);

  /* this should be an ERROR */
  GST_DEBUG (GST_CAT_ELEMENT_FACTORY,"no such elementfactory \"%s\"", name);
  return NULL;
}

static void
gst_element_details_free (GstElementDetails *dp)
{
  g_free (dp->longname);
  g_free (dp->klass);
  g_free (dp->license);
  g_free (dp->description);
  g_free (dp->version);
  g_free (dp->author);
  g_free (dp->copyright);
  g_free (dp);
}

static void
gst_element_factory_cleanup (GstElementFactory *factory)
{
  GList *padtemplates;

  if (factory->details_dynamic) {
    gst_element_details_free (factory->details);
    factory->details_dynamic = FALSE;
  }

  padtemplates = factory->padtemplates;

  while (padtemplates) {
    GstPadTemplate *oldtempl = GST_PAD_TEMPLATE (padtemplates->data);
     
    gst_object_unref (GST_OBJECT (oldtempl));

    padtemplates = g_list_next (padtemplates);
  }
  g_list_free (factory->padtemplates);

  factory->padtemplates = NULL;
  factory->numpadtemplates = 0;

  g_free (GST_PLUGIN_FEATURE (factory)->name);
}

/**
 * gst_element_factory_new:
 * @name: name of new elementfactory
 * @type: GType of new element
 * @details: #GstElementDetails structure with element details
 *
 * Create a new elementfactory capable of insantiating objects of the
 * given type.
 *
 * Returns: new elementfactory
 */
GstElementFactory*
gst_element_factory_new (const gchar *name, GType type,
                        GstElementDetails *details)
{
  GstElementFactory *factory;

  g_return_val_if_fail (name != NULL, NULL);
  g_return_val_if_fail (type, NULL);
  g_return_val_if_fail (details, NULL);

  factory = gst_element_factory_find (name);

  if (!factory)
    factory = GST_ELEMENT_FACTORY (g_object_new (GST_TYPE_ELEMENT_FACTORY, NULL));
  else {
    gst_element_factory_cleanup (factory);
  }

  factory->details = details;
  factory->details_dynamic = FALSE;

  if (!factory->type)
    factory->type = type;
  else if (factory->type != type)
    g_critical ("`%s' requested type change (!)", name);

  GST_PLUGIN_FEATURE (factory)->name = g_strdup (name);

  return factory;
}

/**
 * gst_element_factory_create:
 * @factory: factory to instantiate
 * @name: name of new element
 *
 * Create a new element of the type defined by the given elementfactory.
 * It will be given the name supplied, since all elements require a name as
 * their first argument.
 *
 * Returns: new #GstElement
 */
GstElement*
gst_element_factory_create (GstElementFactory *factory,
                           const gchar *name)
{
  GstElement *element;
  GstElementClass *oclass;

  g_return_val_if_fail (factory != NULL, NULL);

  if (!gst_plugin_feature_ensure_loaded (GST_PLUGIN_FEATURE (factory)))
    return NULL;

  GST_DEBUG (GST_CAT_ELEMENT_FACTORY,
             "creating element from factory \"%s\" (name \"%s\", type %d)", 
             GST_PLUGIN_FEATURE_NAME (factory), GST_STR_NULL (name), (gint) factory->type);

  if (factory->type == 0) {
      g_critical ("Factory for `%s' has no type",
		  GST_PLUGIN_FEATURE_NAME (factory));
      return NULL;
  }

  /* attempt to set the elementfactory class pointer if necessary */
  oclass = GST_ELEMENT_CLASS (g_type_class_ref (factory->type));
  if (oclass->elementfactory == NULL) {
    GST_DEBUG (GST_CAT_ELEMENT_FACTORY, "class %s", GST_PLUGIN_FEATURE_NAME (factory));
    oclass->elementfactory = factory;

    /* copy pad template pointers to the element class, 
     * allow for custom padtemplates */
    oclass->padtemplates = g_list_concat (oclass->padtemplates, 
		    g_list_copy (factory->padtemplates));
    oclass->numpadtemplates += factory->numpadtemplates;
  }

  /* create an instance of the element */
  element = GST_ELEMENT (g_object_new (factory->type, NULL));
  g_assert (element != NULL);

  g_type_class_unref (oclass);

  gst_object_set_name (GST_OBJECT (element), name);

  return element;
}

/**
 * gst_element_factory_make:
 * @factoryname: a named factory to instantiate
 * @name: name of new element
 *
 * Create a new element of the type defined by the given element factory.
 * If name is NULL, then the element will receive a guaranteed unique name,
 * consisting of the element factory name and a number.
 * If name is given, it will be given the name supplied.
 *
 * Returns: new #GstElement (or NULL if unable to create element)
 */
GstElement*
gst_element_factory_make (const gchar *factoryname, const gchar *name)
{
  GstElementFactory *factory;
  GstElement *element;

  g_return_val_if_fail (factoryname != NULL, NULL);

  GST_DEBUG (GST_CAT_ELEMENT_FACTORY, "gstelementfactory: make \"%s\" \"%s\"", 
             factoryname, GST_STR_NULL (name));

  /* gst_plugin_load_element_factory (factoryname); */
  factory = gst_element_factory_find (factoryname);
  if (factory == NULL) {
    GST_INFO (GST_CAT_ELEMENT_FACTORY,"no such element factory \"%s\"!",
	      factoryname);
    return NULL;
  }
  element = gst_element_factory_create (factory, name);
  if (element == NULL) {
    GST_INFO (GST_CAT_ELEMENT_FACTORY,
	      "couldn't create instance of element factory \"%s\"!",
	      factoryname);
    return NULL;
  }

  return element;
}

/**
 * gst_element_factory_make_or_warn:
 * @factoryname: a named factory to instantiate
 * @name: name of new element
 *
 * Create a new element of the type defined by the given element factory
 * using #gst_element_factory_make.
 * Will use g_warning if the element could not be created.
 *
 * Returns: new #GstElement (or NULL if unable to create element)
 */
GstElement*
gst_element_factory_make_or_warn (const gchar *factoryname, const gchar *name)
{
  GstElement *element;
  
  element = gst_element_factory_make (factoryname, name);

  if (element == NULL) 
    g_warning ("Could not create element from factory %s !\n", factoryname);

  return element;
}
    
/**
 * gst_element_factory_add_pad_template :
 * @elementfactory: factory to add the src id to
 * @templ: the padtemplate to add
 *
 * Add the given padtemplate to this elementfactory.
 */
void
gst_element_factory_add_pad_template (GstElementFactory *factory,
			              GstPadTemplate *templ)
{
  g_return_if_fail (factory != NULL);
  g_return_if_fail (templ != NULL);

  gst_object_ref (GST_OBJECT (templ));
  gst_object_sink (GST_OBJECT (templ));

  factory->padtemplates = g_list_append (factory->padtemplates, templ);
  factory->numpadtemplates++;
}

/**
 * gst_element_factory_can_src_caps :
 * @factory: factory to query
 * @caps: the caps to check
 *
 * Checks if the factory can source the given capability.
 *
 * Returns: true if it can src the capabilities
 */
gboolean
gst_element_factory_can_src_caps (GstElementFactory *factory,
		                 GstCaps *caps)
{
  GList *templates;

  g_return_val_if_fail(factory != NULL, FALSE);
  g_return_val_if_fail(caps != NULL, FALSE);

  templates = factory->padtemplates;

  while (templates) {
    GstPadTemplate *template = (GstPadTemplate *)templates->data;

    if (template->direction == GST_PAD_SRC) {
      if (gst_caps_is_always_compatible (GST_PAD_TEMPLATE_CAPS (template), caps))
	return TRUE;
    }
    templates = g_list_next (templates);
  }

  return FALSE;
}

/**
 * gst_element_factory_can_sink_caps :
 * @factory: factory to query
 * @caps: the caps to check
 *
 * Checks if the factory can sink the given capability.
 *
 * Returns: true if it can sink the capabilities
 */
gboolean
gst_element_factory_can_sink_caps (GstElementFactory *factory,
		                  GstCaps *caps)
{
  GList *templates;

  g_return_val_if_fail(factory != NULL, FALSE);
  g_return_val_if_fail(caps != NULL, FALSE);

  templates = factory->padtemplates;

  while (templates) {
    GstPadTemplate *template = (GstPadTemplate *)templates->data;

    if (template->direction == GST_PAD_SINK) {
      if (gst_caps_is_always_compatible (caps, GST_PAD_TEMPLATE_CAPS (template)))
	return TRUE;
    }
    templates = g_list_next (templates);
  }

  return FALSE;
}

/**
 * gst_element_factory_set_rank  :
 * @factory: factory to rank
 * @rank: rank value - higher number means more priority rank
 *
 * Specifies a rank for the element so that 
 * autoplugging uses the most appropriate elements.
 *
 */
void
gst_element_factory_set_rank (GstElementFactory *factory, guint16 rank)
{
  g_return_if_fail (factory != NULL);
  factory->rank = rank;
}

static void
gst_element_factory_unload_thyself (GstPluginFeature *feature)
{
  GstElementFactory *factory;

  factory = GST_ELEMENT_FACTORY (feature);

  factory->type = 0;
}
