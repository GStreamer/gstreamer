/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstpluginfeature.h: Header for base GstXMLRegistry
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


#ifndef __GST_XML_REGISTRY_H__
#define __GST_XML_REGISTRY_H__

#include <gst/gstregistry.h>

G_BEGIN_DECLS
#define GST_TYPE_XML_REGISTRY \
  (gst_xml_registry_get_type())
#define GST_XML_REGISTRY(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_XML_REGISTRY,GstXMLRegistry))
#define GST_XML_REGISTRY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_XML_REGISTRY,GstXMLRegistryClass))
#define GST_IS_XML_REGISTRY(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_XML_REGISTRY))
#define GST_IS_XML_REGISTRY_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_XML_REGISTRY))
typedef struct _GstXMLRegistry GstXMLRegistry;
typedef struct _GstXMLRegistryClass GstXMLRegistryClass;

typedef enum
{
  GST_XML_REGISTRY_NONE,
  GST_XML_REGISTRY_TOP,
  GST_XML_REGISTRY_PATHS,
  GST_XML_REGISTRY_PATH,
  GST_XML_REGISTRY_PATHS_DONE,
  GST_XML_REGISTRY_PLUGIN,
  GST_XML_REGISTRY_FEATURE,
  GST_XML_REGISTRY_PADTEMPLATE,
  GST_XML_REGISTRY_CAPS,
  GST_XML_REGISTRY_STRUCTURE,
  GST_XML_REGISTRY_PROPERTIES
} GstXMLRegistryState;

typedef enum
{
  GST_XML_REGISTRY_READ,
  GST_XML_REGISTRY_WRITE
} GstXMLRegistryMode;

typedef void (*GstXMLRegistryGetPerms) (GstXMLRegistry * registry);
typedef void (*GstXMLRegistryAddPathList) (GstXMLRegistry * registry);
typedef gboolean (*GstXMLRegistryParser) (GMarkupParseContext * context,
    const gchar * tag,
    const gchar * text,
    gsize text_len, GstXMLRegistry * registry, GError ** error);

typedef gboolean (*GstXMLRegistryOpen) (GstXMLRegistry * registry,
    GstXMLRegistryMode mode);
typedef gboolean (*GstXMLRegistryLoad) (GstXMLRegistry * registry,
    gchar * dest, gssize * size);
typedef gboolean (*GstXMLRegistrySave) (GstXMLRegistry * registry,
    gchar * format, ...);
typedef gboolean (*GstXMLRegistryClose) (GstXMLRegistry * registry);

struct _GstXMLRegistry
{
  GstRegistry object;

  gchar *location;
  gboolean open;

  FILE *regfile;
  gchar *buffer;

  GMarkupParseContext *context;
  GList *open_tags;
  GstXMLRegistryState state;
  GstXMLRegistryParser parser;

  GstPlugin *current_plugin;
  GstPluginFeature *current_feature;

  gchar *name_template;
  GstPadDirection direction;
  GstPadPresence presence;
  GstCaps *caps;

  gchar *caps_name;
  gchar *structure_name;

  gboolean in_list;
  GList *entry_list;
  gchar *list_name;
};

struct _GstXMLRegistryClass
{
  GstRegistryClass parent_class;

  GstXMLRegistryGetPerms get_perms_func;
  GstXMLRegistryAddPathList add_path_list_func;
  GstXMLRegistryOpen open_func;
  GstXMLRegistryLoad load_func;
  GstXMLRegistrySave save_func;
  GstXMLRegistryClose close_func;
};


/* normal GObject stuff */
GType gst_xml_registry_get_type (void);

GstRegistry *gst_xml_registry_new (const gchar * name, const gchar * location);

G_END_DECLS
#endif /* __GST_XML_REGISTRY_H__ */
