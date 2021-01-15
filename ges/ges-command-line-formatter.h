/* GStreamer Editing Services
 *
 * Copyright (C) <2015> Thibault Saunier <tsaunier@gnome.org>
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

#pragma once

#include <glib-object.h>
#include "ges-formatter.h"

G_BEGIN_DECLS

typedef struct _GESCommandLineFormatterClass GESCommandLineFormatterClass;
typedef struct _GESCommandLineFormatter GESCommandLineFormatter;

#define GES_TYPE_COMMAND_LINE_FORMATTER             (ges_command_line_formatter_get_type ())
GES_DECLARE_TYPE(CommandLineFormatter, command_line_formatter, COMMAND_LINE_FORMATTER);

struct _GESCommandLineFormatterClass
{
    GESFormatterClass parent_class;
};

struct _GESCommandLineFormatter
{
    GESFormatter parent_instance;

    GESCommandLineFormatterPrivate *priv;
};

GES_API
gchar * ges_command_line_formatter_get_help (gint nargs, gchar ** commands);

GES_API
gchar * ges_command_line_formatter_get_timeline_uri (GESTimeline *timeline);

G_END_DECLS
