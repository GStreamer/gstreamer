/*
 * Copyright (c) 2013, Intel Corporation.
 * Author: Sreerenj Balachandran <sreerenj.balachandran@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */
#include "xml_parse.h"

void
analyzer_node_list_free (GList * list)
{
  g_list_free_full (list, (GDestroyNotify) analyzer_node_free);
}

void
analyzer_node_free (gpointer data)
{
  AnalyzerNode *node = (AnalyzerNode *) data;

  if (node) {
    if (node->field_name)
      g_free (node->field_name);
    if (node->value)
      g_free (node->value);
    if (node->nbits)
      g_free (node->nbits);
    if (node->is_matrix)
      g_free (node->is_matrix);
  }
}

AnalyzerNode *
analyzer_node_new ()
{
  AnalyzerNode *node;

  node = g_slice_new0 (AnalyzerNode);

  return node;
}

GList *
analyzer_get_list_header_strings (char *file_name)
{
  xmlDocPtr doc;
  xmlNodePtr cur, tmp;
  GList *list = NULL;
  AnalyzerNode *node;

  doc = xmlParseFile (file_name);
  if (!doc) {
    g_error ("Failed to do xmlParseFile for the file.. %s\n", file_name);
    goto error;
  }

  cur = xmlDocGetRootElement (doc);
  if (cur == NULL) {
    g_error ("empty document\n");
    xmlFreeDoc (doc);
    goto error;
  }

  if (xmlStrcmp (cur->name, (const xmlChar *) "mpeg2") &&
      xmlStrcmp (cur->name, (const xmlChar *) "h264") &&
      xmlStrcmp (cur->name, (const xmlChar *) "h265")) {
    g_error ("document of the wrong type !!");
    xmlFreeDoc (doc);
    goto error;
  }

  tmp = cur->xmlChildrenNode;
  while (tmp) {
    list = g_list_prepend (list, g_strdup (tmp->name));
    tmp = tmp->next;
  }
  if (list)
    list = g_list_reverse (list);

  return list;

error:
  return NULL;
}

GList *
analyzer_get_list_analyzer_node_from_xml (char *file_name, char *node_name)
{
  xmlDocPtr doc;
  xmlNodePtr cur, tmp;
  GList *list = NULL;
  AnalyzerNode *node;

  doc = xmlParseFile (file_name);
  if (!doc) {
    g_error ("Failed to do xmlParseFile for the file.. %s\n", file_name);
  }

  cur = xmlDocGetRootElement (doc);
  if (cur == NULL) {
    g_error ("empty document\n");
    xmlFreeDoc (doc);
    return;
  }

  if (xmlStrcmp (cur->name, (const xmlChar *) "mpeg2") &&
      xmlStrcmp (cur->name, (const xmlChar *) "h264") &&
      xmlStrcmp (cur->name, (const xmlChar *) "h265")) {
    g_error ("document of the wrong type !!");
    xmlFreeDoc (doc);
    return;
  }

  tmp = cur->xmlChildrenNode;
  while (tmp) {
    if (!xmlStrcmp (tmp->name, (const xmlChar *) node_name)) {
      g_debug ("Parsing the Child: %s \n", tmp->name);

      cur = tmp->xmlChildrenNode;
      while (cur != NULL) {
        xmlChar *key;
        xmlChar *nbits = NULL;
        xmlChar *is_matrix = NULL;
        xmlChar *rows;
        xmlChar *columns;

        key = xmlNodeListGetString (doc, cur->xmlChildrenNode, 1);
        nbits = xmlGetProp (cur, "nbits");
        is_matrix = xmlGetProp (cur, "is-matrix");
        if (is_matrix) {
          rows = xmlGetProp (cur, "rows");
          columns = xmlGetProp (cur, "columns");
        }
        node = g_slice_new0 (AnalyzerNode);
        node->field_name = g_strdup (cur->name);
        if (key) {
          node->value = g_strdup ((gchar *) key);
          xmlFree (key);
        }
        if (nbits) {
          node->nbits = g_strdup ((gchar *) nbits);
          xmlFree (nbits);
        }
        if (is_matrix) {
          node->is_matrix = g_strdup ((gchar *) is_matrix);
          xmlFree (is_matrix);

          node->rows = g_strdup ((gchar *) rows);
          xmlFree (rows);

          node->columns = g_strdup ((gchar *) columns);
          xmlFree (columns);
        }

        list = g_list_prepend (list, node);

        cur = cur->next;
      }
      break;
    }
    tmp = tmp->next;
  }

  xmlFreeDoc (doc);

  if (list)
    list = g_list_reverse (list);

  return list;
}
