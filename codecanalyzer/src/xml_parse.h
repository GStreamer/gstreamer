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
#ifndef __XML_PARSE__
#define __XML_PARSE__

#include <stdio.h>
#include <stdlib.h>
#include <libxml/tree.h>
#include <libxml/parser.h>
#include <gst/gst.h>

typedef enum {
  ANALYZER_ALL,
  ANALYZER_HEADERS,
  ANALYZER_QUANTMATRIX,
  ANALYZER_SLICE,
  ANALYZER_HEXVAL
} AnalyzerHeaderGroup;

typedef struct {
  xmlChar *field_name;
  xmlChar *value;
  xmlChar *nbits;

  xmlChar *is_matrix;
  xmlChar *rows;
  xmlChar *columns;
}AnalyzerNode;

AnalyzerNode *analyzer_node_new ();

GList *
analyzer_get_list_analyzer_node_from_xml (char *file_name, char *node_name);

GList *
analyzer_get_list_header_strings (char *file_name);

void analyzer_node_free (gpointer data);

void analyzer_node_list_free (GList *list);

#endif
