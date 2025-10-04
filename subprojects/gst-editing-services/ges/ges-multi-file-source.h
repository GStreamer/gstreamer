/* GStreamer Editing Services
 * Copyright (C) 2013 Lubosz Sarnecki <lubosz@gmail.com>
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

#include <glib-object.h>
#include <ges/ges-types.h>
#include <ges/ges-video-source.h>

G_BEGIN_DECLS
#define GES_TYPE_MULTI_FILE_SOURCE ges_multi_file_source_get_type()
GES_DECLARE_TYPE(MultiFileSource, multi_file_source, MULTI_FILE_SOURCE);

/**
 * GESMultiFileSource:
 */
struct _GESMultiFileSource
{
  /*< private > */
  GESVideoSource parent;

  gchar *uri;

  GESMultiFileSourcePrivate *priv;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

struct _GESMultiFileSourceClass
{
  GESVideoSourceClass parent_class;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

GES_API
GESMultiFileSource *ges_multi_file_source_new (gchar * uri) G_GNUC_WARN_UNUSED_RESULT;

#define GES_MULTI_FILE_URI_PREFIX "multifile://"

G_END_DECLS
