/* GStreamer
 * Copyright (C) 2006 Josep Torra <josep@fluendo.com>
 * Copyright (C) 2006 Mathieu Garcia  <matthieu@fluendo.com>
 * Copyright (C) 2006 Stefan Kost <ensonic@sonicpulse.de>
 *
 * gstregistrybinary.h: Header for registry handling
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

/* SUGGESTIONS AND TODO :
** ====================
** - Use a compressed registry, but would induce performance loss
** - Encrypt the registry, for security purpose, but would also reduce performances
*/

#ifndef __GST_REGISTRYBINARY_H__
#define __GST_REGISTRYBINARY_H__

#include <gst/gstpad.h>
#include <gst/gstregistry.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/*
 * GST_MAGIC_BINARY_REGISTRY_STR:
 *
 * A tag, written at the beginning of the file
 */
#define GST_MAGIC_BINARY_REGISTRY_STR "\xc0\xde\xf0\x0d"
/*
 * GST_MAGIC_BINARY_REGISTRY_LEN:
 *
 * length of the header tag.
 */
#define GST_MAGIC_BINARY_REGISTRY_LEN (4)

/*
 * GST_MAGIC_BINARY_VERSION_STR:
 *
 * The current version of the binary registry format.
 * This _must_ be updated whenever the registry format changes,
 * we currently use the core version where this change happened.
 */
#define GST_MAGIC_BINARY_VERSION_STR ("0.10.21.2")

/*
 * GST_MAGIC_BINARY_VERSION_LEN:
 *
 * length of the version string.
 */
#define GST_MAGIC_BINARY_VERSION_LEN (64)

typedef struct _GstBinaryRegistryMagic
{
  gchar magic[GST_MAGIC_BINARY_REGISTRY_LEN];
  gchar version[GST_MAGIC_BINARY_VERSION_LEN];
} GstBinaryRegistryMagic;

/*
 * we reference strings directly from the plugins and in this case set CONST to
 * avoid freeing them
 */
enum {
  GST_BINARY_REGISTRY_FLAG_NONE = 0,
  GST_BINARY_REGISTRY_FLAG_CONST = 1
};

/*
 * GstBinaryChunk:
 *
 * Header for binary blobs
 */
typedef struct _GstBinaryChunk
{
  gpointer data;
  guint size;
  guint flags;
  gboolean align;
} GstBinaryChunk;

/*
 * GstBinaryPluginElement:
 *
 * @nfeatures: says how many binary plugin feature structures we will have
 * right after the structure itself.
 *
 * A structure containing (staticely) every information needed for a plugin
 */

typedef struct _GstBinaryPluginElement
{
  gulong file_size;
  gulong file_mtime;

  guint n_deps;

  guint nfeatures;
} GstBinaryPluginElement;

/* GstBinaryDep:
 */
typedef struct _GstBinaryDep
{
  guint flags;
  guint n_env_vars;
  guint n_paths;
  guint n_names;

  guint env_hash;
  guint stat_hash;
} GstBinaryDep;

/*
 * GstBinaryPluginFeature:
 * @rank: rank of the feature
 *
 * A structure containing the plugin features
 */
typedef struct _GstBinaryPluginFeature
{
  gulong rank;
} GstBinaryPluginFeature;

/*
 * GstBinaryElementFactory:
 * @npadtemplates: stores the number of GstBinaryPadTemplate structures
 * following the structure
 * @ninterfaces: stores the number of interface names following the structure
 * @nuriprotocols: stores the number of protocol strings following the structure
 *
 * A structure containing the element factory fields
 */
typedef struct _GstBinaryElementFactory
{
  GstBinaryPluginFeature plugin_feature;

  guint npadtemplates;
  guint ninterfaces;
  guint nuriprotocols;
} GstBinaryElementFactory;

/*
 * GstBinaryTypeFindFactory:
 * @nextensions: stores the number of typefind extensions
 *
 * A structure containing the element factory fields
 */
typedef struct _GstBinaryTypeFindFactory
{
  GstBinaryPluginFeature plugin_feature;

  guint nextensions;
} GstBinaryTypeFindFactory;

/*
 * GstBinaryPadTemplate:
 *
 * A structure containing the static pad templates of a plugin feature
 */
typedef struct _GstBinaryPadTemplate
{
  guint direction;	               /* Either 0:"sink" or 1:"src" */
  GstPadPresence presence;
} GstBinaryPadTemplate;


/* Function prototypes */
gboolean gst_registry_binary_write_cache(GstRegistry *registry, const char *location);
gboolean gst_registry_binary_read_cache(GstRegistry *registry, const char *location);

#endif /* !__GST_REGISTRYBINARY_H__ */

