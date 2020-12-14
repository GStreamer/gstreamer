/* GStreamer Editing Services
 * Copyright (C) 2020 Ubicast SAS
 *               Author: Thibault Saunier <tsaunier@igalia.com>
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
#include <ges/ges.h>

G_BEGIN_DECLS

typedef struct _GESUriSource GESUriSource;

struct _GESUriSource
{
  GstElement *decodebin;        /* Reference owned by parent class */
  gchar *uri;

  GESTrackElement *element;
};

G_GNUC_INTERNAL gboolean      ges_uri_source_select_pad   (GESSource *self, GstPad *pad);
G_GNUC_INTERNAL GstElement *ges_uri_source_create_source  (GESUriSource *self);
G_GNUC_INTERNAL void         ges_uri_source_init          (GESTrackElement *element, GESUriSource *self);

G_END_DECLS
