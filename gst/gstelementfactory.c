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

//#define DEBUG_ENABLED
#include "gst_private.h"

#include "gstelement.h"

static void 		gst_elementfactory_class_init 		(GstElementFactoryClass *klass);
static void 		gst_elementfactory_init 		(GstElementFactory *factory);

#ifndef GST_DISABLE_REGISTRY
static void 		gst_elementfactory_restore_thyself 	(GstObject *object, xmlNodePtr parent);
static xmlNodePtr 	gst_elementfactory_save_thyself 	(GstObject *object, xmlNodePtr parent);
#endif

static void 		gst_elementfactory_unload_thyself 	(GstPluginFeature *feature);

/* global list of registered elementfactories */
static GList* _gst_elementfactories;

static GstPluginFeatureClass *parent_class = NULL;
//static guint gst_elementfactory_signals[LAST_SIGNAL] = { 0 };

GType 
gst_elementfactory_get_type (void) 
{
  static GType elementfactory_type = 0;

  if (!elementfactory_type) {
    static const GTypeInfo elementfactory_info = {
      sizeof (GstElementFactoryClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_elementfactory_class_init,
      NULL,
      NULL,
      sizeof(GstElementFactory),
      0,
      (GInstanceInitFunc) gst_elementfactory_init,
      NULL
    };
    elementfactory_type = g_type_register_static (GST_TYPE_PLUGIN_FEATURE, 
		    				  "GstElementFactory", &elementfactory_info, 0);
  }
  return elementfactory_type;
}

static void
gst_elementfactory_class_init (GstElementFactoryClass *klass)
{
  GObjectClass *gobject_class;
  GstObjectClass *gstobject_class;
  GstPluginFeatureClass *gstpluginfeature_class;

  gobject_class = (GObjectClass*)klass;
  gstobject_class = (GstObjectClass*)klass;
  gstpluginfeature_class = (GstPluginFeatureClass*) klass;

  parent_class = g_type_class_ref (GST_TYPE_PLUGIN_FEATURE);

#ifndef GST_DISABLE_REGISTRY
  gstobject_class->save_thyself = 	GST_DEBUG_FUNCPTR (gst_elementfactory_save_thyself);
  gstobject_class->restore_thyself = 	GST_DEBUG_FUNCPTR (gst_elementfactory_restore_thyself);
#endif

  gstpluginfeature_class->unload_thyself = 	GST_DEBUG_FUNCPTR (gst_elementfactory_unload_thyself);

  _gst_elementfactories = NULL;
}

static void
gst_elementfactory_init (GstElementFactory *factory)
{
  factory->padtemplates = NULL;
  factory->numpadtemplates = 0;

  _gst_elementfactories = g_list_prepend (_gst_elementfactories, factory);
}

/**
 * gst_elementfactory_find:
 * @name: name of factory to find
 *
 * Search for an elementfactory of the given name.
 *
 * Returns: #GstElementFactory if found, NULL otherwise
 */
GstElementFactory*
gst_elementfactory_find (const gchar *name)
{
  GList *walk;
  GstElementFactory *factory;

  g_return_val_if_fail(name != NULL, NULL);

  walk = _gst_elementfactories;
  while (walk) {
    factory = (GstElementFactory *)(walk->data);
    if (!strcmp(name, GST_OBJECT_NAME (factory)))
      return factory;
    walk = g_list_next(walk);
  }

  // this should be an ERROR
  GST_DEBUG (GST_CAT_ELEMENTFACTORY,"no such elementfactory \"%s\"\n", name);
  return NULL;
}

/**
 * gst_elementfactory_get_list:
 *
 * Get the global list of elementfactories.
 *
 * Returns: GList of type #GstElementFactory
 */
const GList*
gst_elementfactory_get_list (void)
{
  return _gst_elementfactories;
}

static void
gst_element_details_free (GstElementDetails *dp)
{
  g_return_if_fail (dp);

  if (dp->longname)
    g_free (dp->longname);
  if (dp->klass)
    g_free (dp->klass);
  if (dp->description)
    g_free (dp->description);
  if (dp->version)
    g_free (dp->version);
  if (dp->author)
    g_free (dp->author);
  if (dp->copyright)
    g_free (dp->copyright);
  g_free (dp);
}

/**
 * gst_elementfactory_new:
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
gst_elementfactory_new (const gchar *name, GType type,
                        GstElementDetails *details)
{
  GstElementFactory *factory;

  g_return_val_if_fail(name != NULL, NULL);
  g_return_val_if_fail (type, NULL);
  g_return_val_if_fail (details, NULL);

  factory = gst_elementfactory_find (name);

  if (!factory)
    factory = GST_ELEMENTFACTORY (g_object_new (GST_TYPE_ELEMENTFACTORY, NULL));

  if (factory->details_dynamic)
    {
      gst_element_details_free (factory->details);
      factory->details_dynamic = FALSE;
    }

  factory->details = details;

  if (!factory->type)
    factory->type = type;
  else if (factory->type != type)
/* FIXME: g_critical is glib-2.0, not glib-1.2
    g_critical ("`%s' requested type change (!)", name);
*/
    g_warning ("`%s' requested type change (!)", name);
  gst_object_set_name (GST_OBJECT (factory), name);

  return factory;
}

/**
 * gst_elementfactory_create:
 * @factory: factory to instantiate
 * @name: name of new element
 *
 * Create a new element of the type defined by the given elementfactory.
 * It wll be given the name supplied, since all elements require a name as
 * their first argument.
 *
 * Returns: new #GstElement
 */
GstElement *
gst_elementfactory_create (GstElementFactory *factory,
                           const gchar *name)
{
  GstElement *element;
  GstElementClass *oclass;

  g_return_val_if_fail(factory != NULL, NULL);
  g_return_val_if_fail(name != NULL, NULL);

  GST_DEBUG (GST_CAT_ELEMENTFACTORY,"creating element from factory \"%s\" with name \"%s\" and type %d\n", 
             GST_OBJECT_NAME (factory), name, factory->type);

  if (!gst_plugin_feature_ensure_loaded (GST_PLUGIN_FEATURE (factory)))
    return NULL;

  if (factory->type == 0) {
/* FIXME: g_critical is glib-2.0, not glib-1.2
      g_critical ("Factory for `%s' has no type",
*/
      g_warning ("Factory for `%s' has no type",
		  gst_object_get_name (GST_OBJECT (factory)));
      return NULL;
  }

  // create an instance of the element
  element = GST_ELEMENT(g_object_new(factory->type,NULL));
  g_assert(element != NULL);

  // attempt to set the elemenfactory class pointer if necessary
  oclass = GST_ELEMENT_CLASS(G_OBJECT_GET_CLASS(element));
  if (oclass->elementfactory == NULL) {
    GST_DEBUG (GST_CAT_ELEMENTFACTORY,"class %s\n", GST_OBJECT_NAME (factory));
    oclass->elementfactory = factory;
  }
  
  // copy pad template pointers to the element class
  oclass->padtemplates = g_list_copy(factory->padtemplates);
  oclass->numpadtemplates = factory->numpadtemplates;
  
  gst_object_set_name (GST_OBJECT (element),name);

  return element;
}

/**
 * gst_elementfactory_make:
 * @factoryname: a named factory to instantiate
 * @name: name of new element
 *
 * Create a new element of the type defined by the given elementfactory.
 * It wll be given the name supplied, since all elements require a name as
 * their first argument.
 *
 * Returns: new #GstElement
 */
GstElement*
gst_elementfactory_make (const gchar *factoryname, const gchar *name)
{
  GstElementFactory *factory;
  GstElement *element;

  g_return_val_if_fail (factoryname != NULL, NULL);
  g_return_val_if_fail (name != NULL, NULL);

  GST_DEBUG (GST_CAT_ELEMENTFACTORY, "gstelementfactory: make \"%s\" \"%s\"\n", factoryname, name);

  //gst_plugin_load_elementfactory(factoryname);
  factory = gst_elementfactory_find(factoryname);
  if (factory == NULL) {
    GST_INFO (GST_CAT_ELEMENTFACTORY,"no such elementfactory \"%s\"!",factoryname);
    return NULL;
  }
  element = gst_elementfactory_create(factory,name);
  if (element == NULL) {
    GST_INFO (GST_CAT_ELEMENTFACTORY,"couldn't create instance of elementfactory \"%s\"!",factoryname);
    return NULL;
  }

  return element;
}

/**
 * gst_elementfactory_add_padtemplate :
 * @elementfactory: factory to add the src id to
 * @templ: the padtemplate to add
 *
 * Add the given padtemplate to this elementfactory.
 */
void
gst_elementfactory_add_padtemplate (GstElementFactory *factory,
			            GstPadTemplate *templ)
{
  GList *padtemplates;
  
  g_return_if_fail(factory != NULL);
  g_return_if_fail(templ != NULL);

  padtemplates = factory->padtemplates;

  gst_object_ref (GST_OBJECT (templ));

  while (padtemplates) {
    GstPadTemplate *oldtempl = GST_PADTEMPLATE (padtemplates->data);
    
    if (!strcmp (oldtempl->name_template, templ->name_template)) {
      gst_object_unref (GST_OBJECT (oldtempl));
      padtemplates->data = templ;
      return;
    }
    
    padtemplates = g_list_next (padtemplates);
  }
  factory->padtemplates = g_list_append (factory->padtemplates, templ);
  factory->numpadtemplates++;
}

/**
 * gst_elementfactory_can_src_caps :
 * @factory: factory to query
 * @caps: the caps to check
 *
 * Checks if the factory can source the given capability.
 *
 * Returns: true if it can src the capabilities
 */
gboolean
gst_elementfactory_can_src_caps (GstElementFactory *factory,
		                 GstCaps *caps)
{
  GList *templates;

  g_return_val_if_fail(factory != NULL, FALSE);
  g_return_val_if_fail(caps != NULL, FALSE);

  templates = factory->padtemplates;

  while (templates) {
    GstPadTemplate *template = (GstPadTemplate *)templates->data;

    if (template->direction == GST_PAD_SRC) {
      if (gst_caps_check_compatibility (GST_PADTEMPLATE_CAPS (template), caps))
	return TRUE;
    }
    templates = g_list_next (templates);
  }

  return FALSE;
}

/**
 * gst_elementfactory_can_sink_caps :
 * @factory: factory to query
 * @caps: the caps to check
 *
 * Checks if the factory can sink the given capability.
 *
 * Returns: true if it can sink the capabilities
 */
gboolean
gst_elementfactory_can_sink_caps (GstElementFactory *factory,
		                  GstCaps *caps)
{
  GList *templates;

  g_return_val_if_fail(factory != NULL, FALSE);
  g_return_val_if_fail(caps != NULL, FALSE);

  templates = factory->padtemplates;

  while (templates) {
    GstPadTemplate *template = (GstPadTemplate *)templates->data;

    if (template->direction == GST_PAD_SINK) {
      if (gst_caps_check_compatibility (caps, GST_PADTEMPLATE_CAPS (template)))
	return TRUE;
    }
    templates = g_list_next (templates);
  }

  return FALSE;
}

static void
gst_elementfactory_unload_thyself (GstPluginFeature *feature)
{
  GstElementFactory *factory;

  factory = GST_ELEMENTFACTORY (feature);

  factory->type = 0;
}

#ifndef GST_DISABLE_REGISTRY
static xmlNodePtr
gst_elementfactory_save_thyself (GstObject *object,
		                 xmlNodePtr parent)
{
  GList *pads;
  GstElementFactory *factory;

  factory = GST_ELEMENTFACTORY (object);
  
  if (GST_OBJECT_CLASS (parent_class)->save_thyself) {
    GST_OBJECT_CLASS (parent_class)->save_thyself (object, parent);
  }

  g_return_val_if_fail(factory != NULL, NULL);

  if (factory->details)
    {
      xmlNewChild(parent,NULL,"longname", factory->details->longname);
      xmlNewChild(parent,NULL,"class", factory->details->klass);
      xmlNewChild(parent,NULL,"description", factory->details->description);
      xmlNewChild(parent,NULL,"version", factory->details->version);
      xmlNewChild(parent,NULL,"author", factory->details->author);
      xmlNewChild(parent,NULL,"copyright", factory->details->copyright);
    }
  else
    g_warning ("elementfactory `%s' is missing details",
	       object->name);

  pads = factory->padtemplates;
  if (pads) {
    while (pads) {
      xmlNodePtr subtree;
      GstPadTemplate *padtemplate = (GstPadTemplate *)pads->data;

      subtree = xmlNewChild(parent, NULL, "padtemplate", NULL);
      gst_padtemplate_save_thyself(padtemplate, subtree);

      pads = g_list_next (pads);
    }
  }
  return parent;
}

static void
gst_elementfactory_restore_thyself (GstObject *object, xmlNodePtr parent)
{
  GstElementFactory *factory = GST_ELEMENTFACTORY (object);
  xmlNodePtr children = parent->xmlChildrenNode;
  
  factory->details_dynamic = TRUE;
  factory->details = g_new0(GstElementDetails, 1);
  factory->padtemplates = NULL;

  if (GST_OBJECT_CLASS (parent_class)->restore_thyself) {
    GST_OBJECT_CLASS (parent_class)->restore_thyself (object, parent);
  }

  while (children) {
    if (!strcmp(children->name, "longname")) {
      factory->details->longname = xmlNodeGetContent(children);
    }
    if (!strcmp(children->name, "class")) {
      factory->details->klass = xmlNodeGetContent(children);
    }
    if (!strcmp(children->name, "description")) {
      factory->details->description = xmlNodeGetContent(children);
    }
    if (!strcmp(children->name, "version")) {
      factory->details->version = xmlNodeGetContent(children);
    }
    if (!strcmp(children->name, "author")) {
      factory->details->author = xmlNodeGetContent(children);
    }
    if (!strcmp(children->name, "copyright")) {
      factory->details->copyright = xmlNodeGetContent(children);
    }
    if (!strcmp(children->name, "padtemplate")) {
       GstPadTemplate *template;

       template = gst_padtemplate_load_thyself (children);

       gst_elementfactory_add_padtemplate (factory, template);
    }

    children = children->next;
  }
}
#endif /* GST_DISABLE_REGISTRY */
