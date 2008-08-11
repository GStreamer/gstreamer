/* GStreamer
 * Copyright (C) 2006 Josep Torra <josep@fluendo.com>
 *               2006 Mathieu Garcia <matthieu@fluendo.com>
 *               2006,2007 Stefan Kost <ensonic@users.sf.net>
 *               2008 Sebastian Dröge <slomo@circular-chaos.org>
 *
 * gstregistrybinary.c: GstRegistryBinary object, support routines
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

/* FIXME:
 * - keep registry binary blob and reference strings
 *   - don't free/unmmap contents when leaving gst_registry_binary_read_cache()
 *     - free at gst_deinit() / _priv_gst_registry_cleanup() ?
 *   - GstPlugin:
 *     - GST_PLUGIN_FLAG_CONST
 *   - GstPluginFeature, GstIndexFactory, GstElementFactory
 *     - needs Flags (GST_PLUGIN_FEATURE_FLAG_CONST)
 *     - can we turn loaded into flag?
 * - why do we collect a list of binary chunks and not write immediately
 *   - because we need to process subchunks, before we can set e.g. nr_of_items
 *     in parent chunk
 * - need more robustness
 *   - don't parse beyond mem-block size
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <errno.h>
#include <stdio.h>

#if defined (_MSC_VER) && _MSC_VER >= 1400
#include <io.h>
#endif

#include <gst/gst_private.h>
#include <gst/gstconfig.h>
#include <gst/gstelement.h>
#include <gst/gsttypefind.h>
#include <gst/gsttypefindfactory.h>
#include <gst/gsturi.h>
#include <gst/gstinfo.h>
#include <gst/gstenumtypes.h>
#include <gst/gstpadtemplate.h>

#include <gst/gstregistrybinary.h>

#include <glib/gstdio.h>        /* for g_stat(), g_mapped_file(), ... */

#include "glib-compat-private.h"


#define GST_CAT_DEFAULT GST_CAT_REGISTRY

/* macros */

#define unpack_element(_inptr, _outptr, _element)  \
  _outptr = (_element *) _inptr; \
  _inptr += sizeof (_element)

#define unpack_const_string(_inptr, _outptr) \
  _outptr = g_intern_string ((const gchar *)_inptr); \
  _inptr += strlen(_outptr) + 1

#define unpack_string(_inptr, _outptr)  \
  _outptr = g_strdup ((gchar *)_inptr); \
  _inptr += strlen(_outptr) + 1

#define ALIGNMENT            (sizeof (void *))
#define alignment(_address)  (gsize)_address%ALIGNMENT
#define align(_ptr)          _ptr += (( alignment(_ptr) == 0) ? 0 : ALIGNMENT-alignment(_ptr))


/* CRC32 calculation */

/* The crc32() function is copyrighted and licensed as specified below.
 * This only applies to this single function:

  crc32.c -- compute the CRC-32 of a data stream
  Copyright (C) 1995-1998 Mark Adler

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.

  Jean-loup Gailly        Mark Adler
  jloup@gzip.org          madler@alumni.caltech.edu
*/

/* Changes for gstreamer:
 * - Changed to use GLib data types
 * - Change function name to _gst_crc32
 * - Return the old CRC instead of 0 when len or buf are 0/NULL
 */

/* ========================================================================
 * Table of CRC-32's of all single-byte values (made by make_crc_table)
 */
static const guint32 crc_table[256] = {
  0x00000000L, 0x77073096L, 0xee0e612cL, 0x990951baL, 0x076dc419L,
  0x706af48fL, 0xe963a535L, 0x9e6495a3L, 0x0edb8832L, 0x79dcb8a4L,
  0xe0d5e91eL, 0x97d2d988L, 0x09b64c2bL, 0x7eb17cbdL, 0xe7b82d07L,
  0x90bf1d91L, 0x1db71064L, 0x6ab020f2L, 0xf3b97148L, 0x84be41deL,
  0x1adad47dL, 0x6ddde4ebL, 0xf4d4b551L, 0x83d385c7L, 0x136c9856L,
  0x646ba8c0L, 0xfd62f97aL, 0x8a65c9ecL, 0x14015c4fL, 0x63066cd9L,
  0xfa0f3d63L, 0x8d080df5L, 0x3b6e20c8L, 0x4c69105eL, 0xd56041e4L,
  0xa2677172L, 0x3c03e4d1L, 0x4b04d447L, 0xd20d85fdL, 0xa50ab56bL,
  0x35b5a8faL, 0x42b2986cL, 0xdbbbc9d6L, 0xacbcf940L, 0x32d86ce3L,
  0x45df5c75L, 0xdcd60dcfL, 0xabd13d59L, 0x26d930acL, 0x51de003aL,
  0xc8d75180L, 0xbfd06116L, 0x21b4f4b5L, 0x56b3c423L, 0xcfba9599L,
  0xb8bda50fL, 0x2802b89eL, 0x5f058808L, 0xc60cd9b2L, 0xb10be924L,
  0x2f6f7c87L, 0x58684c11L, 0xc1611dabL, 0xb6662d3dL, 0x76dc4190L,
  0x01db7106L, 0x98d220bcL, 0xefd5102aL, 0x71b18589L, 0x06b6b51fL,
  0x9fbfe4a5L, 0xe8b8d433L, 0x7807c9a2L, 0x0f00f934L, 0x9609a88eL,
  0xe10e9818L, 0x7f6a0dbbL, 0x086d3d2dL, 0x91646c97L, 0xe6635c01L,
  0x6b6b51f4L, 0x1c6c6162L, 0x856530d8L, 0xf262004eL, 0x6c0695edL,
  0x1b01a57bL, 0x8208f4c1L, 0xf50fc457L, 0x65b0d9c6L, 0x12b7e950L,
  0x8bbeb8eaL, 0xfcb9887cL, 0x62dd1ddfL, 0x15da2d49L, 0x8cd37cf3L,
  0xfbd44c65L, 0x4db26158L, 0x3ab551ceL, 0xa3bc0074L, 0xd4bb30e2L,
  0x4adfa541L, 0x3dd895d7L, 0xa4d1c46dL, 0xd3d6f4fbL, 0x4369e96aL,
  0x346ed9fcL, 0xad678846L, 0xda60b8d0L, 0x44042d73L, 0x33031de5L,
  0xaa0a4c5fL, 0xdd0d7cc9L, 0x5005713cL, 0x270241aaL, 0xbe0b1010L,
  0xc90c2086L, 0x5768b525L, 0x206f85b3L, 0xb966d409L, 0xce61e49fL,
  0x5edef90eL, 0x29d9c998L, 0xb0d09822L, 0xc7d7a8b4L, 0x59b33d17L,
  0x2eb40d81L, 0xb7bd5c3bL, 0xc0ba6cadL, 0xedb88320L, 0x9abfb3b6L,
  0x03b6e20cL, 0x74b1d29aL, 0xead54739L, 0x9dd277afL, 0x04db2615L,
  0x73dc1683L, 0xe3630b12L, 0x94643b84L, 0x0d6d6a3eL, 0x7a6a5aa8L,
  0xe40ecf0bL, 0x9309ff9dL, 0x0a00ae27L, 0x7d079eb1L, 0xf00f9344L,
  0x8708a3d2L, 0x1e01f268L, 0x6906c2feL, 0xf762575dL, 0x806567cbL,
  0x196c3671L, 0x6e6b06e7L, 0xfed41b76L, 0x89d32be0L, 0x10da7a5aL,
  0x67dd4accL, 0xf9b9df6fL, 0x8ebeeff9L, 0x17b7be43L, 0x60b08ed5L,
  0xd6d6a3e8L, 0xa1d1937eL, 0x38d8c2c4L, 0x4fdff252L, 0xd1bb67f1L,
  0xa6bc5767L, 0x3fb506ddL, 0x48b2364bL, 0xd80d2bdaL, 0xaf0a1b4cL,
  0x36034af6L, 0x41047a60L, 0xdf60efc3L, 0xa867df55L, 0x316e8eefL,
  0x4669be79L, 0xcb61b38cL, 0xbc66831aL, 0x256fd2a0L, 0x5268e236L,
  0xcc0c7795L, 0xbb0b4703L, 0x220216b9L, 0x5505262fL, 0xc5ba3bbeL,
  0xb2bd0b28L, 0x2bb45a92L, 0x5cb36a04L, 0xc2d7ffa7L, 0xb5d0cf31L,
  0x2cd99e8bL, 0x5bdeae1dL, 0x9b64c2b0L, 0xec63f226L, 0x756aa39cL,
  0x026d930aL, 0x9c0906a9L, 0xeb0e363fL, 0x72076785L, 0x05005713L,
  0x95bf4a82L, 0xe2b87a14L, 0x7bb12baeL, 0x0cb61b38L, 0x92d28e9bL,
  0xe5d5be0dL, 0x7cdcefb7L, 0x0bdbdf21L, 0x86d3d2d4L, 0xf1d4e242L,
  0x68ddb3f8L, 0x1fda836eL, 0x81be16cdL, 0xf6b9265bL, 0x6fb077e1L,
  0x18b74777L, 0x88085ae6L, 0xff0f6a70L, 0x66063bcaL, 0x11010b5cL,
  0x8f659effL, 0xf862ae69L, 0x616bffd3L, 0x166ccf45L, 0xa00ae278L,
  0xd70dd2eeL, 0x4e048354L, 0x3903b3c2L, 0xa7672661L, 0xd06016f7L,
  0x4969474dL, 0x3e6e77dbL, 0xaed16a4aL, 0xd9d65adcL, 0x40df0b66L,
  0x37d83bf0L, 0xa9bcae53L, 0xdebb9ec5L, 0x47b2cf7fL, 0x30b5ffe9L,
  0xbdbdf21cL, 0xcabac28aL, 0x53b39330L, 0x24b4a3a6L, 0xbad03605L,
  0xcdd70693L, 0x54de5729L, 0x23d967bfL, 0xb3667a2eL, 0xc4614ab8L,
  0x5d681b02L, 0x2a6f2b94L, 0xb40bbe37L, 0xc30c8ea1L, 0x5a05df1bL,
  0x2d02ef8dL
};

/* ========================================================================= */
#define DO1(buf) crc = crc_table[((gint)crc ^ (*buf++)) & 0xff] ^ (crc >> 8);
#define DO2(buf)  DO1(buf); DO1(buf);
#define DO4(buf)  DO2(buf); DO2(buf);
#define DO8(buf)  DO4(buf); DO4(buf);

/* ========================================================================= */
static guint32
_gst_crc32 (guint32 crc, const gchar * buf, guint len)
{
  if (buf == NULL || len == 0)
    return crc;

  crc = crc ^ 0xffffffffL;
  while (len >= 8) {
    DO8 (buf);
    len -= 8;
  }
  if (len)
    do {
      DO1 (buf);
    } while (--len);
  return crc ^ 0xffffffffL;
}

#undef DO1
#undef DO2
#undef DO4
#undef DO8

/* Registry saving */

#ifdef G_OS_WIN32
/* On win32, we can't use g_mkstmp(), because of cross-DLL file I/O problems.
 * So, we just create the entire binary registry in memory, then write it out
 * with g_file_set_contents(), which creates a temporary file internally
 */

typedef struct BinaryRegistryCache
{
  const char *location;
  guint8 *mem;
  gssize len;
} BinaryRegistryCache;

static BinaryRegistryCache *
gst_registry_binary_cache_init (GstRegistry * registry, const char *location)
{
  BinaryRegistryCache *cache = g_new0 (BinaryRegistryCache, 1);
  cache->location = location;
  return cache;
}

static int
gst_registry_binary_cache_write (GstRegistry * registry,
    BinaryRegistryCache * cache, unsigned long offset,
    const void *data, int length)
{
  cache->len = MAX (offset + length, cache->len);
  cache->mem = g_realloc (cache->mem, cache->len);

  memcpy (cache->mem + offset, data, length);

  return length;
}

static gboolean
gst_registry_binary_cache_finish (GstRegistry * registry,
    BinaryRegistryCache * cache, gboolean success)
{
  gboolean ret = TRUE;
  GError *error = NULL;
  if (!g_file_set_contents (cache->location, (const gchar *) cache->mem,
          cache->len, &error)) {
    GST_ERROR ("Failed to write to cache file: %s", error->message);
    g_error_free (error);
    ret = FALSE;
  }

  g_free (cache->mem);
  g_free (cache);
  return ret;
}

#else
typedef struct BinaryRegistryCache
{
  const char *location;
  char *tmp_location;
  unsigned long currentoffset;
} BinaryRegistryCache;

static BinaryRegistryCache *
gst_registry_binary_cache_init (GstRegistry * registry, const char *location)
{
  BinaryRegistryCache *cache = g_new0 (BinaryRegistryCache, 1);

  cache->location = location;
  cache->tmp_location = g_strconcat (location, ".tmpXXXXXX", NULL);
  registry->cache_file = g_mkstemp (cache->tmp_location);
  if (registry->cache_file == -1) {
    gchar *dir;

    /* oops, I bet the directory doesn't exist */
    dir = g_path_get_dirname (location);
    g_mkdir_with_parents (dir, 0777);
    g_free (dir);

    /* the previous g_mkstemp call overwrote the XXXXXX placeholder ... */
    g_free (cache->tmp_location);
    cache->tmp_location = g_strconcat (location, ".tmpXXXXXX", NULL);
    registry->cache_file = g_mkstemp (cache->tmp_location);

    if (registry->cache_file == -1) {
      GST_DEBUG ("g_mkstemp() failed: %s", g_strerror (errno));
      g_free (cache->tmp_location);
      g_free (cache);
      return NULL;
    }
  }

  return cache;
}

static int
gst_registry_binary_cache_write (GstRegistry * registry,
    BinaryRegistryCache * cache, unsigned long offset,
    const void *data, int length)
{
  long written;
  if (offset != cache->currentoffset) {
    if (lseek (registry->cache_file, offset, SEEK_SET) != 0) {
      GST_ERROR ("Seeking to new offset failed");
      return FALSE;
    }
    cache->currentoffset = offset;
  }

  written = write (registry->cache_file, data, length);
  if (written != length) {
    GST_ERROR ("Failed to write to cache file");
  }
  cache->currentoffset += written;

  return written;
}

static gboolean
gst_registry_binary_cache_finish (GstRegistry * registry,
    BinaryRegistryCache * cache, gboolean success)
{
  if (close (registry->cache_file) < 0)
    goto close_failed;

  if (success) {
    /* Only do the rename if we wrote the entire file successfully */
    if (g_rename (cache->tmp_location, cache->location) < 0)
      goto rename_failed;
  }

  g_free (cache->tmp_location);
  g_free (cache);
  GST_INFO ("Wrote binary registry cache");
  return TRUE;

fail_after_close:
  {
    g_remove (cache->tmp_location);
    g_free (cache->tmp_location);
    g_free (cache);
    return FALSE;
  }
close_failed:
  {
    GST_ERROR ("close() failed: %s", g_strerror (errno));
    goto fail_after_close;
  }
rename_failed:
  {
    GST_ERROR ("g_rename() failed: %s", g_strerror (errno));
    goto fail_after_close;
  }
}
#endif

/*
 * gst_registry_binary_write_chunk:
 *
 * Write from a memory location to the registry cache file
 *
 * Returns: %TRUE for success
 */
inline static gboolean
gst_registry_binary_write_chunk (GstRegistry * registry,
    BinaryRegistryCache * cache, const void *mem,
    const gssize size, unsigned long *file_position, gboolean align,
    guint32 * crc32)
{
  gchar padder[ALIGNMENT] = { 0, };
  int padsize = 0;

  /* Padding to insert the struct that requiere word alignment */
  if ((align) && (alignment (*file_position) != 0)) {
    padsize = ALIGNMENT - alignment (*file_position);
    if (gst_registry_binary_cache_write (registry, cache, *file_position,
            padder, padsize) != padsize) {
      GST_ERROR ("Failed to write binary registry padder");
      return FALSE;
    }
    if (padsize > 0)
      *crc32 = _gst_crc32 (*crc32, padder, padsize);
    *file_position += padsize;
  }

  if (gst_registry_binary_cache_write (registry, cache, *file_position,
          mem, size) != size) {
    GST_ERROR ("Failed to write binary registry element");
    return FALSE;
  }
  if (size > 0)
    *crc32 = _gst_crc32 (*crc32, mem, size);

  *file_position += size;

  return TRUE;
}


/*
 * gst_registry_binary_initialize_magic:
 *
 * Initialize the GstBinaryRegistryMagic, setting both our magic number and
 * gstreamer major/minor version
 */
inline static gboolean
gst_registry_binary_initialize_magic (GstBinaryRegistryMagic * m)
{
  memset (m, 0, sizeof (GstBinaryRegistryMagic));

  if (!strncpy (m->magic, GST_MAGIC_BINARY_REGISTRY_STR,
          GST_MAGIC_BINARY_REGISTRY_LEN)
      || !strncpy (m->version, GST_MAGIC_BINARY_VERSION_STR,
          GST_MAGIC_BINARY_VERSION_LEN)) {
    GST_ERROR ("Failed to write magic to the registry magic structure");
    return FALSE;
  }

  m->crc32 = 0;

  return TRUE;
}


/*
 * gst_registry_binary_save_const_string:
 *
 * Store a const string in a binary chunk.
 *
 * Returns: %TRUE for success
 */
inline static gboolean
gst_registry_binary_save_const_string (GList ** list, const gchar * str)
{
  GstBinaryChunk *chunk;

  if (G_UNLIKELY (str == NULL)) {
    GST_ERROR ("unexpected NULL string in plugin or plugin feature data");
    str = "";
  }

  chunk = g_malloc (sizeof (GstBinaryChunk));
  chunk->data = (gpointer) str;
  chunk->size = strlen ((gchar *) chunk->data) + 1;
  chunk->flags = GST_BINARY_REGISTRY_FLAG_CONST;
  chunk->align = FALSE;
  *list = g_list_prepend (*list, chunk);
  return TRUE;
}

/*
 * gst_registry_binary_save_string:
 *
 * Store a string in a binary chunk.
 *
 * Returns: %TRUE for success
 */
inline static gboolean
gst_registry_binary_save_string (GList ** list, gchar * str)
{
  GstBinaryChunk *chunk;

  chunk = g_malloc (sizeof (GstBinaryChunk));
  chunk->data = str;
  chunk->size = strlen ((gchar *) chunk->data) + 1;
  chunk->flags = GST_BINARY_REGISTRY_FLAG_NONE;
  chunk->align = FALSE;
  *list = g_list_prepend (*list, chunk);
  return TRUE;
}


/*
 * gst_registry_binary_save_data:
 *
 * Store some data in a binary chunk.
 *
 * Returns: the initialized chunk
 */
inline static GstBinaryChunk *
gst_registry_binary_make_data (gpointer data, gulong size)
{
  GstBinaryChunk *chunk;

  chunk = g_malloc (sizeof (GstBinaryChunk));
  chunk->data = data;
  chunk->size = size;
  chunk->flags = GST_BINARY_REGISTRY_FLAG_NONE;
  chunk->align = TRUE;
  return chunk;
}


/*
 * gst_registry_binary_save_pad_template:
 *
 * Store pad_templates in binary chunks.
 *
 * Returns: %TRUE for success
 */
static gboolean
gst_registry_binary_save_pad_template (GList ** list,
    GstStaticPadTemplate * template)
{
  GstBinaryPadTemplate *pt;
  GstBinaryChunk *chk;

  pt = g_malloc (sizeof (GstBinaryPadTemplate));
  chk = gst_registry_binary_make_data (pt, sizeof (GstBinaryPadTemplate));

  pt->presence = template->presence;
  pt->direction = template->direction;

  /* pack pad template strings */
  gst_registry_binary_save_const_string (list,
      (gchar *) (template->static_caps.string));
  gst_registry_binary_save_const_string (list, template->name_template);

  *list = g_list_prepend (*list, chk);

  return TRUE;
}


/*
 * gst_registry_binary_save_feature:
 *
 * Store features in binary chunks.
 *
 * Returns: %TRUE for success
 */
static gboolean
gst_registry_binary_save_feature (GList ** list, GstPluginFeature * feature)
{
  const gchar *type_name = g_type_name (G_OBJECT_TYPE (feature));
  GstBinaryPluginFeature *pf = NULL;
  GstBinaryChunk *chk = NULL;
  GList *walk;

  if (!type_name) {
    GST_ERROR ("NULL feature type_name, aborting.");
    return FALSE;
  }

  if (GST_IS_ELEMENT_FACTORY (feature)) {
    GstBinaryElementFactory *ef;
    GstElementFactory *factory = GST_ELEMENT_FACTORY (feature);

    ef = g_malloc (sizeof (GstBinaryElementFactory));
    chk = gst_registry_binary_make_data (ef, sizeof (GstBinaryElementFactory));
    ef->npadtemplates = ef->ninterfaces = ef->nuriprotocols = 0;
    pf = (GstBinaryPluginFeature *) ef;

    /* save interfaces */
    for (walk = factory->interfaces; walk;
        walk = g_list_next (walk), ef->ninterfaces++) {
      gst_registry_binary_save_const_string (list, (gchar *) walk->data);
    }
    GST_DEBUG ("Saved %d Interfaces", ef->ninterfaces);
    /* save uritypes */
    if (GST_URI_TYPE_IS_VALID (factory->uri_type)) {
      if (factory->uri_protocols && *factory->uri_protocols) {
        GstBinaryChunk *subchk;
        gchar **protocol;

        subchk =
            gst_registry_binary_make_data (&factory->uri_type,
            sizeof (factory->uri_type));
        subchk->flags = GST_BINARY_REGISTRY_FLAG_CONST;

        protocol = factory->uri_protocols;
        while (*protocol) {
          gst_registry_binary_save_const_string (list, *protocol++);
          ef->nuriprotocols++;
        }
        *list = g_list_prepend (*list, subchk);
        GST_DEBUG ("Saved %d UriTypes", ef->nuriprotocols);
      } else {
        g_warning ("GStreamer feature '%s' is URI handler but does not provide"
            " any protocols it can handle", feature->name);
      }
    }

    /* save pad-templates */
    for (walk = factory->staticpadtemplates; walk;
        walk = g_list_next (walk), ef->npadtemplates++) {
      GstStaticPadTemplate *template = walk->data;

      if (!gst_registry_binary_save_pad_template (list, template)) {
        GST_ERROR ("Can't fill pad template, aborting.");
        goto fail;
      }
    }

    /* pack element factory strings */
    gst_registry_binary_save_const_string (list, factory->details.author);
    gst_registry_binary_save_const_string (list, factory->details.description);
    gst_registry_binary_save_const_string (list, factory->details.klass);
    gst_registry_binary_save_const_string (list, factory->details.longname);
  } else if (GST_IS_TYPE_FIND_FACTORY (feature)) {
    GstBinaryTypeFindFactory *tff;
    GstTypeFindFactory *factory = GST_TYPE_FIND_FACTORY (feature);
    gchar *str;

    /* we copy the caps here so we can simplify them before saving. This is a lot
     * faster when loading them later on */
    GstCaps *copy = gst_caps_copy (factory->caps);

    tff = g_malloc (sizeof (GstBinaryTypeFindFactory));
    chk =
        gst_registry_binary_make_data (tff, sizeof (GstBinaryTypeFindFactory));
    tff->nextensions = 0;
    pf = (GstBinaryPluginFeature *) tff;

    /* save extensions */
    if (factory->extensions) {
      while (factory->extensions[tff->nextensions]) {
        gst_registry_binary_save_const_string (list,
            factory->extensions[tff->nextensions++]);
      }
    }
    /* save caps */
    gst_caps_do_simplify (copy);
    str = gst_caps_to_string (copy);
    gst_caps_unref (copy);
    gst_registry_binary_save_string (list, str);
  } else if (GST_IS_INDEX_FACTORY (feature)) {
    GstIndexFactory *factory = GST_INDEX_FACTORY (feature);

    pf = g_malloc (sizeof (GstBinaryPluginFeature));
    chk = gst_registry_binary_make_data (pf, sizeof (GstBinaryPluginFeature));
    pf->rank = feature->rank;

    /* pack element factory strings */
    gst_registry_binary_save_const_string (list, factory->longdesc);
  } else {
    GST_WARNING ("unhandled feature type '%s'", type_name);
  }

  if (pf) {
    pf->rank = feature->rank;
    *list = g_list_prepend (*list, chk);

    /* pack plugin feature strings */
    gst_registry_binary_save_const_string (list, feature->name);
    gst_registry_binary_save_const_string (list, (gchar *) type_name);

    return TRUE;
  }

  /* Errors */
fail:
  g_free (chk);
  g_free (pf);
  return FALSE;
}


/*
 * gst_registry_binary_save_plugin:
 *
 * Adapt a GstPlugin to our GstBinaryPluginElement structure, and write it to
 * the registry file.
 */
static gboolean
gst_registry_binary_save_plugin (GList ** list, GstRegistry * registry,
    GstPlugin * plugin)
{
  GstBinaryPluginElement *pe;
  GstBinaryChunk *chk;
  GList *plugin_features = NULL;
  GList *walk;

  pe = g_malloc (sizeof (GstBinaryPluginElement));
  chk = gst_registry_binary_make_data (pe, sizeof (GstBinaryPluginElement));

  pe->file_size = plugin->file_size;
  pe->file_mtime = plugin->file_mtime;
  pe->nfeatures = 0;

  /* pack plugin features */
  plugin_features =
      gst_registry_get_feature_list_by_plugin (registry, plugin->desc.name);
  for (walk = plugin_features; walk; walk = g_list_next (walk), pe->nfeatures++) {
    GstPluginFeature *feature = GST_PLUGIN_FEATURE (walk->data);

    if (!gst_registry_binary_save_feature (list, feature)) {
      GST_ERROR ("Can't fill plugin feature, aborting.");
      goto fail;
    }
  }
  GST_DEBUG ("Save plugin '%s' with %d feature(s)", plugin->desc.name,
      pe->nfeatures);

  gst_plugin_feature_list_free (plugin_features);

  /* pack plugin element strings */
  gst_registry_binary_save_const_string (list, plugin->desc.origin);
  gst_registry_binary_save_const_string (list, plugin->desc.package);
  gst_registry_binary_save_const_string (list, plugin->desc.source);
  gst_registry_binary_save_const_string (list, plugin->desc.license);
  gst_registry_binary_save_const_string (list, plugin->desc.version);
  gst_registry_binary_save_const_string (list, plugin->filename);
  gst_registry_binary_save_const_string (list, plugin->desc.description);
  gst_registry_binary_save_const_string (list, plugin->desc.name);

  *list = g_list_prepend (*list, chk);

  GST_DEBUG ("Found %d features in plugin \"%s\"", pe->nfeatures,
      plugin->desc.name);
  return TRUE;

  /* Errors */
fail:
  gst_plugin_feature_list_free (plugin_features);
  g_free (chk);
  g_free (pe);
  return FALSE;
}

/**
 * gst_registry_binary_write_cache:
 * @registry: a #GstRegistry
 * @location: a filename
 *
 * Write the @registry to a cache to file at given @location.
 * 
 * Returns: %TRUE on success.
 */
gboolean
gst_registry_binary_write_cache (GstRegistry * registry, const char *location)
{
  GList *walk;
  GstBinaryRegistryMagic magic;
  GList *to_write = NULL;
  unsigned long file_position = 0;
  BinaryRegistryCache *cache;

  GST_INFO ("Building binary registry cache image");

  g_return_val_if_fail (GST_IS_REGISTRY (registry), FALSE);

  if (!gst_registry_binary_initialize_magic (&magic))
    goto fail;

  /* iterate trough the list of plugins and fit them into binary structures */
  for (walk = registry->plugins; walk; walk = g_list_next (walk)) {
    GstPlugin *plugin = GST_PLUGIN (walk->data);

    if (!plugin->filename)
      continue;

    if (plugin->flags & GST_PLUGIN_FLAG_CACHED) {
      int ret;
      struct stat statbuf;

      ret = g_stat (plugin->filename, &statbuf);
      if ((ret = g_stat (plugin->filename, &statbuf)) < 0 ||
          plugin->file_mtime != statbuf.st_mtime ||
          plugin->file_size != statbuf.st_size)
        continue;
    }

    if (!gst_registry_binary_save_plugin (&to_write, registry, plugin)) {
      GST_ERROR ("Can't write binary plugin information for \"%s\"",
          plugin->filename);
    }
  }

  GST_INFO ("Writing binary registry cache");

  cache = gst_registry_binary_cache_init (registry, location);
  if (!cache)
    goto fail_free_list;

  /* write magic */
  if (gst_registry_binary_cache_write (registry, cache, file_position,
          &magic, sizeof (GstBinaryRegistryMagic)) !=
      sizeof (GstBinaryRegistryMagic)) {
    GST_ERROR ("Failed to write binary registry magic");
    goto fail_free_list;
  }
  file_position += sizeof (GstBinaryRegistryMagic);

  /* write out data chunks */
  for (walk = to_write; walk; walk = g_list_next (walk)) {
    GstBinaryChunk *cur = walk->data;

    if (!gst_registry_binary_write_chunk (registry, cache, cur->data, cur->size,
            &file_position, cur->align, &magic.crc32)) {
      if (!(cur->flags & GST_BINARY_REGISTRY_FLAG_CONST))
        g_free (cur->data);
      g_free (cur);
      walk->data = NULL;
      goto fail_free_list;
    }
    if (!(cur->flags & GST_BINARY_REGISTRY_FLAG_CONST))
      g_free (cur->data);
    g_free (cur);
    walk->data = NULL;
  }
  g_list_free (to_write);

  if (gst_registry_binary_cache_write (registry, cache, 0, &magic,
          sizeof (GstBinaryRegistryMagic)) != sizeof (GstBinaryRegistryMagic)) {
    GST_ERROR ("Failed to rewrite binary registry magic");
    return FALSE;
  }

  if (!gst_registry_binary_cache_finish (registry, cache, TRUE))
    return FALSE;

  return TRUE;

  /* Errors */
fail_free_list:
  {
    for (walk = to_write; walk; walk = g_list_next (walk)) {
      GstBinaryChunk *cur = walk->data;

      if (!(cur->flags & GST_BINARY_REGISTRY_FLAG_CONST))
        g_free (cur->data);
      g_free (cur);
    }
    g_list_free (to_write);

    (void) gst_registry_binary_cache_finish (registry, cache, FALSE);
    /* fall through */
  }
fail:
  {
    return FALSE;
  }
}


/* Registry loading */

/*
 * gst_registry_binary_check_magic:
 *
 * Check GstBinaryRegistryMagic validity.
 * Return < 0 if something is wrong, -2 means
 * that just the version of the registry is out of
 * date, -1 is a general failure.
 */
static gint
gst_registry_binary_check_magic (gchar ** in, gsize size)
{
  GstBinaryRegistryMagic *m;
  guint32 crc32 = 0;

  align (*in);
  GST_DEBUG ("Reading/casting for GstBinaryRegistryMagic at address %p", *in);
  unpack_element (*in, m, GstBinaryRegistryMagic);

  if (m == NULL || m->magic == NULL || m->version == NULL) {
    GST_WARNING ("Binary registry magic structure is broken");
    return -1;
  }
  if (strncmp (m->magic, GST_MAGIC_BINARY_REGISTRY_STR,
          GST_MAGIC_BINARY_REGISTRY_LEN) != 0) {
    GST_WARNING
        ("Binary registry magic is different : %02x%02x%02x%02x != %02x%02x%02x%02x",
        GST_MAGIC_BINARY_REGISTRY_STR[0] & 0xff,
        GST_MAGIC_BINARY_REGISTRY_STR[1] & 0xff,
        GST_MAGIC_BINARY_REGISTRY_STR[2] & 0xff,
        GST_MAGIC_BINARY_REGISTRY_STR[3] & 0xff, m->magic[0] & 0xff,
        m->magic[1] & 0xff, m->magic[2] & 0xff, m->magic[3] & 0xff);
    return -1;
  }
  if (strncmp (m->version, GST_MAGIC_BINARY_VERSION_STR,
          GST_MAGIC_BINARY_VERSION_LEN)) {
    GST_WARNING ("Binary registry magic version is different : %s != %s",
        GST_MAGIC_BINARY_VERSION_STR, m->version);
    return -2;
  }

  crc32 = _gst_crc32 (crc32, *in, size - sizeof (GstBinaryRegistryMagic));
  if (crc32 != m->crc32) {
    GST_WARNING ("Binary registry CRC32 different: 0x%x != 0x%x\n", crc32,
        m->crc32);
    return -1;
  }

  return 0;
}


/*
 * gst_registry_binary_load_pad_template:
 *
 * Make a new GstStaticPadTemplate from current GstBinaryPadTemplate structure
 *
 * Returns: new GstStaticPadTemplate
 */
static gboolean
gst_registry_binary_load_pad_template (GstElementFactory * factory, gchar ** in)
{
  GstBinaryPadTemplate *pt;
  GstStaticPadTemplate *template;

  align (*in);
  GST_DEBUG ("Reading/casting for GstBinaryPadTemplate at address %p", *in);
  unpack_element (*in, pt, GstBinaryPadTemplate);

  template = g_new0 (GstStaticPadTemplate, 1);
  template->presence = pt->presence;
  template->direction = pt->direction;

  /* unpack pad template strings */
  unpack_const_string (*in, template->name_template);
  unpack_string (*in, template->static_caps.string);

  __gst_element_factory_add_static_pad_template (factory, template);
  GST_DEBUG ("Added pad_template %s", template->name_template);

  return TRUE;
}


/*
 * gst_registry_binary_load_feature:
 *
 * Make a new GstPluginFeature from current binary plugin feature structure
 *
 * Returns: new GstPluginFeature
 */
static gboolean
gst_registry_binary_load_feature (GstRegistry * registry, gchar ** in,
    const gchar * plugin_name)
{
  GstBinaryPluginFeature *pf = NULL;
  GstPluginFeature *feature;
  gchar *type_name = NULL, *str;
  GType type;
  guint i;

  /* unpack plugin feature strings */
  unpack_string (*in, type_name);

  if (!type_name || !*(type_name)) {
    GST_ERROR ("No feature type name");
    return FALSE;
  }

  GST_DEBUG ("Plugin '%s' feature typename : '%s'", plugin_name, type_name);

  if (!(type = g_type_from_name (type_name))) {
    GST_ERROR ("Unknown type from typename '%s' for plugin '%s'", type_name,
        plugin_name);
    return FALSE;
  }
  if ((feature = g_object_new (type, NULL)) == NULL) {
    GST_ERROR ("Can't create feature from type");
    return FALSE;
  }

  if (!GST_IS_PLUGIN_FEATURE (feature)) {
    GST_ERROR ("typename : '%s' is not a plugin feature", type_name);
    goto fail;
  }

  /* unpack more plugin feature strings */
  unpack_string (*in, feature->name);

  if (GST_IS_ELEMENT_FACTORY (feature)) {
    GstBinaryElementFactory *ef;
    GstElementFactory *factory = GST_ELEMENT_FACTORY (feature);

    align (*in);
    GST_LOG ("Reading/casting for GstBinaryElementFactory at address %p", *in);
    unpack_element (*in, ef, GstBinaryElementFactory);
    pf = (GstBinaryPluginFeature *) ef;

    /* unpack element factory strings */
    unpack_string (*in, factory->details.longname);
    unpack_string (*in, factory->details.klass);
    unpack_string (*in, factory->details.description);
    unpack_string (*in, factory->details.author);
    GST_DEBUG ("Element factory : '%s' with npadtemplates=%d",
        factory->details.longname, ef->npadtemplates);

    /* load pad templates */
    for (i = 0; i < ef->npadtemplates; i++) {
      if (!gst_registry_binary_load_pad_template (factory, in)) {
        GST_ERROR ("Error while loading binary pad template");
        goto fail;
      }
    }

    /* load uritypes */
    if (ef->nuriprotocols) {
      GST_DEBUG ("Reading %d UriTypes at address %p", ef->nuriprotocols, *in);

      align (*in);
      factory->uri_type = *((guint *) * in);
      *in += sizeof (factory->uri_type);
      //unpack_element(*in, &factory->uri_type, factory->uri_type);

      factory->uri_protocols = g_new0 (gchar *, ef->nuriprotocols + 1);
      for (i = 0; i < ef->nuriprotocols; i++) {
        unpack_string (*in, str);
        factory->uri_protocols[i] = str;
      }
    }
    /* load interfaces */
    GST_DEBUG ("Reading %d Interfaces at address %p", ef->ninterfaces, *in);
    for (i = 0; i < ef->ninterfaces; i++) {
      unpack_string (*in, str);
      __gst_element_factory_add_interface (factory, str);
      g_free (str);
    }
  } else if (GST_IS_TYPE_FIND_FACTORY (feature)) {
    GstBinaryTypeFindFactory *tff;
    GstTypeFindFactory *factory = GST_TYPE_FIND_FACTORY (feature);

    align (*in);
    GST_DEBUG ("Reading/casting for GstBinaryPluginFeature at address %p", *in);
    unpack_element (*in, tff, GstBinaryTypeFindFactory);
    pf = (GstBinaryPluginFeature *) tff;

    /* load caps */
    unpack_string (*in, str);
    factory->caps = gst_caps_from_string (str);
    g_free (str);
    /* load extensions */
    if (tff->nextensions) {
      GST_DEBUG ("Reading %d Typefind extensions at address %p",
          tff->nextensions, *in);
      factory->extensions = g_new0 (gchar *, tff->nextensions + 1);
      for (i = 0; i < tff->nextensions; i++) {
        unpack_string (*in, str);
        factory->extensions[i] = str;
      }
    }
  } else if (GST_IS_INDEX_FACTORY (feature)) {
    GstIndexFactory *factory = GST_INDEX_FACTORY (feature);

    align (*in);
    GST_DEBUG ("Reading/casting for GstBinaryPluginFeature at address %p", *in);
    unpack_element (*in, pf, GstBinaryPluginFeature);

    /* unpack index factory strings */
    unpack_string (*in, factory->longdesc);
  } else {
    GST_WARNING ("unhandled factory type : %s", G_OBJECT_TYPE_NAME (feature));
    goto fail;
  }

  feature->rank = pf->rank;

  /* should already be the interned string, but better make sure */
  feature->plugin_name = g_intern_string (plugin_name);

  gst_registry_add_feature (registry, feature);
  GST_DEBUG ("Added feature %s", feature->name);

  g_free (type_name);
  return TRUE;

  /* Errors */
fail:
  g_free (type_name);
  if (GST_IS_OBJECT (feature))
    gst_object_unref (feature);
  else
    g_object_unref (feature);
  return FALSE;
}


/*
 * gst_registry_binary_load_plugin:
 *
 * Make a new GstPlugin from current GstBinaryPluginElement structure
 * and save it to the GstRegistry. Return an offset to the next
 * GstBinaryPluginElement structure.
 */
static gboolean
gst_registry_binary_load_plugin (GstRegistry * registry, gchar ** in)
{
  GstBinaryPluginElement *pe;
  GstPlugin *plugin = NULL;
  guint i;

  align (*in);
  GST_LOG ("Reading/casting for GstBinaryPluginElement at address %p", *in);
  unpack_element (*in, pe, GstBinaryPluginElement);

  plugin = g_object_new (GST_TYPE_PLUGIN, NULL);

  /* TODO: also set GST_PLUGIN_FLAG_CONST */
  plugin->flags |= GST_PLUGIN_FLAG_CACHED;
  plugin->file_mtime = pe->file_mtime;
  plugin->file_size = pe->file_size;

  /* unpack plugin element strings */
  unpack_const_string (*in, plugin->desc.name);
  unpack_string (*in, plugin->desc.description);
  unpack_string (*in, plugin->filename);
  unpack_const_string (*in, plugin->desc.version);
  unpack_const_string (*in, plugin->desc.license);
  unpack_const_string (*in, plugin->desc.source);
  unpack_const_string (*in, plugin->desc.package);
  unpack_const_string (*in, plugin->desc.origin);
  GST_LOG ("read strings for name='%s'", plugin->desc.name);
  GST_LOG ("  desc.description='%s'", plugin->desc.description);
  GST_LOG ("  filename='%s'", plugin->filename);
  GST_LOG ("  desc.version='%s'", plugin->desc.version);
  GST_LOG ("  desc.license='%s'", plugin->desc.license);
  GST_LOG ("  desc.source='%s'", plugin->desc.source);
  GST_LOG ("  desc.package='%s'", plugin->desc.package);
  GST_LOG ("  desc.origin='%s'", plugin->desc.origin);

  plugin->basename = g_path_get_basename (plugin->filename);

  /* Takes ownership of plugin */
  gst_registry_add_plugin (registry, plugin);
  GST_DEBUG ("Added plugin '%s' plugin with %d features from binary registry",
      plugin->desc.name, pe->nfeatures);
  for (i = 0; i < pe->nfeatures; i++) {
    if (!gst_registry_binary_load_feature (registry, in, plugin->desc.name)) {
      GST_ERROR ("Error while loading binary feature");
      goto fail;
    }
  }

  return TRUE;

  /* Errors */
fail:
  return FALSE;
}


/**
 * gst_registry_binary_read_cache:
 * @registry: a #GstRegistry
 * @location: a filename
 *
 * Read the contents of the binary cache file at @location into @registry.
 *
 * Returns: %TRUE on success.
 */
gboolean
gst_registry_binary_read_cache (GstRegistry * registry, const char *location)
{
  GMappedFile *mapped = NULL;
  GTimer *timer = NULL;
  gchar *contents = NULL;
  gchar *in = NULL;
  gdouble seconds;
  gsize size;
  GError *err = NULL;
  gboolean res = FALSE;
  gint check_magic_result;

  /* make sure these types exist */
  GST_TYPE_ELEMENT_FACTORY;
  GST_TYPE_TYPE_FIND_FACTORY;
  GST_TYPE_INDEX_FACTORY;

  timer = g_timer_new ();

  mapped = g_mapped_file_new (location, FALSE, &err);
  if (err != NULL) {
    GST_INFO ("Unable to mmap file %s : %s", location, err->message);
    g_error_free (err);
    err = NULL;

    g_file_get_contents (location, &contents, &size, &err);
    if (err != NULL) {
      GST_INFO ("Unable to read file %s : %s", location, err->message);
      g_timer_destroy (timer);
      g_error_free (err);
      return FALSE;
    }
  } else {
    if ((contents = g_mapped_file_get_contents (mapped)) == NULL) {
      GST_ERROR ("Can't load file %s : %s", location, g_strerror (errno));
      goto Error;
    }
    /* check length for header */
    size = g_mapped_file_get_length (mapped);
  }
  /* in is a cursor pointer, we initialize it with the begin of registry and is updated on each read */
  in = contents;
  GST_DEBUG ("File data at address %p", in);
  if (size < sizeof (GstBinaryRegistryMagic)) {
    GST_ERROR ("No or broken registry header");
    goto Error;
  }
  /* check if header is valid */
  if ((check_magic_result = gst_registry_binary_check_magic (&in, size)) < 0) {

    if (check_magic_result == -1)
      GST_ERROR
          ("Binary registry type not recognized (invalid magic) for file at %s",
          location);
    goto Error;
  }

  /* check if there are plugins in the file */

  if (!(((gsize) in + sizeof (GstBinaryPluginElement)) <
          (gsize) contents + size)) {
    GST_INFO ("No binary plugins structure to read");
    /* empty file, this is not an error */
  } else {
    for (;
        ((gsize) in + sizeof (GstBinaryPluginElement)) <
        (gsize) contents + size;) {
      GST_DEBUG ("reading binary registry %" G_GSIZE_FORMAT "(%x)/%"
          G_GSIZE_FORMAT, (gsize) in - (gsize) contents,
          (guint) ((gsize) in - (gsize) contents), size);
      if (!gst_registry_binary_load_plugin (registry, &in)) {
        GST_ERROR ("Problem while reading binary registry");
        goto Error;
      }
    }
  }

  g_timer_stop (timer);
  seconds = g_timer_elapsed (timer, NULL);

  GST_INFO ("loaded %s in %lf seconds", location, seconds);

  res = TRUE;
  /* TODO: once we re-use the pointers to registry contents return here */

Error:
  g_timer_destroy (timer);
  if (mapped) {
    g_mapped_file_free (mapped);
  } else {
    g_free (contents);
  }
  return res;
}


/* FIXME 0.11: these are here for backwards compatibility */

gboolean
gst_registry_xml_read_cache (GstRegistry * registry, const char *location)
{
  return FALSE;
}

gboolean
gst_registry_xml_write_cache (GstRegistry * registry, const char *location)
{
  return FALSE;
}
