/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstxml.c: XML save/restore of pipelines
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

#include "gst_private.h"

#include "gstxml.h"
#include "gstbin.h"

enum {
  OBJECT_LOADED,
  LAST_SIGNAL
};

static void	gst_xml_class_init		(GstXMLClass *klass);
static void	gst_xml_init			(GstXML *xml);

static void	gst_xml_object_loaded		(GstObject *private, GstObject *object, xmlNodePtr self, gpointer data);

static GstObjectClass *parent_class = NULL;
static guint gst_xml_signals[LAST_SIGNAL] = { 0 };

GType
gst_xml_get_type(void)
{
  static GType xml_type = 0;

  if (!xml_type) {
    static const GTypeInfo xml_info = {
      sizeof(GstXMLClass),
      NULL,
      NULL,
      (GClassInitFunc)gst_xml_class_init,
      NULL,
      NULL,
      sizeof(GstXML),
      0,
      (GInstanceInitFunc)gst_xml_init,
    };
    xml_type = g_type_register_static (GST_TYPE_OBJECT, "GstXml", &xml_info, 0);
  }
  return xml_type;
}

static void
gst_xml_class_init (GstXMLClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *)klass;

  parent_class = g_type_class_ref (GST_TYPE_OBJECT);

  gst_xml_signals[OBJECT_LOADED] =
    g_signal_newc ("object_loaded", G_OBJECT_TYPE(gobject_class), G_SIGNAL_RUN_LAST,
                    G_STRUCT_OFFSET (GstXMLClass, object_loaded), NULL, NULL,
                    gst_marshal_VOID__OBJECT_POINTER, G_TYPE_NONE, 2,
                    GST_TYPE_OBJECT, G_TYPE_POINTER);

}

static void
gst_xml_init(GstXML *xml)
{
  xml->topelements = NULL;
}

/**
 * gst_xml_new:
 *
 * Create a new GstXML parser object.
 *
 * Returns: a pointer to a new GstXML object.
 */
GstXML*
gst_xml_new (void)
{
  return GST_XML (g_object_new(GST_TYPE_XML,NULL));
}

/**
 * gst_xml_write:
 * @element: The element to write out
 *
 * Converts the given element into an XML presentation.
 *
 * Returns: a pointer to an XML document
 */
xmlDocPtr
gst_xml_write (GstElement *element)
{
  xmlDocPtr doc;
  xmlNodePtr elementnode;
  xmlNsPtr ns;

  doc = xmlNewDoc ("1.0");
  xmlNewGlobalNs (doc, "http://gstreamer.net/gst-core/1.0/", "gst");
  ns = xmlNewNs (doc->xmlRootNode, "http://gstreamer.net/gst-core/1.0/", "gst");

  doc->xmlRootNode = xmlNewDocNode (doc, ns, "gstreamer", NULL);

  elementnode = xmlNewChild (doc->xmlRootNode, ns, "element", NULL);

  gst_object_save_thyself (GST_OBJECT (element), elementnode);

  return doc;
}

/**
 * gst_xml_parse_doc:
 * @xml: a pointer to a GstXML object
 * @doc: a pointer to an xml document to parse
 * @root: The name of the root object to build
 *
 * Fills the GstXML object with the elements from the
 * xmlDocPtr.
 *
 * Returns: TRUE on success, FALSE otherwise
 */
gboolean
gst_xml_parse_doc (GstXML *xml, xmlDocPtr doc, const guchar *root)
{
  xmlNodePtr field, cur;
  xmlNsPtr ns;

  cur = xmlDocGetRootElement(doc);
  if (cur == NULL) {
    g_warning("gstxml: empty document\n");
    return FALSE ;
  }
  ns = xmlSearchNsByHref(doc, cur, "http://gstreamer.net/gst-core/1.0/");
  if (ns == NULL) {
    g_warning("gstxml: document of wrong type, core namespace not found\n");
    return FALSE;
  }
  if (strcmp(cur->name, "gstreamer")) {
    g_warning("gstxml: XML file is in wrong format\n");
    return FALSE;
  }

  gst_class_signal_connect (GST_OBJECT_CLASS (G_OBJECT_GET_CLASS(xml)),
		  "object_loaded", gst_xml_object_loaded, xml);

  xml->ns = ns;

  field = cur->xmlChildrenNode;

  while (field) {
    if (!strcmp(field->name, "element") && (field->ns == xml->ns)) {
      GstElement *element;

      element = gst_element_restore_thyself(field, NULL);

      xml->topelements = g_list_prepend (xml->topelements, element);
    }
    field = field->next;
  }

  xml->topelements = g_list_reverse (xml->topelements);

  return TRUE;
}

/**
 * gst_xml_parse_file:
 * @xml: a pointer to a GstXML object
 * @fname: The filename with the xml description
 * @root: The name of the root object to build
 *
 * Fills the GstXML object with the corresponding elements from
 * the XML file fname. Optionally it will only build the element from
 * the element node root (if it is not NULL). This feature is useful
 * if you only want to build a specific element from an XML file
 * but not the pipeline it is embedded in.
 *
 * Returns: TRUE on success, FALSE otherwise
 */
gboolean
gst_xml_parse_file (GstXML *xml, const guchar *fname, const guchar *root)
{
  xmlDocPtr doc;

  g_return_val_if_fail(fname != NULL, FALSE);

  doc = xmlParseFile(fname);

  if (!doc) {
    g_warning("gstxml: XML file \"%s\" could not be read\n", fname);
    return FALSE;
  }

  return gst_xml_parse_doc (xml, doc, root);
}

/**
 * gst_xml_parse_memory:
 * @xml: a pointer to a GstXML object
 * @buffer: a pointer to the in memory XML buffer
 * @size: the size of the buffer
 * @root: the name of the root objects to build
 *
 * Fills the GstXML object with the corresponding elements from
 * an in memory XML buffer.
 *
 * Returns: TRUE on success
 */
gboolean
gst_xml_parse_memory (GstXML *xml, guchar *buffer, guint size, const gchar *root)
{
  xmlDocPtr doc;

  g_return_val_if_fail(buffer != NULL, FALSE);

  doc = xmlParseMemory (buffer, size);

  return gst_xml_parse_doc (xml, doc, root);
}

static void
gst_xml_object_loaded (GstObject *private, GstObject *object, xmlNodePtr self, gpointer data)
{
  GstXML *xml = GST_XML (data);

  // FIXME check that this element was created from the same xmlDocPtr...
  g_signal_emit (G_OBJECT (xml), gst_xml_signals[OBJECT_LOADED], 0, object, self);
}

/**
 * gst_xml_get_topelements:
 * @xml: The GstXML to get the elements from
 *
 * Retrive a list of toplevel elements.
 *
 * Returns: a GList of elements
 */
GList*
gst_xml_get_topelements (GstXML *xml)
{
  g_return_val_if_fail (xml != NULL, NULL);

  return xml->topelements;
}

/**
 * gst_xml_get_element:
 * @xml: The GstXML to get the element from
 * @name: The name of element to retreive
 *
 * This function is used to get a pointer to the GstElement corresponding
 * to name in the pipeline description. You would use this if you have
 * to do anything to the element after loading.
 *
 * Returns: a pointer to a new GstElement
 */
GstElement*
gst_xml_get_element (GstXML *xml, const guchar *name)
{
  GstElement *element;
  GList *topelements;

  g_return_val_if_fail(xml != NULL, NULL);
  g_return_val_if_fail(name != NULL, NULL);

  GST_DEBUG (0,"gstxml: getting element \"%s\"\n", name);

  topelements = gst_xml_get_topelements (xml);

  while (topelements) {
    GstElement *top = GST_ELEMENT (topelements->data);

    GST_DEBUG (0,"gstxml: getting element \"%s\"\n", name);
    if (!strcmp (GST_ELEMENT_NAME (top), name)) {
      return top;
    }
    else {
      if (GST_IS_BIN (top)) {
        element = gst_bin_get_by_name (GST_BIN (top), name);

	if (element)
          return element;
      }
    }
    topelements = g_list_next (topelements);
  }
  return NULL;
}
