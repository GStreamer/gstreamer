/* GStreamer Editing Services Pitivi Formatter
 * Copyright (C) 2011-2012 Mathieu Duponchelle <seeed@laposte.net>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#pragma once

G_BEGIN_DECLS

#define GES_TYPE_PITIVI_FORMATTER ges_pitivi_formatter_get_type()
GES_DECLARE_TYPE(PitiviFormatter, pitivi_formatter, PITIVI_FORMATTER);

/**
 * GESPitiviFormatter: (attributes doc.skip=true):
 *
 * Serializes a #GESTimeline to a file using the xptv Pitivi file format
 */
struct _GESPitiviFormatter {
  GESFormatter parent;

  /*< public > */
  /*< private >*/
  GESPitiviFormatterPrivate *priv;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

/**
 * GESPitiviFormatterClass: (attributes doc.skip=true):
 */
struct _GESPitiviFormatterClass
{
  /*< private >*/
  GESFormatterClass parent_class;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

GES_API
GESPitiviFormatter *ges_pitivi_formatter_new (void) G_GNUC_WARN_UNUSED_RESULT;

G_END_DECLS
