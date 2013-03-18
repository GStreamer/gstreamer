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

/**
 * SECTION: ges-gerror
 * @short_description: GError — Categorized error messages
 */

#ifndef __GES_ERROR_H__
#define __GES_ERROR_H__

G_BEGIN_DECLS

/**
 * GES_ASSET_ERROR:
 *
 * An error happend using an asset
 */
#define GES_ASSET_ERROR g_quark_from_static_string("GES_ASSET_ERROR")

/**
 * GES_FORMATTER_ERROR:
 *
 * An error happend using a formatter
 */
#define GES_FORMATTER_ERROR g_quark_from_static_string("GES_FORMATTER_ERROR")

/**
 * GESAssetError:
 * @GES_ASSET_WRONG_ID: The ID passed is malformed
 * @GES_ASSET_ERROR_LOADING: An error happened while loading the asset
 */
typedef enum
{
  GES_ASSET_WRONG_ID,
  GES_ASSET_ERROR_LOADING
} GESAssetError;

/**
 * GESFormatterError:
 * @GES_FORMATTER_WRONG_INPUT_FILE: The formatted files was malformed
 */
typedef enum
{
  GES_FORMATTER_WRONG_INPUT_FILE,
} GESFormatterError;

G_END_DECLS
#endif /* __GES_ERROR_H__ */
