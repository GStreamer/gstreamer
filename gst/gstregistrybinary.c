/* GStreamer
 * Copyright (C)      2006 Josep Torra <josep@fluendo.com>
 *                    2006 Mathieu Garcia  <matthieu@fluendo.com>
 *		      2006 Stefan Kost <ensonic@sonicpulse.de>
 *                    
 * gstregistrybinary.c: GstRegistryBinary object, support routines
 *
 * This library is free software; you can redistribute it and/or
 * modify it ulnder the terms of the GNU Library General Public
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
 * - Add random key to libgstreamer during build and only accept registry,
 *   if key matches (or is the version check enough)
 * - handle cases where we can't mmap
 * - keep registry binary blob and reference strings
 *   (need const flags in GstPlugin, etc.)
 * - why do we collect a list of binary chunks and not write immediately
 *   - because we need to process subchunks, before we can set e.g. nr_of_items
 *     in parnt chunk
 * - need more robustness
 *   - don't parse beyond mem-block size
 *   - include md5-sum ?
 */

#include <gst/gstregistrybinary.h>

#define GST_CAT_DEFAULT GST_CAT_REGISTRY

/* macros */

#define unpack_element(_inptr, _outptr, _element)  \
  _outptr = (_element *)_inptr; \
  _inptr += sizeof (_element)

#define unpack_string(_inptr, _outptr)  \
  _outptr = g_strdup ((gchar *)_inptr); \
  _inptr += strlen(_outptr) + 1

#if !GST_HAVE_UNALIGNED_ACCESS
#  define alignment32(_address)  (size_t)_address%4
#  define align32(_ptr)          _ptr += (( alignment32(_ptr) == 0) ? 0 : 4-alignment32(_ptr))
#else
#  define alignment32(_address)  0
#  define align32(_ptr)          do {} while(0)
#endif

/* Registry saving */

/*
 * gst_registry_binary_write:
 *
 * Write from a memory location to the registry cache file
 *
 * Returns: %TRUE for success
 */
inline static gboolean
gst_registry_binary_write (GstRegistry * registry, const void *mem,
    const ssize_t size, unsigned long *file_position, gboolean align)
{
#if !GST_HAVE_UNALIGNED_ACCESS
  gchar padder[] = { 0, 0, 0, 0 };
  int padsize = 0;

  /* Padding to insert the struct that requiere word alignment */
  if ((align) && (alignment32 (*file_position) != 0)) {
    padsize = 4 - alignment32 (*file_position);
    if (write (registry->cache_file, padder, padsize) != padsize) {
      GST_ERROR ("Failed to write binary registry padder");
      return FALSE;
    }
    *file_position = *file_position + padsize;
  }
#endif

  if (write (registry->cache_file, mem, size) != size) {
    GST_ERROR ("Failed to write binary registry element");
    return FALSE;
  }
  *file_position = *file_position + size;
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
  if (!strncpy (m->magic, GST_MAGIC_BINARY_REGISTRY_STR,
          GST_MAGIC_BINARY_REGISTRY_LEN)
      || !strncpy (m->version, GST_MAJORMINOR, GST_MAGIC_BINARY_VERSION_LEN)) {
    GST_ERROR ("Failed to write magic to the registry magic structure");
    return FALSE;
  }
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
  chunk->flags = GST_BINARY_REGISTRY_FLAG_CONST;
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
  gst_registry_binary_save_string (list,
      (gchar *) (template->static_caps.string));
  gst_registry_binary_save_string (list, template->name_template);

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
  GstBinaryPluginFeature *pf;
  GstBinaryChunk *chk;
  GList *walk;

  if (!type_name) {
    GST_ERROR ("NULL feature type_name, aborting.");
    return FALSE;
  }

  pf = g_malloc (sizeof (GstBinaryPluginFeature));
  chk = gst_registry_binary_make_data (pf, sizeof (GstBinaryPluginFeature));

  pf->rank = feature->rank;
  pf->npadtemplates = pf->ninterfaces = pf->nuriprotocols = 0;

  if (GST_IS_ELEMENT_FACTORY (feature)) {
    GstElementFactory *factory = GST_ELEMENT_FACTORY (feature);

    /* save interfaces */
    for (walk = factory->interfaces; walk;
        walk = g_list_next (walk), pf->ninterfaces++) {
      gst_registry_binary_save_string (list, (gchar *) walk->data);
    }
    GST_DEBUG ("Saved %d Interfaces", pf->ninterfaces);
    /* save uritypes */
    if (GST_URI_TYPE_IS_VALID (factory->uri_type)) {
      if (factory->uri_protocols) {
        GstBinaryChunk *subchk;
        gchar **protocol;

        subchk =
            gst_registry_binary_make_data (&factory->uri_type,
            sizeof (factory->uri_type));
        subchk->flags = GST_BINARY_REGISTRY_FLAG_CONST;

        protocol = factory->uri_protocols;
        while (*protocol) {
          gst_registry_binary_save_string (list, *protocol++);
          pf->nuriprotocols++;
        }
        *list = g_list_prepend (*list, subchk);
        GST_DEBUG ("Saved %d UriTypes", pf->nuriprotocols);
      } else {
        g_warning ("GStreamer feature '%s' is URI handler but does not provide"
            " any protocols it can handle", feature->name);
      }
    }

    /* save pad-templates */
    for (walk = factory->staticpadtemplates; walk;
        walk = g_list_next (walk), pf->npadtemplates++) {
      GstStaticPadTemplate *template = walk->data;

      if (!gst_registry_binary_save_pad_template (list, template)) {
        GST_ERROR ("Can't fill pad template, aborting.");
        goto fail;
      }
    }

    /* pack element factory strings */
    gst_registry_binary_save_string (list, factory->details.author);
    gst_registry_binary_save_string (list, factory->details.description);
    gst_registry_binary_save_string (list, factory->details.klass);
    gst_registry_binary_save_string (list, factory->details.longname);
  } else if (GST_IS_TYPE_FIND_FACTORY (feature)) {
    /* FIXME: save typefind */
  }
#ifndef GST_DISABLE_INDEX
  else if (GST_IS_INDEX_FACTORY (feature)) {
    /* FIXME: save indexers */
  }
#endif

  /* pack plugin feature strings */
  gst_registry_binary_save_string (list, feature->name);
  gst_registry_binary_save_string (list, (gchar *) type_name);

  *list = g_list_prepend (*list, chk);
  return TRUE;

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
  GST_DEBUG ("Save plugin '%s' with %d features", plugin->desc.name,
      pe->nfeatures);

  gst_plugin_feature_list_free (plugin_features);

  /* pack plugin element strings */
  gst_registry_binary_save_string (list, plugin->desc.origin);
  gst_registry_binary_save_string (list, plugin->desc.package);
  gst_registry_binary_save_string (list, plugin->desc.source);
  gst_registry_binary_save_string (list, plugin->desc.license);
  gst_registry_binary_save_string (list, plugin->desc.version);
  gst_registry_binary_save_string (list, plugin->filename);
  gst_registry_binary_save_string (list, plugin->desc.description);
  gst_registry_binary_save_string (list, plugin->desc.name);

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
 *
 * Write the cache to file. Part of the code was taken from gstregistryxml.c
 */
gboolean
gst_registry_binary_write_cache (GstRegistry * registry, const char *location)
{
  GList *walk;
  char *tmp_location;
  GstBinaryRegistryMagic *magic;
  GstBinaryChunk *magic_chunk;
  GList *to_write = NULL;
  unsigned long file_position = 0;

  GST_INFO ("Building binary registry cache image");

  g_return_val_if_fail (GST_IS_REGISTRY (registry), FALSE);
  tmp_location = g_strconcat (location, ".tmpXXXXXX", NULL);
  registry->cache_file = g_mkstemp (tmp_location);
  if (registry->cache_file == -1) {
    char *dir;

    /* oops, I bet the directory doesn't exist */
    dir = g_path_get_dirname (location);
    g_mkdir_with_parents (dir, 0777);
    g_free (dir);

    /* the previous g_mkstemp call overwrote the XXXXXX placeholder ... */
    g_free (tmp_location);
    tmp_location = g_strconcat (location, ".tmpXXXXXX", NULL);
    registry->cache_file = g_mkstemp (tmp_location);

    if (registry->cache_file == -1) {
      GST_DEBUG ("g_mkstemp() failed: %s", g_strerror (errno));
      g_free (tmp_location);
      return FALSE;
    }
  }

  magic = g_malloc (sizeof (GstBinaryRegistryMagic));
  if (!gst_registry_binary_initialize_magic (magic))
    goto fail;

  magic_chunk = g_malloc (sizeof (GstBinaryChunk));
  magic_chunk->data = magic;
  magic_chunk->size = sizeof (GstBinaryRegistryMagic);
  magic_chunk->flags = GST_BINARY_REGISTRY_FLAG_NONE;
  magic_chunk->align = TRUE;

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
  to_write = g_list_prepend (to_write, magic_chunk);

  GST_INFO ("Writing binary registry cache");

  /* write out data chunks */
  for (walk = to_write; walk; walk = g_list_next (walk)) {
    GstBinaryChunk *cur = walk->data;

    if (!gst_registry_binary_write (registry, cur->data, cur->size,
            &file_position, cur->align)) {
      if (!(cur->flags & GST_BINARY_REGISTRY_FLAG_CONST))
        g_free (cur->data);
      g_free (cur);
      g_list_free (to_write);
      goto fail;
    }
    if (!(cur->flags & GST_BINARY_REGISTRY_FLAG_CONST))
      g_free (cur->data);
    g_free (cur);
  }
  g_list_free (to_write);

  if (close (registry->cache_file) < 0) {
    GST_DEBUG ("Can't close registry file : %s", strerror (errno));
    goto fail;
  }

  if (g_file_test (tmp_location, G_FILE_TEST_EXISTS)) {
#ifdef WIN32
    remove (location);
#endif
    rename (tmp_location, location);
  }

  g_free (tmp_location);
  GST_INFO ("Wrote binary registry cache");
  return TRUE;

  /* Errors */
fail:
  g_free (tmp_location);
  return FALSE;
}

/* Registry loading */

/*
 * gst_registry_binary_check_magic:
 *
 * Check GstBinaryRegistryMagic validity.
 * Return FALSE if something is wrong
 */
static gboolean
gst_registry_binary_check_magic (gchar ** in)
{
  GstBinaryRegistryMagic *m;

  align32 (*in);
  GST_DEBUG ("Reading/casting for GstBinaryRegistryMagic at address %p", *in);
  unpack_element (*in, m, GstBinaryRegistryMagic);

  if (m == NULL || m->magic == NULL || m->version == NULL) {
    GST_ERROR ("Binary registry magic structure is broken");
    return FALSE;
  }
  if (strncmp (m->magic, GST_MAGIC_BINARY_REGISTRY_STR,
          GST_MAGIC_BINARY_REGISTRY_LEN) != 0) {
    GST_ERROR
        ("Binary registry magic is different : %02x%02x%02x%02x != %02x%02x%02x%02x",
        GST_MAGIC_BINARY_REGISTRY_STR[0] & 0xff,
        GST_MAGIC_BINARY_REGISTRY_STR[1] & 0xff,
        GST_MAGIC_BINARY_REGISTRY_STR[2] & 0xff,
        GST_MAGIC_BINARY_REGISTRY_STR[3] & 0xff, m->magic[0] & 0xff,
        m->magic[1] & 0xff, m->magic[2] & 0xff, m->magic[3] & 0xff);
    return FALSE;
  }
  if (strncmp (m->version, GST_MAJORMINOR, GST_MAGIC_BINARY_VERSION_LEN)) {
    GST_ERROR ("Binary registry magic version is different : %s != %s",
        GST_MAJORMINOR, m->version);
    return FALSE;
  }
  return TRUE;
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

  align32 (*in);
  GST_DEBUG ("Reading/casting for GstBinaryPadTemplate at address %p", *in);
  unpack_element (*in, pt, GstBinaryPadTemplate);


  template = g_new0 (GstStaticPadTemplate, 1);
  template->presence = pt->presence;
  template->direction = pt->direction;

  /* unpack pad template strings */
  unpack_string (*in, template->name_template);
  unpack_string (*in, template->static_caps.string);

  __gst_element_factory_add_static_pad_template (factory, template);
  GST_DEBUG ("Added pad_template %s", template->name_template);

  return TRUE;
}

/*
 * gst_registry_binary_load_feature:
 *
 * Make a new GstPluginFeature from current GstBinaryPluginFeature structure
 *
 * Returns: new GstPluginFeature
 */
static gboolean
gst_registry_binary_load_feature (GstRegistry * registry, gchar ** in,
    gchar * plugin_name)
{
  GstBinaryPluginFeature *pf;
  GstPluginFeature *feature;
  gchar *type_name = NULL, *str;
  GType type;
  guint i;

  align32 (*in);
  GST_DEBUG ("Reading/casting for GstBinaryPluginFeature at address %p", *in);
  unpack_element (*in, pf, GstBinaryPluginFeature);

  /* unpack plugin feature strings */
  unpack_string (*in, type_name);

  if (!type_name || !*(type_name))
    return FALSE;

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
    GST_ERROR ("typename : '%s' is not a plgin feature", type_name);
    goto fail;
  }

  feature->rank = pf->rank;
  feature->plugin_name = plugin_name;

  /* unpack more plugin feature strings */
  unpack_string (*in, feature->name);

  if (GST_IS_ELEMENT_FACTORY (feature)) {
    GstElementFactory *factory = GST_ELEMENT_FACTORY (feature);

    /* unpack element factory strings */
    unpack_string (*in, factory->details.longname);
    unpack_string (*in, factory->details.klass);
    unpack_string (*in, factory->details.description);
    unpack_string (*in, factory->details.author);
    GST_DEBUG ("Element factory : '%s' with npadtemplates=%d",
        factory->details.longname, pf->npadtemplates);

    /* load pad templates */
    for (i = 0; i < pf->npadtemplates; i++) {
      if (!gst_registry_binary_load_pad_template (factory, in)) {
        GST_ERROR ("Error while loading binary pad template");
        goto fail;
      }
    }

    /* load uritypes */
    if (pf->nuriprotocols) {
      GST_DEBUG ("Reading %d UriTypes at address %p", pf->nuriprotocols, *in);

      align32 (*in);
      factory->uri_type = *((guint *) * in);
      *in += sizeof (factory->uri_type);
      //unpack_element(*in, &factory->uri_type, factory->uri_type);

      factory->uri_protocols = g_new0 (gchar *, pf->nuriprotocols + 1);
      for (i = 0; i < pf->nuriprotocols; i++) {
        unpack_string (*in, str);
        factory->uri_protocols[i] = str;
      }
    }
    /* load interfaces */
    GST_DEBUG ("Reading %d Interfaces at address %p", pf->ninterfaces, *in);
    for (i = 0; i < pf->ninterfaces; i++) {
      unpack_string (*in, str);
      __gst_element_factory_add_interface (factory, str);
      g_free (str);
    }
  } else if (GST_IS_TYPE_FIND_FACTORY (feature)) {
    /* FIXME: load typefind */
  }
#ifndef GST_DISABLE_INDEX
  else if (GST_IS_INDEX_FACTORY (feature)) {
    /* FIXME: load indexers */
  }
#endif

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

  align32 (*in);
  GST_DEBUG ("Reading/casting for GstBinaryPluginElement at address %p", *in);
  unpack_element (*in, pe, GstBinaryPluginElement);

  if (pe->nfeatures < 0) {
    GST_ERROR ("The number of feature structure is not valid !");
    return FALSE;
  }

  if (pe->file_mtime < 0 || pe->file_size < 0) {
    GST_ERROR ("Plugin time or file size is not valid !");
    return FALSE;
  }

  GST_DEBUG ("Adding plugin with %d features from binary registry",
      pe->nfeatures);

  plugin = g_object_new (GST_TYPE_PLUGIN, NULL);

  plugin->flags |= GST_PLUGIN_FLAG_CACHED;
  plugin->file_mtime = pe->file_mtime;
  plugin->file_size = pe->file_size;

  /* unpack plugin element strings */
  unpack_string (*in, plugin->desc.name);
  unpack_string (*in, plugin->desc.description);
  unpack_string (*in, plugin->filename);
  unpack_string (*in, plugin->desc.version);
  unpack_string (*in, plugin->desc.license);
  unpack_string (*in, plugin->desc.source);
  unpack_string (*in, plugin->desc.package);
  unpack_string (*in, plugin->desc.origin);
  GST_DEBUG ("read strings for '%s'", plugin->desc.name);

  plugin->basename = g_path_get_basename (plugin->filename);
  gst_registry_add_plugin (registry, plugin);
  GST_DEBUG ("Added plugin '%s' from binary registry", plugin->desc.name);
  GST_DEBUG ("Number of features %d", pe->nfeatures);
  for (i = 0; i < pe->nfeatures; i++) {
    if (!gst_registry_binary_load_feature (registry, in,
            g_strdup (plugin->desc.name))) {
      GST_ERROR ("Error while loading binary feature");
      goto fail;
    }
  }

  return TRUE;

  /* Errors */
fail:
  gst_object_unref (plugin);
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

  /* make sure these types exist */
  GST_TYPE_ELEMENT_FACTORY;
  GST_TYPE_TYPE_FIND_FACTORY;
#ifndef GST_DISABLE_INDEX
  GST_TYPE_INDEX_FACTORY;
#endif

  timer = g_timer_new ();

  mapped = g_mapped_file_new (location, FALSE, &err);
  if (err != NULL) {
    GST_INFO ("Unable to mmap file: %s\n", err->message);
    g_error_free (err);
    return FALSE;
  }

  if ((contents = g_mapped_file_get_contents (mapped)) == NULL) {
    GST_ERROR ("Can't load file %s : %s", location, strerror (errno));
    g_mapped_file_free (mapped);
    return FALSE;
  }
  /* in is a cursor pointer, we initialize it with the begin of registry and is updated on each read */
  in = contents;
  GST_DEBUG ("File mapped at address %p", in);
  /* check length for header */
  size = g_mapped_file_get_length (mapped);
  if (size < sizeof (GstBinaryRegistryMagic)) {
    GST_INFO ("No or broken registry header");
    return FALSE;
  }
  /* check if header is valid */
  if (!gst_registry_binary_check_magic (&in)) {
    GST_ERROR ("Binary registry type not recognized (invalid magic)");
    g_mapped_file_free (mapped);
    return FALSE;
  }

  /* check if there are plugins in the file */

  if (!(((size_t) in + sizeof (GstBinaryPluginElement)) <
          (size_t) contents + size)) {
    GST_INFO ("No binary plugins structure to read");
    return TRUE;                /* empty file, this is not an error */
  }

  for (;
      ((size_t) in + sizeof (GstBinaryPluginElement)) <
      (size_t) contents + size;) {
    GST_INFO ("reading binary registry %" G_GSIZE_FORMAT "(%x)/%"
        G_GSIZE_FORMAT, (size_t) in - (size_t) contents,
        (guint) ((size_t) in - (size_t) contents), size);
    if (!gst_registry_binary_load_plugin (registry, &in)) {
      GST_ERROR ("Problem while reading binary registry");
      return FALSE;
    }
  }

  g_timer_stop (timer);
  seconds = g_timer_elapsed (timer, NULL);
  g_timer_destroy (timer);

  GST_INFO ("loaded %s in %lf seconds", location, seconds);

  g_mapped_file_free (mapped);
  return TRUE;
}
