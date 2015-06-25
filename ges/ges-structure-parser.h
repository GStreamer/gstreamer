/* GStreamer Editing Services
 *
 * Copyright (C) <2015> Mathieu Duponchelle <mathieu.duponchelle@opencreed.com>
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
#ifndef _GES_STRUCTURE_PARSER_H
#define _GES_STRUCTURE_PARSER_H

#include <glib.h>
#include <gst/gst.h>

G_BEGIN_DECLS

#define GES_TYPE_STRUCTURE_PARSER            ges_structure_parser_get_type()

typedef struct _GESStructureParser GESStructureParser;
typedef struct _GESStructureParserClass GESStructureParserClass;

struct _GESStructureParser
{
  GObject parent;
  GList *structures;

  GList *wrong_strings;

  /*< private > */
  gchar *current_string;
  gboolean add_comma;
};

struct _GESStructureParserClass
{
  /*< private >*/
  GObjectClass parent_class;
};

G_GNUC_INTERNAL GType ges_structure_parser_get_type (void) G_GNUC_CONST;

G_GNUC_INTERNAL GError * ges_structure_parser_get_error (GESStructureParser *self);
G_GNUC_INTERNAL void ges_structure_parser_parse_string (GESStructureParser *self, const gchar *string, gboolean is_symbol);
G_GNUC_INTERNAL void ges_structure_parser_parse_default (GESStructureParser *self, const gchar *text);
G_GNUC_INTERNAL void ges_structure_parser_parse_whitespace (GESStructureParser *self);
G_GNUC_INTERNAL void ges_structure_parser_parse_symbol (GESStructureParser *self, const gchar *symbol);
G_GNUC_INTERNAL void ges_structure_parser_parse_setter (GESStructureParser *self, const gchar *setter);
G_GNUC_INTERNAL void ges_structure_parser_end_of_file (GESStructureParser *self);

G_GNUC_INTERNAL GESStructureParser *ges_structure_parser_new(void);
G_END_DECLS

#endif  /* _GES_STRUCTURE_PARSER_H */
