/* GStreamer Editing Services
 * Copyright (C) 2010 Brandon Lewis <brandon.lewis@collabora.co.uk>
 *               2010 Nokia Corporation
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

#ifndef _GES_KEYFILE_FORMATTER
#define _GES_KEYFILE_FORMATTER

#include <glib-object.h>
#include <ges/ges-timeline.h>

#define GES_TYPE_KEYFILE_FORMATTER ges_keyfile_formatter_get_type()

#define GES_KEYFILE_FORMATTER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GES_TYPE_KEYFILE_FORMATTER, GESKeyfileFormatter))

#define GES_KEYFILE_FORMATTER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GES_TYPE_KEYFILE_FORMATTER, GESKeyfileFormatterClass))

#define GES_IS_KEYFILE_FORMATTER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GES_TYPE_KEYFILE_FORMATTER))

#define GES_IS_KEYFILE_FORMATTER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GES_TYPE_KEYFILE_FORMATTER))

#define GES_KEYFILE_FORMATTER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GES_TYPE_KEYFILE_FORMATTER, GESKeyfileFormatterClass))

/**
 * GESKeyfileFormatter:
 *
 * Serializes a #GESTimeline to a file using #GKeyFile
 */

struct _GESKeyfileFormatter {
  /*< private >*/
  GESFormatter parent;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

struct _GESKeyfileFormatterClass {
  /*< private >*/
  GESFormatterClass parent_class;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

GType ges_keyfile_formatter_get_type (void);

GESKeyfileFormatter *ges_keyfile_formatter_new (void);

#endif /* _GES_KEYFILE_FORMATTER */
