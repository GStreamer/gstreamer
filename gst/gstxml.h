/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wim.taymans@chello.be>
 *
 * gstxml.h: Header for XML save/restore operations
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

#ifndef __GST_XML_H__
#define __GST_XML_H__

#include <parser.h>

// Include compatability defines: if libxml hasn't already defined these,
// we have an old version 1.x
#ifndef xmlChildrenNode
#define xmlChildrenNode childs
#define xmlRootNode root
#endif

#include <gst/gstelement.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define GST_TYPE_XML \
  (gst_xml_get_type())
#define GST_XML(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_XML,GstXML))
#define GST_XML_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_XML,GstXMLClass))
#define GST_IS_XML(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_XML))
#define GST_IS_XML_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_XML))

typedef struct _GstXML GstXML;
typedef struct _GstXMLClass GstXMLClass;

struct _GstXML {
  GstObject object;

  GList      *topelements;

  xmlNsPtr ns;
};

struct _GstXMLClass {
  GstObjectClass parent_class;

  /* signal callbacks */
  void (*object_loaded)         (GstXML *xml, GstObject *object, xmlNodePtr self);
  void (*object_saved)          (GstXML *xml, GstObject *object, xmlNodePtr self);
};

GType		gst_xml_get_type	(void);


/* create an XML document out of a pipeline */
xmlDocPtr	gst_xml_write		(GstElement *element);

GstXML*		gst_xml_new		(void);

gboolean	gst_xml_parse_doc	(GstXML *xml, xmlDocPtr doc, const guchar *root);
gboolean	gst_xml_parse_file	(GstXML *xml, const guchar *fname, const guchar *root);
gboolean	gst_xml_parse_memory	(GstXML *xml, guchar *buffer, guint size, const gchar *root);


GstElement*	gst_xml_get_element	(GstXML *xml, const guchar *name);
GList*		gst_xml_get_topelements (GstXML *xml);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __GST_XML_H__ */
