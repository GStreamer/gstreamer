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

#include "ges-structure-parser.h"

G_DEFINE_TYPE (GESStructureParser, ges_structure_parser, G_TYPE_OBJECT);

static void
ges_structure_parser_init (GESStructureParser * self)
{
}

static void
ges_structure_parser_class_init (GESStructureParserClass * klass)
{
}

void
ges_structure_parser_parse_string (GESStructureParser * self,
    const gchar * text, gboolean is_symbol)
{
  gchar *new_string = NULL;

  if (self->current_string) {
    new_string = g_strconcat (self->current_string, text, NULL);
  } else if (is_symbol) {
    new_string = g_strdup (text);
  }

  g_free (self->current_string);
  self->current_string = new_string;
}

void
ges_structure_parser_parse_default (GESStructureParser * self,
    const gchar * text)
{
  gchar *new_string = NULL;

  if (self->add_comma && self->current_string) {
    new_string = g_strconcat (self->current_string, ",", text, NULL);
    g_free (self->current_string);
    self->current_string = new_string;
    self->add_comma = FALSE;
  } else {
    ges_structure_parser_parse_string (self, text, FALSE);
  }
}

void
ges_structure_parser_parse_whitespace (GESStructureParser * self)
{
  self->add_comma = TRUE;
}

static void
_finish_structure (GESStructureParser * self)
{
  if (self->current_string) {
    GstStructure *structure =
        gst_structure_new_from_string (self->current_string);

    if (structure == NULL) {
      GST_ERROR ("Error creating structure from %s", self->current_string);

      return;
    }

    self->structures = g_list_append (self->structures, structure);
    g_free (self->current_string);
    self->current_string = NULL;
  }
}

void
ges_structure_parser_end_of_file (GESStructureParser * self)
{
  _finish_structure (self);
}

void
ges_structure_parser_parse_symbol (GESStructureParser * self,
    const gchar * symbol)
{
  _finish_structure (self);

  while (*symbol == '-' || *symbol == ' ')
    symbol++;
  ges_structure_parser_parse_string (self, symbol, TRUE);
  self->add_comma = TRUE;
}

void
ges_structure_parser_parse_setter (GESStructureParser * self,
    const gchar * setter)
{
  gchar *parsed_setter;

  _finish_structure (self);

  while (*setter == '-' || *setter == ' ')
    setter++;

  while (*setter != '-')
    setter++;

  setter++;

  parsed_setter = g_strdup_printf ("set-property, property=%s, value=", setter);
  self->add_comma = FALSE;
  ges_structure_parser_parse_string (self, parsed_setter, TRUE);
  g_free (parsed_setter);
}

GESStructureParser *
ges_structure_parser_new (void)
{
  return (g_object_new (GES_TYPE_STRUCTURE_PARSER, NULL));
}
