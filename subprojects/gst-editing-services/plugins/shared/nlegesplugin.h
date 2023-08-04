/* GES and NLE plugins shared header
 *
 * Copyright (C) 2024 Thibault Saunier <tsaunier@igalia.com>
 *
 * nlegesplugin.h
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
 *
 */
#pragma once

#include <gst/gst.h>

#define NLE_QUERY_PARENT_NLE_OBJECT "nle-query-parent-nle-object"
typedef struct
{
  GMutex lock;
  GstElement *nle_object;
} NleQueryParentNleObject;

/* *INDENT-OFF* */
#define NLE_TYPE_QUERY_PARENT_NLE_OBJECT nle_query_parent_nle_object_get_type ()
GType nle_query_parent_nle_object_get_type (void) G_GNUC_CONST;
/* *INDENT-ON* */

void nle_query_parent_nle_object_release (NleQueryParentNleObject * query);
