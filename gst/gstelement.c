/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstelement.c: The base element, all elements derive from this
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

//#define GST_DEBUG_ENABLED
#include <glib.h>
#include "gst_private.h"

#include "gstelement.h"
#include "gstextratypes.h"
#include "gstbin.h"
#include "gstscheduler.h"
#include "gstutils.h"

/* Element signals and args */
enum {
  STATE_CHANGE,
  NEW_PAD,
  PAD_REMOVED,
  ERROR,
  EOS,
  LAST_SIGNAL
};

enum {
  ARG_0,
  /* FILL ME */
};


static void			gst_element_class_init		(GstElementClass *klass);
static void			gst_element_init		(GstElement *element);
static void			gst_element_base_class_init	(GstElementClass *klass);

static void			gst_element_set_property	(GObject *object, guint prop_id, 
								 const GValue *value, GParamSpec *pspec);
static void			gst_element_get_property	(GObject *object, guint prop_id, GValue *value, 
								 GParamSpec *pspec);

static void 			gst_element_dispose 		(GObject *object);

static GstElementStateReturn	gst_element_change_state	(GstElement *element);

#ifndef GST_DISABLE_LOADSAVE
static xmlNodePtr		gst_element_save_thyself	(GstObject *object, xmlNodePtr parent);
GstElement* 			gst_element_restore_thyself 	(xmlNodePtr self, GstObject *parent);
#endif

GType _gst_element_type = 0;

static GstObjectClass *parent_class = NULL;
static guint gst_element_signals[LAST_SIGNAL] = { 0 };

GType gst_element_get_type (void) 
{
  if (!_gst_element_type) {
    static const GTypeInfo element_info = {
      sizeof(GstElementClass),
      (GBaseInitFunc)gst_element_base_class_init,
      NULL,
      (GClassInitFunc)gst_element_class_init,
      NULL,
      NULL,
      sizeof(GstElement),
      0,
      (GInstanceInitFunc)gst_element_init,
      NULL
    };
    _gst_element_type = g_type_register_static(GST_TYPE_OBJECT, "GstElement", &element_info, G_TYPE_FLAG_ABSTRACT);
  }
  return _gst_element_type;
}

static void
gst_element_class_init (GstElementClass *klass)
{
  GObjectClass *gobject_class;
  GstObjectClass *gstobject_class;

  gobject_class = (GObjectClass*) klass;
  gstobject_class = (GstObjectClass*) klass;

  parent_class = g_type_class_ref(GST_TYPE_OBJECT);

  gst_element_signals[STATE_CHANGE] =
    g_signal_new ("state_change", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (GstElementClass, state_change), NULL, NULL,
		  gst_marshal_VOID__INT_INT, G_TYPE_NONE, 2,
                  G_TYPE_INT, G_TYPE_INT);
  gst_element_signals[NEW_PAD] =
    g_signal_new ("new_pad", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (GstElementClass, new_pad), NULL, NULL,
                  gst_marshal_VOID__OBJECT, G_TYPE_NONE, 1,
                  GST_TYPE_PAD);
  gst_element_signals[PAD_REMOVED] =
    g_signal_new ("pad_removed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (GstElementClass, pad_removed), NULL, NULL,
                  gst_marshal_VOID__OBJECT, G_TYPE_NONE, 1,
                   GST_TYPE_PAD);
  gst_element_signals[ERROR] =
    g_signal_new ("error", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (GstElementClass, error), NULL, NULL,
                  gst_marshal_VOID__STRING, G_TYPE_NONE,1,
                   G_TYPE_STRING);
  gst_element_signals[EOS] =
    g_signal_new ("eos", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (GstElementClass,eos), NULL, NULL,
                  gst_marshal_VOID__VOID, G_TYPE_NONE, 0);



  gobject_class->set_property 		= GST_DEBUG_FUNCPTR (gst_element_set_property);
  gobject_class->get_property 		= GST_DEBUG_FUNCPTR (gst_element_get_property);
  gobject_class->dispose 		= GST_DEBUG_FUNCPTR (gst_element_dispose);

#ifndef GST_DISABLE_LOADSAVE
  gstobject_class->save_thyself 	= GST_DEBUG_FUNCPTR (gst_element_save_thyself);
  gstobject_class->restore_thyself 	= GST_DEBUG_FUNCPTR (gst_element_restore_thyself);
#endif

  klass->change_state 			= GST_DEBUG_FUNCPTR (gst_element_change_state);
  klass->elementfactory 	= NULL;
  klass->padtemplates 		= NULL;
  klass->numpadtemplates 	= 0;
}

static void
gst_element_base_class_init (GstElementClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass*) klass;

  gobject_class->set_property =		GST_DEBUG_FUNCPTR(gst_element_set_property);
  gobject_class->get_property =		GST_DEBUG_FUNCPTR(gst_element_get_property);
}

static void
gst_element_init (GstElement *element)
{
  element->current_state = GST_STATE_NULL;
  element->pending_state = -1;
  element->numpads = 0;
  element->numsrcpads = 0;
  element->numsinkpads = 0;
  element->pads = NULL;
  element->loopfunc = NULL;
  element->threadstate = NULL;
  element->sched = NULL;
}


static void
gst_element_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstElementClass *oclass = (GstElementClass *)G_OBJECT_GET_CLASS(object);

  if (oclass->set_property)
    (oclass->set_property)(object,prop_id,value,pspec);
}


static void
gst_element_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstElementClass *oclass = (GstElementClass *)G_OBJECT_GET_CLASS(object);

  if (oclass->get_property)
    (oclass->get_property)(object,prop_id,value,pspec);
}


/**
 * gst_element_set_name:
 * @element: GstElement to set name of
 * @name: new name of element
 *
 * Set the name of the element, getting rid of the old name if there was
 * one.
 */
void
gst_element_set_name (GstElement *element, const gchar *name)
{
  g_return_if_fail (element != NULL);
  g_return_if_fail (GST_IS_ELEMENT (element));
  g_return_if_fail (name != NULL);

  gst_object_set_name (GST_OBJECT (element), name);
}

/**
 * gst_element_get_name:
 * @element: GstElement to get name of
 *
 * Get the name of the element.
 *
 * Returns: name of the element
 */
const gchar*
gst_element_get_name (GstElement *element)
{
  g_return_val_if_fail (element != NULL, NULL);
  g_return_val_if_fail (GST_IS_ELEMENT (element), NULL);

  return GST_OBJECT_NAME (element);
}

/**
 * gst_element_set_parent:
 * @element: GstElement to set parent of
 * @parent: new parent of the object
 *
 * Set the parent of the element.
 */
void
gst_element_set_parent (GstElement *element, GstObject *parent)
{
  g_return_if_fail (element != NULL);
  g_return_if_fail (GST_IS_ELEMENT (element));
  g_return_if_fail (GST_OBJECT_PARENT (element) == NULL);
  g_return_if_fail (parent != NULL);
  g_return_if_fail (GST_IS_OBJECT (parent));
  g_return_if_fail ((gpointer)element != (gpointer)parent);

  gst_object_set_parent (GST_OBJECT (element), parent);
}

/**
 * gst_element_get_parent:
 * @element: GstElement to get the parent of
 *
 * Get the parent of the element.
 *
 * Returns: parent of the element
 */
GstObject*
gst_element_get_parent (GstElement *element)
{
  g_return_val_if_fail (element != NULL, NULL);
  g_return_val_if_fail (GST_IS_ELEMENT (element), NULL);

  return GST_OBJECT_PARENT (element);
}

/**
 * gst_element_add_pad:
 * @element: element to add pad to
 * @pad: pad to add
 *
 * Add a pad (connection point) to the element, setting the parent of the
 * pad to the element (and thus adding a reference).
 */
void
gst_element_add_pad (GstElement *element, GstPad *pad)
{
  g_return_if_fail (element != NULL);
  g_return_if_fail (GST_IS_ELEMENT (element));
  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));

  // first check to make sure the pad's parent is already set
  g_return_if_fail (GST_PAD_PARENT (pad) == NULL);

  // then check to see if there's already a pad by that name here
  g_return_if_fail (gst_object_check_uniqueness (element->pads, GST_PAD_NAME(pad)) == TRUE);

  /* set the pad's parent */
  GST_DEBUG (GST_CAT_ELEMENT_PADS,"setting parent of pad '%s' to '%s'\n",
        GST_PAD_NAME (pad), GST_ELEMENT_NAME (element));
  gst_object_set_parent (GST_OBJECT (pad), GST_OBJECT (element));

  /* add it to the list */
  element->pads = g_list_append (element->pads, pad);
  element->numpads++;
  if (gst_pad_get_direction (pad) == GST_PAD_SRC)
    element->numsrcpads++;
  else
    element->numsinkpads++;

  /* emit the NEW_PAD signal */
  g_signal_emit (G_OBJECT (element), gst_element_signals[NEW_PAD], 0, pad);
}

/**
 * gst_element_remove_pad:
 * @element: element to remove pad from
 * @pad: pad to remove
 *
 * Remove a pad (connection point) from the element, 
 */
void
gst_element_remove_pad (GstElement *element, GstPad *pad)
{
  g_return_if_fail (element != NULL);
  g_return_if_fail (GST_IS_ELEMENT (element));
  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));

  g_return_if_fail (GST_PAD_PARENT (pad) == element);

  /* add it to the list */
  element->pads = g_list_remove (element->pads, pad);
  element->numpads--;
  if (gst_pad_get_direction (pad) == GST_PAD_SRC)
    element->numsrcpads--;
  else
    element->numsinkpads--;

  g_signal_emit (G_OBJECT (element), gst_element_signals[PAD_REMOVED], 0, pad);

  gst_object_unparent (GST_OBJECT (pad));
}

/**
 * gst_element_add_ghost_pad:
 * @element: element to add ghost pad to
 * @pad: pad from which the new ghost pad will be created
 * @name: name of the new ghost pad
 *
 * Create a ghost pad from the given pad, and add it to the list of pads
 * for this element.
 */
void
gst_element_add_ghost_pad (GstElement *element, GstPad *pad, gchar *name)
{
  GstPad *ghostpad;

  g_return_if_fail (element != NULL);
  g_return_if_fail (GST_IS_ELEMENT (element));
  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));

  // then check to see if there's already a pad by that name here
  g_return_if_fail (gst_object_check_uniqueness (element->pads, name) == TRUE);

  GST_DEBUG(GST_CAT_ELEMENT_PADS,"creating new ghost pad called %s, from pad %s:%s\n",
            name,GST_DEBUG_PAD_NAME(pad));
  ghostpad = gst_ghost_pad_new (name, pad);

  /* add it to the list */
  GST_DEBUG(GST_CAT_ELEMENT_PADS,"adding ghost pad %s to element %s\n",
            name, GST_ELEMENT_NAME (element));
  element->pads = g_list_append (element->pads, ghostpad);
  element->numpads++;
  // set the parent of the ghostpad
  gst_object_set_parent (GST_OBJECT (ghostpad), GST_OBJECT (element));

  GST_DEBUG(GST_CAT_ELEMENT_PADS,"added ghostpad %s:%s\n",GST_DEBUG_PAD_NAME(ghostpad));

  /* emit the NEW_GHOST_PAD signal */
  g_signal_emit (G_OBJECT (element), gst_element_signals[NEW_PAD], 0, ghostpad);
}

/**
 * gst_element_remove_ghost_pad:
 * @element: element to remove the ghost pad from
 * @pad: ghost pad to remove
 *
 * removes a ghost pad from an element
 *
 */
void
gst_element_remove_ghost_pad (GstElement *element, GstPad *pad)
{
  g_return_if_fail (element != NULL);
  g_return_if_fail (GST_IS_ELEMENT (element));
  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_GHOST_PAD (pad));

  // FIXME this is redundant?
  // wingo 10-july-2001: I don't think so, you have to actually remove the pad
  // from the element. gst_pad_remove_ghost_pad just removes the ghostpad from
  // the real pad's ghost pad list
  gst_pad_remove_ghost_pad (GST_PAD (GST_PAD_REALIZE (pad)), pad);
  gst_element_remove_pad (element, pad);
}


/**
 * gst_element_get_pad:
 * @element: element to find pad of
 * @name: name of pad to retrieve
 *
 * Retrieve a pad from the element by name.
 *
 * Returns: requested pad if found, otherwise NULL.
 */
GstPad*
gst_element_get_pad (GstElement *element, const gchar *name)
{
  GList *walk;

  g_return_val_if_fail (element != NULL, NULL);
  g_return_val_if_fail (GST_IS_ELEMENT (element), NULL);
  g_return_val_if_fail (name != NULL, NULL);

  // if there aren't any pads, well, we're not likely to find one
  if (!element->numpads)
    return NULL;

  // look through the list, matching by name
  walk = element->pads;
  while (walk) {
    GstPad *pad = GST_PAD(walk->data);
    if (!strcmp (GST_PAD_NAME(pad), name)) {
      GST_INFO(GST_CAT_ELEMENT_PADS,"found pad %s:%s",GST_DEBUG_PAD_NAME(pad));
      return pad;
    }
    walk = g_list_next (walk);
  }

  GST_INFO(GST_CAT_ELEMENT_PADS,"no such pad '%s' in element \"%s\"",name,GST_ELEMENT_NAME(element));
  return NULL;
}

/**
 * gst_element_get_pad_list:
 * @element: element to get pads of
 *
 * Retrieve a list of the pads associated with the element.
 *
 * Returns: GList of pads
 */
GList*
gst_element_get_pad_list (GstElement *element)
{
  g_return_val_if_fail (element != NULL, NULL);
  g_return_val_if_fail (GST_IS_ELEMENT (element), NULL);

  /* return the list of pads */
  return element->pads;
}

/**
 * gst_element_class_add_padtemplate:
 * @klass: element class to add padtemplate to
 * @templ: padtemplate to add
 *
 * Add a padtemplate to an element class. This is useful if you have derived a custom
 * bin and wish to provide an on-request pad at runtime. Plugin writers should use
 * gst_elementfactory_add_padtemplate instead.
 */
void
gst_element_class_add_padtemplate (GstElementClass *klass, GstPadTemplate *templ)
{
  g_return_if_fail (klass != NULL);
  g_return_if_fail (GST_IS_ELEMENT_CLASS (klass));
  g_return_if_fail (templ != NULL);
  g_return_if_fail (GST_IS_PADTEMPLATE (templ));
  
  klass->padtemplates = g_list_append (klass->padtemplates, templ);
  klass->numpadtemplates++;
}

/**
 * gst_element_get_padtemplate_list:
 * @element: element to get padtemplates of
 *
 * Retrieve a list of the padtemplates associated with the element.
 *
 * Returns: GList of padtemplates
 */
GList*
gst_element_get_padtemplate_list (GstElement *element)
{
  GstElementClass *oclass;

  g_return_val_if_fail (element != NULL, NULL);
  g_return_val_if_fail (GST_IS_ELEMENT (element), NULL);

  oclass = GST_ELEMENT_CLASS (G_OBJECT_GET_CLASS(element));

  return oclass->padtemplates;
}

/**
 * gst_element_get_padtemplate_by_name:
 * @element: element to get padtemplate of
 * @name: the name of the padtemplate to get.
 *
 * Retrieve a padtemplate from this element with the
 * given name.
 *
 * Returns: the padtemplate with the given name
 */
GstPadTemplate*
gst_element_get_padtemplate_by_name (GstElement *element, const guchar *name)
{
  GList *padlist;

  g_return_val_if_fail (element != NULL, NULL);
  g_return_val_if_fail (GST_IS_ELEMENT (element), NULL);
  g_return_val_if_fail (name != NULL, NULL);

  padlist = gst_element_get_padtemplate_list (element);

  while (padlist) {
    GstPadTemplate *padtempl = (GstPadTemplate*) padlist->data;

    if (!strcmp (padtempl->name_template, name))
      return padtempl;

    padlist = g_list_next (padlist);
  }

  return NULL;
}

/**
 * gst_element_get_padtemplate_by_compatible:
 * @element: element to get padtemplate of
 * @templ: a template to find a compatible template for
 *
 * Generate a padtemplate for this element compatible with the given
 * template, ie able to link to it.
 *
 * Returns: the padtemplate
 */
static GstPadTemplate*
gst_element_get_padtemplate_by_compatible (GstElement *element, GstPadTemplate *compattempl)
{
  GstPadTemplate *newtempl = NULL;
  GList *padlist;

  GST_DEBUG(GST_CAT_ELEMENT_PADS,"gst_element_get_padtemplate_by_compatible()\n");

  g_return_val_if_fail (element != NULL, NULL);
  g_return_val_if_fail (GST_IS_ELEMENT (element), NULL);
  g_return_val_if_fail (compattempl != NULL, NULL);

  padlist = gst_element_get_padtemplate_list (element);

  while (padlist) {
    GstPadTemplate *padtempl = (GstPadTemplate*) padlist->data;
    gboolean compat = FALSE;

    // Ignore name
    // Ignore presence
    // Check direction (must be opposite)
    // Check caps

    GST_DEBUG(GST_CAT_CAPS,"checking direction and caps\n");
    if (padtempl->direction == GST_PAD_SRC &&
      compattempl->direction == GST_PAD_SINK) {
      GST_DEBUG(GST_CAT_CAPS,"compatible direction: found src pad template\n");
      compat = gst_caps_check_compatibility(GST_PADTEMPLATE_CAPS (padtempl),
					    GST_PADTEMPLATE_CAPS (compattempl));
      GST_DEBUG(GST_CAT_CAPS,"caps are %scompatible\n", (compat?"":"not "));
    } else if (padtempl->direction == GST_PAD_SINK &&
	       compattempl->direction == GST_PAD_SRC) {
      GST_DEBUG(GST_CAT_CAPS,"compatible direction: found sink pad template\n");
      compat = gst_caps_check_compatibility(GST_PADTEMPLATE_CAPS (compattempl),
					    GST_PADTEMPLATE_CAPS (padtempl));
      GST_DEBUG(GST_CAT_CAPS,"caps are %scompatible\n", (compat?"":"not "));
    }

    if (compat) {
      newtempl = padtempl;
      break;
    }

    padlist = g_list_next (padlist);
  }

  return newtempl;
}

static GstPad*
gst_element_request_pad (GstElement *element, GstPadTemplate *templ, const gchar* name)
{
  GstPad *newpad = NULL;
  GstElementClass *oclass;

  oclass = GST_ELEMENT_CLASS (G_OBJECT_GET_CLASS(element));
  if (oclass->request_new_pad)
    newpad = (oclass->request_new_pad)(element, templ, name);

  return newpad;
}

/**
 * gst_element_request_compatible_pad:
 * @element: element to request a new pad from
 * @templ: a pad template to which the new pad should be able to connect
 *
 * Request a new pad from the element. The template will
 * be used to decide what type of pad to create. This function
 * is typically used for elements with a padtemplate with presence
 * GST_PAD_REQUEST.
 *
 * Returns: the new pad that was created.
 */
GstPad*
gst_element_request_compatible_pad (GstElement *element, GstPadTemplate *templ)
{
  GstPadTemplate *templ_new;
  GstPad *pad = NULL;

  g_return_val_if_fail (element != NULL, NULL);
  g_return_val_if_fail (GST_IS_ELEMENT (element), NULL);
  g_return_val_if_fail (templ != NULL, NULL);

  templ_new = gst_element_get_padtemplate_by_compatible (element, templ);
  if (templ_new != NULL)
      pad = gst_element_request_pad (element, templ_new, NULL);

  return pad;
}

/**
 * gst_element_request_pad_by_name:
 * @element: element to request a new pad from
 * @name: the name of the padtemplate to use.
 *
 * Request a new pad from the element. The name argument will
 * be used to decide what padtemplate to use. This function
 * is typically used for elements with a padtemplate with presence
 * GST_PAD_REQUEST.
 *
 * Returns: the new pad that was created.
 */
GstPad*
gst_element_request_pad_by_name (GstElement *element, const gchar *name)
{
  GstPadTemplate *templ = NULL;
  GstPad *pad;
  const gchar *req_name = NULL;
  gboolean templ_found = FALSE;
  GList *list;
  gint n;

  g_return_val_if_fail (element != NULL, NULL);
  g_return_val_if_fail (GST_IS_ELEMENT (element), NULL);
  g_return_val_if_fail (name != NULL, NULL);

  if (strstr (name, "%d")) {
      templ = gst_element_get_padtemplate_by_name (element, name);
      req_name = NULL;
  } else {
      list = gst_element_get_padtemplate_list(element);
      while (!templ_found && list) {
          templ = (GstPadTemplate*) list->data;
          if (strstr (templ->name_template, "%d")) {
              if (sscanf(name, templ->name_template, &n)) {
                  templ_found = TRUE;
                  req_name = name;
                  break;
              }
          }
          list = list->next;
      }
  }
  
  if (templ == NULL)
      return NULL;
  
  pad = gst_element_request_pad (element, templ, req_name);
  
  return pad;
}

/**
 * gst_element_connect:
 * @src: element containing source pad
 * @srcpadname: name of pad in source element
 * @dest: element containing destination pad
 * @destpadname: name of pad in destination element
 *
 * Connect the two named pads of the source and destination elements.
 * Side effect is that if one of the pads has no parent, it becomes a
 * child of the parent of the other element.  If they have different
 * parents, the connection fails.
 */
void
gst_element_connect (GstElement *src, const gchar *srcpadname,
                     GstElement *dest, const gchar *destpadname)
{
  GstPad *srcpad,*destpad;

  g_return_if_fail (src != NULL);
  g_return_if_fail (GST_IS_ELEMENT(src));
  g_return_if_fail (srcpadname != NULL);
  g_return_if_fail (dest != NULL);
  g_return_if_fail (GST_IS_ELEMENT(dest));
  g_return_if_fail (destpadname != NULL);

  /* obtain the pads requested */
  srcpad = gst_element_get_pad (src, srcpadname);
  if (srcpad == NULL) {
    GST_ERROR(src,"source element has no pad \"%s\"",srcpadname);
    return;
  }
  destpad = gst_element_get_pad (dest, destpadname);
  if (srcpad == NULL) {
    GST_ERROR(dest,"destination element has no pad \"%s\"",destpadname);
    return;
  }

  /* we're satisified they can be connected, let's do it */
  gst_pad_connect(srcpad,destpad);
}

/**
 * gst_element_disconnect:
 * @src: element containing source pad
 * @srcpadname: name of pad in source element
 * @dest: element containing destination pad
 * @destpadname: name of pad in destination element
 *
 * Disconnect the two named pads of the source and destination elements.
 */
void
gst_element_disconnect (GstElement *src, const gchar *srcpadname,
                        GstElement *dest, const gchar *destpadname)
{
  GstPad *srcpad,*destpad;

  g_return_if_fail (src != NULL);
  g_return_if_fail (GST_IS_ELEMENT(src));
  g_return_if_fail (srcpadname != NULL);
  g_return_if_fail (dest != NULL);
  g_return_if_fail (GST_IS_ELEMENT(dest));
  g_return_if_fail (destpadname != NULL);

  /* obtain the pads requested */
  srcpad = gst_element_get_pad (src, srcpadname);
  if (srcpad == NULL) {
    GST_ERROR(src,"source element has no pad \"%s\"",srcpadname);
    return;
  }
  destpad = gst_element_get_pad (dest, destpadname);
  if (srcpad == NULL) {
    GST_ERROR(dest,"destination element has no pad \"%s\"",destpadname);
    return;
  }

  /* we're satisified they can be disconnected, let's do it */
  gst_pad_disconnect(srcpad,destpad);
}

/**
 * gst_element_error:
 * @element: Element with the error
 * @error: String describing the error
 *
 * This function is used internally by elements to signal an error
 * condition.  It results in the "error" signal.
 */
void
gst_element_error (GstElement *element, const gchar *error)
{
  g_error("GstElement: error in element '%s': %s\n", GST_ELEMENT_NAME(element), error);

  /* FIXME: this is not finished!!! */

  g_signal_emit (G_OBJECT (element), gst_element_signals[ERROR], 0, error);
}


/**
 * gst_element_get_state:
 * @element: element to get state of
 *
 * Gets the state of the element. 
 *
 * Returns: The element state
 */
GstElementState
gst_element_get_state (GstElement *element)
{
  g_return_val_if_fail (GST_IS_ELEMENT (element), GST_STATE_VOID_PENDING);

  return GST_STATE (element);
}

static void
gst_element_wait_done (GstElement *element, GstElementState old, GstElementState new, GCond *cond)
{
  g_signal_handlers_disconnect_by_func (G_OBJECT (element), gst_element_wait_done, cond);
  g_cond_signal (cond);
}

/**
 * gst_element_wait_state_change:
 * @element: element wait for
 *
 * Wait and block until the element changed its state.
 */
void
gst_element_wait_state_change (GstElement *element)
{
  GCond *cond = g_cond_new ();
  GMutex *mutex = g_mutex_new ();

  g_mutex_lock (mutex);
  g_signal_connect (G_OBJECT (element), "state_change", gst_element_wait_done, cond);
  g_cond_wait (cond, mutex);
  g_mutex_unlock (mutex);

  g_mutex_free (mutex);
  g_cond_free (cond);
}
/**
 * gst_element_set_state:
 * @element: element to change state of
 * @state: new element state
 *
 * Sets the state of the element. This function will only set
 * the elements pending state.
 *
 * Returns: whether or not the state was successfully set.
 */
gint
gst_element_set_state (GstElement *element, GstElementState state)
{
  GstElementClass *oclass;
  GstElementState curpending;
  GstElementStateReturn return_val = GST_STATE_SUCCESS;

//  g_print("gst_element_set_state(\"%s\",%08lx)\n",
//          element->name,state);

  g_return_val_if_fail (element != NULL, GST_STATE_FAILURE);
  g_return_val_if_fail (GST_IS_ELEMENT (element), GST_STATE_FAILURE);
  g_return_val_if_fail (element->sched != NULL, GST_STATE_FAILURE);

  GST_DEBUG_ELEMENT (GST_CAT_STATES,element, "setting state from %s to %s\n",
                     gst_element_statename(GST_STATE(element)),
                     gst_element_statename(state));

  /* start with the current state */
  curpending = GST_STATE(element);

  /* loop until the final requested state is set */
  while (GST_STATE(element) != state && GST_STATE (element) != GST_STATE_VOID_PENDING) {
    /* move the curpending state in the correct direction */
    if (curpending < state) curpending<<=1;
    else curpending>>=1;

    /* set the pending state variable */
    // FIXME: should probably check to see that we don't already have one
    GST_STATE_PENDING (element) = curpending;
    if (curpending != state)
      GST_DEBUG_ELEMENT (GST_CAT_STATES,element,"intermediate: setting state to %s\n",
                         gst_element_statename(curpending));

    /* call the state change function so it can set the state */
    oclass = GST_ELEMENT_CLASS (G_OBJECT_GET_CLASS(element));
    if (oclass->change_state)
      return_val = (oclass->change_state)(element);

    switch (return_val) {
      case GST_STATE_FAILURE:
        GST_DEBUG_ELEMENT (GST_CAT_STATES,element,"have failed change_state return\n");
	return return_val;
      case GST_STATE_ASYNC:
        GST_DEBUG_ELEMENT (GST_CAT_STATES,element,"element will change state async\n");
	return return_val;
      default:
        /* Last thing we do is verify that a successful state change really
         * did change the state... */
        if (GST_STATE(element) != curpending) {
          GST_DEBUG_ELEMENT (GST_CAT_STATES, element, "element claimed state-change success, but state didn't change\n");
          return GST_STATE_FAILURE;
	}
        break;
    }
  }

  return return_val;
}

static GstElementStateReturn
gst_element_change_state (GstElement *element)
{
  GstElementState old_state;

  g_return_val_if_fail (element != NULL, GST_STATE_FAILURE);
  g_return_val_if_fail (GST_IS_ELEMENT (element), GST_STATE_FAILURE);

  old_state = GST_STATE (element);

  if (GST_STATE_PENDING (element) == GST_STATE_VOID_PENDING || old_state == GST_STATE_PENDING (element)) {
    g_warning ("no state change needed for element %s (VOID_PENDING)\n", GST_ELEMENT_NAME (element));
    return GST_STATE_SUCCESS;
  }
  
  GST_INFO (GST_CAT_STATES, "%s default handler sets state from %s to %s %d", GST_ELEMENT_NAME (element),
                     gst_element_statename (old_state),
                     gst_element_statename (GST_STATE_PENDING (element)),
		     GST_STATE_TRANSITION (element));

  if (GST_STATE_TRANSITION (element) == GST_STATE_PAUSED_TO_PLAYING) {
    g_return_val_if_fail (GST_ELEMENT_SCHED (element), GST_STATE_FAILURE);
    
    if (GST_ELEMENT_PARENT (element)) {
      GST_DEBUG (GST_CAT_STATES, "PAUSED->PLAYING: element \"%s\" has parent \"%s\" and sched %p\n",
                 GST_ELEMENT_NAME (element), GST_ELEMENT_NAME (GST_ELEMENT_PARENT (element)), 
		 GST_ELEMENT_SCHED (element));
    }
    gst_scheduler_enable_element (element->sched, element);
  }
  else if (GST_STATE_TRANSITION (element) == GST_STATE_PLAYING_TO_PAUSED) {
    if (GST_ELEMENT_PARENT (element)) {
      GST_DEBUG (GST_CAT_STATES, "PLAYING->PAUSED: element \"%s\" has parent \"%s\" and sched %p\n",
                 GST_ELEMENT_NAME (element), GST_ELEMENT_NAME (GST_ELEMENT_PARENT (element)),
		 GST_ELEMENT_SCHED (element));
    }
    gst_scheduler_disable_element (element->sched, element);
  }

  GST_STATE (element) = GST_STATE_PENDING (element);
  GST_STATE_PENDING (element) = GST_STATE_VOID_PENDING;

  // note: queues' state_change is a special case because it needs to lock
  // for synchronization (from another thread).  since this signal may block
  // or (worse) make another state change, the queue needs to unlock before
  // calling.  thus, gstqueue.c::gst_queue_state_change() blocks, unblocks,
  // unlocks, then emits this. 
  g_signal_emit (G_OBJECT (element), gst_element_signals[STATE_CHANGE], 0,
                   old_state, GST_STATE (element));

  return GST_STATE_SUCCESS;
}

/**
 * gst_element_get_factory:
 * @element: element to request the factory
 *
 * Retrieves the factory that was used to create this element
 *
 * Returns: the factory used for creating this element
 */
GstElementFactory*
gst_element_get_factory (GstElement *element)
{
  GstElementClass *oclass;

  g_return_val_if_fail (element != NULL, NULL);
  g_return_val_if_fail (GST_IS_ELEMENT (element), NULL);

  oclass = GST_ELEMENT_CLASS (G_OBJECT_GET_CLASS(element));

  return oclass->elementfactory;
}

static void
gst_element_dispose (GObject *object)
{
  GstElement *element = GST_ELEMENT (object);
  GList *pads;
  GstPad *pad;
  
  GST_DEBUG_ELEMENT (GST_CAT_REFCOUNTING, element, "dispose\n");

  if (GST_IS_BIN (GST_OBJECT_PARENT (element)))
    gst_bin_remove (GST_BIN (GST_OBJECT_PARENT (element)), element);


  if (element->pads) {
    GList *orig;
    orig = pads = g_list_copy (element->pads);
    while (pads) {
      pad = GST_PAD (pads->data);
      // the gst_object_unparent will do the unreffing
      gst_element_remove_pad(element, pad);
      pads = g_list_next (pads);
    }
    g_list_free (orig);
    g_list_free (element->pads);
    element->pads = NULL;
  }

  element->numsrcpads = 0;
  element->numsinkpads = 0;

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

#ifndef GST_DISABLE_LOADSAVE
/**
 * gst_element_save_thyself:
 * @element: GstElement to save
 * @parent: the xml parent node
 *
 * Saves the element as part of the given XML structure
 *
 * Returns: the new xml node
 */
static xmlNodePtr
gst_element_save_thyself (GstObject *object,
		          xmlNodePtr parent)
{
  GList *pads;
  GstElementClass *oclass;
//  GType type;
  GstElement *element;

  g_return_val_if_fail (GST_IS_ELEMENT (object), parent);

  element = GST_ELEMENT (object);

  oclass = GST_ELEMENT_CLASS (G_OBJECT_GET_CLASS(element));

  xmlNewChild(parent, NULL, "name", GST_ELEMENT_NAME(element));

  if (oclass->elementfactory != NULL) {
    GstElementFactory *factory = (GstElementFactory *)oclass->elementfactory;

    xmlNewChild (parent, NULL, "type", GST_OBJECT_NAME (factory));
    xmlNewChild (parent, NULL, "version", factory->details->version);
  }

//  if (element->manager)
//    xmlNewChild(parent, NULL, "manager", GST_ELEMENT_NAME(element->manager));

/* FIXME FIXME FIXME!
  // output all args to the element
  type = G_OBJECT_TYPE (element);
  while (type != G_TYPE_INVALID) {
    GtkArg *args;
    guint32 *flags;
    guint num_args,i;

    args = gtk_object_query_args (type, &flags, &num_args);

    for (i=0; i<num_args; i++) {
      if ((args[i].type > G_TYPE_NONE) &&
          (flags[i] & GTK_ARG_READABLE)) {
        xmlNodePtr arg;
        gtk_object_getv (G_OBJECT (element), 1, &args[i]);
        arg = xmlNewChild (parent, NULL, "arg", NULL);
        xmlNewChild (arg, NULL, "name", args[i].name);
        switch (args[i].type) {
          case G_TYPE_CHAR:
            xmlNewChild (arg, NULL, "value",
                         g_strdup_printf ("%c", G_VALUE_CHAR (args[i])));
            break;
          case G_TYPE_UCHAR:
            xmlNewChild (arg, NULL, "value",
                         g_strdup_printf ("%d", G_VALUE_UCHAR (args[i])));
            break;
          case G_TYPE_BOOL:
            xmlNewChild (arg, NULL, "value",
                        G_VALUE_BOOL (args[i]) ? "true" : "false");
            break;
          case G_TYPE_INT:
            xmlNewChild (arg, NULL, "value",
                         g_strdup_printf ("%d", G_VALUE_INT (args[i])));
            break;
          case G_TYPE_LONG:
            xmlNewChild (arg, NULL, "value",
                         g_strdup_printf ("%ld", G_VALUE_LONG (args[i])));
            break;
          case G_TYPE_ULONG:
            xmlNewChild (arg, NULL, "value",
                         g_strdup_printf ("%lu", G_VALUE_ULONG (args[i])));
            break;
          case G_TYPE_FLOAT:
            xmlNewChild (arg, NULL, "value",
                         g_strdup_printf ("%f", G_VALUE_FLOAT (args[i])));
            break;
          case G_TYPE_DOUBLE:
            xmlNewChild (arg, NULL, "value",
                         g_strdup_printf ("%g", G_VALUE_DOUBLE (args[i])));
            break;
          case G_TYPE_STRING:
            xmlNewChild (arg, NULL, "value", G_VALUE_STRING (args[i]));
            break;
	  default:
	    if (args[i].type == GST_TYPE_FILENAME) {
              xmlNewChild (arg, NULL, "value", G_VALUE_STRING (args[i]));
	    }
	    break;
        }
      }
    }
    type = gtk_type_parent (type);
  }
*/

  pads = GST_ELEMENT_PADS (element);

  while (pads) {
    GstPad *pad = GST_PAD (pads->data);
    // figure out if it's a direct pad or a ghostpad
    if (GST_ELEMENT (GST_OBJECT_PARENT (pad)) == element) {
      xmlNodePtr padtag = xmlNewChild (parent, NULL, "pad", NULL);
      gst_object_save_thyself (GST_OBJECT (pad), padtag);
    }
    pads = g_list_next (pads);
  }

  return parent;
}

/**
 * gst_element_restore_thyself:
 * @self: the xml node
 * @parent: the parent of this object when it's loaded
 *
 * Load the element from the XML description
 *
 * Returns: the new element
 */
GstElement*
gst_element_restore_thyself (xmlNodePtr self, GstObject *parent)
{
  xmlNodePtr children = self->xmlChildrenNode;
  GstElement *element;
  GstObjectClass *oclass;
  guchar *name = NULL;
  guchar *value = NULL;
  guchar *type = NULL;

  // first get the needed tags to construct the element
  while (children) {
    if (!strcmp (children->name, "name")) {
      name = xmlNodeGetContent (children);
    } else if (!strcmp (children->name, "type")) {
      type = xmlNodeGetContent (children);
    }
    children = children->next;
  }
  g_return_val_if_fail (name != NULL, NULL);
  g_return_val_if_fail (type != NULL, NULL);

  GST_INFO (GST_CAT_XML,"loading \"%s\" of type \"%s\"", name, type);

  element = gst_elementfactory_make (type, name);

  g_return_val_if_fail (element != NULL, NULL);

  // ne need to set the parent on this object bacause the pads
  // will go through the hierarchy to connect to thier peers
  if (parent)
    gst_object_set_parent (GST_OBJECT (element), parent);

  // we have the element now, set the arguments
  children = self->xmlChildrenNode;

  while (children) {
    if (!strcmp (children->name, "arg")) {
      xmlNodePtr child = children->xmlChildrenNode;

      while (child) {
        if (!strcmp (child->name, "name")) {
          name = xmlNodeGetContent (child);
	}
	else if (!strcmp (child->name, "value")) {
          value = xmlNodeGetContent (child);
	}
        child = child->next;
      }
      gst_util_set_object_arg ((GObject *)G_OBJECT (element), name, value);
    }
    children = children->next;
  }
  // we have the element now, set the pads
  children = self->xmlChildrenNode;

  while (children) {
    if (!strcmp (children->name, "pad")) {
      gst_pad_load_and_connect (children, GST_OBJECT (element));
    }
    children = children->next;
  }

  oclass = GST_OBJECT_CLASS (G_OBJECT_GET_CLASS(element));
  if (oclass->restore_thyself)
    (oclass->restore_thyself) (GST_OBJECT (element), self);

  if (parent)
    gst_object_unparent (GST_OBJECT (element));

  gst_class_signal_emit_by_name (GST_OBJECT (element), "object_loaded", self);

  return element;
}
#endif /* GST_DISABLE_LOADSAVE */

/**
 * gst_element_set_sched:
 * @element: Element to set manager of.
 * @sched: @GstScheduler to set.
 *
 * Sets the scheduler of the element.  For internal use only, unless you're
 * writing a new bin subclass.
 */
void
gst_element_set_sched (GstElement *element,
		         GstScheduler *sched)
{
  GST_INFO_ELEMENT (GST_CAT_PARENTAGE, element, "setting scheduler to %p",sched);
  element->sched = sched;
}

/**
 * gst_element_get_sched:
 * @element: Element to get manager of.
 *
 * Returns the scheduler of the element.
 *
 * Returns: Element's scheduler
 */
GstScheduler*
gst_element_get_sched (GstElement *element)
{
  return element->sched;
}

/**
 * gst_element_set_loop_function:
 * @element: Element to set loop function of.
 * @loop: Pointer to loop function.
 *
 * This sets the loop function for the element.  The function pointed to
 * can deviate from the GstElementLoopFunction definition in type of
 * pointer only.
 *
 * NOTE: in order for this to take effect, the current loop function *must*
 * exit.  Assuming the loop function itself is the only one who will cause
 * a new loopfunc to be assigned, this should be no problem.
 */
void
gst_element_set_loop_function(GstElement *element,
                              GstElementLoopFunction loop)
{
  /* set the loop function */
  element->loopfunc = loop;

  /* set the NEW_LOOPFUNC flag so everyone knows to go try again */
  GST_FLAG_SET(element,GST_ELEMENT_NEW_LOOPFUNC);
}

/**
 * gst_element_signal_eos:
 * @element: element to trigger the eos signal of
 *
 * Throws the eos signal to indicate that the end of the stream is reached.
 */
void
gst_element_signal_eos (GstElement *element)
{
  g_return_if_fail (element != NULL);
  g_return_if_fail (GST_IS_ELEMENT (element));

  GST_DEBUG(GST_CAT_EVENT, "signaling EOS on element %s\n",GST_OBJECT_NAME(element));
  g_signal_emit (G_OBJECT (element), gst_element_signals[EOS], 0);
  GST_FLAG_SET(element,GST_ELEMENT_COTHREAD_STOPPING);
}


/**
 * gst_element_statename:
 * @state: The state to get the name of
 *
 * Gets a string representing the given state.
 *
 * Returns: a string with the statename.
 */
const gchar*
gst_element_statename (GstElementState state) 
{
  switch (state) {
#ifdef GST_DEBUG_COLOR
    case GST_STATE_VOID_PENDING: return "NONE_PENDING";break;
    case GST_STATE_NULL: return "\033[01;37mNULL\033[00m";break;
    case GST_STATE_READY: return "\033[01;31mREADY\033[00m";break;
    case GST_STATE_PLAYING: return "\033[01;32mPLAYING\033[00m";break;
    case GST_STATE_PAUSED: return "\033[01;33mPAUSED\033[00m";break;
    default: return g_strdup_printf ("\033[01;37;41mUNKNOWN!\033[00m(%d)", state);
#else
    case GST_STATE_VOID_PENDING: return "NONE_PENDING";break;
    case GST_STATE_NULL: return "NULL";break;
    case GST_STATE_READY: return "READY";break;
    case GST_STATE_PLAYING: return "PLAYING";break;
    case GST_STATE_PAUSED: return "PAUSED";break;
    default: return "UNKNOWN!";
#endif
  }
  return "";
}
