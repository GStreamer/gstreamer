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


static void 	gst_xml_class_init		(GstXMLClass *klass);
static void	gst_xml_init			(GstXML *xml);

static GstObjectClass *parent_class = NULL;

GtkType 
gst_xml_get_type(void) 
{
  static GtkType xml_type = 0;

  if (!xml_type) {
    static const GtkTypeInfo xml_info = {
      "GstXML",
      sizeof(GstElement),
      sizeof(GstElementClass),
      (GtkClassInitFunc)gst_xml_class_init,
      (GtkObjectInitFunc)gst_xml_init,
      (GtkArgSetFunc)NULL,
      (GtkArgGetFunc)NULL,
      (GtkClassInitFunc)NULL,
    };
    xml_type = gtk_type_unique (GST_TYPE_XML, &xml_info);
  }
  return xml_type;
}

static void
gst_xml_class_init (GstXMLClass *klass) 
{
  parent_class = gtk_type_class (GST_TYPE_OBJECT);
}

static void 
gst_xml_init(GstXML *xml) 
{
}

/**
 * gst_xml_write:
 * @element: The element to write out
 *
 * converts the given element into an XML presentation
 *
 * Returns: a pointer to an XML document
 */
xmlDocPtr 
gst_xml_write (GstElement *element) 
{
  xmlDocPtr doc;

  doc = xmlNewDoc ("1.0");
  doc->root = xmlNewDocNode (doc, NULL, "GST-Pipeline", NULL);

  gst_element_save_thyself (element, doc->root);

  return doc;
}

/**
 * gst_xml_new:
 * @fname: The filename with the xml description
 * @root: The name of the root object to build
 *
 * Creates a new GstXML object (and the corresponding elements) from 
 * the XML file fname. Optionally it will only build the element from 
 * the element node root (if it is not NULL). This feature is useful 
 * if you only want to build a specific element from an XML file
 * but not the pipeline it is embedded in. Note also that the XML parse 
 * tree is cached to speed up creating another GstXML object for 
 * the same file
 *
 * Returns: a pointer to a new GstElement
 */
GstXML*
gst_xml_new (const guchar *fname, const guchar *root) 
{
  xmlDocPtr doc;
  xmlNodePtr field;
  GstXML *xml;

  g_return_val_if_fail(fname != NULL, NULL);
	
  doc = xmlParseFile(fname);

  if (!doc) {
    g_print("gstxml: XML file \"%s\" could not be read\n", fname);
    return NULL;
  }
  if (strcmp(doc->root->name, "GST-Pipeline")) {
    g_print("gstxml: XML file \"%s\" is in wrong format\n", fname);
    return NULL;
  }

  xml = GST_XML(gtk_type_new(GST_TYPE_XML));

  xml->topelements = NULL;

  field = doc->root->childs;
  
  while (field) {
    if (!strcmp(field->name, "element")) {
      GstElement *element;
      
      xml->elements = g_hash_table_new(g_str_hash, g_str_equal);

      element = gst_element_load_thyself(field, xml->elements);
      
      g_hash_table_destroy (xml->elements);

      xml->topelements = g_list_prepend (xml->topelements, element);
    }
    field = field->next;
  }

  xml->topelements = g_list_reverse (xml->topelements);

  return xml;
}

/**
 * gst_xml_get_topelements:
 * @xml: The GstXML to get the elements from
 *
 * retrive a list of toplevel elements
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

  g_return_val_if_fail(xml != NULL, NULL);
  g_return_val_if_fail(name != NULL, NULL);

  GST_DEBUG (0,"gstxml: getting element \"%s\"\n", name);

  element = g_hash_table_lookup(xml->elements, name);

  return element;
}
