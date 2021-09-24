/* GStreamer Editing Services
 * Copyright (C) 2013 Thibault Saunier <thibault.saunier@collabora.com>
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

/**
 * SECTION: ges-gerror
 * @title: GESErrors
 * @short_description: GError — Categorized error messages
 */

G_BEGIN_DECLS

/**
 * GES_ERROR:
 *
 * An error happened in GES
 */
#define GES_ERROR g_quark_from_static_string("GES_ERROR")

/**
 * GESError:
 * @GES_ERROR_ASSET_WRONG_ID: The ID passed is malformed
 * @GES_ERROR_ASSET_LOADING: An error happened while loading the asset
 * @GES_ERROR_FORMATTER_MALFORMED_INPUT_FILE: The formatted files was malformed
 * @GES_ERROR_INVALID_FRAME_NUMBER: The frame number is invalid
 * @GES_ERROR_NEGATIVE_LAYER: The operation would lead to a negative
 * #GES_TIMELINE_ELEMENT_LAYER_PRIORITY. (Since: 1.18)
 * @GES_ERROR_NEGATIVE_TIME: The operation would lead to a negative time.
 * E.g. for the #GESTimelineElement:start #GESTimelineElement:duration or
 * #GESTimelineElement:in-point. (Since: 1.18)
 * @GES_ERROR_NOT_ENOUGH_INTERNAL_CONTENT: Some #GESTimelineElement does
 * not have a large enough #GESTimelineElement:max-duration to cover the
 * desired operation. (Since: 1.18)
 * @GES_ERROR_INVALID_OVERLAP_IN_TRACK: The operation would break one of
 * the overlap conditions for the #GESTimeline. (Since: 1.18)
 */
typedef enum
{
  GES_ERROR_ASSET_WRONG_ID,
  GES_ERROR_ASSET_LOADING,
  GES_ERROR_FORMATTER_MALFORMED_INPUT_FILE,
  GES_ERROR_INVALID_FRAME_NUMBER,
  GES_ERROR_NEGATIVE_LAYER,
  GES_ERROR_NEGATIVE_TIME,
  GES_ERROR_NOT_ENOUGH_INTERNAL_CONTENT,
  GES_ERROR_INVALID_OVERLAP_IN_TRACK,
  GES_ERROR_INVALID_EFFECT_BIN_DESCRIPTION,
} GESError;

G_END_DECLS
